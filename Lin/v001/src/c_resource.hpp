// File: src/c_resource.hpp
// Purpose: RAII for C library objects: a unique_ptr with a stateless deleter that calls the library's
//          own release function, so nothing is erased and nothing has to be freed by hand.
#pragma once

#include <memory>

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

}  // namespace jgui::inline v001::detail
