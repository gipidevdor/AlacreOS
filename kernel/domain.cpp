#include "domain.hpp"
#include "ipc.hpp"

extern "C" {
extern const unsigned char __guest_code_start[], __guest_code_end[];
extern const unsigned char __guest_state_start[], __guest_state_end[];
extern const unsigned char __guest_stacks_start[], __guest_stacks_end[];
}

namespace alacre::domain {
namespace {

void resume(size_t slot, const Message *input, uint64_t deadline, Event &event) noexcept {
    arch::resume(slot, input, deadline, event);
    if (event.stop == Stop::fault) {
        retire(slot);
    }
}

} // namespace

bool initialize(const Image (&images)[capacity]) noexcept {
    const auto range = [](const unsigned char *start, const unsigned char *end) {
        return Range{reinterpret_cast<uintptr_t>(start), reinterpret_cast<uintptr_t>(end)};
    };
    const ImageMemory memory{
        range(__guest_code_start, __guest_code_end),
        range(__guest_state_start, __guest_state_end),
        range(__guest_stacks_start, __guest_stacks_end),
    };
    return valid_images(images, memory) && arch::initialize(images);
}

Event run(size_t slot, const Message &input) noexcept {
    const auto deadline = watchdog_deadline();
    Event event{{}, Stop::stopped, 0};
    resume(slot, &input, deadline, event);
    while ((event.stop == Stop::timer || event.stop == Stop::interrupted) && event.timestamp < deadline) {
        resume(slot, nullptr, deadline, event);
    }
    if (event.stop == Stop::call && (event.message.word[0] & ipc::operation_bit) != 0) {
        ipc::endpoints.invoke(slot, event.message);
        event.stop = Stop::ipc;
    } else if (event.stop == Stop::timer || event.stop == Stop::interrupted) {
        retire(slot);
        event.stop = Stop::timeout;
    }
    return event;
}

Event run_until(size_t slot, const Message *input, uint64_t deadline) noexcept {
    Event event{{}, Stop::stopped, 0};
    resume(slot, input, deadline, event);
    return event;
}

void retire(size_t slot) noexcept {
    arch::retire(slot);
    ipc::endpoints.retire(slot);
}

} // namespace alacre::domain
