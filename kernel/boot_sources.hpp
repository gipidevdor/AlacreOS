#pragma once

#include "boot.hpp"

#include <expected>

namespace alacre::boot {

enum class SourceError { truncated, invalid_magic, unsupported_version, invalid_range };

namespace detail {

[[nodiscard]] constexpr uint32_t big32(const unsigned char *bytes) noexcept {
    return static_cast<uint32_t>(bytes[0]) << 24 | static_cast<uint32_t>(bytes[1]) << 16 |
           static_cast<uint32_t>(bytes[2]) << 8 | static_cast<uint32_t>(bytes[3]);
}

[[nodiscard]] constexpr uint32_t little32(const unsigned char *bytes) noexcept {
    return static_cast<uint32_t>(bytes[3]) << 24 | static_cast<uint32_t>(bytes[2]) << 16 |
           static_cast<uint32_t>(bytes[1]) << 8 | static_cast<uint32_t>(bytes[0]);
}

[[nodiscard]] constexpr uint64_t little64(const unsigned char *bytes) noexcept {
    return static_cast<uint64_t>(little32(bytes + 4)) << 32 | little32(bytes);
}

} // namespace detail

inline constexpr uint64_t dtb_header_size = 40;
inline constexpr uint64_t pvh_v1_size = 56;

// The caller supplies a readable header and the size of its physical window.
[[nodiscard]] constexpr std::expected<uint32_t, SourceError>
dtb_size(const unsigned char *header, uint64_t available) noexcept {
    if (available < dtb_header_size) {
        return std::unexpected(SourceError::truncated);
    }
    if (detail::big32(header) != 0xd00dfeed) {
        return std::unexpected(SourceError::invalid_magic);
    }
    const auto version = detail::big32(header + 20);
    const auto compatible_version = detail::big32(header + 24);
    if (version < 17 || compatible_version < 16 || compatible_version > 17) {
        return std::unexpected(SourceError::unsupported_version);
    }
    const auto total = detail::big32(header + 4);
    const auto structure = detail::big32(header + 8);
    const auto strings = detail::big32(header + 12);
    const auto reservations = detail::big32(header + 16);
    // The reservation map must have room for at least its terminating entry.
    if (total > available || reservations < dtb_header_size ||
        reservations > structure || structure - reservations < 16 ||
        reservations % 8 != 0 || structure % 4 != 0 ||
        structure > strings || strings > total ||
        detail::big32(header + 36) < 4 || detail::big32(header + 36) > strings - structure ||
        detail::big32(header + 32) > total - strings) {
        return std::unexpected(SourceError::invalid_range);
    }
    return total;
}

// Read PVH start_info v1 as little-endian bytes, independently of compiler struct layout.
[[nodiscard]] constexpr std::expected<void, SourceError>
read_pvh(BootInfo &info, const unsigned char *header, uint64_t available,
         PhysicalRange table_window) noexcept {
    if (available < 8) {
        return std::unexpected(SourceError::truncated);
    }
    if (detail::little32(header) != 0x336ec578) {
        return std::unexpected(SourceError::invalid_magic);
    }
    if (detail::little32(header + 4) != 1) {
        return std::unexpected(SourceError::unsupported_version);
    }
    if (available < pvh_v1_size) {
        return std::unexpected(SourceError::truncated);
    }
    const auto count = detail::little32(header + 48);
    const PhysicalRange table{detail::little64(header + 40), count * e820_entry_size};
    if (detail::little32(header + 52) != 0 || table.base == 0 ||
        count > max_e820_entries || !table_window.contains(table)) {
        return std::unexpected(SourceError::invalid_range);
    }
    info.e820_table = table;
    info.acpi_rsdp = detail::little64(header + 32);
    return {};
}

} // namespace alacre::boot
