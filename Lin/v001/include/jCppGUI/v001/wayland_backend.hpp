// File: include/jCppGUI/v001/wayland_backend.hpp
// Purpose: The Wayland backend's public face. It names no Wayland type: everything that touches
//          libwayland or xkbcommon is in src/wayland/.
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
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
        auto result = pump(false);
        events_->drain(sink);
        return result;
    }

    template <class Sink>
    std::expected<void, Error> wait(Sink& sink) {
        auto result = pump(true);
        events_->drain(sink);
        return result;
    }

    [[nodiscard]] bool can_present() const noexcept;
    std::expected<PixelBuffer, Error> acquire_frame(Size size);
    std::expected<void, Error> present(Rect damage);
    std::expected<void, Error> sync();
    void begin_move(Serial serial) noexcept;
    void begin_resize(Serial serial, ResizeEdge edge) noexcept;

private:
    WaylandBackend(std::unique_ptr<BackendEventQueue> events,
                   std::unique_ptr<detail::WaylandImpl, detail::WaylandImplDeleter> impl) noexcept;

    std::expected<void, Error> pump(bool may_block);

    // On the heap so the C callbacks can hold their address while this object moves. The implementation
    // refers to the queue, so it is declared second and destroyed first.
    std::unique_ptr<BackendEventQueue> events_;
    std::unique_ptr<detail::WaylandImpl, detail::WaylandImplDeleter> impl_;
};

static_assert(Backend<WaylandBackend>);

}  // namespace jgui::inline v001
