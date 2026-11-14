#include <stddef.h>

extern "C" void *memset(void *destination, int value, size_t count) noexcept {
    auto *byte = static_cast<unsigned char *>(destination);
    for (size_t i = 0; i < count; ++i) {
        byte[i] = static_cast<unsigned char>(value);
    }
    return destination;
}
