// Copyright (c) 2016 Vadim Markovtsev (source{d})
// Copyright (c) 2026 Ratha SIV | MIT License
// C++ solver derived from LAPX's native square and rectangular cores.
// Preserve algorithm, arithmetic precision, initialization, and tie order.
#include "lapjv.h"
#include <algorithm>
#include <cstring>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>
#include <type_traits>
#include <vector>
#include <system_error>

#ifdef LAPCPP_TESTING
#include "../../tests/native/faults.h"
#else
#define LAPCPP_TEST_POINT(site) ((void)0)
#endif

#ifdef _MSC_VER
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

// Shortest augmenting path completion for the square solver.
extern "C" int lapcpp_augment(const void*, int, size_t, size_t,
    size_t*, size_t*, size_t*, const size_t*, size_t*, double*, double*, uint64_t*) noexcept;

// Keep the out-of-line minimum scans in a stable code segment on native
// MSVC x64. This controls generated-code placement, not solver policy or ties.
#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)
#define LAPCPP_MINIMA_CODE __declspec(code_seg(".text$aa_lap_minima"))
#else
#define LAPCPP_MINIMA_CODE
#endif

namespace {

// Classify by exponent bits on IEEE binary32/binary64 targets. Constant-size
// memcpy is alias-safe and optimizes to register moves; other targets use std.
inline bool exact_finite(double value) {
    if constexpr (sizeof(double) == sizeof(uint64_t) &&
                  std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::radix == 2 &&
                  std::numeric_limits<double>::digits == 53 &&
                  std::numeric_limits<double>::max_exponent == 1024) {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
    } else {
        return std::isfinite(value);
    }
}
inline bool exact_finite(float value) {
    if constexpr (sizeof(float) == sizeof(uint32_t) &&
                  std::numeric_limits<float>::is_iec559 &&
                  std::numeric_limits<float>::radix == 2 &&
                  std::numeric_limits<float>::digits == 24 &&
                  std::numeric_limits<float>::max_exponent == 128) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
    } else {
        return std::isfinite(value);
    }
}

constexpr size_t FREE = std::numeric_limits<size_t>::max();
enum Error { Infeasible = 1, Allocation = 2, Internal = 3, Dimensions = 4 };
template<class T> constexpr T inf() { return std::numeric_limits<T>::infinity(); }
template<class T> void grow(std::vector<T>& v, size_t n) {
    if (v.size() < n) { LAPCPP_TEST_POINT(allocation); v.resize(n); }
}
size_t saturated_product(size_t a, size_t b) {
    return b && a > FREE / b ? FREE : a * b;
}
template<class D> struct Minima { D first, second; size_t j1, j2; };
template<class D> inline void scan_value(Minima<D>& m, D value, size_t j) {
    if (value < m.second) {
        if (value >= m.first) { m.second = value; m.j2 = j; }
        else { m.second = m.first; m.first = value; m.j2 = m.j1; m.j1 = j; }
    }
}
template<class T, class D>
LAPCPP_MINIMA_CODE NOINLINE Minima<D> required_minima(const T* __restrict row, const D* __restrict v, size_t n) {
    Minima<D> m{inf<D>(), inf<D>(), FREE, FREE};
    for (size_t j = 0; j < n; ++j) scan_value(m, static_cast<D>(row[j]) - v[j], j);
    return m;
}
template<bool Grouped>
inline Minima<double> scan_float32(const float* __restrict row, const double* __restrict v, size_t n) {
    Minima<double> m{inf<double>(), inf<double>(), FREE, FREE};
    size_t j = 0;
    if constexpr (Grouped) {
        for (; j + 8 <= n; j += 8) {
            bool possible = false;
            for (size_t k = 0; k < 8; ++k) possible |= double(row[j+k]) - v[j+k] < m.second;
            if (possible)
                for (size_t k = 0; k < 8; ++k) scan_value(m, double(row[j+k]) - v[j+k], j+k);
        }
    }
    for (; j < n; ++j) scan_value(m, double(row[j]) - v[j], j);
    return m;
}
NOINLINE Minima<double> grouped_float32(const float* row, const double* v, size_t n) {
    return scan_float32<true>(row, v, n);
}
template<class T> inline Minima<double> square_minima(const T* row, const double* v, size_t n) {
    if constexpr (std::is_same_v<T, float>)
        return n <= 16 ? scan_float32<false>(row, v, n) : grouped_float32(row, v, n);
    else return required_minima(row, v, n);
}
// Keep the four optional scan instantiations together on a separate aligned
// code section on native MSVC x64. Algorithm, column order and ties are unchanged.
#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)
#define LAPCPP_OPTIONAL_SCAN_CODE __declspec(code_seg(".lapopt"))
#else
#define LAPCPP_OPTIONAL_SCAN_CODE
#endif
template<class T, bool Grouped>
LAPCPP_OPTIONAL_SCAN_CODE inline Minima<T> scan_optional(const T* __restrict row, const T* __restrict v, size_t n, T penalty) {
    Minima<T> m{penalty, penalty, FREE, FREE};
    size_t j = 0;
    if constexpr (Grouped)
        for (; j + 8 <= n; j += 8)
            for (size_t k = 0; k < 8; ++k) scan_value(m, T(row[j+k] - v[j+k]), j+k);
    for (; j < n; ++j) scan_value(m, T(row[j] - v[j]), j);
    return m;
}
template<class T> NOINLINE Minima<T> grouped_optional(const T* row, const T* v, size_t n, T penalty) {
    return scan_optional<T, true>(row, v, n, penalty);
}
template<class T> inline Minima<T> optional_minima(const T* row, const T* v, size_t n, T penalty) {
    return n <= 16 ? scan_optional<T, false>(row, v, n, penalty) : grouped_optional(row, v, n, penalty);
}

template<class T>
NOINLINE bool sampled_block_conflicts(const T* costs, size_t n, double minimum, double range) {
    const T* rows[4];
    for (size_t s = 0; s < 4; ++s) rows[s] = costs + (s * (n-1) / 3) * n;
    size_t cheap_columns[4]{};
    const double cheap = minimum + range * .25, expensive = minimum + range * .75;
    for (size_t j = 0; j < n; ++j) {
        bool has_cheap = false, has_expensive = false;
        for (size_t s = 0; s < 4; ++s) {
            const double cost = rows[s][j];
            if (cost < cheap) { ++cheap_columns[s]; has_cheap = true; }
            else if (cost > expensive) has_expensive = true;
            else return false;
        }
        if (!has_cheap || !has_expensive) return false;
    }
    size_t competing = 0;
    for (auto c : cheap_columns) competing += c < n-c;
    if (competing < 3) return false;
    for (size_t i = 0; i < 4; ++i)
        for (size_t j = 0; j < i; ++j)
            if (std::equal(rows[i], rows[i]+n, rows[j])) return false;
    return true;
}
template<class T> NOINLINE bool prefer_row_reduction(const T* costs, size_t n) {
    double minimum = inf<double>(), largest_min = -inf<double>();
    double cheapest_max = -inf<double>(), maximum = -inf<double>();
    for (size_t s = 0; s < 4; ++s) {
        const T* row = costs + (s * (n-1) / 3) * n;
        T low = inf<T>(), high = -inf<T>();
        for (size_t j = 0; j < n; ++j) {
            if (row[j] < low) low = row[j];
            if (row[j] > high) high = row[j];
        }
        if (!exact_finite(low) || !exact_finite(high)) return false;
        if (low < minimum) { minimum = low; cheapest_max = high; }
        largest_min = std::max(largest_min, double(low));
        maximum = std::max(maximum, double(high));
    }
    const double range = maximum - minimum;
    return exact_finite(range) && (largest_min-minimum > range*.5 ||
        (cheapest_max-minimum > range*.75 && sampled_block_conflicts(costs,n,minimum,range)));
}
template<class T, class D>
inline void column_minima(const T* __restrict costs, size_t n, D* __restrict v, size_t* __restrict y) {
    for (size_t j = 0; j < n; ++j) { v[j] = static_cast<D>(costs[j]); y[j] = 0; }
    for (size_t i = 1; i < n; ++i)
        for (size_t j = 0; j < n; ++j) {
            const D value = static_cast<D>(costs[i*n+j]);
            if (value < v[j]) { v[j] = value; y[j] = i; }
        }
}
template<class T> NOINLINE void initialize_columns(const T* costs, size_t n, double* v, size_t* y) {
    if constexpr (std::is_same_v<T, float>) {
        if (n <= 128) {
            float initial[128]{};
            column_minima(costs, n, initial, y);
            for (size_t j = 0; j < n; ++j) v[j] = initial[j];
            return;
        }
    }
    column_minima(costs, n, v, y);
}

struct Square {
    std::vector<size_t> x, y, columns, matches, predecessor;
    std::vector<double> distance, potential;
    template<class T> NOINLINE void solve(const T* costs, size_t n) {
        grow(x,n); grow(y,n); grow(columns,n); grow(matches,n); grow(predecessor,n);
        grow(distance,n); grow(potential,n);
        auto* __restrict xx = x.data(); auto* __restrict yy = y.data();
        auto* __restrict free = matches.data(); auto* __restrict v = potential.data();
        std::fill_n(xx,n,FREE); std::fill_n(free,n,size_t(0));
        if (prefer_row_reduction(costs,n)) {
            std::fill_n(v,n,0.0); std::fill_n(yy,n,FREE);
        } else {
            initialize_columns(costs,n,v,yy);
            for (size_t j = n; j-- > 0;) {
                if (!exact_finite(v[j])) throw Infeasible;
                const size_t i = yy[j];
                if (++free[i] == 1) xx[i] = j;
                else yy[j] = FREE;
            }
        }
        if (n == 1) return;
        size_t num_free = 0;
        for (size_t i = 0; i < n; ++i) {
            if (free[i] == 0) free[num_free++] = i;
            else if (free[i] == 1) {
                const size_t j1 = xx[i];
                double minimum = inf<double>();
                for (size_t j = 0; j < n; ++j)
                    if (j != j1) {
                        const double value = double(costs[i*n+j]) - v[j];
                        if (value < minimum) minimum = value;
                    }
                if (exact_finite(minimum)) v[j1] -= minimum;
            }
        }
        for (int pass = 0; pass < 2; ++pass) {
            const size_t previous = num_free;
            num_free = 0;
            size_t k = 0, reductions = 0;
            while (k < previous) {
                const size_t i = free[k++]; ++reductions;
                const T* row = costs+i*n;
                auto m = square_minima(row,v,n);
                if (!exact_finite(m.first)) throw Infeasible;
                size_t j1 = m.j1;
                if (m.first == m.second && yy[j1] != FREE)
                    for (size_t j = 0; j < n; ++j)
                        if (yy[j] == FREE && double(row[j])-v[j] == m.first) { j1 = j; break; }
                size_t displaced = yy[j1];
                const bool reduce = reductions < saturated_product(k,8);
                const double new_v = v[j1] - (m.second-m.first);
                const bool lowers = reduce && exact_finite(new_v) && new_v < v[j1];
                if (lowers) v[j1] = new_v;
                else if (reduce && exact_finite(m.second) && displaced != FREE && m.j2 != FREE) {
                    j1 = m.j2; displaced = yy[j1];
                }
                xx[i] = j1; yy[j1] = i;
                if (displaced != FREE) {
                    if (lowers) free[--k] = displaced;
                    else free[num_free++] = displaced;
                }
            }
        }
        const int status = lapcpp_augment(costs,std::is_same_v<T,float>,n,num_free,
            xx,yy,columns.data(),free,predecessor.data(),distance.data(),v,nullptr);
        if (status) throw (status == 1 ? Infeasible : Internal);
        for (size_t i = 0; i < n; ++i) {
            if (xx[i] >= n || !exact_finite(costs[i*n+xx[i]])) throw Infeasible;
            if (yy[xx[i]] != i) throw Internal;
        }
    }
};

template<class T> struct Rectangular {
    std::vector<size_t> x, y, free_rows, columns, predecessor;
    std::vector<T> distance, row_dual, column_dual;
    template<bool Optional, bool NearSquare>
    void solve_impl(const T* costs, size_t rows, size_t cols, T penalty) {
        grow(x,rows); grow(y,cols); grow(free_rows,rows); grow(columns,cols);
        grow(predecessor,cols); grow(distance,cols); grow(row_dual,rows); grow(column_dual,cols);
        auto* __restrict xx = x.data(); auto* __restrict yy = y.data();
        auto* __restrict free = free_rows.data(); auto* __restrict order = columns.data();
        auto* __restrict pred = predecessor.data(); auto* __restrict d = distance.data();
        auto* __restrict u = row_dual.data(); auto* __restrict v = column_dual.data();
        std::fill_n(xx,rows,FREE); std::fill_n(yy,cols,FREE);
        std::fill_n(u,rows,T(0)); std::fill_n(v,cols,T(0));
        std::iota(free,free+rows,size_t(0));
        size_t num_free = rows;
        const size_t reduction_budget = rows >= 128 && cols-rows > rows/8 ? saturated_product(rows,size_t(Optional ? 8 : 4)) : FREE;
        const size_t reduction_limit = NearSquare ? std::min(cols,size_t(8)) : cols;
        for (int pass = 0; pass < 2; ++pass) {
            const size_t previous = num_free;
            num_free = 0;
            size_t k = 0, reductions = 0;
            while (k < previous) {
                const size_t i = free[k++]; ++reductions;
                const T* row = costs+i*cols;
                const auto m = Optional ? optional_minima(row,v,cols,penalty) : required_minima(row,v,cols);
                if (!exact_finite(m.first)) throw Infeasible;
                size_t j1 = m.j1;
                if (Optional && j1 == FREE) { xx[i] = FREE; u[i] = penalty; continue; }
                if (NearSquare && m.first == m.second && yy[j1] != FREE) {
                    if (m.j2 != FREE && yy[m.j2] == FREE) j1 = m.j2;
                    else for (size_t j = 0; j < cols; ++j)
                        if (yy[j] == FREE && row[j]-v[j] == m.first) { j1 = j; break; }
                }
                size_t displaced = yy[j1];
                const bool reduce = reductions < std::min(saturated_product(k,reduction_limit), reduction_budget);
                const T new_v = v[j1] - (m.second-m.first);
                if (reduce && exact_finite(m.second) && !exact_finite(new_v)) throw Infeasible;
                const bool lowers = reduce && exact_finite(new_v) && new_v < v[j1];
                if (lowers) v[j1] = new_v;
                else if (reduce && exact_finite(m.second) && displaced != FREE && (Optional || m.j2 != FREE)) {
                    j1 = m.j2;
                    if (Optional && j1 == FREE) { xx[i] = FREE; u[i] = penalty; continue; }
                    displaced = yy[j1];
                }
                xx[i] = j1; yy[j1] = i; u[i] = row[j1]-v[j1];
                if (!exact_finite(u[i])) throw Infeasible;
                if (displaced != FREE) {
                    if (lowers) free[--k] = displaced;
                    else free[num_free++] = displaced;
                }
            }
        }
        for (size_t f = 0; f < num_free; ++f) {
            const size_t root = free[f];
            // Keep root's reduced costs in d; the first relaxation below
            // reuses them instead of reading the row again.
            T minimum = Optional ? penalty : inf<T>();
            for (size_t j = 0; j < cols; ++j) {
                const T value = costs[root*cols+j]-v[j];
                d[j] = value;
                if (value < minimum) minimum = value;
                order[j] = j;
            }
            if (!exact_finite(minimum)) throw Infeasible;
            u[root] = minimum; xx[root] = FREE;
            size_t low = 0, current = root, dummy_row = FREE, sink;
            T dist = 0, dummy_distance = inf<T>();
            for (;;) {
                if constexpr (Optional) {
                    const T candidate = dist + (penalty-u[current]);
                    if (candidate < dummy_distance) { dummy_distance = candidate; dummy_row = current; }
                }
                const T* row = costs+current*cols;
                const T row_potential = u[current];
                T best = inf<T>();
                size_t best_pos = FREE;
                if (low == 0) {
                    // Root relaxation in identity order: same values as relaxing
                    // from d = inf. Infinite columns get pred = root, but cannot
                    // be selected while their distance remains infinite.
                    for (size_t j = 0; j < cols; ++j) {
                        const T candidate = dist + (d[j]-row_potential);
                        d[j] = candidate; pred[j] = current;
                        if (candidate < best || (candidate == best && yy[j] == FREE)) { best = candidate; best_pos = j; }
                    }
                } else for (size_t pos = low; pos < cols; ++pos) {
                    const size_t j = order[pos];
                    const T candidate = dist + (row[j]-v[j]-row_potential);
                    if (candidate < d[j]) { d[j] = candidate; pred[j] = current; }
                    if (d[j] < best || (d[j] == best && yy[j] == FREE)) { best = d[j]; best_pos = pos; }
                }
                if (Optional && exact_finite(dummy_distance) && dummy_distance <= best) {
                    dist = dummy_distance; sink = FREE; break;
                }
                if (!exact_finite(best) || best_pos == FREE) throw Infeasible;
                dist = best;
                std::swap(order[low],order[best_pos]);
                const size_t j = order[low++];
                if (yy[j] == FREE) { sink = j; break; }
                current = yy[j];
            }
            u[root] += dist;
            if (!exact_finite(u[root])) throw Infeasible;
            for (size_t pos = 0; pos < low; ++pos) {
                const size_t j = order[pos];
                const T adjustment = dist-d[j];
                if (yy[j] != FREE) {
                    u[yy[j]] += adjustment;
                    if (!exact_finite(u[yy[j]])) throw Infeasible;
                }
                v[j] -= adjustment;
                if (!exact_finite(v[j])) throw Infeasible;
            }
            size_t j = sink, steps = 0;
            for (; steps < rows; ++steps) {
                const size_t i = Optional && j == FREE ? dummy_row : pred[j];
                if (i == FREE) throw Internal;
                const size_t old = xx[i];
                if (j != FREE) yy[j] = i;
                xx[i] = j;
                if (i == root) break;
                if (old == FREE) throw Internal;
                j = old;
            }
            // An augmenting path visits each row at most once.
            if (steps == rows) throw Internal;
        }
        for (size_t i = 0; i < rows; ++i) {
            if (Optional && xx[i] == FREE) continue;
            if (xx[i] >= cols || !exact_finite(costs[i*cols+xx[i]])) throw Infeasible;
            if (yy[xx[i]] != i) throw Internal;
        }
    }
    void solve(const T* costs, size_t rows, size_t cols, bool optional, T penalty) {
        if (optional) {
            if (!exact_finite(penalty)) throw Infeasible;
            if (rows == cols) solve_impl<true,true>(costs,rows,cols,penalty);
            else solve_impl<true,false>(costs,rows,cols,penalty);
        } else if (rows > 128 && cols-rows <= rows/8) solve_impl<false,true>(costs,rows,cols,penalty);
        else solve_impl<false,false>(costs,rows,cols,penalty);
    }
};

template<class T> struct Worker {
    std::vector<T> orientation;
    Square square;
    Rectangular<T> rectangular;
    void release_large_buffers() {
        if (orientation.capacity() > 8*1024*1024/sizeof(T)) std::vector<T>().swap(orientation);
        if (square.x.capacity() > 65536) square = Square{};
        if (rectangular.y.capacity() > 65536) rectangular = Rectangular<T>{};
    }
};
template<class T> Worker<T>& worker() { thread_local Worker<T> w; return w; }

// A bounded cache of reusable C++ pools. Scheduling differs from Rayon's work
// stealing, so primary kernel comparisons use single calls / n_threads=1.
class Pool {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::function<void()>> queue;
    std::vector<std::thread> threads;
    bool stopping = false;
public:
    explicit Pool(size_t n) {
        try {
            for (size_t i = 0; i < n; ++i) {
                LAPCPP_TEST_POINT(worker);
                threads.emplace_back([this] {
                    for (;;) {
                        std::function<void()> job;
                        {
                            std::unique_lock<std::mutex> lock(mutex);
                            ready.wait(lock,[this] { return stopping || !queue.empty(); });
                            if (stopping && queue.empty()) return;
                            job = std::move(queue.front()); queue.pop_front();
                        }
                        job();
                    }
                });
            }
        } catch (const std::system_error&) {
            stop();
            throw std::bad_alloc();  // Report exhausted worker resources as Allocation.
        } catch (...) { stop(); throw; }
    }
    void stop() noexcept {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
        ready.notify_all();
        for (auto& thread : threads) if (thread.joinable()) thread.join();
    }
    ~Pool() { stop(); }
#ifdef LAPCPP_TESTING
    size_t size() const { return threads.size(); }
#endif
    void submit(std::function<void()> job) {
        LAPCPP_TEST_POINT(submission);
        { std::lock_guard<std::mutex> lock(mutex); queue.push_back(std::move(job)); }
        ready.notify_one();
    }
};
struct PoolCache {
    std::mutex mutex;
    std::deque<std::pair<size_t,std::shared_ptr<Pool>>> entries;
};
PoolCache& pool_cache() { static PoolCache cache; return cache; }
std::shared_ptr<Pool> get_pool(size_t n) {
    auto& state = pool_cache();
    auto& cache = state.entries;
    std::shared_ptr<Pool> evicted, result;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        for (auto& item : cache) if (item.first == n) return item.second;
        result = std::make_shared<Pool>(n);
        cache.emplace_back(n,result);
        if (cache.size() > 2) { evicted = cache.front().second; cache.pop_front(); }
    }
    return result;
}
bool use_parallel(size_t batch, size_t rows, size_t cols) {
    const size_t small = std::min(rows,cols), large = std::max(rows,cols);
    const size_t elements = saturated_product(saturated_product(rows,cols),batch);
    const size_t work = saturated_product(elements,std::min(small,size_t(32)));
    const bool near = large <= saturated_product(small,2);
    const bool competitive = rows != cols && large-small <= small/8;
    const size_t threshold = batch < 8 && !competitive ? 524288 : 262144;
    return batch > 1 && (elements >= 65536 || (near && work >= threshold));
}
template<class T> void solve_one(Worker<T>& w, const T* costs, const void* original,
    bool original32, size_t rows, size_t cols, double limit, bool pairs, bool return_cost,
    int64_t* output, size_t& count, double& total) {
    LAPCPP_TEST_POINT(internal);
    const size_t r = std::min(rows,cols), c = std::max(rows,cols);
    const bool transposed = rows > cols, optional = exact_finite(limit);
    const size_t* assignment;
    if (r == c && !optional) {
        w.square.solve(costs,c); assignment = w.square.x.data();
    } else {
        if (transposed) {
            grow(w.orientation,rows*cols);
            for (size_t i0 = 0; i0 < rows; i0 += 32)
                for (size_t j0 = 0; j0 < cols; j0 += 32)
                    for (size_t i = i0; i < std::min(i0+32,rows); ++i)
                        for (size_t j = j0; j < std::min(j0+32,cols); ++j)
                            w.orientation[j*rows+i] = costs[i*cols+j];
            costs = w.orientation.data();
        }
        const T reject = static_cast<T>(limit/2.0);
        const T penalty = reject+reject;
        w.rectangular.solve(costs,r,c,optional,penalty);
        assignment = transposed ? w.rectangular.y.data() : w.rectangular.x.data();
    }
    count = 0; total = 0;
    for (size_t i = 0; i < rows; ++i) {
        const size_t j = assignment[i];
        if (j >= cols) continue;
        if (pairs) { output[2*count] = int64_t(i); output[2*count+1] = int64_t(j); }
        else { output[count] = int64_t(i); output[r+count] = int64_t(j); }
        ++count;
        if (return_cost) total += original32 ? double(static_cast<const float*>(original)[i*cols+j])
                                             : static_cast<const double*>(original)[i*cols+j];
    }
}
template<class T> int run(const T* costs, const void* original, bool original32,
    size_t batch, size_t rows, size_t cols, double limit, size_t threads, bool automatic,
    bool pairs, bool return_cost, int64_t* output, size_t* counts, double* totals, size_t* failed) {
    threads = std::min(std::max(threads,size_t(1)),batch);
    const size_t stride = rows*cols, out_stride = 2*std::min(rows,cols);
#ifdef LAPCPP_TESTING
    const auto fault = test_faults::plan;
#endif
    auto solve = [&](size_t i) -> int {
        try {
#ifdef LAPCPP_TESTING
            test_faults::Scope injection(fault, i);
#endif
            const void* src = original32 ? static_cast<const void*>(static_cast<const float*>(original)+i*stride)
                                        : static_cast<const void*>(static_cast<const double*>(original)+i*stride);
            solve_one(worker<T>(),costs+i*stride,src,original32,rows,cols,limit,
                      pairs,return_cost,output+i*out_stride,counts[i],totals[i]);
            return 0;
        } catch (Error error) { return error; }
        catch (const std::bad_alloc&) { return Allocation; }
        catch (...) { return Internal; }
    };
    if (threads <= 1 || (automatic && !use_parallel(batch,rows,cols))) {
        for (size_t i = 0; i < batch; ++i) {
            const int status = solve(i);
            if (status) { worker<T>().release_large_buffers(); *failed = i; return status; }
        }
        worker<T>().release_large_buffers();
        return 0;
    }
    auto pool = get_pool(threads);
    std::vector<int> statuses(batch);
    std::atomic<size_t> next{0};
    std::mutex mutex;
    std::condition_variable done;
    size_t pending = 0;
    // On submission failure, wait for all submitted closures before unwinding:
    // every closure references this call's buffers and synchronization objects.
    std::exception_ptr submission_error;
    // Claim consecutive tiny problems: fewer shared increments, and adjacent
    // statuses, counts, totals and outputs stay with one worker. Bound each
    // chunk to about 4096 costs so clustered hard problems cannot serialize.
    const size_t chunk = std::max(std::min({batch/(threads*16), 4096/stride, size_t(64)}), size_t(1));
    for (size_t t = 0; t < threads; ++t) {
        { std::lock_guard<std::mutex> lock(mutex); ++pending; }
        try {
            pool->submit([&] {
                for (;;) {
                    const size_t first = next.fetch_add(chunk,std::memory_order_relaxed);
                    if (first >= batch) break;
                    const size_t last = std::min(first+chunk,batch);
                    for (size_t i = first; i < last; ++i) statuses[i] = solve(i);
                }
                worker<T>().release_large_buffers();
                { std::lock_guard<std::mutex> lock(mutex); --pending; done.notify_one(); }
            });
        } catch (...) {
            { std::lock_guard<std::mutex> lock(mutex); --pending; }
            submission_error = std::current_exception(); break;
        }
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        done.wait(lock,[&] { return pending == 0; });
    }
    if (submission_error) std::rethrow_exception(submission_error);
    for (size_t i = 0; i < batch; ++i)
        if (statuses[i]) { *failed = i; return statuses[i]; }
    return 0;
}
} // namespace

extern "C" int lapcpp_run(const void* costs, int cost32, const void* original, int original32,
    size_t batch, size_t rows, size_t cols, double limit, size_t threads, int automatic,
    int pairs, int return_cost,
    int64_t* output, size_t* counts, double* totals, size_t* failed) {
    if (!failed) return Dimensions;
    *failed = 0;
    if (!batch) return 0;
    if (!rows || !cols || !costs || !original || !output || !counts || !totals ||
        rows > size_t(INT32_MAX) || cols > size_t(INT32_MAX) ||
        rows > FREE/cols || batch > FREE/(rows*cols)/sizeof(double) ||
        batch > FREE/(2*std::min(rows,cols))/sizeof(int64_t)) return Dimensions;
    try {
        if (cost32) return run(static_cast<const float*>(costs),original,original32,batch,rows,cols,
            limit,threads,automatic,pairs,return_cost,output,counts,totals,failed);
        return run(static_cast<const double*>(costs),original,original32,batch,rows,cols,
            limit,threads,automatic,pairs,return_cost,output,counts,totals,failed);
    } catch (const std::bad_alloc&) { return Allocation; }
    catch (...) { return Internal; }
}
