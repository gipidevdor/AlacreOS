#pragma once

#include <expected>
#include <stdint.h>

#if !__STDC_HOSTED__ && !defined(__cpp_lib_freestanding_expected)
#error "Alacre Core requires a C++23 library with freestanding std::expected"
#endif

namespace alacre {

using Tick = uint64_t;
using TaskId = unsigned;

struct ExecutionCharge {
    explicit constexpr ExecutionCharge(Tick amount) noexcept : ticks{amount} {}

    Tick ticks;
};

struct TaskTiming {
    Tick budget;
    Tick deadline;
    Tick period;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return budget != 0 && budget <= deadline && deadline <= period;
    }
};

enum class JobState { idle, ready, completed, budget_exhausted, deadline_missed };

struct JobSnapshot {
    JobState state;
    Tick remaining;
};

enum class EdfError {
    invalid_task,
    invalid_timing,
    configuration_closed,
    active_job,
    faulted_job,
    early_release,
    overflow,
    invalid_time,
    invalid_charge,
    no_running_job,
};

// One EDF partition. The caller serializes events and supplies scheduler time.
class EdfScheduler {
public:
    static constexpr TaskId capacity = 8;
    static constexpr TaskId no_task = capacity;

    [[nodiscard]] std::expected<void, EdfError> configure(TaskId id, TaskTiming timing) noexcept {
        if (id >= capacity) {
            return std::unexpected(EdfError::invalid_task);
        }
        if (started_) {
            return std::unexpected(EdfError::configuration_closed);
        }
        if (!timing.valid()) {
            return std::unexpected(EdfError::invalid_timing);
        }
        slots_[id].timing = timing;
        return {};
    }

    [[nodiscard]] std::expected<void, EdfError> release(TaskId id) noexcept {
        if (id >= capacity || slots_[id].timing.budget == 0) {
            return std::unexpected(EdfError::invalid_task);
        }
        auto &slot = slots_[id];
        if (slot.state == JobState::ready) {
            return std::unexpected(EdfError::active_job);
        }
        if (slot.state == JobState::budget_exhausted ||
            slot.state == JobState::deadline_missed) {
            return std::unexpected(EdfError::faulted_job);
        }
        if (slot.state == JobState::completed && now_ - slot.release < slot.timing.period) {
            return std::unexpected(EdfError::early_release);
        }
        if (now_ > UINT64_MAX - slot.timing.deadline || release_order_ == UINT64_MAX) {
            return std::unexpected(EdfError::overflow);
        }
        started_ = true;
        slot.release = now_;
        slot.deadline = now_ + slot.timing.deadline;
        slot.remaining = slot.timing.budget;
        slot.order = release_order_++;
        slot.state = JobState::ready;
        return {};
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

    // Predict the next boundary if the selected job runs continuously from now_.
    // With no ready job, the caller supplies the next release time.
    [[nodiscard]] Tick next_boundary() const noexcept {
        const auto id = running();
        return id == no_task ? UINT64_MAX : boundary_of(slots_[id]);
    }

    [[nodiscard]] std::expected<void, EdfError>
    advance_to(Tick time, ExecutionCharge execution) noexcept {
        return step(time, execution.ticks, Event::elapsed);
    }

    [[nodiscard]] std::expected<void, EdfError>
    complete_at(Tick time, ExecutionCharge execution) noexcept {
        return step(time, execution.ticks, Event::completion);
    }

    [[nodiscard]] std::expected<JobSnapshot, EdfError> job(TaskId id) const noexcept {
        if (id >= capacity || slots_[id].timing.budget == 0) {
            return std::unexpected(EdfError::invalid_task);
        }
        return JobSnapshot{slots_[id].state, slots_[id].remaining};
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
    [[nodiscard]] std::expected<void, EdfError> step(Tick time, Tick execution,
                                                    Event event) noexcept {
        if (time < now_) {
            return std::unexpected(EdfError::invalid_time);
        }
        const auto id = running();
        if (execution > time - now_ || (id == no_task && execution != 0)) {
            return std::unexpected(EdfError::invalid_charge);
        }
        if (id == no_task) {
            if (event == Event::completion) {
                return std::unexpected(EdfError::no_running_job);
            }
            now_ = time;
            return {};
        }

        auto &current = slots_[id];
        if (execution > current.remaining ||
            (execution == current.remaining && event == Event::elapsed)) {
            current.remaining = 0;
            current.state = JobState::budget_exhausted;
        } else if (event == Event::completion && time <= current.deadline) {
            current.remaining = 0;
            current.state = JobState::completed;
        } else {
            current.remaining -= execution;
        }
        for (auto &slot : slots_) {
            if (slot.state == JobState::ready && slot.deadline <= time) {
                slot.remaining = 0;
                slot.state = JobState::deadline_missed;
            }
        }
        now_ = time;
        return {};
    }

    Slot slots_[capacity]{};
    Tick now_{};
    Tick release_order_{};
    bool started_{};
};

} // namespace alacre
