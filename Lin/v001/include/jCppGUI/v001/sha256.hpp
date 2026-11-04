// File: include/jCppGUI/v001/sha256.hpp
// Purpose: SHA-256, as FIPS 180-4 has it, fed a piece at a time. It is constexpr, so its known answers are checked in static_assert, and it needs no library. It is used to see that
//          two files hold the same bytes, and holds no secret, so nothing here tries to take the same time on all input. jgui.hpp does not include it.
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace jgui::inline v001 {

using Sha256Digest = std::array<std::uint8_t, 32>;

namespace detail {

inline constexpr std::array<std::uint32_t, 64> sha256_rounds{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline constexpr std::size_t sha256_block_bytes = 64;
inline constexpr std::size_t sha256_length_bytes = 8;

[[nodiscard]] constexpr std::uint32_t sha256_byte(char byte) noexcept { return static_cast<std::uint32_t>(static_cast<std::uint8_t>(byte)); }

}  // namespace detail

class Sha256 {
public:
    constexpr void update(std::span<const char> bytes) noexcept {
        total_ += bytes.size();
        while (!bytes.empty()) {
            if (filled_ == 0 && bytes.size() >= detail::sha256_block_bytes) {
                take_block(bytes.first<detail::sha256_block_bytes>());
                bytes = bytes.subspan(detail::sha256_block_bytes);
                continue;
            }
            block_.at(filled_) = bytes.front();
            bytes = bytes.subspan(1);
            if (++filled_ == detail::sha256_block_bytes) {
                take_block(std::span<const char, detail::sha256_block_bytes>{block_});
                filled_ = 0;
            }
        }
    }

    // The sum of what has been given so far. More can still be given afterwards.
    [[nodiscard]] constexpr Sha256Digest digest() const noexcept {
        Sha256 last = *this;
        const std::uint64_t bits = total_ * 8;
        const std::array<char, 1> end_mark{static_cast<char>(0x80)};
        last.update(end_mark);
        const std::array<char, 1> zero{};
        while (last.filled_ != detail::sha256_block_bytes - detail::sha256_length_bytes) last.update(zero);
        std::array<char, detail::sha256_length_bytes> length{};
        for (std::size_t at = 0; at < length.size(); ++at) length.at(at) = static_cast<char>(static_cast<std::uint8_t>((bits >> (56 - at * 8)) & 0xff));
        last.update(length);
        Sha256Digest digest{};
        for (std::size_t at = 0; at < digest.size(); ++at) digest.at(at) = static_cast<std::uint8_t>((last.state_.at(at / 4) >> (24 - (at % 4) * 8)) & 0xff);
        return digest;
    }

private:
    constexpr void take_block(std::span<const char, detail::sha256_block_bytes> block) noexcept {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t at = 0; at < 16; ++at) {
            const std::span<const char> word = block.subspan(at * 4, 4);
            words.at(at) = (detail::sha256_byte(word.front()) << 24) | (detail::sha256_byte(word[1]) << 16) | (detail::sha256_byte(word[2]) << 8) | detail::sha256_byte(word[3]);
        }
        for (std::size_t at = 16; at < words.size(); ++at) {
            const std::uint32_t before_15 = words.at(at - 15);
            const std::uint32_t before_2 = words.at(at - 2);
            const std::uint32_t small_0 = std::rotr(before_15, 7) ^ std::rotr(before_15, 18) ^ (before_15 >> 3);
            const std::uint32_t small_1 = std::rotr(before_2, 17) ^ std::rotr(before_2, 19) ^ (before_2 >> 10);
            words.at(at) = words.at(at - 16) + small_0 + words.at(at - 7) + small_1;
        }
        std::array<std::uint32_t, 8> working = state_;
        auto& [a, b, c, d, e, f, g, h] = working;
        for (std::size_t at = 0; at < words.size(); ++at) {
            const std::uint32_t big_1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t first = h + big_1 + choose + detail::sha256_rounds.at(at) + words.at(at);
            const std::uint32_t big_0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t second = big_0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        for (std::size_t at = 0; at < state_.size(); ++at) state_.at(at) += working.at(at);
    }

    std::array<std::uint32_t, 8> state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<char, detail::sha256_block_bytes> block_{};
    std::size_t filled_{};
    std::uint64_t total_{};
};

}  // namespace jgui::inline v001
