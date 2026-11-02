// File: tests/fake_compositor/wire.hpp
// Purpose: The Wayland wire format, typed and bounds-checked: a message is a little-endian header of the target
//          object, the opcode and the size, then 32-bit arguments. Nothing here reads a byte it has not checked
//          is there, and each refusal is a value. It is constexpr, so static_asserts run it on edge cases.
#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace jgui_test::wire {

static_assert(std::endian::native == std::endian::little, "the wire format is the machine's own, and this code assumes little-endian");

inline constexpr std::size_t header_bytes = 8;
inline constexpr std::size_t max_message_bytes = 4096;  // what libwayland refuses to go beyond

// Which object a message is for.
struct ObjectId {
    std::uint32_t value{};
    friend constexpr bool operator==(const ObjectId&, const ObjectId&) = default;
    friend constexpr auto operator<=>(const ObjectId&, const ObjectId&) = default;
};

inline constexpr ObjectId display_id{1};

// 24.8 fixed point, as raw as it comes.
struct Fixed {
    std::int32_t raw{};
    [[nodiscard]] static constexpr Fixed from_int(std::int32_t whole) noexcept { return Fixed{whole * 256}; }
};

enum class Refusal : std::uint8_t {
    TooShortForHeader,  // not enough bytes yet for a header: wait for more
    SizeTooSmall,       // the size field is under the header's own size
    SizeNotAligned,     // the size field is not a multiple of four
    SizeTooLarge,       // the size field is over the protocol's limit
    Truncated,          // the size field is over what has arrived: wait for more
    ArgumentPastEnd,    // an argument would run past the message's end
    StringLengthZero,   // a string with no room even for its terminator, where one must be there
    StringNotTerminated,
    StringHasNul,
};

struct Header {
    ObjectId object;
    std::uint16_t opcode{};
    std::uint16_t size{};
};

[[nodiscard]] constexpr std::uint32_t load_u32(std::span<const std::byte, 4> bytes) noexcept {
    return std::to_integer<std::uint32_t>(bytes[0]) | (std::to_integer<std::uint32_t>(bytes[1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[2]) << 16U) | (std::to_integer<std::uint32_t>(bytes[3]) << 24U);
}

constexpr void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
    for (const unsigned shift : {0U, 8U, 16U, 24U}) out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}

// A header from the start of what has arrived. TooShortForHeader and Truncated mean only that more must arrive.
[[nodiscard]] constexpr std::expected<Header, Refusal> read_header(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < header_bytes) return std::unexpected(Refusal::TooShortForHeader);
    const std::uint32_t object = load_u32(bytes.first<4>());
    const std::uint32_t both = load_u32(bytes.subspan<4, 4>());
    const std::uint32_t size = both >> 16U;
    if (size < header_bytes) return std::unexpected(Refusal::SizeTooSmall);
    if (size % 4 != 0) return std::unexpected(Refusal::SizeNotAligned);
    if (size > max_message_bytes) return std::unexpected(Refusal::SizeTooLarge);
    if (size > bytes.size()) return std::unexpected(Refusal::Truncated);
    return Header{ObjectId{object}, static_cast<std::uint16_t>(both & 0xFFFFU), static_cast<std::uint16_t>(size)};
}

// Reads one message's arguments, in order, and refuses anything past its end.
class MessageReader {
public:
    constexpr explicit MessageReader(std::span<const std::byte> arguments) noexcept : rest_{arguments} {}

    [[nodiscard]] constexpr bool at_end() const noexcept { return rest_.empty(); }

    [[nodiscard]] constexpr std::expected<std::uint32_t, Refusal> u32() noexcept {
        if (rest_.size() < 4) return std::unexpected(Refusal::ArgumentPastEnd);
        const std::uint32_t value = load_u32(rest_.first<4>());
        rest_ = rest_.subspan(4);
        return value;
    }

    [[nodiscard]] constexpr std::expected<std::int32_t, Refusal> i32() noexcept {
        const auto value = u32();
        if (!value) return std::unexpected(value.error());
        return static_cast<std::int32_t>(*value);
    }

    [[nodiscard]] constexpr std::expected<ObjectId, Refusal> object() noexcept {
        const auto value = u32();
        if (!value) return std::unexpected(value.error());
        return ObjectId{*value};
    }

    // A string on the wire is its length with the terminator, its bytes and padding up to a multiple of four.
    [[nodiscard]] constexpr std::expected<std::string, Refusal> string() {
        const auto length = u32();
        if (!length) return std::unexpected(length.error());
        if (*length == 0) return std::unexpected(Refusal::StringLengthZero);
        // Worked out in 64 bits, so the largest length cannot wrap around to a small one.
        const std::uint64_t padded = (std::uint64_t{*length} + 3U) / 4U * 4U;
        if (padded > rest_.size()) return std::unexpected(Refusal::ArgumentPastEnd);
        const std::span<const std::byte> bytes = rest_.first(*length);
        if (bytes.back() != std::byte{0}) return std::unexpected(Refusal::StringNotTerminated);
        std::string text;
        text.reserve(bytes.size() - 1);
        for (const std::byte byte : bytes.first(bytes.size() - 1)) {
            if (byte == std::byte{0}) return std::unexpected(Refusal::StringHasNul);
            text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
        }
        rest_ = rest_.subspan(static_cast<std::size_t>(padded));
        return text;
    }

private:
    std::span<const std::byte> rest_;
};

// Builds one message, and fills in the size in its header when it is finished.
class MessageWriter {
public:
    constexpr MessageWriter(ObjectId target, std::uint16_t opcode) : opcode_{opcode} {
        append_u32(bytes_, target.value);
        append_u32(bytes_, 0);
    }

    constexpr MessageWriter& u32(std::uint32_t value) {
        append_u32(bytes_, value);
        return *this;
    }
    constexpr MessageWriter& i32(std::int32_t value) { return u32(static_cast<std::uint32_t>(value)); }
    constexpr MessageWriter& fixed(Fixed value) { return i32(value.raw); }
    constexpr MessageWriter& object(ObjectId value) { return u32(value.value); }

    constexpr MessageWriter& string(std::string_view text) {
        append_u32(bytes_, static_cast<std::uint32_t>(text.size() + 1));
        for (const char c : text) bytes_.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        bytes_.push_back(std::byte{0});
        while (bytes_.size() % 4 != 0) bytes_.push_back(std::byte{0});
        return *this;
    }

    constexpr MessageWriter& array(std::span<const std::uint32_t> values) {
        append_u32(bytes_, static_cast<std::uint32_t>(values.size() * 4));
        for (const std::uint32_t value : values) append_u32(bytes_, value);
        return *this;
    }

    [[nodiscard]] constexpr std::vector<std::byte> finish() {
        const std::uint32_t both = (static_cast<std::uint32_t>(bytes_.size()) << 16U) | opcode_;
        std::vector<std::byte> header;
        append_u32(header, both);
        for (std::size_t i = 0; i < 4; ++i) bytes_[4 + i] = header[i];
        return bytes_;
    }

private:
    std::vector<std::byte> bytes_{};
    std::uint16_t opcode_;
};

// --- Edge cases, run at compile time. Reading past any of these arrays would not compile. ---
namespace checks {

// The bytes of a message whose header claims a size, with what follows.
consteval std::vector<std::byte> with_size(std::uint32_t size, std::size_t actual) {
    std::vector<std::byte> bytes;
    append_u32(bytes, 7);
    append_u32(bytes, (size << 16U) | 3U);
    while (bytes.size() < actual) bytes.push_back(std::byte{0});
    return bytes;
}

consteval bool refused_with(const std::vector<std::byte>& bytes, Refusal why) {
    const auto header = read_header(bytes);
    return !header && header.error() == why;
}

static_assert(refused_with({}, Refusal::TooShortForHeader));
static_assert(refused_with(with_size(8, 7), Refusal::SizeTooSmall) == false);
static_assert(refused_with(with_size(0, 8), Refusal::SizeTooSmall));
static_assert(refused_with(with_size(4, 8), Refusal::SizeTooSmall));
static_assert(refused_with(with_size(9, 12), Refusal::SizeNotAligned));
static_assert(refused_with(with_size(4100, 8), Refusal::SizeTooLarge));
static_assert(refused_with(with_size(0xFFFC, 8), Refusal::SizeTooLarge));
static_assert(refused_with(with_size(12, 8), Refusal::Truncated));

consteval bool header_reads_back() {
    const auto bytes = with_size(16, 16);
    const auto header = read_header(bytes);
    return header && header->object == ObjectId{7} && header->opcode == 3 && header->size == 16;
}
static_assert(header_reads_back());

consteval bool at_the_limit() {
    const auto bytes = with_size(4096, 4096);
    return read_header(bytes).has_value();
}
static_assert(at_the_limit());

consteval bool messages_round_trip() {
    const std::array<std::uint32_t, 2> values{1, 2};
    auto bytes = MessageWriter{ObjectId{9}, 2}.u32(5).i32(-1).string("abc").string("abcd").array(values).fixed(Fixed::from_int(2)).finish();
    if (bytes.size() % 4 != 0) return false;
    const auto header = read_header(bytes);
    if (!header || header->object != ObjectId{9} || header->opcode != 2 || header->size != bytes.size()) return false;
    MessageReader reader{std::span<const std::byte>{bytes}.subspan(header_bytes)};
    if (reader.u32() != 5U || reader.i32() != -1 || reader.string() != "abc" || reader.string() != "abcd") return false;
    return reader.u32() == 8U && reader.u32() == 1U && reader.u32() == 2U && reader.i32() == 512 && reader.at_end();
}
static_assert(messages_round_trip());

consteval bool argument_refused(const std::vector<std::byte>& arguments, Refusal why) {
    MessageReader reader{arguments};
    const auto text = reader.string();
    return !text && text.error() == why;
}
consteval std::vector<std::byte> string_with_length(std::uint32_t length, std::size_t body) {
    std::vector<std::byte> bytes;
    append_u32(bytes, length);
    while (bytes.size() < 4 + body) bytes.push_back(std::byte{'x'});
    return bytes;
}
static_assert(argument_refused({}, Refusal::ArgumentPastEnd));
static_assert(argument_refused(string_with_length(0, 0), Refusal::StringLengthZero));
static_assert(argument_refused(string_with_length(5, 4), Refusal::ArgumentPastEnd), "padding must be there too");
static_assert(argument_refused(string_with_length(4, 4), Refusal::StringNotTerminated));
static_assert(argument_refused(string_with_length(0xFFFFFFFF, 4), Refusal::ArgumentPastEnd), "this length must not wrap");
static_assert(argument_refused(string_with_length(0xFFFFFFFD, 4), Refusal::ArgumentPastEnd));

consteval bool embedded_nul_is_refused() {
    std::vector<std::byte> bytes;
    append_u32(bytes, 4);
    for (const std::byte byte : {std::byte{'a'}, std::byte{0}, std::byte{'b'}, std::byte{0}}) bytes.push_back(byte);
    return argument_refused(bytes, Refusal::StringHasNul);
}
static_assert(embedded_nul_is_refused());

consteval bool short_arguments_are_refused() {
    const std::array<std::byte, 3> three{};
    MessageReader reader{three};
    return !reader.u32().has_value() && !reader.i32().has_value() && !reader.object().has_value();
}
static_assert(short_arguments_are_refused());

}  // namespace checks

}  // namespace jgui_test::wire
