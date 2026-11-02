// File: src/c_resource.hpp
// Purpose: RAII for C library objects: a unique_ptr with a stateless deleter that calls the library's
//          own release function, so nothing is erased and nothing has to be freed by hand. Also the
//          one helper that reads a C pointer and count, into memory we own.
#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <ranges>
#include <span>
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

}  // namespace jgui::inline v001::detail
