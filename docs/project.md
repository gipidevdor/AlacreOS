# Project roadmap

The [v0.1 architecture specification](architecture-v0.1.md) defines the timing contract, isolation rules, and acceptance criteria. The order below follows the dependencies of the first implementation milestone.

| Step | Deliverable | Evidence |
| --- | --- | --- |
| Select reference platforms | One physical AArch64 target and one physical x86-64 target, with documented firmware and operating conditions. | A platform record that identifies the timer, interrupt controller, and shared hardware that can delay an RT job. |
| Bring up the kernel | Single-core serial boot runs on both QEMU targets. The next implementation step connects the timer and exception entry, then starts two isolated user-space domains on each target. | Dual-architecture boot smoke tests, followed by timer traces and checks for rejected accesses and capability invocations. |
| Exercise scheduling and IPC | The shared EDF dispatcher handles job events and budget accounting with synthetic ticks. Connect those transitions to live timer interrupts and context switches, then add bounded IPC. | Host and guest EDF tests, followed by queue saturation behavior and a live overrun trace with an independent job. |
| Qualify the first profile | The same reference workload at the 100 µs target on both physical platforms. | Timing analysis with justified bounds, corroborating traces, and an energy baseline at a declared measurement boundary. |

The [EDF example](../examples/edf/README.md) exercises admission in the ideal scheduler model. The [kernel development guide](kernel-development.md) covers the current implementation and its tests. Use the [qualification guide](qualification.md) to record hardware evidence.

The [logical filesystem](architecture-v0.1.md#logical-filesystem) defines the standard directory layout and the first user-space filesystem service. Its implementation follows user-space IPC and is outside the first hard real-time milestone.

## Open decisions

- Select the two physical reference platforms and document the firmware settings that affect timing.
- Choose how to derive worst-case execution time (WCET) and bound timer delivery, system calls, interrupts, and non-preemptible sections.
- Choose a formal verification tool and define how model transitions map to implementation behavior.
- Define the reference workload and an equivalent energy measurement boundary for both targets.
