// File: src/wayland/wayland_backend.cpp
// Purpose: The Wayland backend's public face, over the real C libraries. See wayland_impl.hpp for what it does.
#include "jCppGUI/v001/wayland_backend.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "../geometry_access.hpp"
#include "wayland_impl.hpp"

namespace jgui::inline v001 {

namespace detail {

struct WaylandImpl {
    explicit WaylandImpl(BackendEventQueue& queue, Size fallback) noexcept : core{queue, fallback} {}

    BasicWaylandImpl<LibWayland> core;
};

void WaylandImplDeleter::operator()(WaylandImpl* impl) const noexcept {
    std::default_delete<WaylandImpl>{}(impl);
}

}  // namespace detail

WaylandBackend::WaylandBackend(std::unique_ptr<BackendEventQueue> events,
                               std::unique_ptr<detail::WaylandImpl, detail::WaylandImplDeleter> impl) noexcept
    : events_{std::move(events)}, impl_{std::move(impl)} {}

std::expected<WaylandBackend, Error> WaylandBackend::create(const WaylandOptions& options) {
    if (auto title = detail::check_window_string(options.title); !title) return std::unexpected(std::move(title.error()));
    if (auto app_id = detail::check_window_string(options.app_id); !app_id) return std::unexpected(std::move(app_id.error()));
    if (auto bytes = PixelBuffer::byte_size(options.size); !bytes || *bytes == 0)
        return make_error(ErrorCode::SizeLimit, "the requested window size is out of range");
    auto events = std::make_unique<BackendEventQueue>();
    std::unique_ptr<detail::WaylandImpl, detail::WaylandImplDeleter> impl{
        std::make_unique<detail::WaylandImpl>(*events, options.size).release()};
    if (auto connected = impl->core.connect(options); !connected) return std::unexpected(std::move(connected.error()));
    return WaylandBackend{std::move(events), std::move(impl)};
}

std::expected<void, Error> WaylandBackend::pump_at_once() { return impl_->core.pump(detail::PumpWait::Never); }

std::expected<void, Error> WaylandBackend::pump_until(std::optional<MonotonicTime> deadline) { return impl_->core.pump(detail::PumpWait::WhenIdle, deadline); }

bool WaylandBackend::can_present(Size size) const {
    // A size that is refused goes on to acquire_frame, which reports it.
    const auto bytes = PixelBuffer::byte_size(size);
    return !bytes || impl_->core.room_for(*bytes, size) != detail::FrameRoom::Wait;
}

std::expected<Frame<WaylandBackend>, Error> WaylandBackend::acquire_frame(Size size) {
    // First: what follows may free the buffer an earlier frame chose, and may then fail.
    const std::uint64_t generation = impl_->core.ledger.begin();
    auto bytes = PixelBuffer::byte_size(size);
    if (!bytes) return std::unexpected(std::move(bytes.error()));
    const detail::FrameRoom room = impl_->core.room_for(*bytes, size);
    if (room == detail::FrameRoom::Never) return make_error(ErrorCode::SizeLimit, "one frame of this size is over the memory budget");
    if (room == detail::FrameRoom::Wait) return make_error(ErrorCode::WouldBlock, "the compositor holds too much for this frame yet");

    // Everything the compositor does not hold goes, except one buffer that fits.
    auto& buffers = impl_->core.buffers;
    bool kept_one = false;
    std::erase_if(buffers, [&](const std::unique_ptr<detail::ShmBuffer<detail::LibWayland>>& buffer) {
        if (buffer->busy()) return false;
        if (kept_one || buffer->size() != size) return true;
        kept_one = true;
        return false;
    });
    // The old pixels go before anything new is allocated, so the budget also holds at the peak.
    const std::size_t pixel_count = *bytes / sizeof(std::uint32_t);
    if (impl_->core.frame_pixels.capacity() != pixel_count) std::vector<std::uint32_t>{}.swap(impl_->core.frame_pixels);
    std::size_t chosen = buffers.size();
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        if (!buffers[i]->busy()) chosen = i;
    }
    if (chosen == buffers.size()) {
        auto created = detail::ShmBuffer<detail::LibWayland>::create(impl_->core.shm.get(), size);
        if (!created) return std::unexpected(std::move(created.error()));
        buffers.push_back(std::move(*created));
    }
    try {
        impl_->core.frame_pixels.resize(pixel_count);
    } catch (const std::bad_alloc&) {
        return make_error(ErrorCode::System, "out of memory for the frame's pixels");
    }
    return Frame<WaylandBackend>{generation, chosen, size};
}

std::expected<PixelBuffer, Error> WaylandBackend::frame_pixels(const Frame<WaylandBackend>& frame) {
    if (!impl_->core.ledger.current(frame.generation_)) return make_error(ErrorCode::StaleFrame, "this frame is no longer the current one");
    return PixelBuffer::create(impl_->core.frame_pixels, frame.size_, PixelBuffer::tight_stride(frame.size_));
}

std::expected<void, Error> WaylandBackend::present(Frame<WaylandBackend> frame, Rect damage) {
    if (!impl_->core.ledger.current(frame.generation_)) return make_error(ErrorCode::StaleFrame, "this frame is no longer the current one");
    impl_->core.ledger.finish();
    // A current frame's buffer is still in the list, because only a new acquire removes any.
    detail::ShmBuffer<detail::LibWayland>& buffer = *impl_->core.buffers.at(frame.buffer_);
    if (auto written = buffer.write(impl_->core.frame_pixels); !written) return std::unexpected(std::move(written.error()));
    wl_surface* surface = impl_->core.surface.get();
    if (impl_->core.geometry != buffer.size()) {
        impl_->core.geometry = buffer.size();
        xdg_surface_set_window_geometry(impl_->core.xdg.get(), 0, 0, detail::GeometryAccess::number(impl_->core.geometry.width()),
                                        detail::GeometryAccess::number(impl_->core.geometry.height()));
    }
    buffer.mark_busy();
    wl_surface_attach(surface, buffer.object(), 0, 0);
    wl_surface_damage_buffer(surface, detail::GeometryAccess::number(damage.left()), detail::GeometryAccess::number(damage.top()),
                             detail::GeometryAccess::number(damage.width()), detail::GeometryAccess::number(damage.height()));
    wl_surface_commit(surface);
    return {};
}

std::expected<void, Error> WaylandBackend::sync() {
    return impl_->core.roundtrip();
}

void WaylandBackend::begin_move(Serial serial) noexcept {
    if (impl_->core.seat) xdg_toplevel_move(impl_->core.toplevel.get(), impl_->core.seat.get(), detail::WireAccess::number(serial));
}

void WaylandBackend::begin_resize(Serial serial, ResizeEdge edge) noexcept {
    if (impl_->core.seat)
        xdg_toplevel_resize(impl_->core.toplevel.get(), impl_->core.seat.get(), detail::WireAccess::number(serial), detail::resize_edge_value(edge));
}

void WaylandBackend::set_cursor(CursorShape shape) noexcept { impl_->core.set_cursor_shape(shape); }

}  // namespace jgui::inline v001
