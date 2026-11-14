#include "domain.hpp"
#include "edf_admission.hpp"
#include "user_edf_abi.hpp"
#include "user_smoke.hpp"

namespace alacre::domain {
namespace {

constexpr TaskId independent_job = 0;
constexpr TaskId overrun_job = 1;
constexpr TaskTiming profile[]{
    {8, 32, 64},
    {4, 16, 32},
};
constexpr size_t max_admission_intervals = 64;
static_assert(admit_ideal_edf(profile, max_admission_intervals).status == AdmissionStatus::feasible,
              "The user EDF smoke workload must fit the ideal single-core model");

constexpr Tick longest_period() noexcept {
    Tick longest = 0;
    for (const auto &timing : profile) {
        if (timing.period > longest) {
            longest = timing.period;
        }
    }
    return longest;
}

TaskTiming to_ticks(TaskTiming timing, Tick quantum) noexcept {
    return {timing.budget * quantum, timing.deadline * quantum, timing.period * quantum};
}

bool entered(const Event &event, uint64_t mode) noexcept {
    return event.stop == Stop::call && event.message.word[0] == edf_guest::entered && event.message.word[1] == mode;
}

} // namespace

bool user_edf_smoke() noexcept {
    EdfScheduler scheduler;
    const auto quantum = platform::timer_quantum();
    const auto epoch = platform::monotonic_now();
    constexpr auto max_period = longest_period();
    // Leave room for both relative deadlines, including the later release.
    if (quantum == 0 || quantum > (UINT64_MAX - epoch) / 2 / max_period ||
        !scheduler.configure(independent_job, to_ticks(profile[independent_job], quantum)) ||
        !scheduler.configure(overrun_job, to_ticks(profile[overrun_job], quantum)) ||
        !scheduler.advance_to(epoch, ExecutionCharge{0}) || !scheduler.release(independent_job)) {
        return false;
    }

    constexpr Message start_independent{{0}};
    const auto first = run_until(survivor, &start_independent, scheduler.next_boundary());
    if (!entered(first, 0) || first.timestamp < epoch ||
        !scheduler.advance_to(first.timestamp, ExecutionCharge{first.timestamp - epoch}) ||
        scheduler.running() != independent_job || !scheduler.release(overrun_job) ||
        scheduler.running() != overrun_job) {
        return false;
    }

    auto dispatch = platform::monotonic_now();
    constexpr Message start_overrun{{1}};
    if (!scheduler.advance_to(dispatch, ExecutionCharge{0}) || scheduler.running() != overrun_job) {
        return false;
    }
    const auto second = run_until(overrun, &start_overrun, scheduler.next_boundary());
    if (!entered(second, 1) || second.timestamp < dispatch ||
        !scheduler.advance_to(second.timestamp, ExecutionCharge{second.timestamp - dispatch})) {
        return false;
    }

    // Run through an intermediate timer event before enforcing the final budget.
    bool intermediate_timer_seen = false;
    for (unsigned attempt = 0; attempt < 32 && scheduler.running() == overrun_job; ++attempt) {
        dispatch = platform::monotonic_now();
        if (!scheduler.advance_to(dispatch, ExecutionCharge{0}) || scheduler.running() != overrun_job) {
            return false;
        }
        auto boundary = scheduler.next_boundary();
        if (!intermediate_timer_seen && boundary - dispatch > quantum) {
            boundary = dispatch + quantum;
        }
        const auto event = run_until(overrun, nullptr, boundary);
        if ((event.stop != Stop::timer && event.stop != Stop::interrupted) || event.timestamp < dispatch ||
            !scheduler.advance_to(event.timestamp, ExecutionCharge{event.timestamp - dispatch})) {
            return false;
        }
        if (event.stop == Stop::timer && scheduler.running() == overrun_job) {
            intermediate_timer_seen = true;
        }
    }
    const auto exhausted = scheduler.job(overrun_job);
    if (!intermediate_timer_seen || !exhausted || exhausted->state != JobState::budget_exhausted ||
        scheduler.running() != independent_job) {
        return false;
    }
    retire(overrun);
    if (run_until(overrun, nullptr, 0).stop != Stop::stopped) {
        return false;
    }

    dispatch = platform::monotonic_now();
    if (!scheduler.advance_to(dispatch, ExecutionCharge{0}) || scheduler.running() != independent_job) {
        return false;
    }
    const auto completion = run_until(survivor, nullptr, scheduler.next_boundary());
    if (completion.stop != Stop::call || completion.message.word[0] != edf_guest::completed ||
        completion.message.word[1] != edf_guest::marker || completion.timestamp < dispatch ||
        !scheduler.complete_at(completion.timestamp, ExecutionCharge{completion.timestamp - dispatch})) {
        return false;
    }
    retire(survivor);
    const auto finished = scheduler.job(independent_job);
    return finished && finished->state == JobState::completed && scheduler.running() == EdfScheduler::no_task;
}

} // namespace alacre::domain
