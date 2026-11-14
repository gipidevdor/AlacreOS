# AlacreOS Architecture Specification v0.1

[Roadmap](project.md) · [Kernel development](kernel-development.md) · [Qualification guide](qualification.md) · [EDF admission example](../examples/edf/README.md)

## Purpose and Guarantees

**Alacre Core protects memory, authority, and CPU time.** Each resource must be explicitly represented, allocated through authorized operations, and kept within verifiable limits. This specification defines the microkernel requirements and the evidence needed to substantiate Alacre RT guarantees. Alacre RT treats operational energy as a platform qualification and optimization criterion within the hard real-time contract.

AlacreOS is a capability-based microkernel operating system for AArch64 and x86_64. The design calls for a kernel in freestanding C++23 and targets industrial embedded systems and servers. The project uses the BSD 3-Clause license. Alacre RT is the hard real-time environment, Alacre Linux is the planned Linux compatibility layer, and Alacre SDK is the planned set of development tools and libraries.

### Scope of the Guarantees

A hard real-time guarantee identifies the kernel binary and configuration, the admitted workload, the qualified hardware platform and firmware, and the operating conditions. The target application deadline range is **100 µs–10 ms**. Every timing guarantee requires evidence for the stated configuration.

Platform qualification documents the CPU, timer, interrupt controller, hardware topology and shared resources, peripherals and DMA, firmware, and settings that affect latency. It must enforce or account for bounds on interference from caches, memory, interconnects, other cores, DMA, and firmware activity. A configuration qualifies only when every relevant source of interference is bounded. [Linux kernel documentation on real-time hardware](https://docs.kernel.org/core-api/real-time/hardware.html)

## Execution Model

Alacre Core is preemptive. Non-preemptible sections, including intervals with interrupts masked, must have a justified upper bound on duration. Ready queues, deadline tracking, and time accounting are primarily core-local. The timing analysis must cover any inter-core coordination that can delay an admitted job.

**Partitioned EDF.** Before RT operation begins, each periodic or sporadic task is statically assigned to a logical core. Hard real-time jobs stay on that core throughout RT operation. The kernel selects the eligible job with the earliest absolute deadline. For equal deadlines, it uses the release order assigned by the local scheduler and preserves that order across preemption. A job is eligible when it is ready and has budget remaining. A newly released job with an earlier deadline than the running job preempts it within the stated latency bound.

A task defines a sequence of activations. Each activation releases a job with a release time, an absolute deadline, and a remaining budget. In the initial profile, each task has at most one active job, associated with a thread and a dedicated `SchedulingContext`. A job runs until it completes, is preempted, exhausts its budget, reaches its deadline, or faults. The initial profile permits non-blocking IPC and admits jobs that can execute without self-suspension.

**Memory and interrupts.** Pages, stacks, mappings, and kernel structures needed on RT paths are prepared and made resident before activation. Immediate interrupt handling does the minimum required to acknowledge, contain, and report the event. Deferred work receives a budget and a release contract. The profile declares interrupt rates and burst limits and defines how to contain sources that exceed them.

Drivers, filesystems, networking, and Linux compatibility run in user space. Non-RT activities use dedicated cores or a CPU allocation with temporal isolation and a validated timing analysis. An RT dependency is admissible only when the delays it introduces have verifiable bounds.

## Objects and Invariants

A protection domain groups threads and the resources they are authorized to access. A thread can invoke an object through a capability. The service also needs a `SchedulingContext` with budget available to execute.

| Object | Responsibility |
| --- | --- |
| `AddressSpace` | Virtual address space, authorized mappings, and their permissions. |
| `Thread` | Register state, execution state, and domain membership. |
| `SchedulingContext` | Assigned core, timing parameters, and the job’s deadline and remaining budget. |
| `CapabilitySpace` | The domain’s capabilities and the operations they authorize on each object. |
| `Endpoint` | IPC channel with defined directions, capacity, and behavior when full. |
| `MemoryObject` | Memory with explicit ownership, CPU and DMA access rights, and lifecycle. |
| `IRQBinding` | Authority to receive and handle a specific interrupt source under the configured rules. |

1. **Explicit authority.** The kernel checks the capability’s type, validity, and rights before operating on a protected object. Derivation and delegation can preserve or reduce those rights. Stale references stay invalid when their slots are reused.

2. **Authorized sharing.** Access to domain resources by a user-space thread or an assigned device must stay within the effective mappings and permissions. Drivers that use DMA require an IOMMU or SMMU, or equivalent protection demonstrated on the actual topology. Memory is zeroed before reassignment to a domain that lacked access to its previous contents. [Linux kernel documentation on DMA isolation and IOMMU topology](https://docs.kernel.org/driver-api/vfio.html#groups-devices-and-iommus)

3. **Bounded resources.** The configuration must set quotas for pages, kernel objects, endpoints, messages, and CPU time, with rules for charging and releasing them. Hitting a quota produces a defined outcome and preserves other domains’ quotas.

4. **Bounded RT paths.** Every admitted path, including error paths, has a provable bound. RT paths use preallocated resources, resident memory, bounded accesses, and waits with a defined time limit. RT code excludes dynamic allocation, demand paging, and unbounded scans.

5. **Fault containment.** A faulty domain stays within its assigned rights and quotas, including during shutdown. It can still modify shared memory within the permissions granted before the fault until those permissions are withdrawn or execution stops. The availability of services that depend on the domain follows their fault-handling contracts.

## Timing Contract and Admission

### Parameters and Accounting

| Parameter | Meaning |
| --- | --- |
| `C` | Upper bound on job execution time under the stated conditions, with an explicit accounting scope. |
| `Q` | Enforced job budget. It must cover the worst-case work charged to the context. |
| `T` | Minimum interval between consecutive releases in the scheduler model. |
| `D` | Deadline relative to release. The initial profile requires `0 < D ≤ T`. |
| `J` | Upper bound on the delay from the reference event to job release. |
| Resources and dependencies | Limits on memory, messages, device access, and service use. |

A job released at time `r` must reach an observable completion by `r + D`. A deadline measured from a physical event includes the delays along the entire path, from acquisition to observable effect. Pre-release costs count toward `J` when they occur between its defined start and end points. If the minimum interval applies to source events and jitter can bring releases closer together, the model must use a valid bound on actual inter-release times or a demand function that accounts for that jitter.

In the initial profile, the kernel assigns `Q` at an admitted release and charges the job’s work to it, including system calls executed on its behalf within the stated accounting scope. Any remaining budget expires when the job completes or stops. Completion recorded exactly at the deadline is valid if the job is within budget. An unfinished job loses eligibility at its deadline. The interference model accounts for any delay in enforcing suspension. When a deadline coincides with the next release, the kernel closes the expired job before making the next job eligible. Admission reserves the CPU time each job can actually consume, including the bound on suspension delay where applicable. The model must account consistently for interrupts, dispatch, context switches, and kernel work that cannot be charged to individual jobs.

### Admission Control

In the ideal model of independent sporadic tasks on a preemptive processor, a task’s processor demand over an interval of length `t` is `max(0, floor((t − D) / T) + 1) × C`. For example, two tasks with `C = 60 µs`, `D = 100 µs`, and `T = 1 ms` have a combined utilization of 12%. A simultaneous release requires 120 µs of execution within 100 µs, so the task set fails admission even in the ideal model. [Zhang and Burns, EDF processor-demand analysis, §3](https://www.cs.york.ac.uk/rts/static/papers/R%3AZhang%3A2009a.pdf)

The [runnable EDF example](../examples/edf/README.md) reproduces this rejection and compares it with a feasible task set in the same ideal model.

For each core, admission control must show that admitted demand and additional costs fit within the available processor capacity over every relevant interval. It must account for blocking, non-preemptible sections, timer behavior and clock resolution, kernel overhead, interrupt bursts, and interference from shared resources. The analysis must explicitly assign each cost to demand, available capacity, or a per-job bound and document the assumptions used to combine those terms. If the test enumerates intervals, it must justify the time horizon and the intervals it checks.

The initial profile uses static admission. Before starting the tasks, the system must apply a configuration backed by reproducible analysis. A change to tasks, budgets, topology, or another assumption invalidates admission for the modified configuration until it is reassessed. For IPC or I/O chains, per-core EDF feasibility must be accompanied by end-to-end bounds on release, queueing, service, and delivery.

## Energy and Power Management

Alacre RT considers energy during platform selection, static task placement, and admission. Among configurations that meet the timing contract and any configured power or energy limits, prefer the one that uses less energy for the same useful work. During RT operation, static core assignments and EDF ordering remain in force.

### Power States and Timing Bounds

Each qualified AArch64 and x86_64 configuration declares its permitted CPU and device power states, its minimum delivered performance under the stated power and thermal conditions, and the shared hardware domains affected by each control. The initial profile uses a fixed, qualified performance configuration for RT cores. It permits idle states only when their worst-case entry and wake-up costs, interrupt delivery, and any cache or device restart costs are covered by the timing analysis. That analysis must include sporadic releases that can arrive while a core is idle. [Linux kernel documentation on CPU idle latency](https://docs.kernel.org/admin-guide/pm/cpuidle.html)

Later profiles may admit dynamic voltage and frequency scaling, deeper idle states, or power caps when analysis covers their worst-case effects on all affected RT cores. Admission must account for transition latency, reduced execution capacity, shared power limits, and any hardware or firmware throttling that can occur within the operating envelope. A power or thermal setting that changes an admission assumption requires reassessment. If the platform leaves the qualified thermal envelope, the system follows its declared fault response. [Arm documentation on SCMI power and performance management](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/improved-power-management-system-control-through-scmi) · [Linux kernel documentation on firmware and thermal interference](https://docs.kernel.org/core-api/real-time/hardware.html#firmware)

### Energy Measurement and Controls

Platform backends expose measurements by hardware domain, using available counters or sensors such as Intel RAPL on supported x86_64 hardware and platform telemetry on supported AArch64 hardware. An external meter provides a reference or a fallback when onboard telemetry is unavailable or has insufficient coverage. Each reading identifies its measured boundary, units, sampling interval, and whether the value is metered or estimated. Shared-domain energy can support an attributed estimate when the method is stated. [Linux kernel documentation on RAPL domains and energy counters](https://docs.kernel.org/power/powercap/powercap.html) · [Arm documentation on platform telemetry](https://developer.arm.com/community/arm-community-blogs/b/servers-and-cloud-computing-blog/posts/out-of-band-telemetry-on-arm-neoverse-servers)

Sample telemetry outside critical paths and include any resulting interference in the timing analysis. Give the userspace power manager separate capabilities for reading telemetry and requesting changes. Privileged platform code enforces the power settings admitted for all affected RT domains. Any claimed hard power or energy limit requires a defined measurement boundary, a time window, and evidence that the platform can enforce the limit while meeting admitted deadlines.

### Reference Measurements

For each ISA, record idle power in watts and total energy in joules over a fixed workload run. Report energy per completed unit of work from the aggregate measurement and report deadline misses for the same run. Record the workload, firmware, power profile, thermal conditions, measurement boundary, and instrument uncertainty. Use equivalent boundaries for cross-platform comparisons, such as both systems’ DC inputs, and report package-level readings separately.

## IPC and Resource Lifecycle

Alacre RT uses control messages with a fixed maximum size and preallocated queues. Large payloads can reside in shared pools prepared before activation. The initial endpoint has one producer and one consumer on the same core. Sending to a full queue immediately returns `would_block`, preserving the queue state and buffer rights. For a chain that requires delivery, the analysis must show that the admitted traffic cannot fill the queue or account for the application’s response to a full queue within its contract.

A descriptor identifies a pool buffer and the valid range within it. The protocol defines who can use and return the buffer and when it can be reused. Data that must remain immutable across mutually untrusted domains requires a size-bounded copy or a transfer of write rights that is enforced for both CPU and DMA access and included in the timing analysis. A sender that retains a writable mapping can still modify the shared payload. For the first milestone, messages that require immutability across untrusted domains use a bounded copy.

An operation is *wait-free* if every invocation completes with a defined outcome within a bounded number of its own steps, independently of other participants’ progress. This property is verified for the implementation, its atomic primitives, and the ISA. A bound in elapsed time also requires a platform model.

Invalidating a capability prevents further invocations through that reference. Revoking access through existing mappings requires disabling the mappings, invalidating the applicable CPU and DMA translations, and ensuring that in-flight accesses have ceased. Memory is eligible for reuse only after those conditions are met. The initial profile performs teardown outside the RT job path, and the analysis accounts for any interference it causes.

## Faults and Recovery

If a job exhausts its budget before completion, the kernel makes it ineligible and applies the defined policy. For the first milestone, the kernel raises a fault and suspends the thread. It must enforce and measure the bound on the time from budget exhaustion to suspension. The timing guarantee applies to jobs that observe their contracts. The kernel records whether a job reached its deadline without completing. Deadlines of independent, compliant jobs remain covered when the overrun and its handling stay within the admitted bounds.

The kernel can stop the affected thread and notify the supervisor using preallocated resources. In the initial profile, the kernel keeps per-thread fault state and can coalesce notifications, keeping the fault observable when notifications saturate without waiting for the supervisor. Effects on dependencies, shared resources, and devices follow their respective protocols.

The trusted boundary includes Alacre Core, hardware protection mechanisms, the boot process, and privileged components that establish or can modify the configuration. Timing assumptions cover firmware and the behavior of shared hardware resources. The containment model covers faults in user-space domains within that boundary. A microkernel failure or a shared hardware failure requires a dedicated fault-tolerance model.

Recovery with a deadline requires reserved CPU capacity and memory, a restart protocol, rules for requests in flight, and, where applicable, time bounds for bringing a device to a safe state and reinitializing it. Recovery work must be admitted as part of the stated fault scenario.

## Logical Filesystem

AlacreOS uses the [Filesystem Hierarchy Standard (FHS) 3.0](https://specifications.freedesktop.org/fhs/latest/) as its directory layout and follows [POSIX.1-2024](https://standards.ieee.org/ieee/1003.1/7700/) semantics for the file operations it implements. Each filesystem view presents one tree rooted at `/`. This is the layout visible to applications; disk formats, partitioning, and recovery protocols are separate choices. The initial profile implements a subset of the standards.

| Path | Purpose |
| --- | --- |
| `/usr/bin`, `/usr/sbin`, `/usr/lib`, `/usr/share` | System commands, administration commands, libraries, and architecture-independent data. Vendor files are read-only during normal operation. `/usr/local` is reserved for local administrator installs. |
| `/bin`, `/sbin`, `/lib` | Compatibility symlinks to `/usr/bin`, `/usr/sbin`, and `/usr/lib`. The symlink targets are available with the root at startup. |
| `/etc` | Machine-specific configuration, persistent on installed systems. Packages keep their shipped defaults under `/usr`. |
| `/var/lib`, `/var/log`, `/var/cache`, `/var/spool`, `/var/tmp` | Writable service state, logs, caches, queues, and temporary files that may outlive a reboot. Installed profiles keep `/var` on persistent storage. |
| `/run`, `/tmp` | Separate writable, memory-backed filesystems, recreated at boot. `/run` holds runtime service state; `/tmp` holds temporary files. `/var/run` points to `/run` and `/var/lock` to `/run/lock`. |
| `/home`, `/root` | Home directories for users and the administrator, when present. |
| `/boot`, `/dev` | Boot files and a virtual view of authorized device nodes, respectively. |
| `/media`, `/mnt`, `/opt`, `/srv` | Removable media, temporary mounts, add-on packages, and site data served by the system. |

AlacreOS uses the common [merged `/usr` layout](https://systemd.io/SYSTEMD_FILE_HIERARCHY_REQUIREMENTS/) for system files. Packages use standard subtrees: for example, `/etc/alacre/` for machine settings and `/var/lib/alacre/` for persistent state. They add no top-level directories.

### Lookup and Authority

A user-space virtual filesystem (VFS) service resolves paths and routes operations to the selected filesystem backend. The supervisor assigns each domain a filesystem view and grants access through capabilities. The service checks the rights on the view and on open handles. Mount configuration requires separate authority. Access to `/dev` depends on the device rights granted to the domain.

Absolute paths resolve from the view's root. Relative paths resolve from the working directory or a directory descriptor, as in `openat`. Names are case-sensitive. `.` denotes the current directory; `..` at the view's root stays there. Absolute symlink targets also resolve from that root. Symlinks and mount traversal cannot grant access outside the assigned view or expand the caller's rights. The service caps pathname length, component length, and the number of symlinks followed. [Pathname resolution](https://man7.org/linux/man-pages/man7/path_resolution.7.html)

The native SDK presents POSIX-style file descriptors. The first service supports regular files, directories, and symbolic links. Callers can open, read, write, and close files; inspect metadata and list directories; create and remove entries; and read symbolic link targets. The service profile specifies the supported flags and resource limits. `rename` is atomic within a writable mount for supported cases; across mounts it fails with `EXDEV`. The persistent backend must separately specify what survives a power loss. [Linux `rename(2)` semantics](https://man7.org/linux/man-pages/man2/rename.2.html)

Alacre Linux uses its own filesystem view. Its compatibility layer checks Linux user and group IDs and mode bits within the authority granted to that view; Linux UID 0 does not grant native authority. Host data is shared only through explicitly authorized mounts. Each compatibility profile lists the supported device nodes, Linux ABI paths (such as the ELF interpreter path when dynamic linking is supported), and the implemented portions of `/proc` and `/sys`, where applicable.

### Bootstrap and Real-Time Use

After user-space IPC is available, the first filesystem service uses a read-only memory-backed root with quota-limited writable mounts at `/run` and `/tmp`. It can boot without persistent storage. Acceptance checks cover root layout and links, access denial across views and for unauthorized devices, `EROFS` on writes to the system tree, `EXDEV` on cross-mount renames, runtime quota exhaustion, and loss of temporary state on reboot.

The initial Alacre RT profile loads required files before task activation and places their contents in resident memory. Admitted jobs use those mappings and bounded IPC to hand off logging or persistence to a non-RT service. They do not resolve paths, wait for storage, or depend on an ordinary filesystem operation to meet a deadline. Admission still accounts for interference from filesystem work outside the RT path.

## Implementation and Compatibility

Alacre Core uses a controlled subset of freestanding C++23. The build disables exceptions and RTTI. The core avoids implicit allocation and uses preallocated resources on active RT paths. The build must verify the actual toolchain and runtime dependencies for each ISA.

Internal C++ APIs use `std::expected` for recoverable errors, subject to successful freestanding compilation and linking with the selected toolchain. Freestanding support for `<expected>` was specified during the C++26 cycle. The system call ABI defines layouts, values, and error codes explicitly. [WG21 P2833R2](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2833r2.html)

**Alacre Linux** runs in user space. Each compatibility profile documents the ISA, ELF64 format, process startup conventions, and semantics of the implemented system calls, along with the supported TLS, signals, futexes, and dynamic linking features. Alacre Linux activity that shares resources with Alacre RT must operate within the configuration’s interference bounds. [Arm AArch64 System V ABI](https://github.com/ARM-software/abi-aa/blob/main/sysvabi64/sysvabi64.rst) · [Linux system call ABI conventions](https://docs.kernel.org/process/adding-syscalls.html)

## Verification and First Milestone

### Required Evidence

| Property | Evidence to trace to the implementation |
| --- | --- |
| Authority | A model of capability operations and a proof that derivation, delegation, and slot reuse preserve the effective rights rules. |
| Isolation | A model of mappings, effective CPU and DMA access rights, and, where applicable, revocation before reuse. |
| Budget and scheduling | A model of release, charging, dispatch, preemption, completion, and faults. Verification that each task has at most one active job, that eligible jobs follow EDF ordering, and that budget use and suspension latency are bounded. |
| Resource lifecycle | Invariants for quotas and ownership of queues and buffers, covering successful operations, saturation, faults, and recovery. |
| Deadlines | Reproducible analysis for the workload and platform, justified worst-case execution time (WCET) and interference bounds, configuration verification, and traces that corroborate the model. |

Formal verification must state the assumptions for each property and explain how model transitions map to code and hardware primitives. System tests exercise those transitions and their error paths. Traces can expose violations of the stated bounds. Establishing an upper bound requires evidence beyond the largest observed value.

### First Milestone Acceptance Criteria

The bring-up criteria below apply per target. The first hard real-time release requires at least one qualified physical platform per ISA. Both platforms must admit the same reference workload with a 100 µs deadline from the programmed timer expiry to observable user-space job completion, including interrupt delivery, dispatch, execution, and admitted interference. An application deadline measured from a physical event also includes the relevant I/O path.

- Boot on a reference platform with one active core, a working timer, and two user-space domains with separate address spaces. The kernel rejects an access outside authorized mappings and a request that lacks the required rights, as specified by the contract.

- Run two reference tasks with published parameters and demonstrate one admission case and one rejection case. Traces show releases, preemption, EDF tie-breaking, and completion for admitted workloads.

- Exercise send, receive, queue saturation, and resource return using a fixed-capacity endpoint. Error paths preserve rights and quotas.

- Force a job to exhaust its budget. The kernel suspends it within the specified limit and makes the fault observable. The independent job continues to meet its timing contract. The admission analysis covers this fault case.

- Record an energy baseline alongside timing evidence on each physical reference target as it is brought up. Run the same reference workload and deadline target on AArch64 and x86_64 and report the selected power profiles and measurement boundaries.

The physical reference platforms, numerical bounds for timers, interrupts, system calls, and non-preemptible sections, the method for deriving WCET bounds, and the formal verification tool are open decisions. This milestone provides the evidence needed to extend the model to multicore execution, isolated drivers, and Alacre Linux.
