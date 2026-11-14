#pragma once

#include <stdint.h>

namespace alacre::domain::edf_guest {

inline constexpr uint64_t entered = 0x45444601;
inline constexpr uint64_t completed = 0x45444602;
inline constexpr uint64_t marker = 0x414c414352454446;

} // namespace alacre::domain::edf_guest
