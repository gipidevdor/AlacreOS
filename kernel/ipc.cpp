#include "ipc.hpp"

namespace alacre::ipc {
namespace {

void reply_status(domain::Message &message, Status status) noexcept {
    message = {{static_cast<uint64_t>(status)}};
}

} // namespace

constinit Endpoints endpoints;

size_t Endpoints::Space::vacancy() const noexcept {
    for (size_t slot = 0; slot < cap_capacity; ++slot) {
        const auto &cap = slots[slot];
        if (cap.endpoint == endpoint_capacity && cap.generation < max_generation) {
            return slot;
        }
    }
    return cap_capacity;
}

Handle Endpoints::Space::grant(size_t slot, size_t endpoint, Operation right) noexcept {
    auto &cap = slots[slot];
    cap.endpoint = endpoint;
    cap.right = right;
    return (++cap.generation << slot_bits) | (slot + 1);
}

std::expected<Grants, Status> Endpoints::open(size_t producer, size_t consumer) noexcept {
    if (producer >= domain::capacity || consumer >= domain::capacity || producer == consumer ||
        spaces_[producer].retired || spaces_[consumer].retired) {
        return std::unexpected(Status::invalid_peer);
    }
    auto &sender = spaces_[producer];
    auto &receiver = spaces_[consumer];
    const auto send_slot = sender.vacancy();
    const auto receive_slot = receiver.vacancy();
    if (send_slot == cap_capacity || receive_slot == cap_capacity) {
        return std::unexpected(Status::no_space);
    }
    for (size_t id = 0; id < endpoint_capacity; ++id) {
        auto &endpoint = endpoints_[id];
        if (!endpoint.live) {
            endpoint.producer = producer;
            endpoint.consumer = consumer;
            endpoint.send_slot = send_slot;
            endpoint.receive_slot = receive_slot;
            endpoint.live = true;
            return Grants{sender.grant(send_slot, id, send), receiver.grant(receive_slot, id, receive)};
        }
    }
    return std::unexpected(Status::no_space);
}

Endpoints::Endpoint *Endpoints::resolve(size_t caller, Handle handle, Operation right) noexcept {
    const auto slot = handle & slot_mask;
    if (caller >= domain::capacity || slot == 0 || slot > cap_capacity) {
        return nullptr;
    }
    const auto &cap = spaces_[caller].slots[slot - 1];
    if (cap.endpoint == endpoint_capacity || cap.generation != handle >> slot_bits || cap.right != right) {
        return nullptr;
    }
    return &endpoints_[cap.endpoint];
}

void Endpoints::destroy(Endpoint &endpoint) noexcept {
    spaces_[endpoint.producer].slots[endpoint.send_slot].endpoint = endpoint_capacity;
    spaces_[endpoint.consumer].slots[endpoint.receive_slot].endpoint = endpoint_capacity;
    endpoint = {};
}

Status Endpoints::close(size_t consumer, Handle handle) noexcept {
    auto *endpoint = resolve(consumer, handle, receive);
    if (endpoint == nullptr) {
        return Status::denied;
    }
    destroy(*endpoint);
    return Status::ok;
}

void Endpoints::retire(size_t caller) noexcept {
    if (caller >= domain::capacity) {
        return;
    }
    spaces_[caller].retired = true;
    for (auto &cap : spaces_[caller].slots) {
        if (cap.endpoint != endpoint_capacity) {
            destroy(endpoints_[cap.endpoint]);
        }
    }
}

void Endpoints::invoke(size_t caller, domain::Message &message) noexcept {
    const auto operation = message.word[0];
    if (operation != send && operation != receive) {
        reply_status(message, Status::invalid_operation);
        return;
    }
    auto *endpoint = resolve(caller, message.word[1], static_cast<Operation>(operation));
    if (endpoint == nullptr) {
        reply_status(message, Status::denied);
        return;
    }
    auto &count = endpoint->count;
    auto &head = endpoint->head;
    if (operation == send) {
        if (count == queue_capacity) {
            reply_status(message, Status::would_block);
            return;
        }
        auto &payload = endpoint->queue[(head + count) % queue_capacity];
        for (size_t word = 0; word < payload_words; ++word) {
            payload.word[word] = message.word[word + 2];
        }
        ++count;
        reply_status(message, Status::ok);
        return;
    }
    if (count == 0) {
        reply_status(message, Status::would_block);
        return;
    }
    message.word[0] = static_cast<uint64_t>(Status::ok);
    for (size_t word = 0; word < payload_words; ++word) {
        message.word[word + 1] = endpoint->queue[head].word[word];
    }
    message.word[1 + payload_words] = endpoint->producer;
    head = (head + 1) % queue_capacity;
    --count;
}

} // namespace alacre::ipc
