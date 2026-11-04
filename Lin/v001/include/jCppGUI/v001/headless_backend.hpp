// File: include/jCppGUI/v001/headless_backend.hpp
// Purpose: The backend with no screen. Tests inject events and read the pixels back.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <string>
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

    // Nothing else can ever arrive. So a wait with an empty queue and a deadline runs the clock to it at once, and
    // with none it is an Error rather than a hang.
    template <class Sink>
    constexpr std::expected<void, Error> wait(Sink& sink, std::optional<MonotonicTime> deadline) {
        if (events_.empty()) {
            if (!deadline) return make_error(ErrorCode::WouldBlock, "headless wait with no events queued");
            time_ = std::max(time_, *deadline);
            return {};
        }
        events_.drain(sink);
        return {};
    }

    [[nodiscard]] constexpr MonotonicTime now() const noexcept { return time_; }

    [[nodiscard]] constexpr bool can_present(Size) const noexcept { return !display_holding_; }

    constexpr std::expected<Frame<HeadlessBackend>, Error> acquire_frame(Size size) {
        const std::uint64_t generation = ledger_.begin();
        auto bytes = PixelBuffer::byte_size(size);
        if (!bytes) return std::unexpected(std::move(bytes.error()));
        pixels_.resize(*bytes / sizeof(std::uint32_t));
        size_ = size;
        return Frame<HeadlessBackend>{generation, 0, size};
    }

    constexpr std::expected<PixelBuffer, Error> frame_pixels(const Frame<HeadlessBackend>& frame) {
        if (!ledger_.current(frame.generation_)) return make_error(ErrorCode::StaleFrame, "this frame is no longer the current one");
        return PixelBuffer::create(pixels_, frame.size_, PixelBuffer::tight_stride(frame.size_));
    }

    constexpr std::expected<void, Error> present(Frame<HeadlessBackend> frame, Rect) {
        if (!ledger_.current(frame.generation_)) return make_error(ErrorCode::StaleFrame, "this frame is no longer the current one");
        ledger_.finish();
        ++presented_;
        return {};
    }

    constexpr std::expected<void, Error> sync() { return {}; }

    constexpr void begin_move(Serial serial) noexcept { move_serial_ = serial; }
    constexpr void begin_resize(Serial serial, ResizeEdge edge) noexcept {
        resize_serial_ = serial;
        resize_edge_ = edge;
    }

    constexpr void set_cursor(CursorShape shape) noexcept {
        cursor_ = shape;
        ++cursor_requests_;
    }

    constexpr void copy_text(Serial serial, const SafeClipboardText& text) {
        copied_text_ = text.view();
        copied_serial_ = serial;
    }

    // --- For tests ---
    [[nodiscard]] constexpr std::string_view copied_text() const noexcept { return copied_text_; }
    constexpr void advance_time(std::chrono::nanoseconds by) noexcept { time_ = time_.after(by); }
    constexpr void set_display_holding(bool holding) noexcept { display_holding_ = holding; }
    [[nodiscard]] constexpr std::uint32_t pixel(Point p) const {
        return pixels_.at(PixelBuffer::tight_index(p, size_));
    }
    [[nodiscard]] constexpr Size size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::size_t presented() const noexcept { return presented_; }
    [[nodiscard]] constexpr std::optional<Serial> move_serial() const noexcept { return move_serial_; }
    [[nodiscard]] constexpr std::optional<Serial> resize_serial() const noexcept { return resize_serial_; }
    [[nodiscard]] constexpr std::optional<ResizeEdge> resize_edge() const noexcept { return resize_edge_; }
    [[nodiscard]] constexpr std::optional<CursorShape> cursor() const noexcept { return cursor_; }
    [[nodiscard]] constexpr std::size_t cursor_requests() const noexcept { return cursor_requests_; }

private:
    BackendEventQueue events_{};
    MonotonicTime time_{};
    detail::FrameLedger ledger_{};
    std::vector<std::uint32_t> pixels_{};
    Size size_{};
    std::size_t presented_{};
    bool display_holding_{};
    std::optional<Serial> move_serial_{};
    std::string copied_text_{};
    std::optional<Serial> copied_serial_{};
    std::optional<Serial> resize_serial_{};
    std::optional<ResizeEdge> resize_edge_{};
    std::optional<CursorShape> cursor_{};
    std::size_t cursor_requests_{};
};

static_assert(Backend<HeadlessBackend>);

}  // namespace jgui::inline v001
