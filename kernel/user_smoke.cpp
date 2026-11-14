#include "domain.hpp"
#include "user_smoke.hpp"

extern "C" {
extern unsigned char __identity_code[], __identity_code_end[];
extern unsigned char __identity_client[], __identity_client_end[];
extern unsigned char __identity_state[], __identity_state_end[];
extern unsigned char __ipc_client[], __ipc_client_end[];
extern unsigned char __edf_client[], __edf_client_end[];
extern unsigned char __image_start[];
void identity_guest_entry() noexcept;
void identity_client_entry(uint64_t role, uint64_t session, uint64_t other) noexcept;
void ipc_client_entry() noexcept;
void edf_client_entry(uint64_t mode) noexcept;
}

namespace alacre::domain {
namespace {

static_assert(overrun + 1 == capacity);
constexpr size_t stack_bytes = 2 * page_size;
[[gnu::section(".guest_stacks")]] alignas(page_size) unsigned char stacks[capacity][stack_bytes];

Range range(const unsigned char *begin, const unsigned char *end) noexcept {
    return {reinterpret_cast<uintptr_t>(begin), reinterpret_cast<uintptr_t>(end)};
}

Image image(SmokeSlot slot, Range code, Range state, uintptr_t entry) noexcept {
    return {code, state, range(stacks[slot], stacks[slot] + stack_bytes), entry};
}

} // namespace

bool initialize_user_smoke() noexcept {
    const auto identity_code = range(__identity_code, __identity_code_end);
    const auto identity_state = range(__identity_state, __identity_state_end);
    const auto identity_client = range(__identity_client, __identity_client_end);
    const auto identity_entry = reinterpret_cast<uintptr_t>(identity_client_entry);
    const auto ipc_client = range(__ipc_client, __ipc_client_end);
    const auto ipc_entry = reinterpret_cast<uintptr_t>(ipc_client_entry);
    const auto edf_client = range(__edf_client, __edf_client_end);
    const auto edf_entry = reinterpret_cast<uintptr_t>(edf_client_entry);
    Image images[capacity]{
        image(service, identity_code, identity_state, reinterpret_cast<uintptr_t>(identity_guest_entry)),
        image(management, identity_client, {}, identity_entry),
        image(operations, identity_client, {}, identity_entry),
        image(producer, ipc_client, {}, ipc_entry),
        image(consumer, ipc_client, {}, ipc_entry),
        image(peer, ipc_client, {}, ipc_entry),
        image(survivor, edf_client, {}, edf_entry),
        image(overrun, edf_client, {}, edf_entry),
    };
    if (run(service, {}).stop != Stop::stopped) {
        return false;
    }
    const auto original = images[service];
    images[service].code = range(__image_start, __image_start + page_size);
    images[service].entry = images[service].code.begin;
    if (initialize(images) || run(service, {}).stop != Stop::stopped) {
        return false;
    }
    images[service] = original;
    return initialize(images);
}

} // namespace alacre::domain
