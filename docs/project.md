# Project roadmap

The [v0.1 architecture specification](architecture-v0.1.md) defines the timing contract, isolation rules, and acceptance criteria. The order below follows the dependencies of the first implementation milestone.

| Step | Deliverable | Evidence |
| --- | --- | --- |
| Select reference platforms | One physical AArch64 target and one physical x86-64 target, with documented firmware and operating conditions. | A platform record that identifies the timer, interrupt controller, and shared hardware that can delay an RT job. |
| Bring up the kernel | Single-core boot, timer delivery, and two isolated user-space domains on each target. | Boot and timer traces, plus checks for rejected accesses and capability invocations. |
| Exercise scheduling and IPC | Static admission, EDF dispatch and preemption, bounded IPC, and budget enforcement. | Reproducible acceptance and rejection cases, queue saturation behavior, and a trace of an overrun with an independent job. |
| Qualify the first profile | The same reference workload at the 100 µs target on both physical platforms. | Timing analysis with justified bounds, corroborating traces, and an energy baseline at a declared measurement boundary. |

The [EDF example](../examples/edf/README.md) exercises admission in the ideal scheduler model. Use the [qualification guide](qualification.md) to record implementation and hardware evidence as it becomes available.

## Open decisions

- Select the two physical reference platforms and document the firmware settings that affect timing.
- Choose how to derive worst-case execution time (WCET) and bound timer delivery, system calls, interrupts, and non-preemptible sections.
- Choose a formal verification tool and define how model transitions map to implementation behavior.
- Define the reference workload and an equivalent energy measurement boundary for both targets.
