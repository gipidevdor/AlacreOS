"""Demonstrate processor-demand admission for ideal single-core EDF."""

from dataclasses import dataclass
from fractions import Fraction
from math import lcm
from typing import Optional, Sequence, Tuple


@dataclass(frozen=True)
class Task:
    execution_us: int
    deadline_us: int
    period_us: int

    def __post_init__(self) -> None:
        if self.execution_us <= 0 or not 0 < self.deadline_us <= self.period_us:
            raise ValueError("Expected C > 0 and 0 < D <= T")

    def demand(self, interval_us: int) -> int:
        jobs = max(0, (interval_us - self.deadline_us) // self.period_us + 1)
        return jobs * self.execution_us


def overload(tasks: Sequence[Task]) -> Optional[Tuple[int, int]]:
    """Return the first overloaded interval in one hyperperiod, if any."""
    horizon = lcm(*(task.period_us for task in tasks))
    deadlines = {
        deadline
        for task in tasks
        for deadline in range(task.deadline_us, horizon + 1, task.period_us)
    }

    for interval in sorted(deadlines):
        demand = sum(task.demand(interval) for task in tasks)
        if demand > interval:
            return interval, demand

    return None


def main() -> None:
    examples = {
        "Feasible pair": (Task(40, 100, 1000), Task(50, 100, 1000)),
        "Deadline overload": (Task(60, 100, 1000), Task(60, 100, 1000)),
    }
    for name, tasks in examples.items():
        utilization = sum(Fraction(task.execution_us, task.period_us) for task in tasks)
        failure = overload(tasks)
        if failure is None:
            deadline = min(task.deadline_us for task in tasks)
            demand = sum(task.demand(deadline) for task in tasks)
            result = f"accepted ({demand} µs of demand in {deadline} µs)"
        else:
            interval, demand = failure
            result = f"rejected ({demand} µs of demand in {interval} µs)"
        print(f"{name} ({float(utilization):.0%} utilization): {result}")


if __name__ == "__main__":
    main()
