// File: src/c_resource.hpp
// Purpose: RAII for C library objects: a unique_ptr with a stateless deleter that calls the library's
//          own release function, so nothing is erased and nothing has to be freed by hand. Also the
//          helpers that read a C pointer and count, and a C string, into memory we own.
#pragma once

#include <concepts>
#include <cstddef>
#include <expected>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "jCppGUI/v001/error.hpp"
#include "jCppGUI/v001/geometry.hpp"

namespace jgui::inline v001::detail {

template <auto Release>
struct CallRelease {
    template <class T>
    void operator()(T* object) const noexcept {
        // Some C release functions return a status. A destructor has nowhere to report it, and the object is gone.
        static_cast<void>(Release(object));
    }
};

template <class T, auto Release>
using CResource = std::unique_ptr<T, CallRelease<Release>>;

// The only place a C pointer and count are read. C++ types take over here: the elements are copied into
// a vector we own, and nothing after this works through a view of C-owned memory. The count is still only
// the C side's word for it, and no check can prove it true, so the caller's limit bounds the damage.
// std::views::counted is here only because that conversion must happen somewhere.
template <class T>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] constexpr std::expected<std::vector<T>, Error> copy_from_c(const T* data, std::size_t count, std::size_t limit) {
    if (count == 0) return std::vector<T>{};
    if (data == nullptr) return make_error(ErrorCode::System, "a C library gave a null pointer with a non-zero count");
    if (count > limit) return make_error(ErrorCode::SizeLimit, "a C library gave a count over the limit");
    const std::span<const T> source = std::views::counted(data, saturate_to<std::ptrdiff_t>(count));
    return std::vector<T>(source.begin(), source.end());
}

// The only place a C string is walked. It stops at the terminator or at the limit, whichever comes first, so
// a string with no terminator cannot take the walk further than the limit. A string over the limit is refused.
template <class Char>
    requires(std::same_as<Char, char> || std::same_as<Char, unsigned char>)
[[nodiscard]] constexpr std::expected<std::string, Error> string_from_c(const Char* text, std::size_t limit) {
    if (text == nullptr) return make_error(ErrorCode::System, "a C library gave a null string");
    std::string out;
    for (const Char* at = text; *at != Char{0}; at = std::next(at)) {
        if (out.size() == limit) return make_error(ErrorCode::SizeLimit, "a C library gave a string over the limit");
        if constexpr (std::same_as<Char, char>) {
            out.push_back(*at);
        } else {
            out.push_back(static_cast<char>(*at));
        }
    }
    return out;
}

}  // namespace jgui::inline v001::detail
