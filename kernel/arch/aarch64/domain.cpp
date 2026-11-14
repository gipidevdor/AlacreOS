#include "domain.hpp"

extern "C" void arm_domain_enter(const uint64_t *frame) noexcept;
extern "C" bool arm_acknowledge_domain_irq() noexcept;
extern "C" const unsigned char __image_start[], __kernel_text_end[];

namespace alacre::domain {
namespace {

constexpr uintptr_t ram = 0x40000000;
constexpr uintptr_t kernel_block = 0x40200000;
constexpr uintptr_t block_size = 0x200000;
constexpr size_t entries = page_size / sizeof(uint64_t);
constexpr uint64_t accessed = 1UL << 10;
constexpr uint64_t inner_shareable = 3UL << 8;
constexpr uint64_t user = 1UL << 6;
constexpr uint64_t read_only = 1UL << 7;
constexpr uint64_t pxn = 1UL << 53;
constexpr uint64_t uxn = 1UL << 54;

struct alignas(page_size) Table {
    uint64_t entry[entries];
};

Table device;
Table kernel_root;
Table kernel_ram;
Table kernel_pages;
Table roots[capacity];
Table ram_tables[capacity];
Table pages[capacity];

struct Context {
    uint64_t registers[31];
    uint64_t stack;
    uint64_t pc;
    uint64_t status;
};
static_assert(offsetof(Context, stack) == 248 && offsetof(Context, pc) == 256 && offsetof(Context, status) == 264);

constexpr uint64_t user_status = 0x340; // EL0t, with debug, SError and FIQ masked.
constexpr uint64_t condition_flags = 0xf0000000;

Context contexts[capacity];
bool stopped[capacity];
Context *active{};
Event result{};

uint64_t descriptor(const Table &table) noexcept {
    return reinterpret_cast<uintptr_t>(&table) | 3;
}

void select(const Table &table) noexcept {
    asm volatile("dsb ishst; msr ttbr0_el1, %0; isb; tlbi vmalle1is; dsb ish; isb"
                 : : "r"(reinterpret_cast<uintptr_t>(&table)) : "memory");
}

bool valid(Range range) noexcept {
    return range.page_aligned() && (range.begin == range.end ||
           (kernel_block <= range.begin && range.end <= kernel_block + block_size));
}

} // namespace

bool initialize(const Image (&images)[capacity]) noexcept {
    uint64_t control;
    asm volatile("mrs %0, sctlr_el1" : "=r"(control));
    if ((control & 1) != 0 || !separated(images)) {
        return false;
    }
    for (const auto &image : images) {
        if (!valid(image.code) || !valid(image.state) || !valid(image.stack)) {
            return false;
        }
    }

    constexpr uint64_t normal = accessed | inner_shareable | (1UL << 2);
    constexpr uint64_t mmio = accessed | pxn | uxn;
    device.entry[0x08000000 / block_size] = 0x08000000 | mmio | 1; // GIC
    device.entry[0x09000000 / block_size] = 0x09000000 | mmio | 1; // UART
    kernel_root.entry[0] = descriptor(device);
    kernel_root.entry[1] = descriptor(kernel_ram);
    kernel_ram.entry[0] = ram | normal | pxn | uxn | 1;
    kernel_ram.entry[1] = descriptor(kernel_pages);
    const Range kernel_text{reinterpret_cast<uintptr_t>(__image_start), reinterpret_cast<uintptr_t>(__kernel_text_end)};
    for (size_t page = 0; page < entries; ++page) {
        const auto address = kernel_block + page * page_size;
        const auto flags = kernel_text.contains(address) ? read_only : pxn;
        kernel_pages.entry[page] = address | normal | flags | uxn | (1UL << 11) | 3;
    }

    for (size_t i = 0; i < capacity; ++i) {
        const auto &image = images[i];
        roots[i].entry[0] = descriptor(device);
        roots[i].entry[1] = descriptor(ram_tables[i]);
        ram_tables[i].entry[0] = ram | normal | pxn | uxn | 1;
        ram_tables[i].entry[1] = descriptor(pages[i]);
        for (size_t page = 0; page < entries; ++page) {
            const auto address = kernel_block + page * page_size;
            const auto address_flags = kernel_pages.entry[page];
            uint64_t flags = address_flags & (read_only | pxn | uxn | (1UL << 11));
            if (image.code.contains(address)) {
                flags = user | read_only | pxn | (1UL << 11);
            } else if (image.state.contains(address) || image.stack.contains(address)) {
                flags = user | pxn | uxn | (1UL << 11);
            }
            pages[i].entry[page] = address | normal | flags | 3;
        }
        contexts[i].pc = image.entry;
        contexts[i].stack = image.stack.end;
        contexts[i].status = user_status;
    }

    constexpr uint64_t mair = 0xff00; // Device-nGnRnE and normal write-back memory.
    constexpr uint64_t tcr = 25UL | (1UL << 8) | (1UL << 10) | (3UL << 12) | (1UL << 23);
    asm volatile("msr mair_el1, %0; msr tcr_el1, %1; msr cpacr_el1, xzr; msr cntkctl_el1, xzr; isb"
                 : : "r"(mair), "r"(tcr) : "memory");
    select(kernel_root);
    control |= (1UL << 12) | (1UL << 2) | 1; // I, C, M
    asm volatile("msr sctlr_el1, %0; isb" : : "r"(control) : "memory");
    return true;
}

Event run(size_t slot, Message input) noexcept {
    if (slot >= capacity || stopped[slot]) {
        return {{}, Stop::stopped, 0};
    }
    active = &contexts[slot];
    for (size_t i = 0; i < 6; ++i) {
        active->registers[i] = input.word[i];
    }
    result = {};
    select(roots[slot]);
    arm_watchdog();
    arm_domain_enter(active->registers);
    platform::disarm_timer();
    select(kernel_root);
    active = nullptr;
    stopped[slot] = result.stop != Stop::call;
    return result;
}

extern "C" void arm_domain_sync(const uint64_t *frame, uint64_t syndrome, uintptr_t address) noexcept {
    if (active == nullptr) {
        platform::halt();
    }
    if ((syndrome & 0xfc00ffffU) != ((0x15U << 26) | 1U)) {
        result.stop = Stop::fault;
        result.fault_address = ((syndrome >> 26) == 0x24 && (syndrome & (1U << 10)) == 0) ? address : 0;
        return;
    }
    for (size_t i = 0; i < 31; ++i) {
        active->registers[i] = frame[i];
    }
    active->stack = frame[31];
    active->pc = frame[32];
    active->status = (frame[33] & condition_flags) | user_status;
    for (size_t i = 0; i < 6; ++i) {
        result.message.word[i] = frame[i];
    }
}

extern "C" void arm_domain_irq(const uint64_t *) noexcept {
    if (active == nullptr) {
        platform::halt();
    }
    result.stop = arm_acknowledge_domain_irq() ? Stop::timeout : Stop::fault;
}

} // namespace alacre::domain
