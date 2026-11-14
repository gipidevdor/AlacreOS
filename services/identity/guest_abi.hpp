#pragma once

#include "domain_abi.hpp"

namespace alacre::identity::guest {

enum Request : uint64_t { receive, reply, finish, operate, set_enabled, open_verified, disconnect };

struct Message {
    uint64_t operation;
    uint64_t first;
    uint64_t second;
    uint64_t third;
    uint64_t channel;
    uint64_t time;
};

[[gnu::always_inline]] inline Message call(Message message) noexcept {
    const auto result = domain::trap({{message.operation, message.first, message.second, message.third,
                                       message.channel, message.time}});
    return {result.word[0], result.word[1], result.word[2], result.word[3], result.word[4], result.word[5]};
}

} // namespace alacre::identity::guest
