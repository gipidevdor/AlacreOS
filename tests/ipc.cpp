#include "ipc.hpp"

#include <assert.h>

using alacre::ipc::Endpoints;
using alacre::ipc::Status;

namespace {

void check_status(Endpoints &endpoints, size_t caller, alacre::domain::Message message, Status status) {
    endpoints.invoke(caller, message);
    assert(message.word[0] == static_cast<uint64_t>(status));
    for (size_t word = 1; word < alacre::domain::message_words; ++word) {
        assert(message.word[word] == 0);
    }
}

void check_endpoint_quota() {
    Endpoints endpoints;
    const auto first = endpoints.open(0, 1);
    const auto second = endpoints.open(1, 2);
    const auto third = endpoints.open(2, 3);
    const auto fourth = endpoints.open(3, 4);
    assert(first && second && third && fourth);
    assert(endpoints.open(4, 5) == std::unexpected(Status::no_space));
    assert(endpoints.close(0, first->sender) == Status::denied);
    assert(endpoints.close(1, first->receiver) == Status::ok);
    assert(endpoints.open(4, 5));
    assert(endpoints.close(1, first->receiver) == Status::denied);
    assert(endpoints.close(4, fourth->receiver) == Status::ok);
    assert(endpoints.close(3, third->receiver) == Status::ok);
    assert(endpoints.open(4, 5) && endpoints.open(4, 5));
}

void check_domain_quota() {
    Endpoints endpoints;
    const auto first = endpoints.open(0, 1);
    assert(first && endpoints.open(2, 0) && endpoints.open(0, 3));
    assert(endpoints.open(4, 0) == std::unexpected(Status::no_space));
    assert(endpoints.open(0, 5) == std::unexpected(Status::no_space));
    const auto unrelated = endpoints.open(4, 5);
    assert(unrelated);

    endpoints.retire(0);
    assert(endpoints.open(0, 1) == std::unexpected(Status::invalid_peer));
    assert(endpoints.open(1, 0) == std::unexpected(Status::invalid_peer));
    assert(endpoints.open(4, 5) && endpoints.open(4, 5));
    assert(endpoints.close(5, unrelated->receiver) == Status::ok);
    assert(endpoints.close(1, first->receiver) == Status::denied);
    assert(endpoints.open(1, 2) && endpoints.open(2, 3));
    assert(endpoints.open(1, 3) == std::unexpected(Status::no_space));
}

void check_request_validation() {
    using namespace alacre::ipc;
    Endpoints endpoints;
    assert(endpoints.open(0, 0) == std::unexpected(Status::invalid_peer));
    assert(endpoints.open(alacre::domain::capacity, 0) == std::unexpected(Status::invalid_peer));
    assert(endpoints.open(0, alacre::domain::capacity) == std::unexpected(Status::invalid_peer));
    const auto pipe = endpoints.open(2, 1);
    assert(pipe);
    check_status(endpoints, 2, {{operation_bit + 2, pipe->sender, 11, 12, 13, 14}}, Status::invalid_operation);
    check_status(endpoints, 2, {{send, 0, 11, 12, 13, 14}}, Status::denied);
    check_status(endpoints, 2, {{send, UINT64_MAX}}, Status::denied);
    check_status(endpoints, alacre::domain::capacity, {{send, pipe->sender}}, Status::denied);
    endpoints.retire(alacre::domain::capacity);
    check_status(endpoints, 2, {{send, pipe->sender, 11, 12, 13, 14}}, Status::ok);
    alacre::domain::Message message{{receive, pipe->receiver, 21, 22, 23, 24}};
    endpoints.invoke(1, message);
    assert(message.word[0] == static_cast<uint64_t>(Status::ok));
    for (size_t word = 1; word <= payload_words; ++word) {
        assert(message.word[word] == 10 + word);
    }
    assert(message.word[1 + payload_words] == 2);
    check_status(endpoints, 1, {{receive, pipe->receiver, 21, 22, 23, 24}}, Status::would_block);
}

} // namespace

int main() {
    check_endpoint_quota();
    check_domain_quota();
    check_request_validation();
}
