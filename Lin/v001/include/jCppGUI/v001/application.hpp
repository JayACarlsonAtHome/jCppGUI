// File: include/jCppGUI/v001/application.hpp
// Purpose: The event loop, split in two. ApplicationCore holds all the logic: it owns the backend and
//          the tree by value, sends events into the tree, carries out what widgets ask of the window
//          system, and redraws only when damaged. It is constexpr, so it can be run whole in a static_assert. Application
//          adds only what needs the system: the real font engine and the UI-thread check.
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <thread>
#include <utility>

#include "backend.hpp"
#include "text.hpp"
#include "tree.hpp"

namespace jgui::inline v001 {

template <class B, class Root>
class ApplicationCore {
    static_assert(Backend<B>, "this type is not a backend");

public:
    constexpr ApplicationCore(B backend, Root root) : backend_{std::move(backend)}, root_{std::move(root)} {}

    // Runs until the window is closed or something fails. Sleeps in the backend's wait.
    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> run(const Shaper& text) {
        while (!closed_) {
            if (auto result = settle(text); !result) return result;
            if (closed_) break;
            if (auto result = backend_.wait(*this); !result) return result;
        }
        return {};
    }

    // Handles what is already queued, and draws if that caused damage. Never sleeps.
    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> process_pending(const Shaper& text) {
        if (auto result = backend_.process_pending(*this); !result) return result;
        return settle(text);
    }

    // Waits for the first configure, presents one frame, and waits for the display to confirm it.
    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> present_first_frame(const Shaper& text) {
        while (frames_ == 0) {
            if (auto result = settle(text); !result) return result;
            if (frames_ != 0) break;
            if (closed_) return make_error(ErrorCode::Protocol, "the window closed before its first frame");
            if (auto result = backend_.wait(*this); !result) return result;
        }
        return backend_.sync();
    }

    [[nodiscard]] constexpr Root& ui() noexcept { return root_; }
    [[nodiscard]] constexpr bool closed() const noexcept { return closed_; }
    [[nodiscard]] constexpr std::uint64_t frames() const noexcept { return frames_; }
    [[nodiscard]] constexpr Size size() const noexcept { return root_.size(); }

    // --- The event sink the backend calls, on the UI thread, outside any C callback ---

    constexpr void on(const CloseEvent&) { closed_ = true; }

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
        send_to_tree(PointerMove{event.position, root_.capture_active()});
    }

    constexpr void on(const PointerButton& event) {
        if (event.state == ButtonState::Pressed) {
            send_to_tree(PointerPress{event.position, event.button, event.serial});
        } else {
            send_to_tree(PointerRelease{event.position, event.button});
        }
    }

    constexpr void on(const PointerLeave& event) {
        root_.notify(event);
        carry_out_requests();
    }

    constexpr void on(const KeyEvent& event) { send_to_tree(event); }

private:
    friend struct detail::TestAccess;

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
        if (asked.close) on(CloseEvent{});
    }

    // Reports a failure recorded during delivery, then draws if anything is damaged.
    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> settle(const Shaper& text) {
        if (failure_) {
            Error error = std::move(*failure_);
            failure_.reset();
            return std::unexpected(std::move(error));
        }
        if (closed_ || !have_size_) return {};
        if (!damaged_ && !root_.damaged()) return {};
        if (!backend_.can_present(root_.size())) return {};
        return draw(text);
    }

    template <TextShaper Shaper>
    [[nodiscard]] constexpr std::expected<void, Error> draw(const Shaper& text) {
        const Rect whole{Point{}, root_.size()};
        if (auto laid_out = root_.layout(whole, text); !laid_out) return std::unexpected(std::move(laid_out.error()));
        auto frame = backend_.acquire_frame(root_.size());
        if (!frame) return std::unexpected(std::move(frame.error()));
        auto pixels = backend_.frame_pixels(*frame);
        if (!pixels) return std::unexpected(std::move(pixels.error()));
        Canvas canvas{*pixels};
        canvas.fill_rect(whole, theme::background);
        root_.paint(canvas);
        if (auto presented = backend_.present(std::move(*frame), whole); !presented) return presented;
        root_.clear_damage();
        damaged_ = false;
        ++frames_;
        return {};
    }

    B backend_;
    Root root_;
    std::optional<Error> failure_;
    std::uint64_t frames_{};
    bool have_size_{};
    bool closed_{};
    bool damaged_{};
};

template <Backend B, class Root>
ApplicationCore(B, Root) -> ApplicationCore<B, Root>;

template <Backend B, class Root>
class Application {
public:
    [[nodiscard]] static std::expected<Application, Error> create(B backend, Root root) {
        auto text = TextEngine::create();
        if (!text) return std::unexpected(std::move(text.error()));
        return Application{std::move(backend), std::move(root), std::move(*text)};
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

private:
    friend struct detail::TestAccess;

    Application(B backend, Root root, TextEngine text)
        : core_{std::move(backend), std::move(root)}, text_{std::move(text)}, owner_{std::this_thread::get_id()} {}

    void check_thread() const noexcept {
        require(std::this_thread::get_id() == owner_, "Application used from a thread that does not own it");
    }

    ApplicationCore<B, Root> core_;
    TextEngine text_;
    std::thread::id owner_;
};

template <class B, class Root>
[[nodiscard]] std::expected<Application<B, Root>, Error> make_application(B backend, Root root) {
    return Application<B, Root>::create(std::move(backend), std::move(root));
}

}  // namespace jgui::inline v001
