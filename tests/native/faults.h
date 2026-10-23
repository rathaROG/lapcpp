// Test-only fault injection. This header is never included in a wheel build.
#pragma once
#include <new>
#include <stdexcept>
#include <system_error>
namespace test_faults {
enum class Site { none, allocation, worker, submission, internal };
struct Plan { Site site = Site::none; size_t after = 0; size_t batch = 0; };
inline thread_local Plan plan;
inline thread_local size_t batch_index = 0;
struct Scope {
    Plan saved = plan;
    size_t saved_batch = batch_index;
    Scope(Plan next, size_t batch = 0) { plan = next; batch_index = batch; }
    ~Scope() { plan = saved; batch_index = saved_batch; }
};
inline void hit(Site site) {
    if (plan.site != site || batch_index != plan.batch) return;
    if (plan.after) { --plan.after; return; }
    plan.site = Site::none;
    if (site == Site::worker)
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
    if (site == Site::internal) throw std::runtime_error("injected native failure");
    throw std::bad_alloc();
}
}
#define LAPCPP_TEST_POINT(site) test_faults::hit(test_faults::Site::site)
