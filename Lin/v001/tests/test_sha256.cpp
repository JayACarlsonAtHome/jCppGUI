// File: tests/test_sha256.cpp
// Purpose: SHA-256 against known answers, in static_assert: the FIPS 180-4 examples, and lengths on each side of where padding needs a second block, with the answers made by another
//          implementation. Then the same text given in pieces split at every place, and the million-letter example at run time.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "check.hpp"
#include "jCppGUI/v001/sha256.hpp"

namespace {

using jgui::Sha256;
using jgui::Sha256Digest;

[[nodiscard]] constexpr std::string hex_of(const Sha256Digest& digest) {
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string text;
    for (const std::uint8_t byte : digest) {
        text.push_back(digits.at(byte >> 4));
        text.push_back(digits.at(byte & 0xf));
    }
    return text;
}

[[nodiscard]] constexpr std::string sum_of(std::string_view text) {
    Sha256 sum;
    sum.update(std::span<const char>{text});
    return hex_of(sum.digest());
}

[[nodiscard]] constexpr std::string letters(std::size_t count) { return std::string(count, 'a'); }

static_assert(sum_of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
static_assert(sum_of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
static_assert(sum_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
static_assert(sum_of("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") == "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");

static_assert(sum_of(letters(55)) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
static_assert(sum_of(letters(56)) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
static_assert(sum_of(letters(63)) == "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
static_assert(sum_of(letters(64)) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
static_assert(sum_of(letters(65)) == "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
static_assert(sum_of(letters(119)) == "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb");
static_assert(sum_of(letters(120)) == "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c");

// A byte's top bit must not spread when it widens, which a text of letters cannot show.
static_assert(sum_of("\xFF") == "a8100ae6aa1940d0b663bb31cd466142ebbdbd5187131b92d93818987832eb89");

// In two pieces, split at every place, it is the same sum.
[[nodiscard]] constexpr bool pieces_make_no_difference() {
    const std::string_view text{"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"};
    for (std::size_t split = 0; split <= text.size(); ++split) {
        Sha256 sum;
        sum.update(std::span<const char>{text.substr(0, split)});
        sum.update(std::span<const char>{text.substr(split)});
        if (hex_of(sum.digest()) != "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1") return false;
    }
    return true;
}
static_assert(pieces_make_no_difference());

// Asking for the sum does not end it.
[[nodiscard]] constexpr bool asking_changes_nothing() {
    Sha256 sum;
    sum.update(std::span<const char>{std::string_view{"a"}});
    static_cast<void>(sum.digest());
    sum.update(std::span<const char>{std::string_view{"bc"}});
    return hex_of(sum.digest()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
}
static_assert(asking_changes_nothing());

jgui_test::Checker check;

}  // namespace

int main() {
    // Given as a copy gives it, a chunk at a time.
    const std::string million = letters(1000000);
    Sha256 sum;
    const std::span<const char> all{million};
    for (std::size_t at = 0; at < all.size(); at += 4099) sum.update(all.subspan(at, std::min<std::size_t>(4099, all.size() - at)));
    check(hex_of(sum.digest()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "a million letters");
    return check.finish();
}
