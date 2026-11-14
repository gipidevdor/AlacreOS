#pragma once

#include <expected>

#include "domain.hpp"
#include "ipc_abi.hpp"

namespace alacre::ipc {

struct Grants {
    Handle sender;
    Handle receiver;
};

// One core owns this object and serializes invocations and configuration changes.
class Endpoints {
public:
    static constexpr size_t queue_capacity = 4;

    Endpoints() noexcept = default;
    Endpoints(const Endpoints &) = delete;
    Endpoints &operator=(const Endpoints &) = delete;

    [[nodiscard]] std::expected<Grants, Status> open(size_t producer, size_t consumer) noexcept;
    [[nodiscard]] Status close(size_t consumer, Handle handle) noexcept;
    void retire(size_t domain) noexcept;
    // Replace the request with the reply in the same trap message.
    void invoke(size_t caller, domain::Message &message) noexcept;

private:
    static constexpr size_t endpoint_capacity = 4;
    static constexpr size_t cap_capacity = 3;
    static constexpr unsigned slot_bits = 8;
    static constexpr uint64_t slot_mask = (uint64_t{1} << slot_bits) - 1;
    static constexpr uint64_t max_generation = UINT64_MAX >> slot_bits;
    static_assert(cap_capacity <= slot_mask);

    struct Capability {
        size_t endpoint = endpoint_capacity;
        uint64_t generation = 0;
        Operation right = send;
    };

    struct Space {
        Capability slots[cap_capacity];
        bool retired = false;

        [[nodiscard]] size_t vacancy() const noexcept;
        [[nodiscard]] Handle grant(size_t slot, size_t endpoint, Operation right) noexcept;
    };

    struct Payload {
        uint64_t word[payload_words];
    };

    struct Endpoint {
        Payload queue[queue_capacity];
        size_t head;
        size_t count;
        size_t producer;
        size_t consumer;
        size_t send_slot;
        size_t receive_slot;
        bool live;
    };

    [[nodiscard]] Endpoint *resolve(size_t caller, Handle handle, Operation right) noexcept;
    void destroy(Endpoint &endpoint) noexcept;

    Space spaces_[domain::capacity]{};
    Endpoint endpoints_[endpoint_capacity]{};
};

// The domain runner uses the same table as the trusted bootstrap configuration.
extern Endpoints endpoints;

} // namespace alacre::ipc
