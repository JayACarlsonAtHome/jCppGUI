// File: include/jCppGUI/v001/redraw_points.hpp
// Purpose: What ApplicationCore tells a monitor, and when: the RedrawMonitor concept and NoMonitor, which is empty and
//          does nothing, for the usual case. The monitor that records is in redraw_monitor.hpp, which only a program
//          that asks for it includes. Also the compile-time names of types, with no run-time type information.
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <string_view>

#include "canvas.hpp"
#include "error.hpp"
#include "geometry.hpp"

namespace jgui::inline v001 {

// Why a frame is drawn, the first of these that holds.
enum class RedrawReason : std::uint8_t { FirstFrame, Resize, WidgetDamage };

// One widget that needed a redraw. The names are in static storage, and the position counts depth first from 1.
struct DamagedWidget {
    std::string_view class_name;
    std::string_view tag_name;
    std::size_t position{};
};

namespace detail {

// The compiler's own text for this function, which names T: "... [with T = name]" on g++, "... [T = name]" on clang++.
// It returns a plain pointer because g++ would also list any alias in its return type after the name.
template <class T>
[[nodiscard]] consteval const char* signature_naming() {
    return std::source_location::current().function_name();
}

template <class T>
[[nodiscard]] consteval std::string_view type_name_of() {
    constexpr std::string_view marker = "T = ";
    const std::string_view signature{signature_naming<T>()};
    const std::size_t start = signature.find(marker) + marker.size();
    return signature.substr(start, signature.rfind(']') - start);
}

// A class without its template arguments, which can be very long, and on clang++ can hold a source file's whole path.
template <class T>
[[nodiscard]] consteval std::string_view class_name_of() {
    const std::string_view name = type_name_of<T>();
    return name.substr(0, name.find('<'));
}

}  // namespace detail

// The points of a frame in the order they come: frame_begins; widget_damaged for each damaged widget, when the monitor
// watches; phases_begin; laid_out; acquired; painted; presented. A frame that fails stops where it failed. deferred
// is told instead when there is damage and the display cannot take a frame. between_frames comes after each settle,
// measured or not, and finish when a run ends. painted is given the pixels for the length of that call only.
template <class M>
concept RedrawMonitor = requires(M& monitor, const M& constant, RedrawReason reason, const Rect& area, Size size, const DamagedWidget& widget, const PixelBuffer& pixels) {
    { M::watches } -> std::convertible_to<bool>;
    { monitor.frame_begins(reason, area) } -> std::same_as<void>;
    { monitor.widget_damaged(widget) } -> std::same_as<void>;
    { monitor.phases_begin() } -> std::same_as<void>;
    { monitor.laid_out() } -> std::same_as<void>;
    { monitor.acquired() } -> std::same_as<void>;
    { monitor.painted(pixels) } -> std::same_as<void>;
    { monitor.presented() } -> std::same_as<void>;
    { monitor.deferred(reason, size) } -> std::same_as<void>;
    { monitor.between_frames() } -> std::same_as<void>;
    { monitor.finish() } -> std::same_as<void>;
    { constant.error() } -> std::same_as<std::optional<Error>>;
};

// The usual case: no monitor. Empty, and every point does nothing.
struct NoMonitor {
    static constexpr bool watches = false;
    constexpr void frame_begins(RedrawReason, const Rect&) const noexcept {}
    constexpr void widget_damaged(const DamagedWidget&) const noexcept {}
    constexpr void phases_begin() const noexcept {}
    constexpr void laid_out() const noexcept {}
    constexpr void acquired() const noexcept {}
    constexpr void painted(const PixelBuffer&) const noexcept {}
    constexpr void presented() const noexcept {}
    constexpr void deferred(RedrawReason, Size) const noexcept {}
    constexpr void between_frames() const noexcept {}
    constexpr void finish() const noexcept {}
    [[nodiscard]] constexpr std::optional<Error> error() const { return std::nullopt; }
};

static_assert(RedrawMonitor<NoMonitor>);

}  // namespace jgui::inline v001
