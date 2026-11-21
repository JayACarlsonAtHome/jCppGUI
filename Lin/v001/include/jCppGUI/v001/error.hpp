// File: include/jCppGUI/v001/error.hpp
// Purpose: The error type returned through std::expected, and the always-on check that aborts.
#pragma once

#include <cstdlib>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "jCppGUI/jString/v001/jstring.hpp"

namespace jgui::inline v001 {

enum class ErrorCode {
    Connect,        // no connection to the display server
    MissingGlobal,  // the compositor lacks a required global or version
    Protocol,       // the display connection failed or broke the protocol
    System,         // an operating system call failed
    SizeLimit,      // a size is over a documented limit
    InvalidUtf8,    // text is not valid UTF-8
    TextTooLong,    // a caption is over the length limit
    GlyphTooLarge,  // a glyph bitmap is over the size limit
    Font,           // no usable font, or the font library failed
    Keymap,         // a keymap was refused
    PoolExhausted,  // a pool has no slot left to give
    WouldBlock,     // a headless wait with nothing queued, or a frame the display holds too much to allow
    StaleFrame,     // a frame that a newer acquire or a present has ended
    FileFormat,     // a file's text breaks its format, or asks for what the reader does not do
};

// What went wrong, and its text. The text is a jString: always valid UTF-8, so it can be shown whatever bytes went
// into it, and it never points into itself (g++ 15's -Wdangling-pointer misjudged std::string's small buffer, moved
// out of an Error inside a std::expected).
struct Error {
    // Text from anywhere, such as a file name: each byte sequence that is not UTF-8 is shown as U+FFFD.
    constexpr Error(ErrorCode what, std::string_view text) : code{what}, message{jstr::String<>::from_lossy(text)} {}
    // Text that is already valid.
    constexpr Error(ErrorCode what, jstr::String<> text) noexcept : code{what}, message{std::move(text)} {}

    // A std::string copy of the text, for code that still builds its own messages as std::string: C++23 has no
    // std::string + std::string_view. Showing the text needs no copy: message.units().
    [[nodiscard]] constexpr std::string text() const { return std::string{message.units()}; }

    ErrorCode code;
    jstr::String<> message;
};

[[nodiscard]] constexpr std::unexpected<Error> make_error(ErrorCode code, std::string_view message) {
    return std::unexpected(Error{code, message});
}

// A contract-style check. It stays on in release builds and aborts on violation. During constant
// evaluation a violation reaches the non-constexpr calls below, which is a compile error.
constexpr void require(bool condition, const char* what) noexcept {
    if (!condition) {
        std::cerr << "jCppGUI contract violation: " << what << '\n';
        std::abort();
    }
}

}  // namespace jgui::inline v001
