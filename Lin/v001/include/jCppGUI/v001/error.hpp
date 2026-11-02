// File: include/jCppGUI/v001/error.hpp
// Purpose: The error type returned through std::expected, and the always-on check that aborts.
#pragma once

#include <cstdlib>
#include <expected>
#include <iostream>
#include <string>
#include <utility>

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
    WouldBlock,     // a headless wait with nothing queued, which would never wake
    StaleFrame,     // a frame that a newer acquire or a present has ended
};

struct Error {
    ErrorCode code;
    std::string message;
};

[[nodiscard]] constexpr std::unexpected<Error> make_error(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
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
