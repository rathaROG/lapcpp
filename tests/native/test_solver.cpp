// Include the implementation to inspect internal resource invariants without
// exporting test APIs or fault switches from the installed extension.
#define LAPCPP_TESTING 1
#include "../../src/_lapjv/solver.cpp"
#include <chrono>
#include <future>
#include <iostream>
#include <set>
#include <random>
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(#expr); } while (false)

double brute(const std::vector<double>& a, size_t r, size_t c, size_t row,
             unsigned used, double limit) {
    if (row==r) {
        size_t count=0;
        for (size_t j=0; j<c; ++j) count += (used>>j)&1;
        return std::isfinite(limit) ? (c-count)*limit/2 : 0;
    }
    double best=std::isfinite(limit) ? limit/2+brute(a,r,c,row+1,used,limit) : inf<double>();
    for (size_t j=0; j<c; ++j) if (!(used&(1u<<j)))
        best=std::min(best,a[row*c+j]+brute(a,r,c,row+1,used|(1u<<j),limit));
    return best;
}

template<class T> void numerical_paths() {
    std::mt19937 random(981);
    for (auto shape : {std::pair<size_t,size_t>{5,5}, {4,7}}) {
        const size_t r=shape.first,c=shape.second;
        for (double limit : {inf<double>(),-.5,.5}) for (size_t trial=0; trial<16; ++trial) {
            std::vector<double> reference(r*c);
            for (auto& value : reference) value=(int(random()%17)-8)/4.;
            reference[(trial*7)%(r*c)] = inf<double>();
            const double optimum=brute(reference,r,c,0,0,limit);
            for (bool transpose : {false,true}) {
                const size_t rows=transpose?c:r, cols=transpose?r:c;
                std::vector<T> costs(r*c);
                for (size_t i=0; i<r; ++i) for (size_t j=0; j<c; ++j)
                    costs[transpose?j*r+i:i*c+j]=T(reference[i*c+j]);
                Worker<T> w;
                for (bool pairs : {false,true}) {
                    std::vector<int64_t> output(2*r);
                    size_t count=0; double total=0;
                    solve_one(w,costs.data(),costs.data(),std::is_same<T,float>::value,
                        rows,cols,limit,pairs,true,output.data(),count,total);
                    CHECK((std::isfinite(limit)?total+(r+c-2*count)*limit/2:total)==optimum);
                    std::set<int64_t> seen_rows,seen_cols;
                    for (size_t i=0; i<count; ++i) {
                        auto row=output[pairs?2*i:i],col=output[pairs?2*i+1:r+i];
                        CHECK(row>=0 && size_t(row)<rows && col>=0 && size_t(col)<cols);
                        CHECK(seen_rows.insert(row).second && seen_cols.insert(col).second);
                    }
                }
            }
        }
    }
}

// Binary fractions keep exact objective checks independent of a relative tolerance.
// The 3x4 fixture leaves a row for augmenting search after row reduction.
template<class T> void working_precision() {
    for (auto scale : {std::pair<double,double>{181406.1875,1./4096}, {16777216.,1./8}}) {
        const double high=scale.first, small=scale.second;
        for (size_t r : {size_t(2),size_t(3)}) {
            const size_t c=r+1;
            const std::vector<T> base = r==2
                ? std::vector<T>{T(high),T(2*small),T(high), T(high),T(small),T(high)}
                : std::vector<T>{T(2*small),T(2*small),T(high),T(high),
                                 T(small),T(small),T(high),T(high), T(0),T(0),T(high),T(high)};
            for (bool transpose : {false,true}) {
                const size_t rows=transpose?c:r, cols=transpose?r:c;
                std::vector<T> costs(r*c);
                for (size_t i=0; i<r; ++i) for (size_t j=0; j<c; ++j)
                    costs[transpose?j*r+i:i*c+j]=base[i*c+j];
                for (double limit : {inf<double>(),2*high}) for (bool pairs : {false,true}) {
                    Worker<T> w;
                    std::vector<int64_t> output(2*r);
                    size_t count=0; double total=0;
                    solve_one(w,costs.data(),costs.data(),std::is_same_v<T,float>,
                              rows,cols,limit,pairs,true,output.data(),count,total);
                    CHECK(count==r && total==high+small);
                    std::set<int64_t> seen_rows,seen_cols;
                    double selected=0;
                    for (size_t i=0; i<count; ++i) {
                        const auto row=output[pairs?2*i:i],col=output[pairs?2*i+1:r+i];
                        CHECK(row>=0 && size_t(row)<rows && col>=0 && size_t(col)<cols);
                        CHECK(seen_rows.insert(row).second && seen_cols.insert(col).second);
                        selected+=double(costs[size_t(row)*cols+size_t(col)]);
                    }
                    CHECK(selected==high+small);
                    if (!std::isfinite(limit)) {
                        double dual=0;
                        for (size_t i=0; i<r; ++i) dual+=w.rectangular.row_dual[i];
                        for (size_t j=0; j<c; ++j) dual+=w.rectangular.column_dual[j];
                        CHECK(dual==selected);
                        for (size_t i=0; i<r; ++i) for (size_t j=0; j<c; ++j)
                            CHECK(w.rectangular.row_dual[i]+w.rectangular.column_dual[j]
                                  <=double(base[i*c+j]));
                    }
                }
            }
        }
    }
    // Preserve the cost-type rounding of limit/2 before widening state.
    const T costs[]={T(1./134217728),T(.5),T(.5),T(2)};
    Worker<T> w;
    int64_t output[4]; size_t count=0; double total=0;
    solve_one(w,costs,costs,std::is_same_v<T,float>,2,2,1-1./33554432,
              true,true,output,count,total);
    if constexpr (std::is_same_v<T,float>) CHECK(count==2 && total==1);
    else CHECK(count==1 && total==1./134217728);
}

template<class T> int call(size_t batch, size_t rows, size_t cols, size_t threads,
                          size_t& failed, double limit = inf<double>()) {
    std::vector<T> costs(batch*rows*cols, T(1));
    std::vector<int64_t> output(2*batch*std::min(rows,cols), -1);
    std::vector<size_t> counts(batch);
    std::vector<double> totals(batch);
    const int status = lapcpp_run(costs.data(), std::is_same<T,float>::value,
        costs.data(), std::is_same<T,float>::value, batch, rows, cols, limit,
        threads, 0, 1, 1, output.data(), counts.data(), totals.data(), &failed);
    if (!status) for (size_t b=0; b<batch; ++b) {
        const size_t k = std::isfinite(limit) && limit < 1 ? 0 : std::min(rows,cols);
        CHECK(counts[b] == k && totals[b] == double(k));
        std::set<int64_t> columns;
        for (size_t i=0; i<k; ++i) {
            const auto base = 2*b*std::min(rows,cols)+2*i;
            CHECK(output[base] >= 0 && size_t(output[base]) < rows);
            CHECK(output[base+1] >= 0 && size_t(output[base+1]) < cols);
            if (i) CHECK(output[base] > output[base-2]);
            columns.insert(output[base+1]);
        }
        CHECK(columns.size() == k);
    }
    return status;
}

void exercise_pool(const std::shared_ptr<Pool>& p, size_t count) {
    CHECK(p->size() == count);
    std::mutex mutex;
    std::condition_variable ready;
    std::set<std::thread::id> ids;
    bool release = false;
    std::vector<std::future<void>> completed;
    for (size_t i=0; i<count; ++i) {
        auto done = std::make_shared<std::promise<void>>();
        completed.push_back(done->get_future());
        p->submit([&,done] {
            {
                std::unique_lock<std::mutex> lock(mutex);
                ids.insert(std::this_thread::get_id());
                ready.notify_all();
                ready.wait(lock,[&] { return release; });
            }
            done->set_value();
        });
    }
    bool all_started;
    {
        std::unique_lock<std::mutex> lock(mutex);
        all_started = ready.wait_for(lock,std::chrono::seconds(10),[&] { return ids.size()==count; });
        release = true;
    }
    ready.notify_all();
    for (auto& done : completed) done.get();
    CHECK(all_started);
}

template<class T> void storage_and_failures() {
    for (auto shape : {std::pair<size_t,size_t>{3,10000}, {10000,3}, {10,10}}) {
        Worker<T> w;
        const auto r=shape.first, c=shape.second;
        std::vector<T> costs(r*c,T(1));
        std::vector<int64_t> out(2*std::min(r,c));
        size_t count=0; double total=0;
        solve_one(w,costs.data(),costs.data(),std::is_same<T,float>::value,r,c,.5,true,true,out.data(),count,total);
        CHECK(count==0 && total==0 && w.square.x.empty());
        CHECK(w.rectangular.x.size()==std::min(r,c) && w.rectangular.y.size()==std::max(r,c));
        CHECK(w.orientation.size()==(r>c ? r*c : 0));
    }
    Worker<T> w;
    w.orientation.resize(8*1024*1024/sizeof(T)+1);
    w.square.x.resize(65537); w.rectangular.y.resize(65537);
    w.release_large_buffers();
    CHECK(w.orientation.capacity()==0 && w.square.x.capacity()==0 && w.rectangular.y.capacity()==0);
    size_t failed=99;
    for (size_t after : {size_t(0),size_t(2)}) {
        worker<T>() = Worker<T>{};
        {
            test_faults::Scope inject({test_faults::Site::allocation,after,0});
            CHECK(call<T>(3,7,7,1,failed)==Allocation && failed==0);
        }
        CHECK(call<T>(3,7,7,1,failed)==0);
    }
    for (size_t threads : {size_t(1),size_t(3)}) {
        {
            test_faults::Scope inject({test_faults::Site::internal,0,2});
            CHECK(call<T>(5,7,9,threads,failed)==Internal && failed==2);
        }
        CHECK(call<T>(5,7,9,threads,failed)==0);
        std::vector<T> bad(5*3*3,T(0));
        std::fill(bad.begin()+9,bad.begin()+18,inf<T>());
        std::fill(bad.begin()+36,bad.end(),inf<T>());
        std::vector<int64_t> output(30);
        std::vector<size_t> counts(5);
        std::vector<double> totals(5);
        CHECK(lapcpp_run(bad.data(),std::is_same<T,float>::value,bad.data(),
            std::is_same<T,float>::value,5,3,3,inf<double>(),threads,0,1,1,
            output.data(),counts.data(),totals.data(),&failed)==Infeasible);
        CHECK(failed==1);
        CHECK(call<T>(5,3,3,threads,failed)==0);
    }
}

int main() {
    try {
        numerical_paths<float>(); numerical_paths<double>();
        working_precision<float>(); working_precision<double>();
        storage_and_failures<float>(); storage_and_failures<double>();
        auto retained=get_pool(16);
        CHECK(retained==get_pool(16));
        exercise_pool(retained,16);
        for (size_t n : {size_t(3),size_t(4),size_t(2)}) exercise_pool(get_pool(n),n);
        { std::lock_guard<std::mutex> lock(pool_cache().mutex); CHECK(pool_cache().entries.size()<=2); }
        exercise_pool(retained,16); // Eviction must not invalidate an in-flight owner.
        size_t failed=99;
        for (size_t after : {size_t(0),size_t(2)}) {
            { test_faults::Scope inject({test_faults::Site::worker,after,0});
              CHECK(call<double>(7,4,4,7,failed)==Allocation && failed==0); }
        }
        CHECK(call<double>(7,4,4,7,failed)==0);
        for (size_t after : {size_t(0),size_t(1)}) {
            { test_faults::Scope inject({test_faults::Site::submission,after,0});
              CHECK(call<double>(7,4,4,3,failed)==Allocation); }
            CHECK(call<double>(7,4,4,3,failed)==0);
        }
        get_pool(5); get_pool(6); // Evict the count used by the next call.
        CHECK(call<double>(3,4,4,128,failed)==0);
        { std::lock_guard<std::mutex> lock(pool_cache().mutex);
          CHECK(pool_cache().entries.back().first==3 && pool_cache().entries.back().second->size()==3); }
        double scalar=0, total=0; int64_t index=0; size_t count=0;
        CHECK(lapcpp_run(&scalar,0,&scalar,0,FREE,2,2,inf<double>(),1,0,1,1,
                         &index,&count,&total,&failed)==Dimensions);
        std::vector<std::future<void>> concurrent;
        for (size_t t=1; t<=8; ++t) concurrent.push_back(std::async(std::launch::async,[t] {
            size_t failure=0;
            for (size_t i=0; i<10; ++i) {
                CHECK(call<double>(5,17,19,t,failure)==0);
                CHECK(call<float>(5,19,17,t,failure,.5)==0);
            }
        }));
        for (auto& task : concurrent) task.get();
        for (size_t b : {size_t(0),size_t(1),size_t(2),FREE}) {
            CHECK(saturated_product(FREE,b)==(b==0 ? 0 : FREE));
            CHECK(use_parallel(b,16,128)==use_parallel(b,128,16));
            if (b<=1) CHECK(!use_parallel(b,FREE,FREE));
        }
        CHECK(use_parallel(FREE,FREE,FREE));
        std::cout << "C++ native resource, failure, recovery and concurrency checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
