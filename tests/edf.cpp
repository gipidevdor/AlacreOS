#include "edf.hpp"

#include <assert.h>

using alacre::EdfScheduler;
using alacre::EdfError;
using alacre::ExecutionCharge;
using alacre::JobState;

namespace {

void preemption_preserves_budget_and_release_order() {
    EdfScheduler edf;
    assert(edf.configure(0, {5, 10, 20}));
    assert(edf.configure(1, {2, 10, 20}));
    assert(edf.configure(2, {2, 3, 20}));
    assert(edf.release(1));
    assert(edf.release(0));
    assert(edf.running() == 1);
    assert(edf.advance_to(1, ExecutionCharge{1}));
    assert(edf.release(2));
    assert(edf.running() == 2);
    assert(edf.job(1) && edf.job(1)->remaining == 1);
    assert(edf.next_boundary() == 3);
    assert(edf.advance_to(3, ExecutionCharge{2}));
    assert(edf.job(2) && edf.job(2)->state == JobState::budget_exhausted);
    assert(edf.release(2) == std::unexpected(EdfError::faulted_job));
    assert(edf.running() == 1);
    assert(edf.complete_at(4, ExecutionCharge{1}));
    assert(edf.running() == 0);
    assert(edf.complete_at(9, ExecutionCharge{5}));
    assert(edf.running() == EdfScheduler::no_task);
}

void exact_completion_and_expiry() {
    EdfScheduler edf;
    assert(edf.configure(0, {4, 4, 4}));
    assert(edf.configure(1, {1, 4, 4}));
    assert(edf.release(0));
    assert(edf.release(1));
    assert(edf.complete_at(4, ExecutionCharge{4}));
    assert(edf.job(0) && edf.job(0)->state == JobState::completed);
    assert(edf.job(1) && edf.job(1)->state == JobState::deadline_missed);
    assert(edf.release(0));
    assert(edf.release(0) == std::unexpected(EdfError::active_job));
    assert(edf.complete_at(5, ExecutionCharge{1}));
    assert(edf.release(0) == std::unexpected(EdfError::early_release));
    assert(edf.advance_to(8, ExecutionCharge{0}));
    assert(edf.release(0));
}

void invalid_events_preserve_state() {
    EdfScheduler edf;
    assert(edf.configure(0, {1, 3, 2}) == std::unexpected(EdfError::invalid_timing));
    assert(edf.configure(EdfScheduler::no_task, {1, 1, 1}) == std::unexpected(EdfError::invalid_task));
    assert(edf.job(EdfScheduler::no_task) == std::unexpected(EdfError::invalid_task));
    assert(edf.job(0) == std::unexpected(EdfError::invalid_task));
    assert(edf.release(0) == std::unexpected(EdfError::invalid_task));
    assert(edf.complete_at(0, ExecutionCharge{0}) == std::unexpected(EdfError::no_running_job));
    assert(edf.advance_to(5, ExecutionCharge{1}) == std::unexpected(EdfError::invalid_charge));
    assert(edf.configure(0, {1, 2, 3}));
    const auto idle = edf.job(0);
    assert(idle && idle->state == JobState::idle && idle->remaining == 0);
    assert(edf.release(0));
    assert(edf.configure(1, {1, 1, 1}) == std::unexpected(EdfError::configuration_closed));
    assert(edf.advance_to(2, ExecutionCharge{3}) == std::unexpected(EdfError::invalid_charge));
    assert(edf.complete_at(2, ExecutionCharge{3}) == std::unexpected(EdfError::invalid_charge));
    assert(edf.next_boundary() == 1);
    assert(edf.job(0) && edf.job(0)->remaining == 1);
    assert(edf.complete_at(1, ExecutionCharge{1}));
    assert(edf.advance_to(0, ExecutionCharge{0}) == std::unexpected(EdfError::invalid_time));
    assert(edf.advance_to(UINT64_MAX, ExecutionCharge{0}));
    assert(edf.release(0) == std::unexpected(EdfError::overflow));
    assert(edf.job(0) && edf.job(0)->state == JobState::completed);
}

void deadline_limits_resume_near_clock_limit() {
    EdfScheduler edf;
    assert(edf.configure(0, {8, 10, 10}));
    assert(edf.configure(1, {5, 5, 5}));
    assert(edf.advance_to(UINT64_MAX - 10, ExecutionCharge{0}));
    assert(edf.release(0));
    assert(edf.release(1));
    assert(edf.complete_at(UINT64_MAX - 5, ExecutionCharge{5}));
    assert(edf.running() == 0);
    assert(edf.next_boundary() == UINT64_MAX);
    assert(edf.advance_to(UINT64_MAX, ExecutionCharge{5}));
    assert(edf.job(0) && edf.job(0)->state == JobState::deadline_missed);
}

void wall_time_and_execution_are_independent() {
    EdfScheduler edf;
    assert(edf.configure(0, {5, 10, 20}));
    assert(edf.configure(1, {3, 10, 20}));
    assert(edf.configure(2, {4, 20, 30}));
    assert(edf.release(0));
    assert(edf.release(1));
    assert(edf.release(2));
    assert(edf.advance_to(8, ExecutionCharge{0}));
    assert(edf.job(0) && edf.job(0)->remaining == 5);
    assert(edf.next_boundary() == 10);
    assert(edf.advance_to(12, ExecutionCharge{2}));
    assert(edf.job(0) && edf.job(0)->state == JobState::deadline_missed);
    assert(edf.job(1) && edf.job(1)->state == JobState::deadline_missed);
    assert(edf.running() == 2);
    assert(edf.job(2) && edf.job(2)->remaining == 4);
    assert(edf.complete_at(17, ExecutionCharge{5}));
    assert(edf.job(2) && edf.job(2)->state == JobState::budget_exhausted);
}

void late_completion_is_a_miss() {
    EdfScheduler edf;
    assert(edf.configure(0, {5, 10, 10}));
    assert(edf.release(0));
    assert(edf.complete_at(11, ExecutionCharge{5}));
    assert(edf.job(0) && edf.job(0)->state == JobState::deadline_missed);
}

} // namespace

int main() {
    preemption_preserves_budget_and_release_order();
    exact_completion_and_expiry();
    invalid_events_preserve_state();
    deadline_limits_resume_near_clock_limit();
    wall_time_and_execution_are_independent();
    late_completion_is_a_miss();
}
