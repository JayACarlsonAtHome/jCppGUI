// File: src/wayland/wire_access.hpp
// Purpose: The one way to read the number inside a wire value, for the calls into libwayland and
//          xkbcommon. It is defined only here, so nothing above the backend line can use it.
#pragma once

#include <cstdint>

#include "jCppGUI/v001/events.hpp"

namespace jgui::inline v001::detail {

struct WireAccess {
    template <class Tag>
    [[nodiscard]] static constexpr std::uint32_t number(WireValue<Tag> value) noexcept {
        return value.value_;
    }
};

}  // namespace jgui::inline v001::detail
