#include "guest_abi.hpp"

using namespace alacre::identity::guest;

extern "C" [[noreturn]] void identity_client_entry(uint64_t role, uint64_t session, uint64_t other) noexcept {
    bool passed;
    if (role == 1) {
        passed = call({set_enabled, session, 2, 0, 0, 0}).first == 1;
    } else {
        passed = call({operate, session, 0, 0, 1, 0}).first == 1 &&
                 call({set_enabled, other, 2, 0, 1, 0}).first == 0 &&
                 call({open_verified, 0, 1, 0, 1, 0}).first == 0 &&
                 call({operate, session, 0, 0, 0, 0}).first == 0;
    }
    auto probe = call({finish, passed, 0, 0, 0, 0});
    if (role == 1 && probe.second != 0) {
        passed = call({set_enabled, session, 2, probe.third, 0, 0}).first == probe.second - 1;
        probe = call({finish, passed, 0, 0, 0, 0});
    }
    if (probe.first != 0) {
        if (probe.third == 0) {
            const auto value = *reinterpret_cast<const volatile uint8_t *>(probe.first);
            (void)call({finish, value, 0, 0, 0, 0});
        } else {
            *reinterpret_cast<volatile uint8_t *>(probe.first) = 0;
            (void)call({finish, 0, 0, 0, 0, 0});
        }
    }
    for (;;) {
        asm volatile("" ::: "memory");
    }
}
