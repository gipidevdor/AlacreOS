#include "domain.hpp"
#include "apic.h"

extern "C" void x86_domain_enter(const uint64_t *frame, void *kernel_stack) noexcept;
extern "C" void x86_acknowledge_domain_timer() noexcept;
extern "C" const unsigned char __image_start[], __kernel_text_end[];

namespace alacre::domain {
namespace {

constexpr size_t entries = page_size / sizeof(uint64_t);
constexpr uintptr_t limit = entries * page_size;
constexpr uint64_t present = 1;
constexpr uint64_t writable = 2;
constexpr uint64_t user = 4;
constexpr uint64_t nx = 1UL << 63;

struct alignas(page_size) Table {
    uint64_t entry[entries];
};

Table apic_directory;
Table apic_table;
Table roots[capacity];
Table pdpts[capacity];
Table directories[capacity];
Table pages[capacity];

constexpr size_t frame_words = 20;
alignas(16) uint64_t contexts[capacity][frame_words];
bool stopped[capacity];
constexpr size_t arguments[]{10, 11, 12, 13, 9, 8}; // RDI, RSI, RDX, RCX, R8, R9
static_assert(sizeof(arguments) / sizeof(arguments[0]) == message_words);
uint64_t *active{};
uint64_t kernel_root{};
Event *active_event{};

struct [[gnu::packed]] DescriptorTablePointer {
    uint16_t limit;
    uint64_t base;
};

struct [[gnu::packed]] TaskState {
    uint32_t reserved;
    uint64_t rsp[3];
    uint64_t reserved_next;
    uint64_t ist[7];
    uint64_t reserved_last;
    uint16_t reserved_word;
    uint16_t io_base;
};
static_assert(sizeof(TaskState) == 104 && offsetof(TaskState, rsp) == 4);

constinit TaskState tss{.reserved = 0, .rsp = {}, .reserved_next = 0, .ist = {},
                        .reserved_last = 0, .reserved_word = 0, .io_base = sizeof(TaskState)};

alignas(8) uint64_t gdt[]{
    0, 0x00af9a000000ffff, 0x00cf92000000ffff, 0x00cff2000000ffff, 0x00affa000000ffff, 0, 0,
};

void install_tss() noexcept {
    const auto base = reinterpret_cast<uintptr_t>(&tss);
    gdt[5] = (sizeof(tss) - 1) | ((base & 0xffffff) << 16) | (0x89UL << 40) | ((base & 0xff000000) << 32);
    gdt[6] = base >> 32;
    const DescriptorTablePointer gdtr{sizeof(gdt) - 1, reinterpret_cast<uintptr_t>(gdt)};
    asm volatile("lgdt %0; ltr %w1" : : "m"(gdtr), "r"(0x28U) : "memory");
}

void select(uint64_t root) noexcept {
    asm volatile("mov %0, %%cr3" : : "r"(root) : "memory");
}

uint64_t descriptor(const Table &table, uint64_t flags) noexcept {
    return reinterpret_cast<uintptr_t>(&table) | present | flags;
}

bool valid(Range range) noexcept {
    return range.page_aligned() && range.end <= limit;
}

} // namespace

bool arch::initialize(const Image (&images)[capacity]) noexcept {
    if (kernel_root != 0) {
        return false;
    }
    for (const auto &image : images) {
        if (!valid(image.code) || !valid(image.state) || !valid(image.stack)) {
            return false;
        }
    }
    uint32_t leaf = 0x80000000;
    uint32_t features;
    asm volatile("cpuid" : "+a"(leaf) : : "rbx", "rcx", "rdx");
    if (leaf < 0x80000001) {
        return false;
    }
    leaf = 0x80000001;
    asm volatile("cpuid" : "+a"(leaf), "=d"(features) : : "rbx", "rcx");
    if ((features & (1U << 20)) == 0) {
        return false;
    }

    uint32_t low;
    uint32_t high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080U));
    constexpr uint32_t nx_enable = 1U << 11;
    constexpr uint32_t syscall_enable = 1;
    asm volatile("wrmsr" : : "a"((low | nx_enable) & ~syscall_enable), "d"(high), "c"(0xc0000080U) : "memory");
    asm volatile("wrmsr" : : "a"(0U), "d"(0U), "c"(0x174U) : "memory"); // Disable SYSENTER.
    asm volatile("mov %%cr3, %0" : "=r"(kernel_root));
    uint64_t control;
    asm volatile("mov %%cr0, %0" : "=r"(control));
    control |= (1UL << 16) | (1UL << 3); // Write protect supervisor accesses, trap FP/SIMD.
    asm volatile("mov %0, %%cr0" : : "r"(control) : "memory");
    asm volatile("mov %%cr4, %0" : "=r"(control));
    control |= 1UL << 2; // Restrict timestamp-counter access to ring 0.
    asm volatile("mov %0, %%cr4" : : "r"(control) : "memory");
    install_tss();

    apic_directory.entry[(QEMU_XAPIC_BASE >> 21) & 511] = descriptor(apic_table, writable);
    apic_table.entry[(QEMU_XAPIC_BASE >> 12) & 511] = QEMU_XAPIC_BASE | nx | 0x1b;
    const auto text_start = reinterpret_cast<uintptr_t>(__image_start);
    const auto text_end = reinterpret_cast<uintptr_t>(__kernel_text_end);
    for (size_t i = 0; i < capacity; ++i) {
        const auto &image = images[i];
        roots[i].entry[0] = descriptor(pdpts[i], writable | user);
        pdpts[i].entry[0] = descriptor(directories[i], writable | user);
        pdpts[i].entry[QEMU_XAPIC_BASE >> 30] = descriptor(apic_directory, writable);
        directories[i].entry[0] = descriptor(pages[i], writable | user);
        for (size_t page = 0; page < entries; ++page) {
            const uintptr_t address = page * page_size;
            uint64_t flags = text_start <= address && address < text_end ? 0 : writable | nx;
            if (image.code.contains(address)) {
                flags = user;
            } else if (image.state.contains(address) || image.stack.contains(address)) {
                flags = writable | user | nx;
            }
            pages[i].entry[page] = address | present | flags;
        }
        auto &frame = contexts[i];
        frame[15] = image.entry;
        frame[16] = 0x23; // Ring 3 code.
        frame[17] = 0x202; // IF set, IOPL zero.
        frame[18] = image.stack.end - sizeof(uint64_t);
        frame[19] = 0x1b; // Ring 3 data.
    }
    return true;
}

void arch::resume(size_t slot, const Message *input, uint64_t deadline, Event &event) noexcept {
    if (kernel_root == 0 || slot >= capacity || stopped[slot]) {
        event.stop = Stop::stopped;
        return;
    }
    active = contexts[slot];
    if (input != nullptr) {
        for (size_t i = 0; i < message_words; ++i) {
            active[arguments[i]] = input->word[i];
        }
    }
    active_event = &event;
    select(reinterpret_cast<uintptr_t>(&roots[slot]));
    platform::arm_timer(deadline);
    auto *kernel_stack = reinterpret_cast<unsigned char *>(&tss) + offsetof(TaskState, rsp);
    x86_domain_enter(active, kernel_stack);
    platform::disarm_timer();
    select(kernel_root);
    active_event = nullptr;
    active = nullptr;
}

void arch::retire(size_t slot) noexcept {
    if (slot < capacity) {
        stopped[slot] = true;
    }
}

extern "C" uint64_t x86_domain_event(const uint64_t *frame, uint64_t kind) noexcept {
    const auto timestamp = platform::monotonic_now();
    if (active_event == nullptr) {
        platform::halt();
    }
    auto &event = *active_event;
    event.timestamp = timestamp;
    if (kind == 0 || kind == 3) {
        for (size_t i = 0; i < frame_words; ++i) {
            active[i] = frame[i];
        }
        // DF must survive a timer preemption between STD and CLD.
        constexpr uint64_t user_flags = 0xcd5;
        active[17] = (frame[17] & user_flags) | 0x202;
        if (kind == 3) {
            x86_acknowledge_domain_timer();
            event.stop = Stop::timer;
        } else {
            for (size_t i = 0; i < message_words; ++i) {
                event.message.word[i] = frame[arguments[i]];
            }
            event.stop = Stop::call;
        }
    } else {
        event.stop = Stop::fault;
        if (kind == 1) {
            asm volatile("mov %%cr2, %0" : "=r"(event.fault_address));
        }
    }
    return tss.rsp[0];
}

} // namespace alacre::domain
