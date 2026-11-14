#pragma once

#include <stdint.h>

namespace alacre {

using Tick = uint64_t;
using TaskId = unsigned;

struct TaskTiming {
    Tick budget;
    Tick deadline;
    Tick period;
};

enum class JobState { invalid_task, idle, ready, completed, budget_exhausted, deadline_missed };

struct JobSnapshot {
    JobState state;
    Tick remaining;
};

enum class EdfStatus {
    ok,
    invalid_task,
    invalid_timing,
    configuration_closed,
    active_job,
    faulted_job,
    early_release,
    overflow,
    invalid_time,
    missed_boundary,
    no_running_job,
};

// One EDF partition. The caller serializes events and supplies scheduler time.
class EdfScheduler {
public:
    static constexpr TaskId capacity = 8;
    static constexpr TaskId no_task = capacity;

    [[nodiscard]] EdfStatus configure(TaskId id, TaskTiming timing) noexcept {
        if (id >= capacity) {
            return EdfStatus::invalid_task;
        }
        if (started_) {
            return EdfStatus::configuration_closed;
        }
        if (timing.budget == 0 || timing.budget > timing.deadline ||
            timing.deadline > timing.period) {
            return EdfStatus::invalid_timing;
        }
        slots_[id].timing = timing;
        return EdfStatus::ok;
    }

    [[nodiscard]] EdfStatus release(TaskId id) noexcept {
        if (id >= capacity || slots_[id].timing.budget == 0) {
            return EdfStatus::invalid_task;
        }
        auto &slot = slots_[id];
        if (slot.state == JobState::ready) {
            return EdfStatus::active_job;
        }
        if (slot.state == JobState::budget_exhausted ||
            slot.state == JobState::deadline_missed) {
            return EdfStatus::faulted_job;
        }
        if (slot.state == JobState::completed && now_ - slot.release < slot.timing.period) {
            return EdfStatus::early_release;
        }
        if (now_ > UINT64_MAX - slot.timing.deadline || release_order_ == UINT64_MAX) {
            return EdfStatus::overflow;
        }
        started_ = true;
        slot.release = now_;
        slot.deadline = now_ + slot.timing.deadline;
        slot.remaining = slot.timing.budget;
        slot.order = release_order_++;
        slot.state = JobState::ready;
        return EdfStatus::ok;
    }

    [[nodiscard]] TaskId running() const noexcept {
        TaskId best = no_task;
        for (TaskId id = 0; id < capacity; ++id) {
            const auto &candidate = slots_[id];
            if (candidate.state != JobState::ready) {
                continue;
            }
            if (best == no_task || candidate.deadline < slots_[best].deadline ||
                (candidate.deadline == slots_[best].deadline && candidate.order < slots_[best].order)) {
                best = id;
            }
        }
        return best;
    }

    // While a job runs, this is the earlier of its deadline and budget boundary.
    // With no ready job, the caller supplies the next release time.
    [[nodiscard]] Tick next_boundary() const noexcept {
        const auto id = running();
        return id == no_task ? UINT64_MAX : boundary_of(slots_[id]);
    }

    [[nodiscard]] EdfStatus advance_to(Tick time) noexcept {
        return step(time, Event::elapsed);
    }

    [[nodiscard]] EdfStatus complete_at(Tick time) noexcept {
        return step(time, Event::completion);
    }

    [[nodiscard]] JobSnapshot job(TaskId id) const noexcept {
        if (id >= capacity || slots_[id].timing.budget == 0) {
            return {JobState::invalid_task, 0};
        }
        return {slots_[id].state, slots_[id].remaining};
    }

private:
    enum class Event { elapsed, completion };

    struct Slot {
        TaskTiming timing{};
        Tick release{};
        Tick deadline{};
        Tick remaining{};
        Tick order{};
        JobState state{JobState::idle};
    };

    [[nodiscard]] Tick boundary_of(const Slot &slot) const noexcept {
        const Tick until_deadline = slot.deadline - now_;
        return slot.remaining < until_deadline ? now_ + slot.remaining : slot.deadline;
    }

    // Process completion, budget exhaustion, and expiry before releases at this tick.
    [[nodiscard]] EdfStatus step(Tick time, Event event) noexcept {
        if (time < now_) {
            return EdfStatus::invalid_time;
        }
        const auto id = running();
        if (id == no_task) {
            if (event == Event::completion) {
                return EdfStatus::no_running_job;
            }
            now_ = time;
            return EdfStatus::ok;
        }

        auto &current = slots_[id];
        if (time > boundary_of(current)) {
            return EdfStatus::missed_boundary;
        }
        current.remaining -= time - now_;
        if (event == Event::completion) {
            current.remaining = 0;
            current.state = JobState::completed;
        } else if (current.remaining == 0) {
            current.state = JobState::budget_exhausted;
        }
        for (auto &slot : slots_) {
            if (slot.state == JobState::ready && slot.deadline == time) {
                slot.remaining = 0;
                slot.state = JobState::deadline_missed;
            }
        }
        now_ = time;
        return EdfStatus::ok;
    }

    Slot slots_[capacity]{};
    Tick now_{};
    Tick release_order_{};
    bool started_{};
};

} // namespace alacre
