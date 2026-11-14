# Kernel Development

The bootstrap starts a freestanding C++23 ELF image on one virtual core and initializes a polling serial console. Both images run the same EDF scenario in kernel mode using synthetic ticks. It releases a second job with an earlier deadline, exhausts that job’s budget, then completes the first job with its remaining budget.

## Build and Test

Use Clang with the AArch64 and x86-64 ELF backends, the ELF linker LLD, a host C++23 compiler, Make, Python 3.9 or newer, and QEMU with the `virt-8.2` and `pc-i440fx-8.2` machines. Run these commands from the repository root. [Clang cross-compilation guide](https://clang.llvm.org/docs/CrossCompilation.html)

```sh
make kernels
make test
make smoke
```

Set `CLANGXX` or `LD_LLD` on the Make command line if the cross tools are outside `PATH`. `HOST_CXX` selects the compiler for the native tests. The images are written to `build/aarch64/kernel.elf` and `build/x86_64/kernel.elf`. The smoke test passes when each guest prints its architecture followed by `EDF smoke: PASS`.

| Target | Boot and console |
| --- | --- |
| AArch64 | QEMU loads the ELF image into the `virt-8.2` machine. The entry code sets up the stack and clears BSS. Serial output uses the development machine’s PL011 UART. [QEMU Arm virt](https://www.qemu.org/docs/master/system/arm/virt.html) |
| x86-64 | The QEMU PC loader uses the PVH ELF entry note. The bootstrap enters in 32-bit protected mode, initializes page tables, enables long mode, and sets up the stack. Serial output uses COM1. [Xen x86/HVM direct boot ABI](https://xenbits.xen.org/docs/4.16-testing/misc/pvh.html) |

## EDF Dispatcher Contract

One `EdfScheduler` represents one core’s partition. It stores up to eight task slots inline. The caller configures each task before the first successful release and serializes all events. The dispatcher validates `0 < Q ≤ D ≤ T`. Offline admission must separately establish feasibility for the workload and platform.

- Call `advance_to(t)` to charge elapsed execution to the selected job. Use `complete_at(t)` when that execution ends in completion. Process these events before submitting releases at tick `t`.
- Process the next budget or deadline boundary before advancing beyond it. With a running job, `next_boundary()` returns the earlier boundary. Use `running()` to distinguish an idle partition from a boundary at the maximum representable tick.
- A completion at the boundary succeeds if it stays within budget and meets the deadline. Without completion, simultaneous budget exhaustion and deadline expiry record budget exhaustion for the running job. Other ready jobs with that deadline expire in the same step.
- A faulted slot remains ineligible until the partition is reinitialized. A completed slot can release another job after its minimum inter-release interval. Errors leave the scheduler state intact.

Selection compares absolute deadlines and uses release order to break ties. Selection and expiry scans visit at most eight slots each. The tick value supplied during the boot test represents execution time directly. Hardware integration must distinguish time charged to a job from interrupt and kernel overhead and must bound the delay in enforcing a boundary.

## Freestanding C++23

The kernel build uses a fixed-size data model, strict compiler warnings, and an ELF link that rejects unresolved symbols. It disables exceptions, RTTI, and the compiler-generated stack protector for this bootstrap. Configure and release operations return checked status codes while the cross-target standard library is being qualified. The current macOS libc++ installation uses a hosted configuration, and its headers failed the freestanding `<expected>` probe. The architecture calls for `std::expected` once the relevant C++23 operations compile and link with a qualified freestanding library on both targets.

Hardware timer IRQs, exception handling, live context switching, address-space isolation, capabilities, and IPC are subsequent implementation work. The [qualification guide](qualification.md) defines the evidence for the 100 µs deadline and operational energy on physical hardware.
