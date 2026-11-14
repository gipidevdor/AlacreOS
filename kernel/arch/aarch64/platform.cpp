#include "platform.hpp"

#include <stdint.h>

namespace alacre::platform {
namespace {

// This address is specific to the versioned QEMU virt-8.2 development machine.
constexpr uintptr_t uart_base = 0x09000000;

volatile uint32_t &uart(uintptr_t offset) {
    return *reinterpret_cast<volatile uint32_t *>(uart_base + offset);
}

} // namespace

void init_console() {
    uart(0x30) = 0;             // Disable UART before configuring it.
    uart(0x24) = 13;            // Integer divisor for 115200 baud at 24 MHz.
    uart(0x28) = 1;
    uart(0x2c) = 3U << 5;       // Eight data bits.
    uart(0x30) = (1U << 8) | 1; // Transmitter and UART enable.
}

void put_char(char character) {
    while ((uart(0x18) & (1U << 5)) != 0) {
        asm volatile("yield");
    }
    uart(0) = static_cast<unsigned char>(character);
}

const char *name() {
    return "aarch64";
}

[[noreturn]] void halt() {
    for (;;) {
        asm volatile("wfi");
    }
}

} // namespace alacre::platform
