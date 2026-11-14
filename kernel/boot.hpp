#pragma once

#include <stddef.h>
#include <stdint.h>

namespace alacre {

enum class BootPlatform : uint32_t { qemu_virt_aarch64 = 1, qemu_pc_x86_64 = 2 };

struct PhysicalRange {
    uint64_t base;
    uint64_t size;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return size != 0 && size <= UINT64_MAX - base;
    }

    [[nodiscard]] constexpr bool contains(PhysicalRange range) const noexcept {
        if (!valid() || !range.valid() || range.base < base) {
            return false;
        }
        const auto offset = range.base - base;
        return offset <= size && range.size <= size - offset;
    }
};

// Native-endian handoff between the platform adapter and Alacre Core.
struct BootInfo {
    static constexpr uint32_t magic_value = 0x414c4352;
    static constexpr uint16_t current_version = 1;

    uint32_t magic;
    uint16_t version;
    uint16_t size;
    BootPlatform platform;
    uint32_t reserved;
    PhysicalRange kernel_image;
    PhysicalRange platform_data;
    PhysicalRange e820_table;
    uint64_t acpi_rsdp;
};

static_assert(offsetof(BootInfo, kernel_image) == 16);
static_assert(offsetof(BootInfo, platform_data) == 32);
static_assert(offsetof(BootInfo, e820_table) == 48);
static_assert(offsetof(BootInfo, acpi_rsdp) == 64);
static_assert(sizeof(BootInfo) == 72);

inline constexpr uint64_t e820_entry_size = 24;
inline constexpr uint64_t max_e820_entries = 128;

[[nodiscard]] constexpr bool valid_boot_info(const BootInfo &info) noexcept {
    if (info.magic != BootInfo::magic_value || info.version != BootInfo::current_version ||
        info.size != sizeof(BootInfo) || info.reserved != 0 ||
        !info.kernel_image.valid() || info.kernel_image.base == 0 ||
        info.platform_data.base == 0 ||
        !PhysicalRange{0, info.kernel_image.base}.contains(info.platform_data)) {
        return false;
    }
    switch (info.platform) {
    case BootPlatform::qemu_virt_aarch64:
        return info.e820_table.base == 0 && info.e820_table.size == 0 && info.acpi_rsdp == 0;
    case BootPlatform::qemu_pc_x86_64:
        return info.e820_table.base != 0 &&
               PhysicalRange{0, info.kernel_image.base}.contains(info.e820_table) &&
               info.e820_table.size % e820_entry_size == 0 &&
               info.e820_table.size / e820_entry_size <= max_e820_entries;
    }
    return false;
}

} // namespace alacre
