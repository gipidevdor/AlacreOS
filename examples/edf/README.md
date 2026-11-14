# EDF admission on one ideal core

Run from the repository root with Python 3.9 or newer.

```sh
python3 examples/edf/admission.py
```

```text
Feasible pair (9% utilization): accepted (90 µs of demand in 100 µs)
Deadline overload (12% utilization): rejected (120 µs of demand in 100 µs)
```

Each pair consists of independent sporadic tasks with a 1 ms minimum interval between releases (`T`) and a 100 µs relative deadline (`D`). Their worst-case job execution times (`C`) are 40 and 50 µs in the first pair, and 60 µs each in the second. When both tasks release together, the second pair demands 120 µs before either deadline despite using only 12% of the core over the longer interval.

## What the example checks

The [processor-demand formula](../../docs/architecture-v0.1.md#admission-control) counts jobs that can both arrive and become due inside an interval of length `t`. The check examines the points where demand increases, `D + kT` for each task, through the least common multiple `H` of the periods. For `0 < D ≤ T`, demand at `H` is `H × U`, where `U` is total utilization. If `U > 1`, demand must exceed capacity by the last checked deadline. If `U ≤ 1`, extending any interval by `H` adds `H` units of capacity and only `H × U` units of demand. This enumeration is intended for small examples because the hyperperiod can grow rapidly.

The model assumes a fully preemptive processor, independent jobs, and zero scheduling overhead. The [qualification guide](../../docs/qualification.md) covers the additional evidence for a platform, including interrupts, IPC, and budget enforcement.
