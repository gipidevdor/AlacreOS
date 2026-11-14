#include "guest_abi.hpp"
#include "identity.hpp"

namespace {

using namespace alacre::identity;
using namespace alacre::identity::guest;

[[gnu::section(".identity_state")]] constinit Directory directory;

Message process(Message request) noexcept {
    const auto channel = static_cast<ChannelId>(request.channel);
    const auto session = static_cast<SessionId>(request.first);
    const auto target = static_cast<AccountId>(request.second);
    switch (request.operation) {
    case open_verified:
        if (const auto opened = directory.open_session(target, channel, request.time)) {
            return {reply, 1, static_cast<uint64_t>(*opened), 0, 0, 0};
        }
        break;
    case operate:
        if (directory.authorize(session, channel, Permission::operate, request.time)) {
            return {reply, 1, 0, 0, 0, 0};
        }
        break;
    case set_enabled:
        if (request.third <= 1 && directory.set_enabled(session, channel, target, request.third == 1, request.time)) {
            return {reply, 1, 0, 0, 0, 0};
        }
        break;
    case disconnect:
        directory.disconnect(channel, request.time);
        return {reply, 1, 0, 0, 0, 0};
    }
    return {reply, 0, 0, 0, 0, 0};
}

} // namespace

extern "C" [[noreturn]] void identity_guest_entry() noexcept {
    // Commissioning data for the development image's IPC self-test.
    constexpr AccountConfig accounts[]{
        {AccountId{1}, Role::administrator, true}, {AccountId{2}, Role::operator_, true},
    };
    if (!directory.commission(accounts, 2, 1'000'000'000'000)) {
        (void)call({finish, 0, 0, 0, 0, 0});
    }
    auto request = call({receive, 0, 0, 0, 0, 0});
    for (;;) {
        request = call(process(request));
    }
}
