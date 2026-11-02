// File: src/wayland/wire_access.hpp
// Purpose: The one way to make a wire value from a number and to read the number back, for the calls into
//          libwayland and xkbcommon. It is defined only here, so nothing above the backend line can use it.
#pragma once

#include <cstdint>

#include "jCppGUI/v001/events.hpp"

namespace jgui::inline v001::detail {

struct WireAccess {
    template <class Value>
    [[nodiscard]] static constexpr Value make(std::uint32_t value) noexcept {
        return Value{typename Value::FromWire{}, value};
    }

    template <class Tag>
    [[nodiscard]] static constexpr std::uint32_t number(WireValue<Tag> value) noexcept {
        return value.value_;
    }
};

}  // namespace jgui::inline v001::detail
