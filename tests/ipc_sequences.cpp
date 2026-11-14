#include "ipc.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <deque>

namespace {

using namespace alacre::ipc;
using alacre::domain::Message;

using Payload = std::array<uint64_t, payload_words>;
constexpr size_t producer = 0;
constexpr size_t consumer = 1;
constexpr size_t unprivileged = 2;

void check_reply(Endpoints &endpoints, size_t caller, Message message, Status status,
                 const Payload &expected = {}, size_t sender = 0) {
    endpoints.invoke(caller, message);
    assert(message.word[0] == static_cast<uint64_t>(status));
    assert(std::equal(expected.begin(), expected.end(), message.word + 1));
    assert(message.word[1 + payload_words] == sender);
}

void compare_with_fifo(const uint8_t *data, size_t size) {
    Endpoints endpoints;
    auto channel = endpoints.open(producer, consumer);
    assert(channel);
    Grants stale{};
    std::deque<Payload> fifo;
    for (size_t step = 0; step < size; ++step) {
        const uint64_t value = data[step];
        const Payload payload{step, value, ~value, UINT64_MAX};
        const Message request{{send, channel->sender, payload[0], payload[1], payload[2], payload[3]}};
        const Message read{{receive, channel->receiver}};
        switch (value % 8) {
        case 0:
        case 1:
        case 2:
            if (fifo.size() == Endpoints::queue_capacity) {
                check_reply(endpoints, producer, request, Status::would_block);
            } else {
                check_reply(endpoints, producer, request, Status::ok);
                fifo.push_back(payload);
            }
            break;
        case 3:
        case 4:
            if (fifo.empty()) {
                check_reply(endpoints, consumer, read, Status::would_block);
            } else {
                check_reply(endpoints, consumer, read, Status::ok, fifo.front(), producer);
                fifo.pop_front();
            }
            break;
        case 5:
            assert(endpoints.close(consumer, channel->receiver) == Status::ok);
            stale = *channel;
            fifo.clear();
            channel = endpoints.open(producer, consumer);
            assert(channel);
            break;
        case 6:
            check_reply(endpoints, producer, {{send, stale.sender}}, Status::denied);
            check_reply(endpoints, consumer, {{receive, stale.receiver}}, Status::denied);
            break;
        case 7:
            check_reply(endpoints, unprivileged, request, Status::denied);
            check_reply(endpoints, producer, read, Status::denied);
            break;
        }
    }
    for (const auto &payload : fifo) {
        check_reply(endpoints, consumer, {{receive, channel->receiver}}, Status::ok, payload, producer);
    }
    check_reply(endpoints, consumer, {{receive, channel->receiver}}, Status::would_block);
}

} // namespace

#if defined(ALACRE_FUZZ)
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    compare_with_fifo(data, size);
    return 0;
}
#else
int main() {
    // Exhaust every send/receive ordering long enough to fill, drain and wrap the ring.
    std::array<uint8_t, 12> trace{};
    for (size_t ordering = 0; ordering < (size_t{1} << trace.size()); ++ordering) {
        for (size_t step = 0; step < trace.size(); ++step) {
            trace[step] = (ordering & (size_t{1} << step)) == 0 ? 0 : 3;
        }
        compare_with_fifo(trace.data(), trace.size());
    }
    constexpr uint8_t revoke[]{0, 0, 7, 5, 6, 3, 0, 3};
    compare_with_fifo(revoke, sizeof(revoke));
}
#endif
