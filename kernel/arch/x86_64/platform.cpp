#include "platform.hpp"

#include <stdint.h>

namespace alacre::platform {
namespace {

constexpr uint16_t com1 = 0x03f8;

void out(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t in(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

} // namespace

void init_console() {
    out(com1 + 1, 0);    // Disable UART interrupts.
    out(com1 + 3, 0x80); // Select divisor registers.
    out(com1, 1);        // 115200 baud.
    out(com1 + 1, 0);
    out(com1 + 3, 0x03); // Eight data bits, no parity, one stop bit.
    out(com1 + 2, 0x07); // Enable and reset FIFOs.
}

void put_char(char character) {
    while ((in(com1 + 5) & 0x20) == 0) {
        asm volatile("pause");
    }
    out(com1, static_cast<uint8_t>(character));
}

const char *name() {
    return "x86_64";
}

[[noreturn]] void halt() {
    for (;;) {
        asm volatile("hlt");
    }
}

} // namespace alacre::platform
