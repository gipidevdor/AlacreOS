#include "edf.hpp"

#include <assert.h>

using alacre::EdfScheduler;
using alacre::EdfStatus;
using alacre::JobState;

namespace {

void preemption_preserves_budget_and_release_order() {
    EdfScheduler edf;
    assert(edf.configure(0, {5, 10, 20}) == EdfStatus::ok);
    assert(edf.configure(1, {2, 10, 20}) == EdfStatus::ok);
    assert(edf.configure(2, {2, 3, 20}) == EdfStatus::ok);
    assert(edf.release(1) == EdfStatus::ok);
    assert(edf.release(0) == EdfStatus::ok);
    assert(edf.running() == 1);
    assert(edf.advance_to(1) == EdfStatus::ok);
    assert(edf.release(2) == EdfStatus::ok);
    assert(edf.running() == 2);
    assert(edf.job(1).remaining == 1);
    assert(edf.next_boundary() == 3);
    assert(edf.advance_to(3) == EdfStatus::ok);
    assert(edf.job(2).state == JobState::budget_exhausted);
    assert(edf.release(2) == EdfStatus::faulted_job);
    assert(edf.running() == 1);
    assert(edf.complete_at(4) == EdfStatus::ok);
    assert(edf.running() == 0);
    assert(edf.complete_at(9) == EdfStatus::ok);
    assert(edf.running() == EdfScheduler::no_task);
}

void exact_completion_and_expiry() {
    EdfScheduler edf;
    assert(edf.configure(0, {4, 4, 4}) == EdfStatus::ok);
    assert(edf.configure(1, {1, 4, 4}) == EdfStatus::ok);
    assert(edf.release(0) == EdfStatus::ok);
    assert(edf.release(1) == EdfStatus::ok);
    assert(edf.complete_at(4) == EdfStatus::ok);
    assert(edf.job(0).state == JobState::completed);
    assert(edf.job(1).state == JobState::deadline_missed);
    assert(edf.release(0) == EdfStatus::ok);
    assert(edf.release(0) == EdfStatus::active_job);
    assert(edf.complete_at(5) == EdfStatus::ok);
    assert(edf.release(0) == EdfStatus::early_release);
    assert(edf.advance_to(8) == EdfStatus::ok);
    assert(edf.release(0) == EdfStatus::ok);
}

void invalid_events_preserve_state() {
    EdfScheduler edf;
    assert(edf.configure(0, {1, 3, 2}) == EdfStatus::invalid_timing);
    assert(edf.configure(EdfScheduler::no_task, {1, 1, 1}) == EdfStatus::invalid_task);
    assert(edf.job(EdfScheduler::no_task).state == JobState::invalid_task);
    assert(edf.job(0).state == JobState::invalid_task);
    assert(edf.release(0) == EdfStatus::invalid_task);
    assert(edf.complete_at(0) == EdfStatus::no_running_job);
    assert(edf.configure(0, {1, 2, 3}) == EdfStatus::ok);
    assert(edf.release(0) == EdfStatus::ok);
    assert(edf.configure(1, {1, 1, 1}) == EdfStatus::configuration_closed);
    assert(edf.advance_to(2) == EdfStatus::missed_boundary);
    assert(edf.complete_at(2) == EdfStatus::missed_boundary);
    assert(edf.next_boundary() == 1);
    assert(edf.job(0).remaining == 1);
    assert(edf.complete_at(1) == EdfStatus::ok);
    assert(edf.advance_to(0) == EdfStatus::invalid_time);
    assert(edf.advance_to(UINT64_MAX) == EdfStatus::ok);
    assert(edf.release(0) == EdfStatus::overflow);
    assert(edf.job(0).state == JobState::completed);
}

void deadline_limits_resume_near_clock_limit() {
    EdfScheduler edf;
    assert(edf.configure(0, {8, 10, 10}) == EdfStatus::ok);
    assert(edf.configure(1, {5, 5, 5}) == EdfStatus::ok);
    assert(edf.advance_to(UINT64_MAX - 10) == EdfStatus::ok);
    assert(edf.release(0) == EdfStatus::ok);
    assert(edf.release(1) == EdfStatus::ok);
    assert(edf.complete_at(UINT64_MAX - 5) == EdfStatus::ok);
    assert(edf.running() == 0);
    assert(edf.next_boundary() == UINT64_MAX);
    assert(edf.advance_to(UINT64_MAX) == EdfStatus::ok);
    assert(edf.job(0).state == JobState::deadline_missed);
}

} // namespace

int main() {
    preemption_preserves_budget_and_release_order();
    exact_completion_and_expiry();
    invalid_events_preserve_state();
    deadline_limits_resume_near_clock_limit();
}
