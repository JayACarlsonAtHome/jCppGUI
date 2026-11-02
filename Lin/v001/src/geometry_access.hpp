// File: src/geometry_access.hpp
// Purpose: The one way for the code in src/ to read the number inside a geometry value, for the calls into
//          libwayland and the font libraries. It is defined only here, so nothing above the backend line can use it.
#pragma once

#include <cstdint>

#include "jCppGUI/v001/geometry.hpp"

namespace jgui::inline v001::detail {

struct GeometryAccess {
    template <class Tag>
    [[nodiscard]] static constexpr std::int32_t number(Edge<Tag> value) noexcept {
        return value.value_;
    }
    template <class Tag>
    [[nodiscard]] static constexpr std::int32_t number(Extent<Tag> value) noexcept {
        return value.value_;
    }
};

}  // namespace jgui::inline v001::detail
