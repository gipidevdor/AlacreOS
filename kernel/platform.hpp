#pragma once

#include "boot_sources.hpp"

namespace alacre::platform {

void init_console();
void put_char(char character);
const char *name();
[[nodiscard]] std::expected<void, boot::SourceError>
read_boot_info(BootInfo &info, uint64_t loader_argument) noexcept;
[[nodiscard]] bool init_timer() noexcept;
[[nodiscard]] uint64_t monotonic_now() noexcept;
[[nodiscard]] uint64_t timer_quantum() noexcept;
void arm_timer(uint64_t deadline) noexcept;
void disarm_timer() noexcept;
void wait_for_interrupt() noexcept;
[[noreturn]] void complete_context() noexcept;

using Context = uint64_t *;
// The caller supplies the aligned top of a dedicated, zeroed stack.
[[nodiscard]] Context prepare_context(uint64_t *stack_top, void (*entry)()) noexcept;
[[noreturn]] void halt();

} // namespace alacre::platform

extern "C" alacre::platform::Context
kernel_timer_event(alacre::platform::Context interrupted, uint64_t timestamp) noexcept;
extern "C" alacre::platform::Context kernel_completion_event(uint64_t timestamp) noexcept;
