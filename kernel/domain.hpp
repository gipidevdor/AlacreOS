#pragma once

#include <stddef.h>
#include <stdint.h>

#include "domain_abi.hpp"
#include "platform.hpp"

namespace alacre::domain {

inline constexpr size_t page_size = 4096;
inline constexpr size_t capacity = 8;

struct Range {
    uintptr_t begin;
    uintptr_t end;

    [[nodiscard]] constexpr bool contains(uintptr_t address) const noexcept {
        return begin <= address && address < end;
    }

    [[nodiscard]] constexpr bool page_aligned() const noexcept {
        return begin <= end && begin % page_size == 0 && end % page_size == 0;
    }
};

struct Image {
    Range code;
    Range state;
    Range stack;
    uintptr_t entry;
};

struct ImageMemory {
    Range code;
    Range state;
    Range stacks;
};

[[nodiscard]] constexpr bool overlap(Range first, Range second) noexcept {
    return first.begin < first.end && second.begin < second.end &&
           first.begin < second.end && second.begin < first.end;
}

// Check the linker-owned memory pools before installing user mappings.
[[nodiscard]] constexpr bool valid_images(const Image (&images)[capacity], ImageMemory memory) noexcept {
    const auto inside = [](Range pool, Range image) {
        return image.page_aligned() && pool.begin <= image.begin && image.end <= pool.end;
    };
    if (!memory.code.page_aligned() || !memory.state.page_aligned() || !memory.stacks.page_aligned() ||
        overlap(memory.code, memory.state) || overlap(memory.code, memory.stacks) ||
        overlap(memory.state, memory.stacks)) {
        return false;
    }
    for (size_t i = 0; i < capacity; ++i) {
        const auto &image = images[i];
        if (!image.code.contains(image.entry) || !inside(memory.code, image.code) ||
            !inside(memory.stacks, image.stack) || image.stack.begin == image.stack.end ||
            ((image.state.begin != 0 || image.state.end != 0) && !inside(memory.state, image.state))) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (overlap(image.state, images[j].state) || overlap(image.stack, images[j].stack)) {
                return false;
            }
        }
    }
    return true;
}

enum class Stop : uint64_t { call, fault, timeout, stopped, ipc, timer, interrupted };

struct Event {
    Message message;
    Stop stop;
    uintptr_t fault_address;
    uint64_t timestamp{};
};

inline uint64_t watchdog_deadline() noexcept {
    const auto now = platform::monotonic_now();
    const auto interval = platform::timer_quantum();
    return interval <= (UINT64_MAX - now) / 4 ? now + interval * 4 : UINT64_MAX;
}

// These entry points are used by the single-core bootstrap with interrupts masked.
[[nodiscard]] bool initialize(const Image (&images)[capacity]) noexcept;
// An IPC event contains the kernel's reply for the caller's next resume.
[[nodiscard]] Event run(size_t slot, const Message &input) noexcept;
// A null input resumes the interrupted register state. Timer events are resumable.
[[nodiscard]] Event run_until(size_t slot, const Message *input, uint64_t deadline) noexcept;
void retire(size_t slot) noexcept;

namespace arch {
[[nodiscard]] bool initialize(const Image (&images)[capacity]) noexcept;
// Writes into a fresh event or reuses the empty message from an immediate timer/IRQ retry.
void resume(size_t slot, const Message *input, uint64_t deadline, Event &event) noexcept;
void retire(size_t slot) noexcept;
}

} // namespace alacre::domain
