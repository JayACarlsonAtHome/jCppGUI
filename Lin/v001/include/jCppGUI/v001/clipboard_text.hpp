// File: include/jCppGUI/v001/clipboard_text.hpp
// Purpose: The sanitizer that stands between this program and the system's clipboard. Text is looked at, and whatever could do harm where it lands is named, so a person can be asked before anything happens. A clipboard takes
//          only SafeClipboardText, and the one way to make one is through here, so text that did not pass through does not compile. All of this is constexpr.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "cell_text.hpp"
#include "text.hpp"

namespace jgui::inline v001 {

inline constexpr std::size_t most_clipboard_bytes = 4096;

// How many of each thing that could do harm were found. Control, direction and invisible characters, and bytes that are not UTF-8, are replaced whatever anyone answers. Shell characters are left as they are, because
// they may be all that makes a name right, so a person is told of them. Text past the limit is cut.
struct ClipboardFindings {
    std::size_t not_text{};    // bytes that are not UTF-8
    std::size_t control{};     // what can press Enter in a shell or drive a terminal: newlines, tabs, escapes
    std::size_t direction{};   // what makes text show in another order than it is
    std::size_t invisible{};   // what is there and does not show
    std::size_t shell{};       // what a shell would act on, if this were pasted into one and run
    bool cut{};

    [[nodiscard]] constexpr bool none() const noexcept { return not_text == 0 && control == 0 && direction == 0 && invisible == 0 && shell == 0 && !cut; }
};

class SafeClipboardText;

struct ClipboardInspection;

[[nodiscard]] constexpr ClipboardInspection inspect_clipboard_text(std::string_view text);

// Text that has been through inspect_clipboard_text: valid UTF-8, within the limit, and with nothing in it that is a control, direction or invisible character.
class SafeClipboardText {
public:
    [[nodiscard]] constexpr std::string_view view() const noexcept { return text_; }

private:
    constexpr explicit SafeClipboardText(std::string text) : text_{std::move(text)} {}
    friend constexpr ClipboardInspection inspect_clipboard_text(std::string_view text);

    std::string text_;
};

struct ClipboardInspection {
    SafeClipboardText text;
    ClipboardFindings found{};
};

// Text the sanitizer found something in. Nothing has gone to the clipboard, and nothing will unless a person says so.
struct ClipboardQuestion {
    SafeClipboardText text;
    ClipboardFindings found{};
};

namespace detail {

// How long the UTF-8 character starting here is, or 0 when these bytes are not one.
[[nodiscard]] constexpr std::size_t utf8_character_length(std::string_view text, std::size_t at) noexcept {
    const auto lead = static_cast<unsigned char>(text[at]);
    std::size_t length = 0;
    if (lead < 0x80U) {
        length = 1;
    } else if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4;
    }
    if (length == 0 || at + length > text.size() || !valid_utf8(text.substr(at, length))) return 0;
    return length;
}

[[nodiscard]] constexpr std::uint32_t utf8_code_point(std::string_view character) noexcept {
    const auto byte = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(character[i])); };
    switch (character.size()) {
        case 1: return byte(0);
        case 2: return ((byte(0) & 0x1FU) << 6U) | (byte(1) & 0x3FU);
        case 3: return ((byte(0) & 0x0FU) << 12U) | ((byte(1) & 0x3FU) << 6U) | (byte(2) & 0x3FU);
        default: return ((byte(0) & 0x07U) << 18U) | ((byte(1) & 0x3FU) << 12U) | ((byte(2) & 0x3FU) << 6U) | (byte(3) & 0x3FU);
    }
}

// The joiners U+200C and U+200D stay out of these: some languages and emoji need them to be written at all.
[[nodiscard]] constexpr bool is_control_character(std::uint32_t c) noexcept { return c < 0x20U || (c >= 0x7FU && c <= 0x9FU) || c == 0x2028U || c == 0x2029U; }
[[nodiscard]] constexpr bool is_direction_character(std::uint32_t c) noexcept { return c == 0x061CU || c == 0x200EU || c == 0x200FU || (c >= 0x202AU && c <= 0x202EU) || (c >= 0x2066U && c <= 0x2069U); }
[[nodiscard]] constexpr bool is_invisible_character(std::uint32_t c) noexcept { return c == 0x00ADU || c == 0x200BU || (c >= 0x2060U && c <= 0x2064U) || c == 0xFEFFU || (c >= 0xE0000U && c <= 0xE007FU); }
[[nodiscard]] constexpr bool is_shell_character(std::uint32_t c) noexcept { return c == ';' || c == '|' || c == '&' || c == '$' || c == '`' || c == '<' || c == '>'; }

}  // namespace detail

[[nodiscard]] constexpr ClipboardInspection inspect_clipboard_text(std::string_view text) {
    constexpr std::string_view replacement = "\xEF\xBF\xBD";
    ClipboardFindings found;
    std::string out;
    out.reserve(std::min(text.size(), most_clipboard_bytes));
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t length = detail::utf8_character_length(text, at);
        std::string_view keep = replacement;
        if (length == 0) {
            ++found.not_text;
            at += 1;
        } else {
            const std::string_view character = text.substr(at, length);
            const std::uint32_t c = detail::utf8_code_point(character);
            at += length;
            if (detail::is_control_character(c)) {
                ++found.control;
            } else if (detail::is_direction_character(c)) {
                ++found.direction;
            } else if (detail::is_invisible_character(c)) {
                ++found.invisible;
            } else {
                if (detail::is_shell_character(c)) ++found.shell;
                keep = character;
            }
        }
        if (out.size() + keep.size() > most_clipboard_bytes) {
            found.cut = true;
            break;
        }
        out.append(keep);
    }
    return ClipboardInspection{SafeClipboardText{std::move(out)}, found};
}

// What was found, in words, for a person deciding.
[[nodiscard]] constexpr std::string clipboard_findings_text(const ClipboardFindings& found) {
    std::string out;
    const auto add = [&](std::size_t count, std::string_view what) {
        if (count == 0) return;
        if (!out.empty()) out.append("; ");
        out.append(what).append(": ").append(decimal_text(count, false));
    };
    add(found.shell, "Characters a shell would act on, such as ; | & $");
    add(found.control, "Control characters, such as a newline or a terminal escape");
    add(found.direction, "Characters that change the order text shows in");
    add(found.invisible, "Characters that do not show");
    add(found.not_text, "Bytes that are not text");
    if (found.cut) {
        if (!out.empty()) out.append("; ");
        out.append("Too long, so it is cut to 4096 bytes");
    }
    return out;
}

}  // namespace jgui::inline v001
