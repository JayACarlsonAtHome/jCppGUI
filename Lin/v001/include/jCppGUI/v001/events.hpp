// File: include/jCppGUI/v001/events.hpp
// Purpose: Every event as its own struct: those a backend delivers, and those the toolkit sends
//          through the widget tree.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>

#include "geometry.hpp"

namespace jgui::inline v001 {

namespace detail {
// Makes a wire value from a number and reads the number back. It is defined only in src/wayland/, so nothing above
// the backend line can do either at run time.
struct WireAccess;

struct SerialTag;
struct PointerButtonTag;
struct KeysymTag;
struct ScrollDownTag;
struct ScrollAcrossTag;
}  // namespace detail

// A number that belongs to the display protocol. Each meaning is its own type, with no implicit conversion
// in or out, so a serial cannot be passed as a button. Above the backend line these can be compared and, for
// the kinds that carry no authority, named at compile time: a button or a key has a fixed meaning. Only a backend
// or a test can make one from a number at run time, and only a backend can read one.
template <class Tag>
class WireValue {
public:
    // A serial is the proof that a press really happened, so it can never be named: it only comes from a backend.
    consteval explicit WireValue(std::uint32_t value) noexcept
        requires(!std::same_as<Tag, detail::SerialTag>)
        : value_{value} {}

    friend constexpr bool operator==(const WireValue&, const WireValue&) = default;

private:
    friend struct detail::WireAccess;
    friend struct detail::TestAccess;

    struct FromWire {};
    constexpr WireValue(FromWire, std::uint32_t value) noexcept : value_{value} {}

    std::uint32_t value_;
};

using Serial = WireValue<detail::SerialTag>;             // needed to start a compositor move or resize
using PointerButtonCode = WireValue<detail::PointerButtonTag>;  // the Linux input code, as in MouseButton
using Keysym = WireValue<detail::KeysymTag>;

// Which modifier keys are held, as a set: see Modifier.
class ModifierMask {
public:
    constexpr ModifierMask() noexcept = default;
    consteval explicit ModifierMask(std::uint32_t bits) noexcept : bits_{bits} {}

    friend constexpr bool operator==(const ModifierMask&, const ModifierMask&) = default;
    [[nodiscard]] friend constexpr ModifierMask operator|(ModifierMask a, ModifierMask b) noexcept {
        ModifierMask both;
        both.bits_ = a.bits_ | b.bits_;
        return both;
    }
    [[nodiscard]] constexpr bool holds(ModifierMask other) const noexcept { return (bits_ & other.bits_) == other.bits_; }

private:
    std::uint32_t bits_{};
};

// The edge or corner a compositor resize starts from.
enum class ResizeEdge : std::uint8_t { TopSide, BottomSide, LeftSide, RightSide, TopLeft, TopRight, BottomLeft, BottomRight };

// What the pointer looks like over the window: the plain arrow, or the arrow for resizing from one edge or corner.
enum class CursorShape : std::uint8_t { Default, ResizeTop, ResizeBottom, ResizeLeft, ResizeRight, ResizeTopLeft, ResizeTopRight, ResizeBottomLeft, ResizeBottomRight, ResizeColumn };

// --- Delivered by a backend ---

struct CloseEvent {};

struct ResizeEvent {
    Size size;
};

struct PointerMove {
    Point position;
    bool captured{};  // set by the toolkit: another widget, or this one, holds the pointer
    bool entered{};   // set by a backend: the pointer just came into the window, so what it looks like is not yet known

    // Only the newest position of a run of motion matters, as long as what else each one says is the same.
    [[nodiscard]] constexpr bool absorb(const PointerMove& newer) noexcept {
        if (captured != newer.captured || entered != newer.entered) return false;
        position = newer.position;
        return true;
    }
};

enum class ButtonState : std::uint8_t { Pressed, Released };

struct PointerButton {
    Point position;
    PointerButtonCode button;
    ButtonState state;
    Serial serial;
    ModifierMask modifiers{};  // what was held when it happened
};

struct PointerLeave {};

// The most one event can ask for, whatever a device or a compositor says.
inline constexpr std::int32_t max_scroll_steps = 16;
inline constexpr std::int32_t max_scroll_pixels = 1024;

// How far a wheel or a finger moved along one axis: in steps where the device has them, otherwise in pixels.
// Positive is down or to the right. It holds at most the limits above, so anything hostile stops at them here.
template <class Tag>
class ScrollAmount {
public:
    constexpr ScrollAmount() noexcept = default;

    [[nodiscard]] static constexpr ScrollAmount of_steps(std::int32_t steps) noexcept {
        return ScrollAmount{std::clamp(steps, -max_scroll_steps, max_scroll_steps), 0};
    }
    [[nodiscard]] static constexpr ScrollAmount of_pixels(std::int32_t pixels) noexcept {
        return ScrollAmount{0, std::clamp(pixels, -max_scroll_pixels, max_scroll_pixels)};
    }

    [[nodiscard]] constexpr std::int32_t steps() const noexcept { return steps_; }
    [[nodiscard]] constexpr std::int32_t pixels() const noexcept { return pixels_; }
    [[nodiscard]] constexpr bool none() const noexcept { return steps_ == 0 && pixels_ == 0; }

    friend constexpr bool operator==(const ScrollAmount&, const ScrollAmount&) = default;

private:
    constexpr ScrollAmount(std::int32_t steps, std::int32_t pixels) noexcept : steps_{steps}, pixels_{pixels} {}

    std::int32_t steps_{};
    std::int32_t pixels_{};
};

using ScrollDown = ScrollAmount<detail::ScrollDownTag>;
using ScrollAcross = ScrollAmount<detail::ScrollAcrossTag>;

// One pointer frame's scrolling, with where the pointer was.
struct PointerScroll {
    Point position;
    ScrollDown down{};
    ScrollAcross across{};
};

struct KeyEvent {
    Keysym keysym;
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
inline constexpr Keysym Plus{0x002b};
inline constexpr Keysym Minus{0x002d};
inline constexpr Keysym UpperA{0x0041};
inline constexpr Keysym UpperC{0x0043};
inline constexpr Keysym UpperV{0x0056};
inline constexpr Keysym LowerA{0x0061};
inline constexpr Keysym LowerC{0x0063};
inline constexpr Keysym LowerV{0x0076};
inline constexpr Keysym BackSpace{0xff08};
inline constexpr Keysym Escape{0xff1b};
inline constexpr Keysym KeypadPlus{0xffab};
inline constexpr Keysym KeypadMinus{0xffad};
inline constexpr Keysym Return{0xff0d};
inline constexpr Keysym Home{0xff50};
inline constexpr Keysym Left{0xff51};
inline constexpr Keysym Up{0xff52};
inline constexpr Keysym Right{0xff53};
inline constexpr Keysym Down{0xff54};
inline constexpr Keysym PageUp{0xff55};
inline constexpr Keysym PageDown{0xff56};
inline constexpr Keysym End{0xff57};
}  // namespace Key

// --- Sent through the tree by the toolkit ---

struct PointerPress {
    Point position;
    PointerButtonCode button;
    Serial serial;  // the backend's serial for this press, for a compositor move or resize
    ModifierMask modifiers{};
};

struct PointerRelease {
    Point position;
    PointerButtonCode button;
};

// Questions, with an answer: see the Widget concept.
struct HoldsCapture {};  // a widget answers true when it holds the pointer
struct QueryDamage {};   // a widget answers true when it must be painted again

// Notices, with no answer: see the Widget concept. PointerLeave, above, is one too.
struct ClearFocus {};   // every widget gives up focus
struct ClearDamage {};  // a frame was painted
struct CollectDead {};  // the outermost dispatch ended: free what was marked for destruction

}  // namespace jgui::inline v001
