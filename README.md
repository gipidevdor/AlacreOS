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

Drivers, filesystems, networking, and the planned Alacre Linux compatibility layer run in user space.

## First milestone

Alacre Core boots on one emulated core per target and exercises the same C++23 EDF dispatcher on both architectures. The first milestone extends that path to hardware timer interrupts, isolated user-space domains, capabilities, and bounded IPC. On qualified physical hardware, the reference workload targets **100 µs from programmed timer expiry to observable user-space job completion**.

Run the tests with Clang, LLD, QEMU, Make, and Python installed.

```sh
make test
make smoke
```

See the [kernel development guide](docs/kernel-development.md) for toolchain settings, boot details, and the dispatcher contract.

Run the [EDF admission example](examples/edf/README.md) with Python 3.9 or newer from the repository root.

```sh
python3 examples/edf/admission.py
```

The example reproduces an accepted workload and a rejected workload in the ideal single-core model. The [qualification guide](docs/qualification.md) describes the additional evidence required on physical hardware.
