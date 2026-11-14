#include "platform.hpp"
#include "apic.h"

#include <stdint.h>

extern "C" {
void timer_vector();
void completion_vector();
void spurious_vector();
void unexpected_vector();
void identity_syscall_vector();
void identity_page_fault_vector();
void identity_protection_vector();
void identity_invalid_opcode_vector();
void identity_no_fp_vector();
void identity_divide_vector();
void identity_debug_vector();
void identity_breakpoint_vector();
void identity_overflow_vector();
void identity_segment_fault_vector();
}

namespace alacre::platform {
namespace {

constexpr uint16_t com1 = 0x03f8;

void out(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t in(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

struct InterruptGate {
    uint16_t offset_low;
    uint16_t code_selector;
    uint8_t stack_index;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

struct [[gnu::packed]] InterruptTablePointer {
    uint16_t limit;
    uint64_t base;
};

static_assert(sizeof(InterruptGate) == 16 && sizeof(InterruptTablePointer) == 10);

alignas(16) constinit InterruptGate interrupt_table[256]{};
constinit uint64_t calibration_ticks{};

constexpr unsigned timer_vector_index = 0x40;
constexpr unsigned completion_vector_index = 0x41;
constexpr unsigned spurious_vector_index = 0xff;
constexpr uint32_t lvt_masked = 1U << 16;
constexpr uintptr_t apic_eoi = 0x0b0;
constexpr uintptr_t apic_timer_lvt = 0x320;
constexpr uintptr_t apic_timer_initial_count = 0x380;
constexpr uintptr_t apic_timer_current_count = 0x390;
constexpr uint32_t calibration_count = 625'000;

volatile uint32_t &apic(uintptr_t offset) {
    return *reinterpret_cast<volatile uint32_t *>(QEMU_XAPIC_BASE + offset);
}

void install_gate(unsigned vector, void (*entry)(), uint8_t attributes = 0x8e) noexcept {
    const auto address = reinterpret_cast<uintptr_t>(entry);
    interrupt_table[vector] = {
        static_cast<uint16_t>(address), 0x08, 0, attributes,
        static_cast<uint16_t>(address >> 16), static_cast<uint32_t>(address >> 32), 0,
    };
}

} // namespace

extern "C" Context platform_timer_interrupt(Context interrupted) noexcept {
    const auto timestamp = monotonic_now();
    apic(apic_eoi) = 0;
    return kernel_timer_event(interrupted, timestamp);
}

extern "C" void x86_acknowledge_domain_timer() noexcept {
    apic(apic_eoi) = 0;
}

extern "C" Context platform_completion_interrupt() noexcept {
    return kernel_completion_event(monotonic_now());
}

void init_console() {
    out(com1 + 1, 0);    // Disable UART interrupts.
    out(com1 + 3, 0x80); // Select divisor registers.
    out(com1, 1);        // 115200 baud.
    out(com1 + 1, 0);
    out(com1 + 3, 0x03); // Eight data bits, no parity, one stop bit.
    out(com1 + 2, 0x07); // Enable and reset FIFOs.
}

void put_char(char character) {
    while ((in(com1 + 5) & 0x20) == 0) {
        asm volatile("pause");
    }
    out(com1, static_cast<uint8_t>(character));
}

const char *name() {
    return "x86_64";
}

std::expected<void, boot::SourceError>
read_boot_info(BootInfo &info, uint64_t loader_argument) noexcept {
    // The QEMU PVH option ROM places start_info and E820 below the kernel image.
    const PhysicalRange window{0, info.kernel_image.base};
    if (loader_argument == 0 || !window.contains({loader_argument, boot::pvh_v1_size})) {
        return std::unexpected(boot::SourceError::invalid_range);
    }
    const auto *header = reinterpret_cast<const unsigned char *>(loader_argument);
    if (auto result = boot::read_pvh(info, header, boot::pvh_v1_size, window); !result) {
        return result;
    }
    info.platform = BootPlatform::qemu_pc_x86_64;
    info.platform_data = {loader_argument, boot::pvh_v1_size};
    return {};
}

bool init_timer() noexcept {
    uint32_t leaf = 1;
    uint32_t features;
    asm volatile("cpuid" : "+a"(leaf), "=d"(features) : : "rbx", "rcx");
    constexpr uint32_t required = (1U << 9) | (1U << 5) | (1U << 4) | (1U << 26);
    // APIC, MSR, TSC, and SSE2 (LFENCE).
    if ((features & required) != required) {
        return false;
    }

    uint32_t apic_base_low;
    uint32_t apic_base_high;
    asm volatile("rdmsr" : "=a"(apic_base_low), "=d"(apic_base_high) : "c"(0x1b));
    constexpr uint32_t apic_enabled = 1U << 11;
    constexpr uint32_t x2apic_enabled = 1U << 10;
    if ((apic_base_low & (0xfffff000U | apic_enabled | x2apic_enabled)) !=
        (QEMU_XAPIC_BASE | apic_enabled) || apic_base_high != 0) {
        return false;
    }

    for (unsigned vector = 0; vector < 256; ++vector) {
        install_gate(vector, unexpected_vector);
    }
    install_gate(timer_vector_index, timer_vector);
    install_gate(completion_vector_index, completion_vector);
    install_gate(spurious_vector_index, spurious_vector);
    install_gate(0x42, identity_syscall_vector, 0xee); // Ring 3 may invoke this gate.
    install_gate(14, identity_page_fault_vector);
    install_gate(13, identity_protection_vector);
    install_gate(6, identity_invalid_opcode_vector);
    install_gate(7, identity_no_fp_vector);
    install_gate(0, identity_divide_vector);
    install_gate(1, identity_debug_vector);
    install_gate(3, identity_breakpoint_vector, 0xee);
    install_gate(4, identity_overflow_vector, 0xee);
    install_gate(11, identity_segment_fault_vector);
    install_gate(12, identity_segment_fault_vector);
    install_gate(17, identity_segment_fault_vector);
    const InterruptTablePointer idtr{sizeof(interrupt_table) - 1,
                                    reinterpret_cast<uintptr_t>(interrupt_table)};
    asm volatile("lidt %0" : : "m"(idtr) : "memory");

    out(0x21, 0xff); // Mask both legacy PICs.
    out(0xa1, 0xff);
    apic(0x080) = 0; // Task priority.
    apic(apic_timer_lvt) = lvt_masked | timer_vector_index;
    apic(apic_timer_initial_count) = 0;
    apic(0x350) = lvt_masked; // LINT0
    apic(0x360) = lvt_masked; // LINT1
    apic(0x370) = lvt_masked; // Error
    apic(0x0f0) = (1U << 8) | spurious_vector_index;
    apic(0x3e0) = 0x3; // Divide by 16.
    const auto started = monotonic_now();
    apic(apic_timer_initial_count) = calibration_count;
    for (unsigned attempt = 0; attempt < 10'000'000; ++attempt) {
        if (apic(apic_timer_current_count) == 0) {
            const auto ended = monotonic_now();
            if (ended <= started) {
                return false;
            }
            calibration_ticks = ended - started;
            break;
        }
        asm volatile("pause");
    }
    if (calibration_ticks == 0) {
        return false;
    }
    apic(apic_timer_lvt) = timer_vector_index;
    return true;
}

uint64_t monotonic_now() noexcept {
    uint32_t low;
    uint32_t high;
    asm volatile("lfence; rdtsc; lfence" : "=a"(low), "=d"(high) : : "memory");
    return (static_cast<uint64_t>(high) << 32) | low;
}

uint64_t timer_quantum() noexcept {
    return calibration_ticks;
}

void arm_timer(uint64_t deadline) noexcept {
    const auto now = monotonic_now();
    const auto delta = deadline > now ? deadline - now : 0;
    uint32_t count = UINT32_MAX;
    if (delta <= UINT64_MAX / calibration_count) {
        const auto numerator = delta * calibration_count;
        const auto scaled = numerator / calibration_ticks + (numerator % calibration_ticks != 0);
        if (scaled < UINT32_MAX) {
            count = scaled == 0 ? 1U : static_cast<uint32_t>(scaled);
        }
    }
    apic(apic_timer_initial_count) = count;
}

void disarm_timer() noexcept {
    apic(apic_timer_initial_count) = 0;
}

void wait_for_interrupt() noexcept {
    // STI defers delivery through HLT, so a pending IRQ cannot be missed.
    asm volatile("sti; hlt; cli" ::: "memory");
}

[[noreturn]] void complete_context() noexcept {
    asm volatile("int $0x41" ::: "memory");
    halt();
}

Context prepare_context(uint64_t *stack_top, void (*entry)()) noexcept {
    // An iretq frame follows fifteen saved general-purpose registers.
    // Reserve a return slot to satisfy SysV's stack alignment at function entry.
    auto *entry_stack = stack_top - 1;
    auto *frame = entry_stack - 20;
    frame[15] = reinterpret_cast<uintptr_t>(entry);
    frame[16] = 0x08; // CS
    frame[17] = 0x202; // RFLAGS with IF set.
    frame[18] = reinterpret_cast<uintptr_t>(entry_stack);
    frame[19] = 0x10; // SS
    return frame;
}

[[noreturn]] void halt() {
    asm volatile("cli" ::: "memory");
    for (;;) {
        asm volatile("hlt");
    }
}

} // namespace alacre::platform
