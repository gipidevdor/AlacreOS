#pragma once

#include <stddef.h>

namespace alacre::domain {

enum SmokeSlot : size_t { service, management, operations, producer, consumer, peer, survivor, overrun };

[[nodiscard]] bool initialize_user_smoke() noexcept;
[[nodiscard]] bool identity_smoke() noexcept;
[[nodiscard]] bool ipc_smoke() noexcept;
[[nodiscard]] bool user_edf_smoke() noexcept;

} // namespace alacre::domain
