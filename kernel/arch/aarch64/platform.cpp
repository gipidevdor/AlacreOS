#include "platform.hpp"

#include <stdint.h>

extern "C" const unsigned char exception_vectors[];

namespace alacre::platform {
namespace {

// This address is specific to the versioned QEMU virt-8.2 development machine.
constexpr uintptr_t uart_base = 0x09000000;

volatile uint32_t &uart(uintptr_t offset) {
    return *reinterpret_cast<volatile uint32_t *>(uart_base + offset);
}

constexpr uintptr_t gic_distributor_base = 0x08000000;
constexpr uintptr_t gic_cpu_base = 0x08010000;
constexpr uint32_t timer_irq = 30;
constexpr uint32_t timer_irq_bit = 1U << timer_irq;
constexpr uint32_t interrupt_id_mask = 0x3ff;
constexpr uint32_t first_special_id = 1020;

volatile uint32_t &gicd(uintptr_t offset) {
    return *reinterpret_cast<volatile uint32_t *>(gic_distributor_base + offset);
}

volatile uint32_t &gicc(uintptr_t offset) {
    return *reinterpret_cast<volatile uint32_t *>(gic_cpu_base + offset);
}

constinit uint64_t quantum{};

void disable_timer() noexcept {
    asm volatile("msr cntp_ctl_el0, xzr; isb" ::: "memory");
}

} // namespace

extern "C" bool arm_acknowledge_domain_irq() noexcept {
    const uint32_t acknowledge = gicc(0x00c);
    const uint32_t irq = acknowledge & interrupt_id_mask;
    if (irq >= first_special_id) {
        return false;
    }
    if (irq != timer_irq) {
        halt();
    }
    disable_timer();
    gicc(0x010) = acknowledge;
    asm volatile("dsb sy" ::: "memory");
    return true;
}

extern "C" Context platform_timer_interrupt(Context interrupted) noexcept {
    const auto timestamp = monotonic_now();
    return arm_acknowledge_domain_irq() ? kernel_timer_event(interrupted, timestamp) : interrupted;
}

extern "C" Context platform_completion_interrupt(uint64_t syndrome) noexcept {
    const auto timestamp = monotonic_now();
    // SVC in AArch64, immediate zero.
    if ((syndrome & 0xfc00ffffU) != (0x15U << 26)) {
        halt();
    }
    return kernel_completion_event(timestamp);
}

void init_console() {
    uart(0x30) = 0;             // Disable UART before configuring it.
    uart(0x24) = 13;            // Integer divisor for 115200 baud at 24 MHz.
    uart(0x28) = 1;
    uart(0x2c) = 3U << 5;       // Eight data bits.
    uart(0x30) = (1U << 8) | 1; // Transmitter and UART enable.
}

void put_char(char character) {
    while ((uart(0x18) & (1U << 5)) != 0) {
        asm volatile("yield");
    }
    uart(0) = static_cast<unsigned char>(character);
}

const char *name() {
    return "aarch64";
}

std::expected<void, boot::SourceError>
read_boot_info(BootInfo &info, uint64_t loader_argument) noexcept {
    constexpr uint64_t ram_base = 0x40000000;
    if (loader_argument != ram_base || info.kernel_image.base <= ram_base) {
        return std::unexpected(boot::SourceError::invalid_range);
    }
    const auto *header = reinterpret_cast<const unsigned char *>(loader_argument);
    const auto size = boot::dtb_size(header, info.kernel_image.base - ram_base);
    if (!size) {
        return std::unexpected(size.error());
    }
    info.platform = BootPlatform::qemu_virt_aarch64;
    info.platform_data = {ram_base, *size};
    return {};
}

bool init_timer() noexcept {
    uint64_t current_el;
    uint64_t frequency;
    asm volatile("mrs %0, CurrentEL; mrs %1, cntfrq_el0" : "=r"(current_el), "=r"(frequency));
    if (current_el != 4 || frequency < 100 || frequency > UINT32_MAX) {
        return false;
    }
    quantum = frequency / 100;

    asm volatile("msr vbar_el1, %0; isb" : : "r"(exception_vectors) : "memory");
    disable_timer();

    gicd(0x000) = 0;              // Disable forwarding during setup.
    gicd(0x180) = UINT32_MAX;     // Disable all local SGIs and PPIs where writable.
    gicd(0x280) = timer_irq_bit;  // Clear any pending timer interrupt.
    *reinterpret_cast<volatile uint8_t *>(gic_distributor_base + 0x400 + timer_irq) = 0x80;
    gicc(0x004) = 0xff;           // Priority mask.
    gicd(0x100) = timer_irq_bit;
    gicc(0x000) = 1;
    gicd(0x000) = 1;
    asm volatile("dsb sy; isb" ::: "memory");
    return true;
}

uint64_t monotonic_now() noexcept {
    uint64_t counter;
    asm volatile("isb; mrs %0, cntpct_el0; isb" : "=r"(counter) : : "memory");
    return counter;
}

uint64_t timer_quantum() noexcept {
    return quantum;
}

void arm_timer(uint64_t deadline) noexcept {
    asm volatile("msr cntp_cval_el0, %0; msr cntp_ctl_el0, %1; isb"
                 : : "r"(deadline), "r"(uint64_t{1}) : "memory");
}

void disarm_timer() noexcept {
    disable_timer();
}

void wait_for_interrupt() noexcept {
    asm volatile("msr daifclr, #2; isb; wfi; msr daifset, #2" ::: "memory");
}

[[noreturn]] void complete_context() noexcept {
    asm volatile("svc #0" ::: "memory");
    halt();
}

Context prepare_context(uint64_t *stack_top, void (*entry)()) noexcept {
    // Keep these indices in step with the exception frame in interrupts.S.
    auto *frame = stack_top - 34;
    frame[32] = reinterpret_cast<uintptr_t>(entry);
    frame[33] = 0x345; // EL1h, with debug, SError, and FIQ masked.
    return frame;
}

[[noreturn]] void halt() {
    asm volatile("msr daifset, #0xf" ::: "memory");
    for (;;) {
        asm volatile("wfi");
    }
}

} // namespace alacre::platform
