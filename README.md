<picture>
  <source media="(prefers-color-scheme: dark)" srcset=".github/brand/cover-dark.svg">
  <img alt="" src=".github/brand/cover-light.svg" width="960" height="360">
</picture>

# AlacreOS

*Determinism by Design.*

AlacreOS is a capability-based microkernel operating system for industrial embedded systems and servers. Its design targets hard real-time workloads on qualified AArch64 and x86-64 platforms, with a kernel in freestanding C++23.

[Architecture specification](docs/architecture-v0.1.md) · [Roadmap](docs/project.md) · [Contributing](CONTRIBUTING.md) · [BSD 3-Clause](LICENSE)

## The design

| Area | Approach |
| --- | --- |
| Memory and authority | Alacre Core isolates address spaces and checks the rights carried by each capability. |
| CPU time | Alacre RT assigns work to cores before execution. Each core schedules eligible jobs by earliest deadline first (EDF) and enforces their budgets. |
| Communication | The first IPC profile uses preallocated queues and bounded messages. A full queue returns immediately. |
| Platform qualification | Timing evidence covers the workload, kernel, hardware, and firmware. Energy is measured alongside deadline outcomes for the same workload. |

Drivers, filesystems, networking, and the planned Alacre Linux compatibility layer run in user space. The [logical filesystem](docs/architecture-v0.1.md#logical-filesystem) follows the Unix directory hierarchy defined by FHS 3.0 and uses POSIX semantics for supported operations.

The [native identity service](docs/native-identity.md) runs in an isolated management domain in both QEMU development images. A separate host test exercises certificate authentication and persistent account state.

## First milestone

Alacre Core boots on one emulated core per target and receives platform data through a versioned boot handoff. Both development images exercise native identity, fixed-capacity IPC and EDF budget enforcement in isolated user-space domains. The build checks the user-space EDF test workload for feasible processor demand in the ideal single-core model. The first milestone targets **100 µs from programmed timer expiry to observable user-space job completion** on qualified physical hardware.

On Ubuntu 24.04, install the toolchains listed in the [kernel development guide](docs/kernel-development.md) and run:

```sh
make test HOST_CXX=g++-14
make smoke
```

The guide also covers other GCC toolchains, boot details, and the dispatcher contract.

Run the [EDF admission example](examples/edf/README.md) with Python 3.9 or newer from the repository root.

```sh
python3 examples/edf/admission.py
```

The example reproduces an accepted workload and a rejected workload in the ideal single-core model. The [qualification guide](docs/qualification.md) describes the additional evidence required on physical hardware.
