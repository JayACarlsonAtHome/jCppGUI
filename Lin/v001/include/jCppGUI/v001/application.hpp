// File: include/jCppGUI/v001/application.hpp
// Purpose: The event loop, split in two. ApplicationCore holds all the logic: it owns the backend and
//          the tree by value, sends events into the tree, carries out what widgets ask of the window
//          system, and redraws only when damaged. It is constexpr, so it can be run whole in a static_assert. Application
//          adds only what needs the system: the real font engine and the UI-thread check. A monitor, chosen by type,
//          is told what happens at named points: the core itself reads no file, and time comes only through the backend.
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

#include "backend.hpp"
#include "redraw_points.hpp"
#include "text.hpp"
#include "tree.hpp"

namespace jgui::inline v001 {

template <class B, class Root, class Monitor = NoMonitor>
class ApplicationCore {
    static_assert(Backend<B>, "this type is not a backend");
    static_assert(RedrawMonitor<Monitor>, "this type is not a redraw monitor");

public:
    constexpr ApplicationCore(B backend, Root root, Monitor monitor = Monitor{})
        : backend_{std::move(backend)}, root_{std::move(root)}, painting_{std::make_unique<detail::FrameLedger>()}, monitor_{std::move(monitor)} {}

    // Runs until the window is closed or something fails. Sleeps in the backend's wait.
    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> run(const Shaper& text) {
        const Entry entry{*this};
        auto result = run_until_closed(text);
        monitor_.finish();
        return result;
    }

    // Handles what is already queued, and draws if that caused damage. Never sleeps.
    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> process_pending(const Shaper& text) {
        const Entry entry{*this};
        if (auto result = backend_.process_pending(*this); !result) return std::unexpected(std::move(result.error()));
        return take_a_turn(text);
    }

    // Waits for the first configure, presents one frame, and waits for the display to confirm it.
    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> present_first_frame(const Shaper& text) {
        const Entry entry{*this};
        auto result = wait_for_first_frame(text);
        monitor_.finish();
        return result;
    }

    // For a program that runs its own loop through process_pending: a monitor's last word. run and
    // present_first_frame do this by themselves.
    constexpr void finish_monitor() {
        const Entry entry{*this};
        monitor_.finish();
    }

    // Why a monitor stopped, for the program to report at exit. It gives out the reason and never the monitor.
    [[nodiscard]] constexpr std::optional<Error> monitor_error() const { return monitor_.error(); }

    [[nodiscard]] constexpr Root& ui() noexcept { return root_; }
    [[nodiscard]] constexpr bool closed() const noexcept { return closed_; }
    [[nodiscard]] constexpr std::uint64_t frames() const noexcept { return frames_; }
    [[nodiscard]] constexpr Size size() const noexcept { return root_.size(); }
    // When the widgets next want to act, as of the last turn. A program with its own process_pending loop calls again by then.
    [[nodiscard]] constexpr std::optional<MonotonicTime> next_wake() const noexcept { return wake_; }

private:
    friend struct detail::TestAccess;
    template <class... E>
    friend class EventQueue;

    // Every way of running the loop starts here. A callback or a widget that reaches this application and tries to
    // run it from inside would redraw and deliver events in the middle of a delivery, so that aborts.
    class Entry {
    public:
        constexpr explicit Entry(ApplicationCore& core) noexcept : core_{core} {
            require(!core_.entered_ && !core_.root_.delivering(), "an application run from inside its own callback or paint");
            core_.entered_ = true;
        }
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        constexpr ~Entry() { core_.entered_ = false; }

    private:
        ApplicationCore& core_;
    };

    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> run_until_closed(const Shaper& text) {
        while (!closed_) {
            if (auto result = take_a_turn(text); !result) return std::unexpected(std::move(result.error()));
            if (closed_) break;
            if (auto result = backend_.wait(*this, wake_); !result) return std::unexpected(std::move(result.error()));
        }
        return {};
    }

    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> wait_for_first_frame(const Shaper& text) {
        while (frames_ == 0) {
            if (auto result = settle(text); !result) return std::unexpected(std::move(result.error()));
            if (frames_ != 0) break;
            if (closed_) return make_error(ErrorCode::Protocol, "the window closed before its first frame");
            if (auto result = backend_.wait(*this, std::nullopt); !result) return std::unexpected(std::move(result.error()));
        }
        return backend_.sync();
    }

    // --- The event sink, which only a backend's event queue calls: on the UI thread, outside any C callback ---

    // What the widgets were waiting for goes with the window.
    constexpr void on(const CloseEvent&) {
        closed_ = true;
        wake_.reset();
    }

    constexpr void on(const ResizeEvent& event) {
        // A size from outside is validated before anything uses it. A refused one leaves the window
        // as it was and is reported by the next settle.
        if (auto bytes = PixelBuffer::byte_size(event.size); !bytes) {
            failure_ = std::move(bytes.error());
            return;
        }
        root_.set_size(event.size);
        have_size_ = true;
        damaged_ = true;
    }

    constexpr void on(const PointerMove& event) {
        // A compositor leaves the pointer's look undefined at each enter, so nothing about it is known now.
        if (event.entered) cursor_.reset();
        send_to_tree(PointerMove{event.position, root_.capture_active()});
        // A tree with no frame says nothing, and the pointer must still look like something.
        if (!cursor_) show_cursor(CursorShape::Default);
    }

    constexpr void on(const PointerButton& event) {
        if (event.state == ButtonState::Pressed) {
            send_to_tree(PointerPress{event.position, event.button, event.serial, event.modifiers});
        } else {
            send_to_tree(PointerRelease{event.position, event.button});
        }
    }

    constexpr void on(const PointerLeave& event) {
        root_.notify(event);
        carry_out_requests();
    }

    constexpr void on(const PointerScroll& event) { send_to_tree(event); }

    constexpr void on(const KeyEvent& event) { send_to_tree(event); }

    // Delivers the event, then carries out what the widgets asked of the window system. The answer is for
    // a parent routing between its children, and nothing above the tree needs it.
    template <class E>
    constexpr void send_to_tree(const E& event) {
        root_.dispatch(event);
        carry_out_requests();
    }

    constexpr void carry_out_requests() {
        const WindowRequests asked = root_.take_requests();
        if (asked.move) backend_.begin_move(*asked.move);
        if (asked.resize) backend_.begin_resize(asked.resize->serial, asked.resize->edge);
        if (asked.cursor) show_cursor(*asked.cursor);
        if (asked.close) on(CloseEvent{});
    }

    // Goes to the backend only when the shape changes.
    constexpr void show_cursor(CursorShape shape) {
        if (cursor_ == shape) return;
        cursor_ = shape;
        backend_.set_cursor(shape);
    }

    // Asks every widget what has come due, on every turn, whatever woke the loop.
    constexpr void run_timers() {
        if (closed_) return;
        wake_ = root_.run_timers(backend_.now());
        carry_out_requests();
    }

    // One turn of the loop, after what was queued has been delivered. A frame's layout is when a widget can learn something only a shaper can tell it, with
    // no event to say so. So after a frame the timers run once more, and what they damaged is drawn. It stops there, so this always ends.
    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> take_a_turn(const Shaper& text) {
        run_timers();
        const std::uint64_t frames_before = frames_;
        if (auto result = settle(text); !result) return result;
        if (frames_ == frames_before) return {};
        run_timers();
        if (closed_ || (!damaged_ && !root_.damaged())) return {};
        return settle(text);
    }

    // Reports a failure recorded during delivery, then draws if anything is damaged. Whatever came of it, nothing
    // is being measured now, so this is when a monitor may do its own work.
    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> settle(const Shaper& text) {
        auto result = settle_damage(text);
        monitor_.between_frames();
        return result;
    }

    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> settle_damage(const Shaper& text) {
        if (failure_) {
            Error error = std::move(*failure_);
            failure_.reset();
            return std::unexpected(std::move(error));
        }
        if (closed_ || !have_size_) return {};
        if (!damaged_ && !root_.damaged()) return {};
        if (!backend_.can_present(root_.size())) {
            monitor_.deferred(redraw_reason(), root_.size());
            return {};
        }
        return draw(text);
    }

    // Asks nothing of the tree beyond what settle already knows: a monitor must not change what the tree is asked.
    [[nodiscard]] constexpr RedrawReason redraw_reason() const noexcept {
        if (frames_ == 0) return RedrawReason::FirstFrame;
        return damaged_ ? RedrawReason::Resize : RedrawReason::WidgetDamage;
    }

    // Every canvas of this frame stops working when this returns, before the pixels leave for the display.
    constexpr void paint(const PixelBuffer& pixels, const Rect& whole) {
        const std::uint64_t generation = painting_->begin();
        {
            Canvas canvas{pixels, *painting_, generation};
            canvas.fill_rect(whole, theme::background);
            root_.paint(canvas);
        }
        painting_->finish();
    }

    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> draw(const Shaper& text) {
        const Rect whole{Point{}, root_.size()};
        monitor_.frame_begins(redraw_reason(), whole);
        if constexpr (Monitor::watches) root_.report_damage(monitor_);
        monitor_.phases_begin();
        if (auto laid_out = root_.layout(whole, text); !laid_out) return std::unexpected(std::move(laid_out.error()));
        monitor_.laid_out();
        auto frame = backend_.acquire_frame(root_.size());
        if (!frame) return std::unexpected(std::move(frame.error()));
        auto pixels = backend_.frame_pixels(*frame);
        if (!pixels) return std::unexpected(std::move(pixels.error()));
        monitor_.acquired();
        paint(*pixels, whole);
        monitor_.painted(*pixels);
        if (auto presented = backend_.present(std::move(*frame), whole); !presented) return std::unexpected(std::move(presented.error()));
        monitor_.presented();
        root_.clear_damage();
        damaged_ = false;
        ++frames_;
        // After the last thing a monitor measures. Only a monitor that asks for this pays for it.
        if constexpr (ItemRecorder<Monitor>) root_.record_items(monitor_);
        return {};
    }

    B backend_;
    Root root_;
    // On the heap, so a canvas can hold its address while this object moves.
    std::unique_ptr<detail::FrameLedger> painting_;
    std::optional<Error> failure_{};
    std::uint64_t frames_{};
    std::optional<MonotonicTime> wake_{};
    std::optional<CursorShape> cursor_{};
    bool have_size_{};
    bool closed_{};
    bool damaged_{};
    bool entered_{};
    // Last, and taking no room when it is empty, so a core with no monitor is as it would be with no member at all.
    [[no_unique_address]] Monitor monitor_;
};

template <Backend B, class Root>
ApplicationCore(B, Root) -> ApplicationCore<B, Root>;

template <Backend B, class Root, class Monitor>
ApplicationCore(B, Root, Monitor) -> ApplicationCore<B, Root, Monitor>;

template <Backend B, class Root, class Monitor = NoMonitor>
class Application {
public:
    [[nodiscard]] static std::expected<Application, Error> create(B backend, Root root, Monitor monitor = Monitor{}) {
        auto text = TextEngine::create();
        if (!text) return std::unexpected(std::move(text.error()));
        return Application{std::move(backend), std::move(root), std::move(monitor), std::move(*text)};
    }

    [[nodiscard]] std::expected<void, Error> run() {
        check_thread();
        return core_.run(text_);
    }
    [[nodiscard]] std::expected<void, Error> process_pending() {
        check_thread();
        return core_.process_pending(text_);
    }
    [[nodiscard]] std::expected<void, Error> present_first_frame() {
        check_thread();
        return core_.present_first_frame(text_);
    }
    void finish_monitor() {
        check_thread();
        core_.finish_monitor();
    }
    [[nodiscard]] std::optional<Error> monitor_error() const {
        check_thread();
        return core_.monitor_error();
    }

    [[nodiscard]] Root& ui() {
        check_thread();
        return core_.ui();
    }
    [[nodiscard]] bool closed() const {
        check_thread();
        return core_.closed();
    }
    [[nodiscard]] std::uint64_t frames() const {
        check_thread();
        return core_.frames();
    }
    [[nodiscard]] Size size() const {
        check_thread();
        return core_.size();
    }
    [[nodiscard]] std::optional<MonotonicTime> next_wake() const {
        check_thread();
        return core_.next_wake();
    }

private:
    friend struct detail::TestAccess;

    Application(B backend, Root root, Monitor monitor, TextEngine text)
        : core_{std::move(backend), std::move(root), std::move(monitor)}, text_{std::move(text)}, owner_{std::this_thread::get_id()} {}

    void check_thread() const noexcept {
        require(std::this_thread::get_id() == owner_, "Application used from a thread that does not own it");
    }

    ApplicationCore<B, Root, Monitor> core_;
    TextEngine text_;
    std::thread::id owner_;
};

template <class B, class Root, class Monitor = NoMonitor>
[[nodiscard]] std::expected<Application<B, Root, Monitor>, Error> make_application(B backend, Root root, Monitor monitor = Monitor{}) {
    return Application<B, Root, Monitor>::create(std::move(backend), std::move(root), std::move(monitor));
}

}  // namespace jgui::inline v001
