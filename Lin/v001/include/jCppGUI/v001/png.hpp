// File: include/jCppGUI/v001/png.hpp
// Purpose: Turns a WindowPicture into the bytes of a PNG file, with no library and all of it constexpr. It holds eight-bit RGB, because a window is opaque, and stored blocks with no compression, so there is nothing here that could
//          be led astray by what it reads: it only ever writes, sizes come from the picture, and a file is about three bytes for each pixel. jgui.hpp does not include it.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "canvas.hpp"
#include "error.hpp"

namespace jgui::inline v001 {

// The checksum zlib calls crc32, which a PNG has on each chunk.
[[nodiscard]] constexpr std::uint32_t png_crc32(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

// The checksum a zlib stream ends with.
[[nodiscard]] constexpr std::uint32_t png_adler32(std::span<const std::uint8_t> bytes) noexcept {
    constexpr std::uint32_t modulus = 65521;
    std::uint32_t low = 1;
    std::uint32_t high = 0;
    for (const std::uint8_t byte : bytes) {
        low = (low + byte) % modulus;
        high = (high + low) % modulus;
    }
    return (high << 16U) | low;
}

namespace detail {

constexpr void png_append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (const unsigned shift : {24U, 16U, 8U, 0U}) out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
}

// A row is a filter byte saying "as it is", then red, green and blue.
[[nodiscard]] constexpr std::vector<std::uint8_t> png_rows_of(const WindowPicture& picture) {
    const std::size_t columns = picture.columns();
    const std::size_t rows = picture.rows();
    std::vector<std::uint8_t> raw;
    raw.reserve((columns * 3 + 1) * rows);
    const std::span<const std::uint32_t> argb = picture.argb();
    for (std::size_t row = 0; row < rows; ++row) {
        raw.push_back(0);
        for (const std::uint32_t pixel : argb.subspan(row * columns, columns)) {
            for (const unsigned shift : {16U, 8U, 0U}) raw.push_back(static_cast<std::uint8_t>((pixel >> shift) & 0xFFU));
        }
    }
    return raw;
}

// A zlib stream that says "stored" and holds the bytes as they are, in blocks of at most what a block's length can say.
[[nodiscard]] constexpr std::vector<std::uint8_t> png_stored_zlib(std::span<const std::uint8_t> raw) {
    constexpr std::size_t most_in_a_block = 65535;
    std::vector<std::uint8_t> out{0x78, 0x01};
    out.reserve(raw.size() + raw.size() / most_in_a_block * 5 + 16);
    std::size_t at = 0;
    do {
        const std::size_t length = std::min(raw.size() - at, most_in_a_block);
        const auto length16 = static_cast<std::uint16_t>(length);
        const auto inverted = static_cast<std::uint16_t>(~length16);
        out.push_back(at + length == raw.size() ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(length16 & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(length16 >> 8U));
        out.push_back(static_cast<std::uint8_t>(inverted & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(inverted >> 8U));
        for (const std::uint8_t byte : raw.subspan(at, length)) out.push_back(byte);
        at += length;
    } while (at < raw.size());
    png_append_be32(out, png_adler32(raw));
    return out;
}

constexpr void png_append_chunk(std::vector<std::uint8_t>& out, std::string_view type, std::span<const std::uint8_t> data) {
    png_append_be32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t checked_from = out.size();
    for (const char letter : type) out.push_back(static_cast<std::uint8_t>(letter));
    for (const std::uint8_t byte : data) out.push_back(byte);
    png_append_be32(out, png_crc32(std::span<const std::uint8_t>{out}.subspan(checked_from)));
}

}  // namespace detail

// A PNG has no way to say a picture with nothing in it, so that is refused.
[[nodiscard]] constexpr std::expected<std::vector<std::uint8_t>, Error> png_of(const WindowPicture& picture) {
    if (picture.columns() == 0 || picture.rows() == 0) return make_error(ErrorCode::SizeLimit, "the window has no size yet");
    std::vector<std::uint8_t> header;
    detail::png_append_be32(header, picture.columns());
    detail::png_append_be32(header, picture.rows());
    for (const std::uint8_t field : std::array<std::uint8_t, 5>{8, 2, 0, 0, 0}) header.push_back(field);  // eight bits a channel, RGB, the only compression and filters there are, not interlaced
    const std::vector<std::uint8_t> stream = detail::png_stored_zlib(detail::png_rows_of(picture));
    std::vector<std::uint8_t> out{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    out.reserve(stream.size() + 64);
    detail::png_append_chunk(out, "IHDR", header);
    detail::png_append_chunk(out, "IDAT", stream);
    detail::png_append_chunk(out, "IEND", std::span<const std::uint8_t>{});
    return out;
}

}  // namespace jgui::inline v001
