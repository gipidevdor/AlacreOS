#pragma once

#include <stdint.h>

namespace alacre::domain {

inline constexpr unsigned message_words = 6;

// The kernel derives the caller from the domain it entered.
struct Message {
    uint64_t word[message_words]{};
};

[[gnu::always_inline]] inline Message trap(Message message) noexcept {
#if defined(__aarch64__)
    register uint64_t a asm("x0") = message.word[0];
    register uint64_t b asm("x1") = message.word[1];
    register uint64_t c asm("x2") = message.word[2];
    register uint64_t d asm("x3") = message.word[3];
    register uint64_t e asm("x4") = message.word[4];
    register uint64_t f asm("x5") = message.word[5];
    asm volatile("svc #1" : "+r"(a), "+r"(b), "+r"(c), "+r"(d), "+r"(e), "+r"(f) : : "cc", "memory");
#elif defined(__x86_64__)
    uint64_t a = message.word[0];
    uint64_t b = message.word[1];
    uint64_t c = message.word[2];
    uint64_t d = message.word[3];
    register uint64_t e asm("r8") = message.word[4];
    register uint64_t f asm("r9") = message.word[5];
    asm volatile("int $0x42" : "+D"(a), "+S"(b), "+d"(c), "+c"(d), "+r"(e), "+r"(f) : : "cc", "memory");
#else
#error "Unsupported guest architecture"
#endif
    return {{a, b, c, d, e, f}};
}

} // namespace alacre::domain
