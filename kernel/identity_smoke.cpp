#include "domain.hpp"
#include "guest_abi.hpp"

extern "C" {
extern unsigned char __identity_code[], __identity_code_end[];
extern unsigned char __identity_client[], __identity_client_end[];
extern unsigned char __identity_state[], __identity_state_end[];
void identity_guest_entry() noexcept;
void identity_client_entry(uint64_t role, uint64_t session, uint64_t other) noexcept;
}

namespace alacre::domain {
namespace {

using namespace identity::guest;
using Transport = alacre::domain::Message;

enum Slot : size_t { service, management, operations };
constexpr size_t stack_bytes = 2 * page_size;
alignas(page_size) unsigned char stacks[capacity][stack_bytes];

Range range(const unsigned char *begin, const unsigned char *end) noexcept {
    return {reinterpret_cast<uintptr_t>(begin), reinterpret_cast<uintptr_t>(end)};
}

bool called(const Event &event, Request operation) noexcept {
    return event.stop == Stop::call && event.message.word[0] == operation;
}

Event to_service(Slot caller, Transport request) noexcept {
    request.word[4] = caller;
    request.word[5] = platform::monotonic_now();
    return run(service, request);
}

// Only the bootstrap fixture may submit open_verified. A client trap can only
// invoke the operations granted to its endpoint. Identity still checks the role.
Event invoke(Slot caller, const Event &event) noexcept {
    if (!called(event, operate) && !called(event, set_enabled)) {
        return {Transport{{reply, 0}}, Stop::call, 0};
    }
    return to_service(caller, event.message);
}

bool refused_access(Slot slot, uintptr_t address, bool write) noexcept {
    const auto event = run(slot, {{0, address, 0, write}});
    if (event.stop != Stop::fault || event.fault_address != address || run(slot, {}).stop != Stop::stopped) {
        return false;
    }
    const auto disconnected = to_service(slot, {{disconnect}});
    return called(disconnected, reply) && disconnected.message.word[1] == 1;
}

} // namespace

bool identity_smoke() noexcept {
    const auto client_code = range(__identity_client, __identity_client_end);
    const auto identity_state = range(__identity_state, __identity_state_end);
    const auto client_entry = reinterpret_cast<uintptr_t>(identity_client_entry);
    const Image images[capacity]{
        {range(__identity_code, __identity_code_end), identity_state,
         range(stacks[service], stacks[service] + stack_bytes), reinterpret_cast<uintptr_t>(identity_guest_entry)},
        {client_code, {}, range(stacks[management], stacks[management] + stack_bytes), client_entry},
        {client_code, {}, range(stacks[operations], stacks[operations] + stack_bytes), client_entry},
    };
    if (!initialize(images) || !called(run(service, {}), receive)) {
        return false;
    }

    const auto admin = to_service(management, {{open_verified, 0, 1}});
    const auto worker = to_service(operations, {{open_verified, 0, 2}});
    if (!called(admin, reply) || !called(worker, reply) ||
        admin.message.word[1] != 1 || worker.message.word[1] != 1) {
        return false;
    }

    auto request = run(operations, {{2, worker.message.word[2], admin.message.word[2]}});
    const Request expected[]{operate, set_enabled, open_verified};
    for (const auto operation : expected) {
        if (!called(request, operation)) {
            return false;
        }
        const auto response = invoke(operations, request);
        if (!called(response, reply)) {
            return false;
        }
        request = run(operations, response.message);
    }
    if (!called(request, operate)) {
        return false;
    }

    const auto disable = run(management, {{1, admin.message.word[2], 0}});
    if (!called(disable, set_enabled)) {
        return false;
    }
    const auto disabled = invoke(management, disable);
    if (!called(disabled, reply)) {
        return false;
    }
    auto manager_done = run(management, disabled.message);
    if (!called(manager_done, finish) || manager_done.message.word[1] != 1) {
        return false;
    }

    const auto revoked = invoke(operations, request);
    if (!called(revoked, reply)) {
        return false;
    }
    const auto worker_done = run(operations, revoked.message);
    if (!called(worker_done, finish) || worker_done.message.word[1] != 1 ||
        !refused_access(operations, identity_state.begin, false)) {
        return false;
    }
    // After the worker fault, the independent manager can still use the service.
    const auto enable = run(management, {{0, 0, 2, 1}});
    if (!called(enable, set_enabled)) {
        return false;
    }
    const auto enabled = invoke(management, enable);
    if (!called(enabled, reply)) {
        return false;
    }
    manager_done = run(management, enabled.message);
    return called(manager_done, finish) && manager_done.message.word[1] == 1 &&
           refused_access(management, client_code.begin, true);
}

} // namespace alacre::domain
