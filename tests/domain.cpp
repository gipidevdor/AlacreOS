#include "domain.hpp"

#include <cassert>

namespace {

using namespace alacre::domain;

constexpr ImageMemory memory{{0x1000, 0x4000}, {0x4000, 0x6000}, {0x8000, 0x8000 + capacity * page_size}};

void validate_memory_pools() {
    Image images[capacity]{};
    for (size_t slot = 0; slot < capacity; ++slot) {
        const auto stack = memory.stacks.begin + slot * page_size;
        images[slot] = {{0x1000, 0x2000}, {}, {stack, stack + page_size}, 0x1fff};
    }
    images[0].state = {0x4000, 0x5000};
    images[1].state = {0x5000, 0x6000};
    assert(valid_images(images, memory));

    auto bad_pool = memory;
    bad_pool.code.end = 0x5000;
    assert(!valid_images(images, bad_pool));
    bad_pool = memory;
    bad_pool.stacks.begin = 0x5000;
    assert(!valid_images(images, bad_pool));
    bad_pool = memory;
    bad_pool.stacks.begin = 0x3000;
    assert(!valid_images(images, bad_pool));
    bad_pool = memory;
    bad_pool.stacks.begin = 0x8001;
    assert(!valid_images(images, bad_pool));

    const auto original = images[1];
    images[1].entry = images[1].code.end;
    assert(!valid_images(images, memory));
    images[1].entry = original.entry;
    images[1].code.end += 1;
    assert(!valid_images(images, memory));
    images[1].code.end = 0x5000;
    assert(!valid_images(images, memory));
    images[1] = original;
    images[1].state = images[0].state;
    assert(!valid_images(images, memory));
    images[1].state = {0, page_size};
    assert(!valid_images(images, memory));
    images[1] = original;
    images[1].stack = images[0].stack;
    assert(!valid_images(images, memory));
    images[1].stack = {0x9000, 0x9000};
    assert(!valid_images(images, memory));
    images[1].stack = {0x7000, 0x8000};
    assert(!valid_images(images, memory));
    images[1].stack = {memory.stacks.end, memory.stacks.end + page_size};
    assert(!valid_images(images, memory));
    images[1].stack = original.stack;
    assert(valid_images(images, memory));
}

} // namespace

int main() {
    validate_memory_pools();
}
