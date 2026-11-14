#pragma once

#include <stdint.h>

namespace alacre::identity::guest {

enum Request : uint64_t { receive, reply, finish, operate, set_enabled, open_verified, disconnect };

struct Message {
    uint64_t operation;
    uint64_t first;
    uint64_t second;
    uint64_t third;
    uint64_t channel;
    uint64_t time;
};

[[gnu::always_inline]] inline Message call(Message message) noexcept {
#if defined(__aarch64__)
    register uint64_t a asm("x0") = message.operation;
    register uint64_t b asm("x1") = message.first;
    register uint64_t c asm("x2") = message.second;
    register uint64_t d asm("x3") = message.third;
    register uint64_t e asm("x4") = message.channel;
    register uint64_t f asm("x5") = message.time;
    asm volatile("svc #1" : "+r"(a), "+r"(b), "+r"(c), "+r"(d), "+r"(e), "+r"(f) : : "cc", "memory");
#elif defined(__x86_64__)
    uint64_t a = message.operation;
    uint64_t b = message.first;
    uint64_t c = message.second;
    uint64_t d = message.third;
    register uint64_t e asm("r8") = message.channel;
    register uint64_t f asm("r9") = message.time;
    asm volatile("int $0x42" : "+D"(a), "+S"(b), "+d"(c), "+c"(d), "+r"(e), "+r"(f) : : "cc", "memory");
#else
#error "Unsupported identity guest architecture"
#endif
    return {a, b, c, d, e, f};
}

} // namespace alacre::identity::guest
