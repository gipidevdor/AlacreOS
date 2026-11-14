#pragma once

#include <stddef.h>
#include <stdint.h>

#include "platform.hpp"

namespace alacre::domain {

inline constexpr size_t page_size = 4096;
inline constexpr size_t capacity = 3;

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

[[nodiscard]] constexpr bool overlap(Range first, Range second) noexcept {
    return first.begin < first.end && second.begin < second.end &&
           first.begin < second.end && second.begin < first.end;
}

[[nodiscard]] constexpr bool separated(const Image (&images)[capacity]) noexcept {
    for (size_t i = 0; i < capacity; ++i) {
        const auto &image = images[i];
        if (!image.code.contains(image.entry) || image.stack.begin == image.stack.end ||
            overlap(image.state, image.stack)) {
            return false;
        }
        for (size_t j = 0; j < capacity; ++j) {
            if (overlap(image.code, images[j].state) || overlap(image.code, images[j].stack) ||
                (i != j && (overlap(image.state, images[j].state) ||
                            overlap(image.state, images[j].stack) || overlap(image.stack, images[j].stack)))) {
                return false;
            }
        }
    }
    return true;
}

// A trap transports six integers. The kernel derives the caller from the active domain.
struct Message {
    uint64_t word[6]{};
};

enum class Stop : uint64_t { call, fault, timeout, stopped };

struct Event {
    Message message;
    Stop stop;
    uintptr_t fault_address;
};

inline void arm_watchdog() noexcept {
    const auto now = platform::monotonic_now();
    const auto interval = platform::timer_quantum();
    platform::arm_timer(interval <= (UINT64_MAX - now) / 4 ? now + interval * 4 : UINT64_MAX);
}

// These entry points are used by the single-core bootstrap with interrupts masked.
[[nodiscard]] bool initialize(const Image (&images)[capacity]) noexcept;
[[nodiscard]] Event run(size_t slot, Message input) noexcept;
[[nodiscard]] bool identity_smoke() noexcept;

} // namespace alacre::domain
