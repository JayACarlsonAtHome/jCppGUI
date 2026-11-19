// File: include/jCppGUI/v001/wayland_backend.hpp
// Purpose: The Wayland backend's public face. It names no Wayland type: everything that touches
//          libwayland or xkbcommon is in src/wayland/.
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <optional>
#include <string>

#include "backend.hpp"

namespace jgui::inline v001 {

namespace detail {
struct WaylandImpl;
struct WaylandImplDeleter {
    void operator()(WaylandImpl* impl) const noexcept;
};
}  // namespace detail

struct WaylandOptions {
    std::string title;
    std::string app_id;
    Size size;  // used when the compositor leaves the choice to the client
};

class WaylandBackend {
public:
    [[nodiscard]] static std::expected<WaylandBackend, Error> create(const WaylandOptions& options);

    template <class Sink>
    std::expected<void, Error> process_pending(Sink& sink) {
        auto result = pump_at_once();
        events_->drain(sink);
        return result;
    }

    template <class Sink>
    std::expected<void, Error> wait(Sink& sink, std::optional<MonotonicTime> deadline) {
        auto result = pump_until(deadline);
        events_->drain(sink);
        return result;
    }

    [[nodiscard]] MonotonicTime now() const noexcept { return monotonic_now(); }
    [[nodiscard]] bool can_present(Size size) const;
    std::expected<Frame<WaylandBackend>, Error> acquire_frame(Size size);
    std::expected<PixelBuffer, Error> frame_pixels(const Frame<WaylandBackend>& frame);
    std::expected<void, Error> present(Frame<WaylandBackend> frame, Rect damage);
    std::expected<void, Error> sync();
    void begin_move(Serial serial) noexcept;
    void begin_resize(Serial serial, ResizeEdge edge) noexcept;
    void set_cursor(CursorShape shape) noexcept;
    // Makes the window this size, to fit what it holds, when the compositor has said there is room for it and the
    // window is not maximized, fullscreen or tiled. False, and nothing changes, otherwise.
    [[nodiscard]] bool grow_to(Size wanted) noexcept;
    // Offers this text as the clipboard, until something else takes the clipboard. With a compositor that has no clipboard it does nothing.
    void copy_text(Serial serial, const SafeClipboardText& text);

private:
    WaylandBackend(std::unique_ptr<BackendEventQueue> events,
                   std::unique_ptr<detail::WaylandImpl, detail::WaylandImplDeleter> impl) noexcept;

    std::expected<void, Error> pump_at_once();
    std::expected<void, Error> pump_until(std::optional<MonotonicTime> deadline);

    // On the heap so the C callbacks can hold their address while this object moves. The implementation
    // refers to the queue, so it is declared second and destroyed first.
    std::unique_ptr<BackendEventQueue> events_;
    std::unique_ptr<detail::WaylandImpl, detail::WaylandImplDeleter> impl_;
};

static_assert(Backend<WaylandBackend>);

}  // namespace jgui::inline v001
