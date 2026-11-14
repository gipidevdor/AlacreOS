<picture>
  <source media="(prefers-color-scheme: dark)" srcset=".github/brand/cover-dark.svg">
  <img alt="" src=".github/brand/cover-light.svg" width="960" height="360">
</picture>

# AlacreOS

*Determinism by Design.*

AlacreOS is a capability-based microkernel operating system project for industrial embedded systems and servers. It targets hard real-time operation on qualified AArch64 and x86-64 platforms and uses freestanding C++23.

The project is at the architecture stage. The first implementation milestone covers single-core boot, address-space isolation, capabilities, a timer, EDF scheduling, and bounded IPC on both architectures.

[Architecture specification](docs/architecture-v0.1.md) · [Project status](docs/project.md) · [Contributing](CONTRIBUTING.md) · [License](LICENSE)
