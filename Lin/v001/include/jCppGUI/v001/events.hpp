// File: include/jCppGUI/v001/events.hpp
// Purpose: Every event as its own struct: those a backend delivers, and those the toolkit sends
//          through the widget tree.
#pragma once

#include <cstdint>

#include "geometry.hpp"

namespace jgui::inline v001 {

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
    std::uint32_t button{};  // the Linux input code, as in MouseButton
    ButtonState state{ButtonState::Released};
    std::uint32_t serial{};  // needed to start a compositor move or resize
};

struct PointerLeave {};

struct KeyEvent {
    std::uint32_t keysym{};
    std::uint32_t modifiers{};  // a mask of Modifier values
    bool pressed{};
};

namespace MouseButton {
inline constexpr std::uint32_t Left = 0x110;
}

namespace Modifier {
inline constexpr std::uint32_t Shift = 1U << 0U;
inline constexpr std::uint32_t Ctrl = 1U << 1U;
inline constexpr std::uint32_t Alt = 1U << 2U;
inline constexpr std::uint32_t Super = 1U << 3U;
}  // namespace Modifier

// The keysyms the toolkit itself understands, with the values xkbcommon defines.
namespace Keysym {
inline constexpr std::uint32_t Space = 0x0020;
inline constexpr std::uint32_t Return = 0xff0d;
}  // namespace Keysym

// --- Sent through the tree by the toolkit ---

struct PointerPress {
    Point position;
    std::uint32_t button{};
};

struct PointerRelease {
    Point position;
    std::uint32_t button{};
};

struct ClearFocus {};    // every widget gives up focus
struct HoldsCapture {};  // a widget answers true when it holds the pointer
struct QueryDamage {};   // a widget answers true when it must be painted again
struct ClearDamage {};   // a frame was painted
struct CollectDead {};   // the outermost dispatch ended: free what was marked for destruction

}  // namespace jgui::inline v001
