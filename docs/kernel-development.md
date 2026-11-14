# Kernel Development

The bootstrap starts a freestanding C++23 ELF image on one virtual core and initializes a polling serial console. A timer interrupt dispatches the first test thread. A second thread with an earlier deadline preempts it at release and runs until it exhausts its budget. The kernel resumes the first thread at its saved execution point. That thread checks its stack marker, reports completion through a kernel-mode trap, and returns control to the bootstrap.

## Build and Test

On x86-64 Ubuntu 24.04, install GCC 14 for both targets, LLVM tools, Make, Python, and QEMU. The reference identity host test also uses OpenSSL and SQLite development headers. Run these commands from the repository root. [Ubuntu GCC 14 for x86-64](https://packages.ubuntu.com/noble-updates/g%2B%2B-14) · [Ubuntu GCC 14 for AArch64](https://packages.ubuntu.com/noble/g%2B%2B-14-aarch64-linux-gnu)

```sh
sudo apt-get update
sudo apt-get install g++-14 g++-14-aarch64-linux-gnu libc6-dev-arm64-cross \
    lld llvm make python3 qemu-system-arm qemu-system-x86 libssl-dev libsqlite3-dev
make kernels
make test HOST_CXX=g++-14
make identity-targets
make identity-host-test HOST_CXX=g++-14
make smoke
```

The default guest compilers are `aarch64-linux-gnu-g++-14` and `x86_64-linux-gnu-g++-14`. Select other GCC ELF compilers with `AARCH64_CXX` and `X86_64_CXX`. If the compiler needs explicit target header paths, pass them as `-isystem` options in `AARCH64_CPPFLAGS` or `X86_64_CPPFLAGS`. Use `make -B` when changing compilers or header paths. `LD_LLD` selects the linker and `HOST_CXX` selects the compiler for native tests.

On macOS, the tested combination is Arm GNU Toolchain 15.2.Rel1 for AArch64, GCC 16.2 with target-configured freestanding libstdc++ headers for x86-64, and Apple Clang for the native tests. The Homebrew `x86_64-elf-gcc` package supplies the compiler. Provide the matching libstdc++ header set separately if the installation omits it.

The images are written to `build/aarch64/kernel.elf` and `build/x86_64/kernel.elf`. The smoke test checks each guest’s architecture, boot handoff, `Context EDF smoke: PASS`, and `Isolated identity IPC: PASS` on the serial console. Set `OBJCOPY` to the path to `llvm-objcopy` if the tool is outside `PATH`.

`make identity-targets` builds the freestanding [native identity core](native-identity.md) for both ISAs. `make test` exercises its policy in the host process. `make identity-host-test` runs the reference OpenSSL and SQLite adapters and accepts `HOST_IDENTITY_CPPFLAGS` and `HOST_IDENTITY_LIBS` for installations outside the toolchain search paths.

| Target | Boot and console |
| --- | --- |
| AArch64 | QEMU loads the ELF image into the `virt-8.2` machine. The entry code sets up the stack and clears BSS. Serial output uses the development machine’s PL011 UART. [QEMU Arm virt](https://www.qemu.org/docs/master/system/arm/virt.html) |
| x86-64 | The QEMU PC loader uses the PVH ELF entry note. The bootstrap enters in 32-bit protected mode, initializes page tables, enables long mode, and sets up the stack. Serial output uses COM1. [Xen x86/HVM direct boot ABI](https://xenbits.xen.org/docs/4.16-testing/misc/pvh.html) |

## Boot Handoff v1

The loader loads the ELF segments. Each assembly entry point masks maskable interrupts, zeros the linker-defined BSS, prepares the stack, and enters `kernel_main` with one physical address. AArch64 supplies the Devicetree address defined by the QEMU `virt` ELF boot protocol. x86-64 preserves the loader’s `%ebx` across the long mode transition and passes the PVH start record address. The C++ platform adapter decodes the source before the shared kernel consumes `BootInfo`.

| Field | Meaning |
| --- | --- |
| Header | Native-endian magic, version, byte size, platform ID, and a reserved field that must be zero. Version 1 is 72 bytes on both targets. |
| Kernel image | Half-open physical range from the first loaded kernel section through the end of BSS. It includes the bootstrap stack and any bootstrap page tables. |
| Platform data | The Devicetree blob on AArch64 or the PVH v1 start record on x86-64, identified by physical base and byte size. |
| E820 table | The physical byte range of the x86-64 memory map. AArch64 memory and device information remains in the Devicetree. |
| ACPI RSDP | The optional x86-64 physical address reported by the loader. A zero value means no address was supplied. |

The adapter validates the Devicetree header and block bounds within the space below the kernel image, or the PVH v1 record and E820 table bounds within the initially mapped low-memory window below the x86-64 kernel. The handoff borrows firmware data in place. Consumers must keep those ranges reserved and mapped while using them. Traversing the Devicetree, interpreting individual E820 entries, and validating or mapping ACPI tables are separate steps before their contents can guide memory allocation or device setup. The reference QEMU PC configuration can supply E820 without an RSDP. [Devicetree blob format](https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html) · [Xen PVH start record](https://raw.githubusercontent.com/xen-project/xen/master/xen/include/public/arch-x86/hvm/start_info.h)

## EDF Dispatcher Contract

One `EdfScheduler` represents one core’s partition. It stores up to eight task slots inline. The caller configures each task before the first successful release and serializes all events. The dispatcher validates `0 < Q ≤ D ≤ T`. Offline admission must separately establish feasibility for the workload and platform.

- Pass a monotonic timestamp and an explicit `ExecutionCharge` to `advance_to` or `complete_at`. Charge only execution attributable to the previously selected job. The charge must fit within the elapsed interval and must be zero when the partition has no ready job. Apply the event before issuing releases at the same timestamp.
- With a selected job, `next_boundary()` predicts the earlier of its deadline and the time at which its budget would run out if it executed continuously. A caller with an observed timestamp beyond that boundary can still settle the event. Use `running()` to distinguish an idle partition from a boundary at the maximum representable tick.
- A completion at the deadline succeeds if it stays within budget. An over-budget completion records budget exhaustion. A completion after its deadline and within budget records a deadline miss. Without completion, simultaneous budget exhaustion and deadline expiry record budget exhaustion for the selected job. The dispatcher expires every other ready job whose deadline has been reached.
- A faulted slot remains ineligible until the partition is reinitialized. A completed slot can release another job after its minimum inter-release interval. Errors leave the scheduler state intact.
- The state-changing operations return `std::expected<void, EdfError>`. `job(id)` returns `std::expected<JobSnapshot, EdfError>` so callers can distinguish an invalid slot from a configured idle task.

Selection compares absolute deadlines and uses release order to break ties. Selection and expiry scans visit at most eight slots each.

## Timer Interrupts

| Target | Interrupt path |
| --- | --- |
| AArch64 | QEMU `virt-8.2` with GICv2. The EL1 physical Generic Timer raises PPI 30. The handler disables the source before completing the interrupt at the GIC. [Arm Generic Timer](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/Generic%20Timer.pdf?revision=c710e7a7-9f52-4901-8c9d-91b19f44f9c7) · [QEMU virt interrupt wiring](https://github.com/qemu/qemu/blob/v8.2.0/hw/arm/virt.c) |
| x86-64 | QEMU `pc-i440fx-8.2` with xAPIC and the legacy PICs masked. A dedicated uncached bootstrap mapping covers the Local APIC page. The one-shot timer enters through an IDT interrupt gate, and the handler writes the APIC end-of-interrupt register. [Intel System Programming Guide, Local APIC Timer](https://cdrdv2-public.intel.com/835754/253668-sdm-vol-3a.pdf) |

The interrupt entry saves integer registers and the return state on the interrupted thread’s stack. After sampling the local counter and acknowledging the timer, the handler applies the execution charge, processes a release if due, and chooses the frame to restore. The test threads run in kernel mode with general-purpose registers and separate stacks. The bootstrap waits with `WFI` on AArch64 and `HLT` on x86-64. A missing interrupt is detected by the host smoke-test timeout. Unexpected exceptions halt the guest.

AArch64 uses `CNTPCT_EL0` for timestamps and programs the absolute EDF event into `CNTP_CVAL_EL0`. The x86-64 QEMU TCG target uses TSC timestamps and a Local APIC one-shot countdown. At boot it samples the TSC delta for a known, masked APIC count and uses that ratio to convert future event times. The conversion rounds up and uses a bounded countdown. On delivery, the dispatcher uses the observed timestamp, so an early or capped timer can be rearmed. This calibration supports the development test. A physical real-time profile needs separately justified bounds on timer conversion and interrupt delivery. [Arm Generic Timer counter and comparator](https://documentation-service.arm.com/static/66c4754f32f35b31ceb317ff) · [Intel System Programming Guide, Local APIC Timer](https://cdrdv2-public.intel.com/835754/253668-sdm-vol-3a.pdf)

### Live Scheduling Contract

At dispatch, the test advances the scheduler to a fresh clock sample with zero execution charge. It arms the earlier of the next release and the selected job’s deadline or projected budget exhaustion. When an interrupt or completion trap arrives, it charges the interval from that dispatch sample to the timestamp in the handler. The charge includes the final scheduling work and the exception-entry overhead. The initial bootstrap wait advances deadline time with zero charge. Admission must eventually account for the clock-to-timer conversion and the delay from expiry to enforcement.

## Freestanding C++23

The kernel build uses a fixed-size data model, strict compiler warnings, and a static ELF link that rejects unresolved symbols. It disables exceptions, RTTI, and the compiler-generated stack protector for this bootstrap. Kernel builds require the `__cpp_lib_freestanding_expected` feature macro in C++23 mode. The linker receives only kernel object files, so the guest images use the target headers without linking a C or C++ standard library. [GCC libstdc++ compilation options](https://gcc.gnu.org/onlinedocs/libstdc%2B%2B/manual/using.html)

The development identity path uses statically mapped EL0 or ring 3 domains with separate page tables. It copies six integer registers per trap and stamps the caller and clock in the kernel. A fault or timer watchdog stops the active domain. General capabilities, queued IPC and EDF scheduling for admitted user-mode jobs are subsequent work. The [qualification guide](qualification.md) defines the evidence for the 100 µs deadline and operational energy on physical hardware.
