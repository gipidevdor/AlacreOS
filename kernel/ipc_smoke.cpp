#include "ipc.hpp"
#include "user_smoke.hpp"

namespace alacre::domain {
namespace {

using ipc::Handle;
using ipc::Status;

bool round_trip(SmokeSlot caller, Message request, Message expected) noexcept {
    const auto handled = run(caller, request);
    if (handled.stop != Stop::ipc) {
        return false;
    }
    // Resume the caller and check the registers returned through the guest ABI.
    const auto reply = run(caller, handled.message);
    if (reply.stop != Stop::call) {
        return false;
    }
    for (size_t word = 0; word < sizeof(Message::word) / sizeof(uint64_t); ++word) {
        if (reply.message.word[word] != expected.word[word]) {
            return false;
        }
    }
    return true;
}

bool send(SmokeSlot caller, Handle handle, uint64_t value, Status status = Status::ok) noexcept {
    return round_trip(caller, {{ipc::send, handle, value, ~value, value + 1, value + 2}},
                      {{static_cast<uint64_t>(status)}});
}

bool receive(SmokeSlot caller, Handle handle, Status status) noexcept {
    return round_trip(caller, {{ipc::receive, handle}}, {{static_cast<uint64_t>(status)}});
}

bool receive(Handle handle, SmokeSlot sender, uint64_t value) noexcept {
    return round_trip(consumer, {{ipc::receive, handle}},
                      {{static_cast<uint64_t>(Status::ok), value, ~value, value + 1, value + 2, sender}});
}

} // namespace

bool ipc_smoke() noexcept {
    using ipc::endpoints;
    constexpr SmokeSlot callers[]{producer, consumer, peer};
    for (const auto caller : callers) {
        const auto ready = run(caller, {});
        if (ready.stop != Stop::call || ready.message.word[0] != 0) {
            return false;
        }
    }

    const auto pipe = endpoints.open(producer, consumer);
    if (!pipe ||
        !send(peer, pipe->sender, 99, Status::denied) ||
        !send(consumer, pipe->receiver, 99, Status::denied) ||
        !receive(producer, pipe->sender, Status::denied) ||
        !receive(consumer, pipe->receiver, Status::would_block)) {
        return false;
    }

    constexpr uint64_t first = 10;
    constexpr uint64_t next = first + ipc::Endpoints::queue_capacity;
    for (auto value = first; value < next; ++value) {
        if (!send(producer, pipe->sender, value)) {
            return false;
        }
    }
    if (!send(producer, pipe->sender, next, Status::would_block) ||
        !receive(pipe->receiver, producer, first) || !send(producer, pipe->sender, next)) {
        return false;
    }
    for (auto value = first + 1; value <= next; ++value) {
        if (!receive(pipe->receiver, producer, value)) {
            return false;
        }
    }
    if (!receive(consumer, pipe->receiver, Status::would_block) || !send(producer, pipe->sender, 20) ||
        endpoints.close(consumer, pipe->receiver) != Status::ok) {
        return false;
    }

    const auto reopened = endpoints.open(producer, consumer);
    const auto independent = endpoints.open(peer, consumer);
    if (!reopened || !independent ||
        !send(producer, pipe->sender, 99, Status::denied) ||
        !receive(consumer, pipe->receiver, Status::denied) ||
        !receive(consumer, reopened->receiver, Status::would_block) ||
        !send(producer, reopened->sender, 30) || !send(peer, independent->sender, 40)) {
        return false;
    }

    return run(producer, {}).stop == Stop::timeout && run(producer, {}).stop == Stop::stopped &&
           receive(consumer, reopened->receiver, Status::denied) && receive(independent->receiver, peer, 40) &&
           receive(consumer, independent->receiver, Status::would_block);
}

} // namespace alacre::domain
