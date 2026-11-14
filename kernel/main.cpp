#include "domain.hpp"
#include "edf.hpp"
#include "platform.hpp"

#include <atomic>

static_assert(__STDC_HOSTED__ == 0, "Alacre Core must be compiled in freestanding mode");

extern "C" {
extern unsigned char __image_start[];
extern unsigned char __image_end[];
}

namespace {

constinit alacre::EdfScheduler scheduler;
constinit alacre::BootInfo boot_info{
    .magic = alacre::BootInfo::magic_value,
    .version = alacre::BootInfo::current_version,
    .size = sizeof(alacre::BootInfo),
    .platform = {},
    .reserved = 0,
    .kernel_image = {},
    .platform_data = {},
    .e820_table = {},
    .acpi_rsdp = 0,
};

void print(const char *message) {
    for (; *message != '\0'; ++message) {
        alacre::platform::put_char(*message);
    }
}

// This demonstration runs two privileged integer-only contexts on dedicated stacks.
using alacre::platform::Context;
using alacre::Tick;

constexpr alacre::TaskId first_job = 0;
constexpr alacre::TaskId urgent_job = 1;
constexpr unsigned stack_words = 2048;
alignas(16) uint64_t job_stacks[2][stack_words]{};
constinit Context contexts[2]{};
constinit Context bootstrap_context{};
constinit alacre::TaskId active = alacre::EdfScheduler::no_task;
constinit Tick urgent_release{};
constinit Tick dispatched_at{};
constinit std::atomic<bool> urgent_pending{true};
constinit std::atomic<bool> urgent_ran{false};
constinit std::atomic<bool> finished{false};
static_assert(std::atomic<bool>::is_always_lock_free);

[[noreturn]] void fail(const char *message) {
    print(message);
    alacre::platform::halt();
}

[[noreturn]] void run_first_job() {
    // A timer interrupt must preserve and later resume this execution point.
    constexpr uint64_t expected = 0x414c414352454f53;
    volatile uint64_t stack_marker = expected;
    if (!urgent_pending.load(std::memory_order_relaxed)) {
        fail("Context EDF smoke: FAIL (unexpected first entry)\n");
    }
    while (urgent_pending.load(std::memory_order_relaxed)) {
        asm volatile("" ::: "memory");
    }
    if (stack_marker != expected || !urgent_ran.load(std::memory_order_relaxed)) {
        fail("Context EDF smoke: FAIL (resume state)\n");
    }
    alacre::platform::complete_context();
}

[[noreturn]] void run_urgent_job() {
    // Deliberately runs until the timer enforces its budget.
    urgent_ran.store(true, std::memory_order_relaxed);
    for (;;) {
        asm volatile("" ::: "memory");
    }
}

[[nodiscard]] Context dispatch(Tick timestamp) noexcept {
    active = scheduler.running();
    if (active == alacre::EdfScheduler::no_task) {
        fail("Context EDF smoke: FAIL (no eligible job)\n");
    }

    dispatched_at = alacre::platform::monotonic_now();
    if (dispatched_at < timestamp ||
        !scheduler.advance_to(dispatched_at, alacre::ExecutionCharge{0}) ||
        scheduler.running() != active) {
        fail("Context EDF smoke: FAIL (stale dispatch)\n");
    }

    // Rebase the budget prediction on the dispatch sample. Wall deadlines stay absolute.
    auto boundary = scheduler.next_boundary();
    if (urgent_pending.load(std::memory_order_relaxed) && urgent_release < boundary) {
        boundary = urgent_release;
    }
    alacre::platform::arm_timer(boundary);
    return contexts[active];
}

void verify_edf() {
    const auto quantum = alacre::platform::timer_quantum();
    const auto epoch = alacre::platform::monotonic_now();
    if (quantum == 0 || quantum > (UINT64_MAX - epoch) / 128 ||
        !scheduler.configure(first_job, {quantum * 16, quantum * 64, quantum * 128}) ||
        !scheduler.configure(urgent_job, {quantum, quantum * 16, quantum * 32})) {
        fail("Context EDF smoke: FAIL (configuration)\n");
    }
    contexts[first_job] =
        alacre::platform::prepare_context(job_stacks[first_job] + stack_words, run_first_job);
    contexts[urgent_job] =
        alacre::platform::prepare_context(job_stacks[urgent_job] + stack_words, run_urgent_job);
    urgent_release = epoch + quantum * 4;
    alacre::platform::arm_timer(epoch + quantum);
    do {
        alacre::platform::wait_for_interrupt();
    } while (!finished.load(std::memory_order_relaxed));

    const auto first = scheduler.job(first_job);
    const auto urgent = scheduler.job(urgent_job);
    if (!first || first->state != alacre::JobState::completed ||
        !urgent || urgent->state != alacre::JobState::budget_exhausted) {
        fail("Context EDF smoke: FAIL (wrong final state)\n");
    }
    print("Context EDF smoke: PASS\n");
}

[[noreturn]] void run_kernel(const alacre::BootInfo &info) {
    if (!alacre::valid_boot_info(info)) {
        print("Boot handoff: FAIL (invalid contract)\n");
        alacre::platform::halt();
    }

    switch (info.platform) {
    case alacre::BootPlatform::qemu_virt_aarch64:
        print("Boot handoff v1: PASS (DTB)\n");
        break;
    case alacre::BootPlatform::qemu_pc_x86_64:
        print(info.acpi_rsdp != 0 ? "Boot handoff v1: PASS (PVH, E820, ACPI RSDP)\n"
                                  : "Boot handoff v1: PASS (PVH, E820)\n");
        break;
    }
    if (!alacre::platform::init_timer()) {
        print("Timer setup: FAIL\n");
        alacre::platform::halt();
    }
    verify_edf();
    if (!alacre::domain::identity_smoke()) {
        fail("Isolated identity IPC: FAIL\n");
    }
    print("Isolated identity IPC: PASS\n");
    alacre::platform::halt();
}

} // namespace

extern "C" Context kernel_timer_event(Context interrupted, uint64_t timestamp) noexcept {
    using alacre::ExecutionCharge;
    // The APIC may already have latched an interrupt when completion disarms it.
    if (finished.load(std::memory_order_relaxed)) {
        return interrupted;
    }
    if (bootstrap_context == nullptr) {
        bootstrap_context = interrupted;
        if (!scheduler.advance_to(timestamp, ExecutionCharge{0}) ||
            !scheduler.release(first_job)) {
            fail("Context EDF smoke: FAIL (first release)\n");
        }
        return dispatch(timestamp);
    }

    if (active == alacre::EdfScheduler::no_task || timestamp < dispatched_at ||
        !scheduler.advance_to(timestamp, ExecutionCharge{timestamp - dispatched_at})) {
        fail("Context EDF smoke: FAIL (timer accounting)\n");
    }
    contexts[active] = interrupted;
    if (urgent_pending.load(std::memory_order_relaxed) && timestamp >= urgent_release) {
        if (scheduler.running() != first_job || !scheduler.release(urgent_job) ||
            scheduler.running() != urgent_job) {
            fail("Context EDF smoke: FAIL (urgent release)\n");
        }
        urgent_pending.store(false, std::memory_order_relaxed);
    }
    return dispatch(timestamp);
}

extern "C" Context kernel_completion_event(uint64_t timestamp) noexcept {
    if (active != first_job || urgent_pending.load(std::memory_order_relaxed) ||
        timestamp < dispatched_at ||
        !scheduler.complete_at(timestamp, alacre::ExecutionCharge{timestamp - dispatched_at})) {
        fail("Context EDF smoke: FAIL (completion)\n");
    }
    alacre::platform::disarm_timer();
    active = alacre::EdfScheduler::no_task;
    finished.store(true, std::memory_order_relaxed);
    return bootstrap_context;
}

extern "C" [[noreturn]] void kernel_main(uint64_t loader_argument) {
    alacre::platform::init_console();
    print("Alacre Core booted: ");
    print(alacre::platform::name());
    print("\n");

    const auto image_start = reinterpret_cast<uintptr_t>(__image_start);
    const auto image_end = reinterpret_cast<uintptr_t>(__image_end);
    boot_info.kernel_image = {image_start, image_end - image_start};
    if (!alacre::platform::read_boot_info(boot_info, loader_argument)) {
        print("Boot handoff: FAIL (invalid loader data)\n");
        alacre::platform::halt();
    }
    run_kernel(boot_info);
}
