// File: src/c_resource.hpp
// Purpose: RAII for C library objects: a unique_ptr with a stateless deleter that calls the library's
//          own release function, so nothing is erased and nothing has to be freed by hand. Also the
//          one way a C library's pointer and count become a span.
#pragma once

#include <concepts>
#include <cstddef>
#include <memory>
#include <ranges>
#include <span>

#include "jCppGUI/v001/geometry.hpp"

namespace jgui::inline v001::detail {

template <auto Release>
struct CallRelease {
    template <class T>
    void operator()(T* object) const noexcept {
        static_cast<void>(Release(object));
    }
};

template <class T, auto Release>
using CResource = std::unique_ptr<T, CallRelease<Release>>;

// Where a C library's pointer and count become a span. The count is only the library's word for it, so
// no check here can prove it true. Every such boundary uses this, so they can all be found.
template <class T, std::integral Count>
[[nodiscard]] std::span<T> c_array(T* data, Count count) noexcept {
    return std::views::counted(data, saturate_to<std::ptrdiff_t>(count));
}

}  // namespace jgui::inline v001::detail
