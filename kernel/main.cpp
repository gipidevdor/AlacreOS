#include "edf.hpp"
#include "platform.hpp"

namespace {

constinit alacre::EdfScheduler scheduler;

void print(const char *message) {
    for (; *message != '\0'; ++message) {
        alacre::platform::put_char(*message);
    }
}

// Drive the kernel-linked scheduler with synthetic ticks during bring-up.
bool verify_edf() {
    using alacre::EdfStatus;
    using alacre::JobState;
    return scheduler.configure(0, {4, 10, 20}) == EdfStatus::ok &&
           scheduler.configure(1, {2, 4, 20}) == EdfStatus::ok &&
           scheduler.release(0) == EdfStatus::ok &&
           scheduler.running() == 0 &&
           scheduler.advance_to(1) == EdfStatus::ok &&
           scheduler.release(1) == EdfStatus::ok &&
           scheduler.running() == 1 &&
           scheduler.advance_to(3) == EdfStatus::ok &&
           scheduler.job(1).state == JobState::budget_exhausted &&
           scheduler.running() == 0 &&
           scheduler.complete_at(6) == EdfStatus::ok &&
           scheduler.job(0).state == JobState::completed;
}

} // namespace

extern "C" [[noreturn]] void kernel_main() {
    alacre::platform::init_console();
    print("Alacre Core booted: ");
    print(alacre::platform::name());
    print(verify_edf() ? "\nEDF smoke: PASS\n" : "\nEDF smoke: FAIL\n");
    alacre::platform::halt();
}
