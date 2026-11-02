// File: include/jCppGUI/v001/events.hpp
// Purpose: Every event as its own struct: those a backend delivers, and those the toolkit sends
//          through the widget tree.
#pragma once

#include <cstdint>

#include "geometry.hpp"

namespace jgui::inline v001 {

namespace detail {
// Reads the number inside a wire value. It is defined only in src/wayland/, so nothing above the backend
// line can turn one of these types back into a number.
struct WireAccess;

struct SerialTag;
struct PointerButtonTag;
struct KeysymTag;
}  // namespace detail

// A number that belongs to the display protocol. Each meaning is its own type, with no implicit conversion
// in or out, so a serial cannot be passed as a button. The toolkit can only compare them.
template <class Tag>
class WireValue {
public:
    constexpr WireValue() noexcept = default;
    constexpr explicit WireValue(std::uint32_t value) noexcept : value_{value} {}

    friend constexpr bool operator==(const WireValue&, const WireValue&) = default;

private:
    friend struct detail::WireAccess;
    std::uint32_t value_{};
};

using Serial = WireValue<detail::SerialTag>;             // needed to start a compositor move or resize
using PointerButtonCode = WireValue<detail::PointerButtonTag>;  // the Linux input code, as in MouseButton
using Keysym = WireValue<detail::KeysymTag>;

// Which modifier keys are held, as a set: see Modifier.
class ModifierMask {
public:
    constexpr ModifierMask() noexcept = default;
    constexpr explicit ModifierMask(std::uint32_t bits) noexcept : bits_{bits} {}

    friend constexpr bool operator==(const ModifierMask&, const ModifierMask&) = default;
    [[nodiscard]] friend constexpr ModifierMask operator|(ModifierMask a, ModifierMask b) noexcept {
        return ModifierMask{a.bits_ | b.bits_};
    }
    [[nodiscard]] constexpr bool holds(ModifierMask other) const noexcept { return (bits_ & other.bits_) == other.bits_; }

private:
    std::uint32_t bits_{};
};

// The edge or corner a compositor resize starts from.
enum class ResizeEdge : std::uint8_t { TopSide, BottomSide, LeftSide, RightSide, TopLeft, TopRight, BottomLeft, BottomRight };

// --- Delivered by a backend ---

struct CloseEvent {};

struct ResizeEvent {
    Size size;
};

struct PointerMove {
    Point position;
    bool captured{};  // set by the toolkit: another widget, or this one, holds the pointer
};

enum class ButtonState : std::uint8_t { Pressed, Released };

struct PointerButton {
    Point position;
    PointerButtonCode button{};
    ButtonState state{ButtonState::Released};
    Serial serial{};
};

struct PointerLeave {};

struct KeyEvent {
    Keysym keysym{};
    ModifierMask modifiers{};
    bool pressed{};
};

namespace MouseButton {
inline constexpr PointerButtonCode Left{0x110};
}

namespace Modifier {
inline constexpr ModifierMask Shift{1U << 0U};
inline constexpr ModifierMask Ctrl{1U << 1U};
inline constexpr ModifierMask Alt{1U << 2U};
inline constexpr ModifierMask Super{1U << 3U};
}  // namespace Modifier

// The keysyms the toolkit itself understands, with the values xkbcommon defines.
namespace Key {
inline constexpr Keysym Space{0x0020};
inline constexpr Keysym Return{0xff0d};
}  // namespace Keysym

// --- Sent through the tree by the toolkit ---

struct PointerPress {
    Point position;
    PointerButtonCode button{};
    Serial serial{};  // the backend's serial for this press, for a compositor move or resize
};

struct PointerRelease {
    Point position;
    PointerButtonCode button{};
};

// Questions, with an answer: see the Widget concept.
struct HoldsCapture {};  // a widget answers true when it holds the pointer
struct QueryDamage {};   // a widget answers true when it must be painted again

// Notices, with no answer: see the Widget concept. PointerLeave, above, is one too.
struct ClearFocus {};   // every widget gives up focus
struct ClearDamage {};  // a frame was painted
struct CollectDead {};  // the outermost dispatch ended: free what was marked for destruction

}  // namespace jgui::inline v001
