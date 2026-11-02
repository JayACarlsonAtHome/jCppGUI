// File: include/jCppGUI/v001/headless_backend.hpp
// Purpose: The backend with no screen. Tests inject events and read the pixels back.
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "backend.hpp"

namespace jgui::inline v001 {

class HeadlessBackend {
public:
    // Declared before the constructor that calls it: Clang instantiates a constexpr template at its
    // first use, and in constant evaluation it does not look again for a body parsed later.
    template <class T>
    constexpr void inject(const T& event) {
        require(events_.push(event), "headless event queue is full");
    }

    // Starts as a compositor would: with one resize that gives the window its first size.
    constexpr explicit HeadlessBackend(Size initial) { inject(ResizeEvent{initial}); }

    template <class Sink>
    constexpr std::expected<void, Error> process_pending(Sink& sink) {
        events_.drain(sink);
        return {};
    }

    // Nothing else can ever arrive, so a wait with an empty queue is an Error rather than a hang.
    template <class Sink>
    constexpr std::expected<void, Error> wait(Sink& sink) {
        if (events_.empty()) return make_error(ErrorCode::WouldBlock, "headless wait with no events queued");
        events_.drain(sink);
        return {};
    }

    [[nodiscard]] constexpr bool can_present() const noexcept { return true; }

    constexpr std::expected<PixelBuffer, Error> acquire_frame(Size size) {
        auto bytes = PixelBuffer::byte_size(size);
        if (!bytes) return std::unexpected(std::move(bytes.error()));
        pixels_.resize(*bytes / sizeof(std::uint32_t));
        size_ = size;
        return PixelBuffer::create(pixels_, size, sat_mul(size.width(), 4));
    }

    constexpr std::expected<void, Error> present(Rect) {
        ++presented_;
        return {};
    }

    constexpr std::expected<void, Error> sync() { return {}; }

    constexpr void begin_move(Serial serial) noexcept { move_serial_ = serial; }
    constexpr void begin_resize(Serial serial, ResizeEdge edge) noexcept {
        resize_serial_ = serial;
        resize_edge_ = edge;
    }

    // --- For tests ---
    [[nodiscard]] constexpr std::uint32_t pixel(Point p) const {
        return pixels_.at(static_cast<std::size_t>(p.y) * static_cast<std::size_t>(size_.width()) +
                          static_cast<std::size_t>(p.x));
    }
    [[nodiscard]] constexpr Size size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::size_t presented() const noexcept { return presented_; }
    [[nodiscard]] constexpr std::optional<Serial> move_serial() const noexcept { return move_serial_; }
    [[nodiscard]] constexpr std::optional<Serial> resize_serial() const noexcept { return resize_serial_; }
    [[nodiscard]] constexpr std::optional<ResizeEdge> resize_edge() const noexcept { return resize_edge_; }

private:
    BackendEventQueue events_;
    std::vector<std::uint32_t> pixels_;
    Size size_{};
    std::size_t presented_{};
    std::optional<Serial> move_serial_;
    std::optional<Serial> resize_serial_;
    std::optional<ResizeEdge> resize_edge_;
};

static_assert(Backend<HeadlessBackend>);

}  // namespace jgui::inline v001
