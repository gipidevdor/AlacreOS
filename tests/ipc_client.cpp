#include "ipc_abi.hpp"

using alacre::domain::trap;

extern "C" [[noreturn]] void ipc_client_entry() noexcept {
    auto request = trap({});
    while (request.word[0] != 0) {
        request = trap(trap(request));
    }

    // The smoke test stops this domain through the watchdog.
    for (;;) {
        asm volatile("" ::: "memory");
    }
}
