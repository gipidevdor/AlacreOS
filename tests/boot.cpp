#include "boot_sources.hpp"

#include <array>
#include <cassert>

namespace {

using alacre::BootInfo;
using alacre::BootPlatform;
using alacre::PhysicalRange;
using alacre::boot::SourceError;

constexpr void write32(unsigned char *bytes, uint32_t value, bool big_endian) {
    for (unsigned index = 0; index < 4; ++index) {
        const unsigned position = big_endian ? 3 - index : index;
        bytes[position] = static_cast<unsigned char>(value >> (8 * index));
    }
}

void validate_dtb_header() {
    std::array<unsigned char, 40> header{};
    const auto parse = [&](uint64_t available) {
        return alacre::boot::dtb_size(header.data(), available);
    };
    const auto store = [&](unsigned offset, uint32_t value) {
        write32(header.data() + offset, value, true);
    };

    assert(parse(39) == std::unexpected(SourceError::truncated));
    assert(parse(128) == std::unexpected(SourceError::invalid_magic));
    store(0, 0xd00dfeed);
    store(4, 128);
    store(8, 56);
    store(12, 104);
    store(16, 40);
    store(20, 17);
    store(24, 16);
    store(32, 24);
    store(36, 48);
    assert(parse(128) == 128U);
    assert(parse(127) == std::unexpected(SourceError::invalid_range));
    store(16, 44);
    assert(parse(128) == std::unexpected(SourceError::invalid_range));
    store(16, 40);
    store(36, 49);
    assert(parse(128) == std::unexpected(SourceError::invalid_range));
    store(24, 18);
    assert(parse(128) == std::unexpected(SourceError::unsupported_version));
}

void reject_invalid_dtb_fields_independently() {
    constexpr uint32_t fields[]{0xd00dfeed, 128, 56, 104, 40, 17, 16, 0, 24, 48};
    constexpr struct {
        unsigned field;
        uint32_t value;
        SourceError error;
    } cases[]{
        {5, 16, SourceError::unsupported_version}, {6, 15, SourceError::unsupported_version},
        {4, 32, SourceError::invalid_range}, {4, 64, SourceError::invalid_range},
        {2, 52, SourceError::invalid_range}, {2, 61, SourceError::invalid_range},
        {2, 108, SourceError::invalid_range},
        {3, 129, SourceError::invalid_range}, {8, 25, SourceError::invalid_range},
        {9, 3, SourceError::invalid_range},
    };
    for (const auto &test : cases) {
        std::array<unsigned char, 40> header{};
        for (unsigned field = 0; field < sizeof(fields) / sizeof(uint32_t); ++field) {
            write32(header.data() + field * 4, field == test.field ? test.value : fields[field], true);
        }
        assert(alacre::boot::dtb_size(header.data(), 128) == std::unexpected(test.error));
    }
    std::array<unsigned char, 40> misaligned_reservations{};
    for (unsigned field = 0; field < sizeof(fields) / sizeof(uint32_t); ++field) {
        write32(misaligned_reservations.data() + field * 4, fields[field], true);
    }
    write32(misaligned_reservations.data() + 8, 64, true);
    write32(misaligned_reservations.data() + 36, 40, true);
    assert(alacre::boot::dtb_size(misaligned_reservations.data(), 128) == 128U);
    write32(misaligned_reservations.data() + 16, 44, true);
    assert(alacre::boot::dtb_size(misaligned_reservations.data(), 128) ==
           std::unexpected(SourceError::invalid_range));
}

void validate_pvh_header() {
    std::array<unsigned char, 56> header{};
    BootInfo info{};
    const auto parse = [&](uint64_t available) {
        return alacre::boot::read_pvh(info, header.data(), available, {0, 0x100000});
    };
    const auto store = [&](unsigned offset, uint32_t value) {
        write32(header.data() + offset, value, false);
    };

    assert(parse(7) == std::unexpected(SourceError::truncated));
    assert(parse(56) == std::unexpected(SourceError::invalid_magic));
    store(0, 0x336ec578);
    assert(parse(56) == std::unexpected(SourceError::unsupported_version));
    store(4, 1);
    assert(parse(55) == std::unexpected(SourceError::truncated));
    store(40, 0xfffe8);
    assert(parse(56) == std::unexpected(SourceError::invalid_range));
    store(48, 2);
    assert(parse(56) == std::unexpected(SourceError::invalid_range));
    assert(info.e820_table.size == 0);
    store(48, 1);
    assert(parse(56));
    assert(info.e820_table.base == 0xfffe8 && info.e820_table.size == 24);
    assert(info.acpi_rsdp == 0);
    store(32, 0xf0000);
    assert(parse(56));
    assert(info.acpi_rsdp == 0xf0000);
    store(52, 1);
    assert(parse(56) == std::unexpected(SourceError::invalid_range));
    store(52, 0);
    store(40, 0x4000);
    store(48, 129);
    assert(parse(56) == std::unexpected(SourceError::invalid_range));
    assert(info.e820_table.base == 0xfffe8);
    store(40, 0);
    store(48, 1);
    assert(parse(56) == std::unexpected(SourceError::invalid_range));
    assert(info.e820_table.base == 0xfffe8 && info.acpi_rsdp == 0xf0000);
}

void validate_handoff() {
    BootInfo info{
        BootInfo::magic_value, BootInfo::current_version, sizeof(BootInfo),
        BootPlatform::qemu_virt_aarch64, 0, {0x40200000, 0x2000},
        {0x40000000, 0x1000}, {}, 0,
    };
    assert(alacre::valid_boot_info(info));
    auto broken = info;
    broken.magic = 0;
    assert(!alacre::valid_boot_info(broken));
    broken = info;
    broken.reserved = 1;
    assert(!alacre::valid_boot_info(broken));
    broken = info;
    broken.kernel_image.size = 0;
    assert(!alacre::valid_boot_info(broken));
    broken = info;
    broken.kernel_image.base = 0;
    assert(!alacre::valid_boot_info(broken));
    broken = info;
    broken.platform_data.base = 0;
    assert(!alacre::valid_boot_info(broken));
    broken = info;
    broken.e820_table.base = 1;
    assert(!alacre::valid_boot_info(broken));
    broken = info;
    broken.e820_table.size = 1;
    assert(!alacre::valid_boot_info(broken));
    broken = info;
    broken.acpi_rsdp = 1;
    assert(!alacre::valid_boot_info(broken));
    info.platform_data.base = info.kernel_image.base;
    assert(!alacre::valid_boot_info(info));
    info.platform_data.base = 0x40000000;
    --info.size;
    assert(!alacre::valid_boot_info(info));
    ++info.size;
    ++info.version;
    assert(!alacre::valid_boot_info(info));
    --info.version;
    info.platform = static_cast<BootPlatform>(0);
    assert(!alacre::valid_boot_info(info));
    info.platform = BootPlatform::qemu_pc_x86_64;
    assert(!alacre::valid_boot_info(info));
    info.e820_table = {0x9000, 24};
    assert(alacre::valid_boot_info(info));
    info.e820_table.size = 25;
    assert(!alacre::valid_boot_info(info));
    info.e820_table.size = alacre::e820_entry_size * (alacre::max_e820_entries + 1);
    assert(!alacre::valid_boot_info(info));
    info.e820_table = {info.kernel_image.base - 8, 24};
    assert(!alacre::valid_boot_info(info));
    constexpr PhysicalRange overflowing{UINT64_MAX - 15, 16};
    static_assert(!overflowing.valid());
    static_assert(!PhysicalRange{0, 100}.contains({99, 2}));
}

} // namespace

int main() {
    validate_dtb_header();
    reject_invalid_dtb_fields_independently();
    validate_pvh_header();
    validate_handoff();
}
