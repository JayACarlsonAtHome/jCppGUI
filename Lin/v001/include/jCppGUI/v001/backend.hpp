// File: include/jCppGUI/v001/backend.hpp
// Purpose: The boundary between the toolkit and a display system: the Backend concept, and the typed
//          buffer that holds backend events until they can be delivered to a templated sink.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "canvas.hpp"
#include "clipboard_text.hpp"
#include "error.hpp"
#include "events.hpp"
#include "timer.hpp"

namespace jgui::inline v001 {

// An event that can take in the next one of its own type, so that a run of them holds one slot. absorb changes
// nothing and answers false when taking the newer one in would lose something.
template <class T>
concept Coalescing = requires(T& older, const T& newer) {
    { older.absorb(newer) } noexcept -> std::same_as<bool>;
};

// Keeps events of several types in arrival order, each in a queue of its own type, with nothing
// erased. Drained in order, it calls the overload of sink.on for each event's own type.
// A Coalescing event pushed straight after another of its type that is still held may merge with it, and
// that is the only way two events become one. Order against every other type stays as it arrived.
// A display event arrives inside a C callback; it is held here and delivered after that callback has
// returned, so toolkit code and user callbacks never run under libwayland. This is input buffering at
// the system boundary. Messages between widgets are immediate and never pass through here.
template <class... E>
class EventQueue {
public:
    static constexpr std::size_t limit = 65536;

    // False when the queue is full and the event was not kept. An event that merges needs no room of its own.
    template <class T>
    [[nodiscard]] constexpr bool push(const T& event) {
        static_assert((std::same_as<T, E> || ...), "this event type is not one this queue holds");
        Queue<T>& queue = std::get<Queue<T>>(queues_);
        if constexpr (Coalescing<T>) {
            if (queue.has_next() && queue.items.back().sequence + 1 == next_sequence_ && queue.items.back().event.absorb(event)) return true;
        }
        if (count_ >= limit) return false;
        queue.items.push_back(Stamped<T>{next_sequence_++, event});
        ++count_;
        return true;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }

    template <class Sink>
    constexpr void drain(Sink& sink) {
        while (count_ > 0) {
            std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
            ((oldest = std::get<Queue<E>>(queues_).has_next() ? std::min(oldest, std::get<Queue<E>>(queues_).next().sequence) : oldest), ...);
            (deliver_if_oldest(std::get<Queue<E>>(queues_), oldest, sink) || ...);
        }
    }

private:
    template <class T>
    struct Stamped {
        std::uint64_t sequence;
        T event;
    };

    // One type's events. The storage is reused once a queue has been read to its end.
    template <class T>
    struct Queue {
        std::vector<Stamped<T>> items{};
        std::size_t head{};

        [[nodiscard]] constexpr bool has_next() const noexcept { return head < items.size(); }
        [[nodiscard]] constexpr const Stamped<T>& next() const { return items[head]; }
        constexpr void advance() {
            if (++head == items.size()) {
                items.clear();
                head = 0;
            }
        }
    };

    template <class T, class Sink>
    constexpr bool deliver_if_oldest(Queue<T>& queue, std::uint64_t oldest, Sink& sink) {
        if (!queue.has_next() || queue.next().sequence != oldest) return false;
        const T event = queue.next().event;
        queue.advance();
        --count_;
        sink.on(event);
        return true;
    }

    std::tuple<Queue<E>...> queues_{};
    std::uint64_t next_sequence_{};
    std::size_t count_{};
};

using BackendEventQueue = EventQueue<CloseEvent, ResizeEvent, PointerMove, PointerButton, PointerLeave, PointerScroll, KeyEvent>;

// All of one window's frame buffers, and the memory a frame is drawn in, must fit in this.
inline constexpr std::size_t window_pixel_budget = std::size_t{1} << 30U;

namespace detail {
// Whether a frame of some size can start now. Ready: it fits. Wait: not yet, because the display holds too
// much, and releases will make room. Never: one frame alone does not fit, and no release can change that.
enum class FrameRoom : std::uint8_t { Ready, Wait, Never };

// The buffers the display holds. Nothing else counts, because any buffer it does not hold can be dropped.
struct HeldBuffers {
    std::size_t bytes{};
    std::size_t count{};
};

// A frame needs one buffer and the memory it is drawn in, each of frame_bytes. Waiting is only ever the answer
// while something is held, so it always ends if the display releases what it holds.
[[nodiscard]] constexpr FrameRoom frame_room(std::size_t frame_bytes, HeldBuffers held, bool reuses_a_buffer,
                                             std::size_t max_buffers, std::size_t budget) noexcept {
    if (frame_bytes > budget / 2) return FrameRoom::Never;
    if (!reuses_a_buffer && held.count >= max_buffers) return FrameRoom::Wait;
    return held.bytes > budget - frame_bytes * 2 ? FrameRoom::Wait : FrameRoom::Ready;
}
}  // namespace detail

// One frame being drawn, which only its own backend type can make or read. It holds no pointer, only which
// buffer and which generation it is, so an old one can do nothing but be refused. It cannot be copied, and
// presenting takes it by value, so it cannot be presented twice. A frame moved from has generation 0.
template <class Owner>
class Frame {
public:
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    constexpr Frame(Frame&& other) noexcept
        : generation_{std::exchange(other.generation_, 0)}, buffer_{other.buffer_}, size_{other.size_} {}
    constexpr Frame& operator=(Frame&& other) noexcept {
        generation_ = std::exchange(other.generation_, 0);
        buffer_ = other.buffer_;
        size_ = other.size_;
        return *this;
    }
    constexpr ~Frame() = default;

private:
    friend Owner;
    constexpr Frame(std::uint64_t generation, std::size_t buffer, Size size) noexcept
        : generation_{generation}, buffer_{buffer}, size_{size} {}

    std::uint64_t generation_;
    std::size_t buffer_;
    Size size_;
};

namespace detail {
// Stands in for a real sink when a backend is checked against the concept.
struct ProbeSink {
    constexpr void on(const CloseEvent&) {}
    constexpr void on(const ResizeEvent&) {}
    constexpr void on(const PointerMove&) {}
    constexpr void on(const PointerButton&) {}
    constexpr void on(const PointerLeave&) {}
    constexpr void on(const PointerScroll&) {}
    constexpr void on(const KeyEvent&) {}
};
}  // namespace detail

// process_pending delivers what is available without sleeping. wait sleeps until something arrives, or until the
// deadline if it is given one, and delivers what came. now is the backend's monotonic clock. A frame is acquire_frame, frame_pixels and drawing, then present. frame_pixels and
// present refuse a frame that is not the current one. can_present is false while the display holds too much
// for a frame of that size, which is the signal to wait and not an error, and sync waits until the display has
// processed all requests so far. set_cursor says what the pointer should look like over the window, and a
// backend that cannot show that shape leaves it as it is. Only a change is ever asked for.
template <class B>
concept Backend = requires(B& backend, const B& constant, detail::ProbeSink& sink, std::optional<MonotonicTime> deadline, Size size, Rect damage,
                           Serial serial, ResizeEdge edge, CursorShape shape, const Frame<B>& frame, Frame<B>&& taken, const SafeClipboardText& safe_text) {
    { backend.process_pending(sink) } -> std::same_as<std::expected<void, Error>>;
    { backend.wait(sink, deadline) } -> std::same_as<std::expected<void, Error>>;
    { constant.now() } -> std::same_as<MonotonicTime>;
    { constant.can_present(size) } -> std::same_as<bool>;
    { backend.acquire_frame(size) } -> std::same_as<std::expected<Frame<B>, Error>>;
    { backend.frame_pixels(frame) } -> std::same_as<std::expected<PixelBuffer, Error>>;
    { backend.present(std::move(taken), damage) } -> std::same_as<std::expected<void, Error>>;
    { backend.sync() } -> std::same_as<std::expected<void, Error>>;
    { backend.begin_move(serial) } -> std::same_as<void>;
    { backend.begin_resize(serial, edge) } -> std::same_as<void>;
    { backend.set_cursor(shape) } -> std::same_as<void>;
    { backend.copy_text(serial, safe_text) } -> std::same_as<void>;
};

}  // namespace jgui::inline v001
