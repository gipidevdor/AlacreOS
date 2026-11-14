#pragma once

#include <stddef.h>

#include "edf.hpp"

namespace alacre {

enum class AdmissionStatus { feasible, overload, invalid_timing, analysis_limit };

struct AdmissionResult {
    AdmissionStatus status;
    Tick interval{};
};

// Independent sporadic tasks on one ideal preemptive core. The budget is the
// processor demand reserved per release. Validate the fixed table before use.
template <size_t count>
[[nodiscard]] consteval AdmissionResult admit_ideal_edf(const TaskTiming (&tasks)[count],
                                                       size_t max_intervals) noexcept {
    static_assert(count <= EdfScheduler::capacity);
    for (const auto &task : tasks) {
        if (!task.valid()) {
            return {AdmissionStatus::invalid_timing};
        }
    }

    Tick horizon = 1;
    for (const auto &task : tasks) {
        auto divisor = horizon;
        auto remainder = task.period;
        while (remainder != 0) {
            const auto next = divisor % remainder;
            divisor = remainder;
            remainder = next;
        }
        const auto factor = task.period / divisor;
        if (horizon > UINT64_MAX / factor) {
            return {AdmissionStatus::analysis_limit};
        }
        horizon *= factor;
    }

    // Demand increases only at a deadline. For D <= T, every hyperperiod
    // adds the same demand, and a feasible first hyperperiod has utilization <= 1.
    Tick deadlines[count]{};
    for (size_t i = 0; i < count; ++i) {
        deadlines[i] = tasks[i].deadline;
    }
    for (size_t checked = 0; ; ++checked) {
        Tick interval = 0;
        for (const auto deadline : deadlines) {
            if (deadline != 0 && (interval == 0 || deadline < interval)) {
                interval = deadline;
            }
        }
        if (interval == 0) {
            return {AdmissionStatus::feasible};
        }
        if (checked == max_intervals) {
            return {AdmissionStatus::analysis_limit, interval};
        }

        auto available = interval;
        for (size_t i = 0; i < count; ++i) {
            const auto &task = tasks[i];
            if (interval >= task.deadline) {
                const auto jobs = (interval - task.deadline) / task.period + 1;
                if (jobs > available / task.budget) {
                    return {AdmissionStatus::overload, interval};
                }
                available -= jobs * task.budget;
            }
            if (deadlines[i] == interval) {
                deadlines[i] = task.period <= horizon - interval ? interval + task.period : 0;
            }
        }
    }
}

} // namespace alacre
