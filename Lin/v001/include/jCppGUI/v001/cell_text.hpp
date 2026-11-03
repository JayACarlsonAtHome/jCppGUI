// File: include/jCppGUI/v001/cell_text.hpp
// Purpose: A value as the text of a cell, with the way of formatting it chosen at compile time by its type. A type with
//          no CellFormat has no text, and using it as one does not compile. A program can add a CellFormat for its own type.
#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "geometry.hpp"

namespace jgui::inline v001 {

// Each kind of value this knows has a static text(const T&), which gives a std::string or a std::string_view.
template <class T>
struct CellFormat;

template <class T>
concept HasCellFormat = requires(const T& value) {
    { CellFormat<T>::text(value) } -> std::convertible_to<std::string_view>;
};

// A whole number in decimal, for the widest whole number there is.
[[nodiscard]] constexpr std::string decimal_text(std::uint64_t magnitude, bool negative) {
    std::array<char, 21> reversed{};
    std::size_t length = 0;
    do {
        reversed[length++] = static_cast<char>('0' + static_cast<int>(magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0);
    if (negative) reversed[length++] = '-';
    std::string text(length, '\0');
    for (std::size_t i = 0; i < length; ++i) text[i] = reversed[length - 1 - i];
    return text;
}

namespace detail {

// A character type is text and not a number, and a bool has a format of its own, so neither counts as a number here.
template <class T>
concept CellNumber = std::integral<T> && !std::same_as<T, bool> && !std::same_as<T, char> && !std::same_as<T, wchar_t> &&
                     !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t>;

// Reads a geometry value's number, only to show it as text.
struct CellNumbers {
    template <class Tag>
    [[nodiscard]] static constexpr std::string text(Edge<Tag> value) {
        return number_text(value.value_);
    }
    template <class Tag>
    [[nodiscard]] static constexpr std::string text(Extent<Tag> value) {
        return number_text(value.value_);
    }
    template <CellNumber T>
    [[nodiscard]] static constexpr std::string number_text(T value) {
        if constexpr (std::signed_integral<T>) {
            const auto wide = static_cast<std::int64_t>(value);
            if (wide < 0) return decimal_text(std::uint64_t{0} - static_cast<std::uint64_t>(wide), true);
        }
        return decimal_text(static_cast<std::uint64_t>(value), false);
    }
};

}  // namespace detail

template <detail::CellNumber T>
struct CellFormat<T> {
    [[nodiscard]] static constexpr std::string text(T value) { return detail::CellNumbers::number_text(value); }
};

template <>
struct CellFormat<bool> {
    [[nodiscard]] static constexpr std::string_view text(bool value) noexcept { return value ? "true" : "false"; }
};

template <>
struct CellFormat<std::string> {
    [[nodiscard]] static constexpr std::string_view text(const std::string& value) noexcept { return value; }
};

template <class Tag>
struct CellFormat<Edge<Tag>> {
    [[nodiscard]] static constexpr std::string text(Edge<Tag> value) { return detail::CellNumbers::text(value); }
};

template <class Tag>
struct CellFormat<Extent<Tag>> {
    [[nodiscard]] static constexpr std::string text(Extent<Tag> value) { return detail::CellNumbers::text(value); }
};

}  // namespace jgui::inline v001
