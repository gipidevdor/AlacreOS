#pragma once

#include "domain_abi.hpp"

namespace alacre::ipc {

using Handle = uint64_t;
inline constexpr uint64_t operation_bit = uint64_t{1} << 63;

enum Operation : uint64_t { send = operation_bit, receive };
enum class Status : uint64_t { ok, denied, would_block, invalid_operation, invalid_peer, no_space };

inline constexpr unsigned payload_words = 4;
static_assert(payload_words + 2 == domain::message_words);

} // namespace alacre::ipc
