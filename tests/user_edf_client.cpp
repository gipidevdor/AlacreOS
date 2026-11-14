#include "domain_abi.hpp"
#include "user_edf_abi.hpp"

extern "C" [[noreturn]] void edf_client_entry(uint64_t mode) noexcept {
    using namespace alacre::domain;
    volatile uint64_t saved = edf_guest::marker;
    (void)trap({{edf_guest::entered, mode}});
    if (mode == 0) {
        (void)trap({{edf_guest::completed, saved}});
    }
    for (;;) {
        asm volatile("" ::: "memory");
    }
}
