// File: tests/test_fake_compositor.cpp
// Purpose: The real WaylandBackend against a compositor that does what each script says, with no screen. Each scenario
//          is one CTest entry. A scenario passes when the client ended as expected, with no crash or sanitizer report,
//          and when it broke no rule of the protocol: see tests/fake_compositor/compositor.hpp.
#include <fcntl.h>
#include <sys/mman.h>  // for memfd_create
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <concepts>
#include <expected>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "fake_compositor/harness.hpp"
#include "help_ui.hpp"
#include "jCppGUI/v001/base_directory.hpp"
#include "jCppGUI/v001/jgui.hpp"
#include "jCppGUI/v001/redraw_monitor.hpp"
#include "jCppGUI/v001/wayland_backend.hpp"
#include "jCppGUI/v001/headless_backend.hpp"
#include "monitor_helpers.hpp"
#include "test_access.hpp"
#include "viewer_helpers.hpp"
#include "viewer_ui.hpp"

namespace {

using namespace jgui_test::fake;
namespace wire = jgui_test::wire;
using jgui::ErrorCode;
using wire::Fixed;

constexpr std::int32_t int32_most = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t int32_least = std::numeric_limits<std::int32_t>::min();
constexpr std::uint32_t uint32_most = std::numeric_limits<std::uint32_t>::max();

// Where things are in the demo's 640x480 window, with the default metrics: see tests/test_frame.cpp.
constexpr std::int32_t on_help_x = 12;
constexpr std::int32_t on_help_y = 32;
constexpr std::int32_t on_title_x = 300;
constexpr std::int32_t on_title_y = 12;
constexpr std::int32_t on_left_edge_x = 1;
constexpr std::int32_t on_left_edge_y = 240;
constexpr std::int32_t on_close_x = 624;
constexpr std::int32_t on_close_y = 12;
constexpr std::int32_t nowhere_x = 300;
constexpr std::int32_t nowhere_y = 300;
constexpr std::uint32_t key_return = 28;  // the evdev code, 8 under xkb's <RTRN>

[[nodiscard]] UniqueFd memfd_holding(std::string_view text) {
    UniqueFd fd{::memfd_create("jcppgui-fake-keymap", MFD_CLOEXEC)};
    if (fd.get() >= 0) static_cast<void>(jgui::detail::write_at(fd.get(), std::span{text}, 0));
    return fd;
}

// The seed keymap, from where CTest says it is, with the terminator a compositor sends.
[[nodiscard]] std::string valid_keymap() {
    std::string text;
    const auto* path = std::getenv("JGUI_TEST_KEYMAP");
    if (path == nullptr) return text;
    const UniqueFd fd{::open(path, O_RDONLY | O_CLOEXEC)};
    struct stat status {};
    if (fd.get() < 0 || ::fstat(fd.get(), &status) != 0) return text;
    text.resize(static_cast<std::size_t>(status.st_size));
    if (!jgui::detail::read_at(fd.get(), std::span{text}, 0)) text.clear();
    if (!text.empty()) text.push_back('\0');
    return text;
}

void send_keymap(Session& session, std::uint32_t format, UniqueFd fd, std::uint32_t size) {
    if (session.keyboard()) session.queue_with_fd(event::keyboard_keymap(*session.keyboard(), format, size), std::move(fd));
}

void send_valid_keymap(Session& session) {
    const std::string text = valid_keymap();
    if (text.empty()) session.violation("the seed keymap could not be read");
    send_keymap(session, wire::keymap_xkb_v1, memfd_holding(text), static_cast<std::uint32_t>(text.size()));
}

void pointer_enter(Session& session, std::int32_t x, std::int32_t y) {
    session.pointer_enter(Fixed::from_int(x), Fixed::from_int(y));
}
void pointer_motion(Session& session, std::int32_t x, std::int32_t y) {
    if (session.pointer()) session.queue(event::pointer_motion(*session.pointer(), 0, Fixed::from_int(x), Fixed::from_int(y)));
}
void pointer_button(Session& session, std::uint32_t button, std::uint32_t state) {
    if (session.pointer()) session.queue(event::pointer_button(*session.pointer(), session.next_serial(), 0, button, state));
}

// Steps that must each be delivered alone, because motion in one delivery merges and only the newest is seen. A ping
// follows each step. The client answers while it dispatches, and that answer leaves at its next flush, which comes after
// it has delivered what it read. So the next step, sent on the answer, cannot merge with this one, however slow the client.
class PacedSteps {
public:
    // Runs steps up to this index, the next one now and the rest as each answer comes.
    template <class Step>
    void run_until(Session& session, std::size_t end, const Step& step) {
        end_ = end;
        on_answer(session, step);
    }
    template <class Step>
    void on_answer(Session& session, const Step& step) {
        if (next_ >= end_ || !session.wm_base()) return;
        step(next_++);
        session.queue(event::ping(*session.wm_base(), session.next_serial()));
    }
    [[nodiscard]] constexpr bool done() const noexcept { return next_ >= end_; }

private:
    std::size_t next_{};
    std::size_t end_{};
};

void left_click(Session& session, std::int32_t x, std::int32_t y) {
    pointer_motion(session, x, y);
    pointer_button(session, wire::button_left, wire::button_pressed);
    pointer_button(session, wire::button_left, wire::button_released);
}

// Whether any pixel in the rows below the menu bar differs from the window's background.
[[nodiscard]] bool help_line_showing(Session& session, ObjectId buffer) {
    const auto pixels = session.pixels_of(buffer);
    if (!pixels) return false;
    constexpr std::size_t stride = 640;
    for (std::size_t y = 60; y < 90; ++y)
        for (std::size_t x = 8; x < 600; ++x)
            if (pixels->at(y * stride + x) != jgui::theme::background.argb) return true;
    return false;
}

// --- The whole path, as a real session would drive it ---

// Pointer to pixels through the wire, then every other thing a window asks of the compositor.
struct HappyPath {
    enum class Step { FirstFrame, ClickHelp, CheckShown, TitleMove, EdgeResize, CloseBox, Done };
    static constexpr ClientEnd end = ends_clean;
    Step step{Step::FirstFrame};
    std::uint32_t ping_serial{};
    bool pinged{};
    bool ponged{};
    bool moved{};
    bool resized{};
    std::uint32_t press_serial{};
    std::optional<ObjectId> latest{};

    void on(Session& session, const SurfaceReady&) {
        if (session.wm_base()) {
            ping_serial = session.next_serial();
            pinged = true;
            session.queue(event::ping(*session.wm_base(), ping_serial));
        }
        session.configure(640, 480);
    }
    void on(Session&, const Pong& pong) {
        ponged = pong.serial == ping_serial;
    }
    void on(Session& session, const BufferCommitted& commit) {
        if (commit.width != 640 || commit.height != 480) session.violation("the first size was not used");
        latest = commit.buffer;
        session.release(commit.buffer);
    }
    void on(Session& session, const MoveRequested& request) {
        moved = true;
        if (request.serial != press_serial) session.violation("a move with another serial than the press");
    }
    void on(Session& session, const ResizeRequested& request) {
        resized = true;
        if (request.serial != press_serial) session.violation("a resize with another serial than the press");
        if (request.edge != wire::resize_edge_left) session.violation("a resize of another edge");
    }
    void on(Session& session, const Idle&) {
        switch (step) {
            case Step::FirstFrame:
                if (session.commits_with_buffer() == 0) return;
                pointer_enter(session, on_help_x, on_help_y);
                left_click(session, on_help_x, on_help_y);
                step = Step::ClickHelp;
                return;
            case Step::ClickHelp:
                if (!latest || !help_line_showing(session, *latest)) session.violation("clicking Help did not show its line");
                pointer_motion(session, nowhere_x, nowhere_y);
                step = Step::CheckShown;
                return;
            case Step::CheckShown:
                press_serial = session.serial_ + 1;
                pointer_motion(session, on_title_x, on_title_y);
                pointer_button(session, wire::button_left, wire::button_pressed);
                step = Step::TitleMove;
                return;
            case Step::TitleMove:
                if (!moved) session.violation("pressing the title strip did not ask for a move");
                pointer_button(session, wire::button_left, wire::button_released);
                pointer_motion(session, on_left_edge_x, on_left_edge_y);
                press_serial = session.serial_ + 1;
                pointer_button(session, wire::button_left, wire::button_pressed);
                step = Step::EdgeResize;
                return;
            case Step::EdgeResize:
                if (!resized) session.violation("pressing the left edge did not ask for a resize");
                pointer_button(session, wire::button_left, wire::button_released);
                left_click(session, on_close_x, on_close_y);
                step = Step::CloseBox;
                return;
            case Step::CloseBox:
            case Step::Done:
            default:
                session.violation("the close box did not end the program");
                session.hang_up();
                step = Step::Done;
                return;
        }
    }
    void finish(Session& session) const {
        if (!pinged || !ponged) session.violation("a ping was not answered with its serial");
        if (session.title() != "jCppGUI") session.violation("the title did not arrive: " + session.title());
        if (session.app_id() != "jcppgui.test") session.violation("the app id did not arrive: " + session.app_id());
    }
};

// The keyboard's whole path: a real keymap, focus from a click, and Return firing the focused button.
struct KeyboardPath {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    std::optional<ObjectId> latest{};
    void on(Session& session, const KeyboardCreated&) { send_valid_keymap(session); }
    void on(Session& session, const BufferCommitted& commit) {
        latest = commit.buffer;
        session.release(commit.buffer);
    }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ++idles;
        if (idles == 1) {
            pointer_enter(session, on_help_x, on_help_y);
            left_click(session, on_help_x, on_help_y);
        } else if (idles == 2) {
            if (!latest || !help_line_showing(session, *latest)) session.violation("the click did not show the line");
            if (session.keyboard()) {
                session.queue(event::keyboard_enter(*session.keyboard(), session.next_serial(), *session.window_surface()));
                session.queue(event::keyboard_modifiers(*session.keyboard(), session.next_serial(), 0, 0, 0, 0));
                session.queue(event::keyboard_key(*session.keyboard(), session.next_serial(), 0, key_return, 1));
                session.queue(event::keyboard_key(*session.keyboard(), session.next_serial(), 0, key_return, 0));
            }
        } else {
            if (!latest || help_line_showing(session, *latest)) session.violation("Return on the focused button did not hide the line");
            session.close_window();
        }
    }
};

// --- Sizes ---

template <std::int32_t SentWidth, std::int32_t SentHeight, std::int32_t WantWidth, std::int32_t WantHeight>
struct ConfigureSize {
    static constexpr ClientEnd end = ends_clean;
    std::int32_t got_width{};
    std::int32_t got_height{};
    void on(Session& session, const SurfaceReady&) { session.configure(SentWidth, SentHeight); }
    void on(Session& session, const BufferCommitted& commit) {
        got_width = commit.width;
        got_height = commit.height;
        session.release(commit.buffer);
        session.close_window();
    }
    void finish(Session& session) const {
        if (got_width != WantWidth || got_height != WantHeight)
            session.violation("the buffer is " + std::to_string(got_width) + "x" + std::to_string(got_height) + ", and " + std::to_string(WantWidth) + "x" + std::to_string(WantHeight) + " was expected");
    }
};

// A storm of configures: each must be acknowledged, in order, and the window ends at the last size.
struct ConfigureFlood {
    static constexpr ClientEnd end = ends_clean;
    static constexpr std::int32_t count = 2000;
    std::int32_t last_width{};
    void on(Session& session, const SurfaceReady&) {
        for (std::int32_t i = 1; i <= count; ++i) session.configure(200 + i % 50, 200);
        last_width = 200 + count % 50;
    }
    void on(Session& session, const ConfigureAcked& ack) {
        if (ack.count == static_cast<std::size_t>(count) && session.commits_with_buffer() > 0) session.close_window();
    }
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (session.acks() == static_cast<std::size_t>(count)) {
            if (commit.width != last_width) session.violation("the last commit is not at the last size");
            session.close_window();
        }
    }
    void finish(Session& session) const {
        if (session.acks() != static_cast<std::size_t>(count)) session.violation("not every configure was acknowledged: " + std::to_string(session.acks()));
    }
};

// --- Buffers ---

// A release for a buffer the client has not attached yet, and of one already released.
struct ReleaseBeforeAttach {
    static constexpr ClientEnd end = ends_clean;
    std::size_t frames{};
    void on(Session& session, const BufferCreated& created) { session.send_release_anyway(created.buffer); }
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (++frames == 3) {
            session.close_window();
        } else {
            session.configure(300 + static_cast<std::int32_t>(frames), 300);
        }
    }
};

struct ReleaseThreeTimes {
    static constexpr ClientEnd end = ends_clean;
    std::size_t frames{};
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        session.send_release_anyway(commit.buffer);
        session.send_release_anyway(commit.buffer);
        if (++frames == 4) {
            session.close_window();
        } else {
            session.configure(300, 300 + static_cast<std::int32_t>(frames));
        }
    }
};

// Never releasing: the client stops at its budget of four buffers and waits, still answering. Then it closes cleanly.
struct NeverRelease {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    std::int32_t next_width{300};
    void on(Session& session, const BufferCommitted&) { session.configure(++next_width, 300); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() < 4) return;
        ++idles;
        if (idles == 1) {
            if (session.commits_with_buffer() != 4 || session.buffers_held() != 4) session.violation("the client went past its budget of four buffers");
            // It still answers: this must be acknowledged, and must not make it draw.
            for (std::int32_t i = 0; i < 5; ++i) session.configure(++next_width, 300);
        } else {
            if (session.commits_with_buffer() != 4) session.violation("the client drew while it should have waited");
            session.close_window();
        }
    }
    void finish(Session& session) const {
        if (session.acks() < 9) session.violation("a client waiting for buffers must still acknowledge configures");
    }
};

// The wait ends when the compositor gives a buffer back: the frame it was waiting to draw comes then.
struct ReleaseLate {
    static constexpr ClientEnd end = ends_clean;
    std::vector<ObjectId> held{};
    std::size_t idles{};
    std::int32_t next_width{300};
    bool released{};
    void on(Session& session, const BufferCommitted& commit) {
        held.push_back(commit.buffer);
        if (released) {
            if (commit.width != next_width) session.violation("the frame after the wait is not at the latest size");
            session.close_window();
        } else {
            session.configure(++next_width, 300);
        }
    }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() != 4 || released) return;
        session.release(held.front());
        released = true;
    }
    void finish(Session& session) const {
        if (session.commits_with_buffer() != 5) session.violation("the wait did not end with one more frame: " + std::to_string(session.commits_with_buffer()));
    }
};

// A compositor trying to change the size of the file under the client, on each pool.
struct PoolAttack {
    static constexpr ClientEnd end = ends_clean;
    std::size_t attacks{};
    void on(Session& session, const PoolCreated& created) {
        ++attacks;
        Pool& pool = session.pool(created.pool);
        const int fd = pool.file.get();
        if (::ftruncate(fd, 0) == 0) session.violation("the compositor could truncate the buffer file");
        if (::ftruncate(fd, ::off_t{pool.bytes} * 2) == 0) session.violation("the compositor could grow the buffer file");
        if (::posix_fallocate(fd, 0, ::off_t{pool.bytes} * 2) == 0) session.violation("the compositor could reserve past the buffer file's end");
        if (::fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) == 0) session.violation("the compositor could add a seal");
        const int seals = ::fcntl(fd, F_GET_SEALS);
        if ((seals & F_SEAL_SHRINK) == 0 || (seals & F_SEAL_GROW) == 0 || (seals & F_SEAL_SEAL) == 0) session.violation("the buffer file is not sealed");
        struct stat status {};
        if (::fstat(fd, &status) != 0 || status.st_size != pool.bytes) session.violation("the buffer file changed size");
    }
    void finish(Session& session) const {
        if (attacks < 2) session.violation("the pools were not all attacked");
    }
};

// --- The seat ---

struct CapabilitiesFlap {
    static constexpr ClientEnd end = ends_clean;
    std::size_t created{};
    std::size_t released{};
    std::size_t idles{};
    void on(Session&, const PointerCreated&) { ++created; }
    void on(Session&, const KeyboardCreated&) { ++created; }
    void on(Session&, const PointerReleased&) { ++released; }
    void on(Session&, const KeyboardReleased&) { ++released; }
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0 || !session.seat()) return;
        if (++idles == 1) {
            for (int i = 0; i < 50; ++i) {
                session.queue(event::seat_capabilities(*session.seat(), 0));
                session.queue(event::seat_capabilities(*session.seat(), wire::capability_pointer | wire::capability_keyboard));
                session.queue(event::seat_capabilities(*session.seat(), wire::capability_pointer));
                session.queue(event::seat_capabilities(*session.seat(), wire::capability_keyboard));
            }
            session.queue(event::seat_capabilities(*session.seat(), wire::capability_pointer | wire::capability_keyboard));
        } else {
            pointer_enter(session, on_help_x, on_help_y);
            session.close_window();
        }
    }
    void finish(Session& session) const {
        if (created < 100 || released != created) session.violation("capabilities were not followed: created " + std::to_string(created) + ", released " + std::to_string(released));
    }
};

// The seat goes while a button is down: nothing may stay captured, and the objects must be let go in order.
struct SeatRemovedWhileHeld {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    bool seat_released{};
    std::optional<ObjectId> old_pointer{};
    std::optional<ObjectId> old_keyboard{};
    std::optional<ObjectId> latest{};
    void on(Session& session, const KeyboardCreated&) { send_valid_keymap(session); }
    void on(Session& session, const BufferCommitted& commit) {
        latest = commit.buffer;
        session.release(commit.buffer);
    }
    void on(Session& session, const SeatReleased&) {
        seat_released = true;
        // Late events for what has gone, as a compositor racing the release could send.
        if (old_pointer) session.queue(event::pointer_motion(*old_pointer, 0, Fixed::from_int(1), Fixed::from_int(1)));
        if (old_keyboard) session.queue(event::keyboard_key(*old_keyboard, session.next_serial(), 0, key_return, 1));
    }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ++idles;
        if (idles == 1) {
            old_pointer = session.pointer();
            old_keyboard = session.keyboard();
            pointer_enter(session, on_help_x, on_help_y);
            pointer_button(session, wire::button_left, wire::button_pressed);
        } else if (idles == 2) {
            if (session.registry()) session.queue(event::global_remove(*session.registry(), seat_name));
        } else {
            if (!latest) return;
            const auto pixels = session.pixels_of(*latest);
            constexpr std::size_t on_button = 28 * 640 + 8;
            if (!pixels || pixels->at(on_button) != jgui::theme::button_normal.argb) session.violation("the button still looks pressed after its seat went");
            session.close_window();
        }
    }
    void finish(Session& session) const {
        if (!seat_released || session.pointer() || session.keyboard()) session.violation("the seat's objects were not released");
    }
};

// Missing or too old globals: each is a clean refusal.
struct NoSeat {
    static constexpr ClientEnd end = ends_with(ErrorCode::MissingGlobal, "wl_seat");
    static constexpr GlobalSet globals{.seat = false};
};
struct NoCompositor {
    static constexpr ClientEnd end = ends_with(ErrorCode::MissingGlobal, "wl_compositor");
    static constexpr GlobalSet globals{.compositor = false};
};
struct NoShm {
    static constexpr ClientEnd end = ends_with(ErrorCode::MissingGlobal, "wl_shm");
    static constexpr GlobalSet globals{.shm = false};
};
struct NoWmBase {
    static constexpr ClientEnd end = ends_with(ErrorCode::MissingGlobal, "xdg_wm_base");
    static constexpr GlobalSet globals{.wm_base = false};
};
struct OldCompositor {
    static constexpr ClientEnd end = ends_with(ErrorCode::MissingGlobal, "wl_compositor");
    static constexpr GlobalSet globals{.compositor_version = 3};
};
struct OldSeat {
    static constexpr ClientEnd end = ends_with(ErrorCode::MissingGlobal, "wl_seat");
    static constexpr GlobalSet globals{.seat_version = 4};
};

// --- Keymaps ---

constexpr ClientEnd keymap_refused = ends_with(ErrorCode::Keymap, "keymap");

struct KeymapPipe {
    static constexpr ClientEnd end = keymap_refused;
    UniqueFd writer{};
    void on(Session& session, const KeyboardCreated&) {
        std::array<int, 2> ends{-1, -1};
        if (::pipe(ends.data()) != 0) {
            session.violation("no pipe");
            return;
        }
        writer = UniqueFd{ends[1]};
        send_keymap(session, wire::keymap_xkb_v1, UniqueFd{ends[0]}, 64);
    }
};
struct KeymapDirectory {
    static constexpr ClientEnd end = keymap_refused;
    void on(Session& session, const KeyboardCreated&) { send_keymap(session, wire::keymap_xkb_v1, UniqueFd{::open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC)}, 64); }
};
struct KeymapShort {
    static constexpr ClientEnd end = keymap_refused;
    void on(Session& session, const KeyboardCreated&) { send_keymap(session, wire::keymap_xkb_v1, memfd_holding("short"), 4096); }
};
struct KeymapNoTerminator {
    static constexpr ClientEnd end = keymap_refused;
    void on(Session& session, const KeyboardCreated&) {
        const std::string text(std::size_t{1} << 20U, 'x');
        send_keymap(session, wire::keymap_xkb_v1, memfd_holding(text), static_cast<std::uint32_t>(text.size()));
    }
};
struct KeymapHugeSize {
    static constexpr ClientEnd end = keymap_refused;
    void on(Session& session, const KeyboardCreated&) { send_keymap(session, wire::keymap_xkb_v1, memfd_holding("x"), uint32_most); }
};
struct KeymapFormat7 {
    static constexpr ClientEnd end = keymap_refused;
    void on(Session& session, const KeyboardCreated&) { send_keymap(session, 7, memfd_holding(std::string_view{"x\0", 2}), 2); }
};

// A keyboard with no keymap has no keys: they arrive and nothing happens.
struct KeymapNone {
    static constexpr ClientEnd end = ends_clean;
    void on(Session& session, const KeyboardCreated&) { send_keymap(session, wire::keymap_no_keymap, memfd_holding(""), 0); }
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (session.keyboard()) session.queue(event::keyboard_key(*session.keyboard(), session.next_serial(), 0, key_return, 1));
        session.close_window();
    }
};

// A good keymap, then every extreme a key or modifier message can hold.
struct KeysExtreme {
    static constexpr ClientEnd end = ends_clean;
    void on(Session& session, const KeyboardCreated&) { send_valid_keymap(session); }
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (!session.keyboard()) return;
        const ObjectId keyboard = *session.keyboard();
        for (const std::uint32_t key : {0U, 1U, key_return, 247U, 248U, 65535U, uint32_most - 8U, uint32_most - 7U, uint32_most}) {
            for (const std::uint32_t state : {0U, 1U, 2U, 7U, uint32_most}) session.queue(event::keyboard_key(keyboard, session.next_serial(), uint32_most, key, state));
        }
        for (const std::uint32_t mask : {0U, 1U, 0xFFU, 0x80000000U, uint32_most}) {
            session.queue(event::keyboard_modifiers(keyboard, session.next_serial(), mask, mask, mask, mask));
            session.queue(event::keyboard_key(keyboard, session.next_serial(), 0, key_return, 1));
        }
        session.close_window();
    }
};

// --- The pointer ---

struct PointerExtremes {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0 || !session.pointer() || !session.window_surface()) return;
        if (++idles > 1) {
            session.close_window();
            return;
        }
        const ObjectId pointer = *session.pointer();
        const ObjectId surface = *session.window_surface();
        // Leaving with no enter first, and twice.
        session.queue(event::pointer_leave(pointer, session.next_serial(), surface));
        session.queue(event::pointer_leave(pointer, session.next_serial(), surface));
        for (const std::int32_t x : {int32_most, int32_least, 0, -1, -256, 256}) {
            for (const std::int32_t y : {int32_most, int32_least, 0, -1}) {
                session.pointer_enter(Fixed{x}, Fixed{y});
                session.queue(event::pointer_motion(pointer, uint32_most, Fixed{y}, Fixed{x}));
                session.queue(event::pointer_button(pointer, session.next_serial(), 0, uint32_most, 9));
                session.queue(event::pointer_button(pointer, session.next_serial(), 0, 0, 1));
                session.queue(event::pointer_button(pointer, session.next_serial(), 0, wire::button_left, 1));
                session.queue(event::pointer_button(pointer, session.next_serial(), 0, wire::button_left, uint32_most));
                session.queue(event::pointer_frame(pointer));
            }
        }
        for (std::int32_t i = 0; i < 2000; ++i) session.queue(event::pointer_motion(pointer, 0, Fixed::from_int(i % 640), Fixed::from_int(i % 480)));
        // None of that may have left a press behind that a release would turn into a click.
        session.queue(event::pointer_leave(pointer, session.next_serial(), surface));
    }
};

// Events of versions this client never bound, which libwayland passes on without looking at the version. With an empty
// entry in the listener, this ended the whole program in an abort, which fuzzing found.
struct EventsOfLaterVersions {
    static constexpr ClientEnd end = ends_clean;
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (session.toplevel()) {
            session.queue(wire::MessageWriter{*session.toplevel(), 2}.i32(800).i32(600).finish());
            session.queue(wire::MessageWriter{*session.toplevel(), 3}.array(std::span<const std::uint32_t>{}).finish());
        }
        if (session.pointer()) {
            session.queue(wire::MessageWriter{*session.pointer(), 9}.u32(0).i32(120).finish());
            session.queue(wire::MessageWriter{*session.pointer(), 10}.u32(0).u32(1).finish());
            session.queue(wire::MessageWriter{*session.pointer(), 11}.i32(0).i32(0).finish());
        }
        session.close_window();
    }
};

// More motion at once than the toolkit's queue holds. It merges, so this always ends cleanly.
void flood_with_motion(Session& session, std::int32_t count) {
    for (std::int32_t i = 0; i < count; ++i) session.queue(event::pointer_motion(*session.pointer(), 0, Fixed::from_int(nowhere_x), Fixed::from_int(nowhere_y)));
}

struct EventFlood {
    static constexpr ClientEnd end = ends_clean;
    static constexpr std::int32_t count = 70000;
    bool flooded{};
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (flooded || !session.pointer()) return;
        flooded = true;
        flood_with_motion(session, count);
        session.close_window();
    }
};

// A flood that arrives during the sync after the first frame. Nothing drains the queue there, and the answer to the
// sync comes after every event sent here, because the client commits and then syncs. So no event is ever delivered
// until the sync ends, and the outcome does not depend on timing: without merging, this always reaches the limit.
struct EventFloodInSync {
    static constexpr ClientEnd end = ends_clean;
    static constexpr bool first_frame_only = true;
    bool flooded{};
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (flooded || !session.pointer()) return;
        flooded = true;
        flood_with_motion(session, EventFlood::count);
    }
};

// Motion with a press between each, so nothing can merge, during that same sync. More events than the limit come before
// the answer, so the limit is always reached, and the one right ending is its Error.
struct EventFloodMixed {
    static constexpr ClientEnd end = ends_with(ErrorCode::SizeLimit, "queue limit");
    static constexpr bool first_frame_only = true;
    bool flooded{};
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (flooded || !session.pointer()) return;
        flooded = true;
        for (std::int32_t i = 0; i < EventFlood::count; ++i) {
            session.queue(event::pointer_motion(*session.pointer(), 0, Fixed::from_int(nowhere_x), Fixed::from_int(nowhere_y)));
            pointer_button(session, wire::button_left, wire::button_pressed);
        }
    }
};

// --- The connection going wrong ---

constexpr ClientEnd lost_the_display = ends_with(ErrorCode::Connect, "connection");

struct DisconnectAtOnce {
    static constexpr ClientEnd end = ends_with_some_error;
    void on(Session& session, const SeatBound&) { session.hang_up(); }
};
struct DisconnectAfterFrame {
    static constexpr ClientEnd end = lost_the_display;
    void on(Session& session, const BufferCommitted&) { session.hang_up(); }
};

template <std::uint32_t Size, std::uint32_t Object = 1, std::uint32_t Opcode = 0>
struct RawHeader {
    static constexpr ClientEnd end = ends_with_some_error;
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        std::vector<std::byte> bytes;
        wire::append_u32(bytes, Object);
        wire::append_u32(bytes, (Size << 16U) | Opcode);
        session.queue(std::move(bytes));
    }
    // A header that promises more than ever comes would wait for it, so the compositor goes.
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() > 0) session.hang_up();
    }
};

// Part of a message, then the end of the connection, while the client sleeps in the event loop. This found a real bug:
// libwayland never reported the end, and the client spun at full speed for ever.
struct TruncatedThenGone {
    static constexpr ClientEnd end = ends_with(ErrorCode::Connect, "closed the connection");
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        std::vector<std::byte> bytes;
        wire::append_u32(bytes, 1);
        wire::append_u32(bytes, 16U << 16U);
        session.queue(std::move(bytes));
        session.hang_up();
    }
};

// The same while the client waits for the first roundtrip, in connect.
struct TruncatedDuringConnect {
    static constexpr ClientEnd end = ends_with(ErrorCode::Connect, "closed the connection");
    void on(Session& session, const SeatBound&) {
        std::vector<std::byte> bytes;
        wire::append_u32(bytes, 1);
        wire::append_u32(bytes, 16U << 16U);
        session.queue(std::move(bytes));
        session.hang_up();
    }
};

// And in the roundtrip after the first frame.
struct TruncatedDuringFirstFrame {
    static constexpr ClientEnd end = ends_with(ErrorCode::Connect, "closed the connection");
    static constexpr bool first_frame_only = true;
    void on(Session& session, const BufferCommitted&) {
        std::vector<std::byte> bytes;
        wire::append_u32(bytes, 1);
        wire::append_u32(bytes, 16U << 16U);
        session.queue(std::move(bytes));
        session.hang_up();
    }
};

// An event for an object that never was is dropped by libwayland, and the client lives.
struct UnknownObject {
    static constexpr ClientEnd end = ends_clean;
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        std::vector<std::byte> bytes;
        wire::append_u32(bytes, 0xDEAD);
        wire::append_u32(bytes, 8U << 16U);
        session.queue(std::move(bytes));
        session.close_window();
    }
};

// Arguments that do not fit what the event declares.
struct ArgumentsTooShort {
    static constexpr ClientEnd end = ends_with_some_error;
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (session.toplevel()) session.queue(wire::MessageWriter{*session.toplevel(), 0}.i32(1).finish());
    }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() > 0) session.hang_up();
    }
};
struct StringNotTerminated {
    static constexpr ClientEnd end = ends_with_some_error;
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (session.seat()) session.queue(wire::MessageWriter{*session.seat(), 1}.u32(4).u32(0x41414141).finish());
    }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() > 0) session.hang_up();
    }
};
struct KeymapWithNoDescriptor {
    static constexpr ClientEnd end = ends_with_some_error;
    void on(Session& session, const KeyboardCreated&) {
        if (session.keyboard()) session.queue(event::keyboard_keymap(*session.keyboard(), wire::keymap_xkb_v1, 16));
    }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() > 0) session.hang_up();
    }
};

// The compositor reports a protocol error on an object: connection_error's EPROTO path, with the interface's name.
struct ProtocolErrorOnSurface {
    static constexpr ClientEnd end = ends_with(ErrorCode::Protocol, "protocol error 3 on xdg_surface");
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (session.xdg_surface()) session.queue(event::display_error(*session.xdg_surface(), 3, "a test of the error path"));
    }
};
// On wl_display itself, libwayland turns the codes into errno values, so this is not EPROTO.
struct ProtocolErrorOnDisplay {
    static constexpr ClientEnd end = ends_with(ErrorCode::Connect, "display connection failed");
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        session.queue(event::display_error(wire::display_id, 1, "a test of the error path"));
    }
};
// An error on an object that was never made.
struct ProtocolErrorOnNothing {
    static constexpr ClientEnd end = ends_with_some_error;
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        session.queue(event::display_error(ObjectId{0xDEAD}, 5, "a test of the error path"));
    }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() > 0) session.hang_up();
    }
};

// A real EINTR in poll: a timer interrupts the client's sleep, again and again, with no automatic restart.
struct SignalStorm {
    static constexpr ClientEnd end = ends_clean;
    std::size_t frames{};
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (++frames == 20) {
            session.close_window();
        } else {
            session.configure(300 + static_cast<std::int32_t>(frames), 300);
        }
    }
    // The compositor waits a while each time, so that the client's poll really is asleep when the signals land.
    void on(Session&, const ConfigureAcked&) { std::this_thread::sleep_for(std::chrono::milliseconds{3}); }
};

volatile std::sig_atomic_t signals_seen = 0;
void count_signal(int) { signals_seen = signals_seen + 1; }

void start_signal_storm() {
    // poll is never restarted after a signal, with or without SA_RESTART: a sleeping one returns EINTR.
    static_cast<void>(std::signal(SIGALRM, &count_signal));
    itimerval timer{};
    timer.it_interval.tv_usec = 500;
    timer.it_value.tv_usec = 500;
    ::setitimer(ITIMER_REAL, &timer, nullptr);
}

// --- What the pointer looks like, through the real backend ---

// Every edge and corner, and the middle, in one go with each said twice, because only a change may reach the
// compositor. Then out and back in, in the same region, which the compositor has forgotten, so the same shape goes
// again. Then out and back in on a corner, which must be right at once. Each with the new enter's serial.
struct CursorAllEdges {
    static constexpr ClientEnd end = ends_clean;
    static constexpr std::array<std::array<std::int32_t, 3>, 10> stops{{
        {300, 1, static_cast<std::int32_t>(wire::shape_n_resize)},
        {300, 478, static_cast<std::int32_t>(wire::shape_s_resize)},
        {1, 240, static_cast<std::int32_t>(wire::shape_w_resize)},
        {638, 240, static_cast<std::int32_t>(wire::shape_e_resize)},
        {1, 1, static_cast<std::int32_t>(wire::shape_nw_resize)},
        {638, 1, static_cast<std::int32_t>(wire::shape_ne_resize)},
        {1, 478, static_cast<std::int32_t>(wire::shape_sw_resize)},
        {638, 478, static_cast<std::int32_t>(wire::shape_se_resize)},
        {300, 300, static_cast<std::int32_t>(wire::shape_default)},
        {on_title_x, on_title_y + 5, static_cast<std::int32_t>(wire::shape_default)},
    }};
    std::size_t idles{};
    PacedSteps paced{};
    // Each stop twice, in deliveries of their own, so the second shows that only a change is sent.
    [[nodiscard]] static auto step(Session& session) {
        return [&session](std::size_t i) { pointer_motion(session, stops[i / 2][0], stops[i / 2][1]); };
    }
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Pong&) { paced.on_answer(session, step(session)); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0 || !paced.done()) return;
        ++idles;
        if (idles == 1) {
            pointer_enter(session, nowhere_x, nowhere_y);
            paced.run_until(session, stops.size() * 2, step(session));
        } else if (idles == 2) {
            session.pointer_leave();
            pointer_enter(session, nowhere_x, nowhere_y);
        } else if (idles == 3) {
            session.pointer_leave();
            pointer_enter(session, 1, 1);
        } else {
            session.close_window();
        }
    }
    void finish(Session& session) const {
        std::vector<std::uint32_t> wanted{wire::shape_default};
        for (const auto& stop : stops) {
            const auto shape = static_cast<std::uint32_t>(stop[2]);
            if (wanted.back() != shape) wanted.push_back(shape);
        }
        wanted.push_back(wire::shape_default);
        wanted.push_back(wire::shape_nw_resize);
        if (session.shapes() != wanted) session.violation("the shapes that arrived are not those of the regions crossed, once each");
        if (session.set_cursors() != 0) session.violation("the drawn arrow was used beside the shapes");
    }
};

// A compositor without the protocol: the drawn arrow at each enter, nothing else, and no failure.
struct CursorNoProtocol {
    static constexpr ClientEnd end = ends_clean;
    static constexpr GlobalSet globals{.cursor_shape = false};
    std::size_t idles{};
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ++idles;
        if (idles == 1) {
            pointer_enter(session, on_left_edge_x, on_left_edge_y);
            pointer_motion(session, nowhere_x, nowhere_y);
            pointer_motion(session, on_left_edge_x, on_left_edge_y);
        } else {
            session.close_window();
        }
    }
    void finish(Session& session) const {
        if (session.set_cursors() != 1) session.violation("the drawn arrow must be set once, at the enter");
        if (!session.shapes().empty()) session.violation("a shape was asked for with no such protocol");
    }
};

// The seat goes with a shape set: the device goes before the pointer, which the compositor checks, and the window goes on.
struct CursorSeatRemoved {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    bool device_was_there{};
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ++idles;
        if (idles == 1) {
            pointer_enter(session, on_left_edge_x, on_left_edge_y);
        } else if (idles == 2) {
            device_was_there = session.cursor_device().has_value();
            if (session.registry()) session.queue(event::global_remove(*session.registry(), seat_name));
        } else if (idles == 3) {
            if (session.cursor_device()) session.violation("the device outlived the seat");
            session.configure(500, 400);
        } else {
            session.close_window();
        }
    }
    void finish(Session& session) const {
        if (!device_was_there) session.violation("there was no device to begin with");
        if (session.shapes() != std::vector<std::uint32_t>{wire::shape_w_resize}) session.violation("the shape before the seat went did not arrive");
    }
};

// The seat loses its pointer and gets a new one. The old device goes first, and the new one belongs to the new pointer.
struct CursorPointerFlap {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0 || !session.seat()) return;
        ++idles;
        if (idles == 1) {
            pointer_enter(session, on_left_edge_x, on_left_edge_y);
        } else if (idles == 2) {
            session.queue(event::seat_capabilities(*session.seat(), wire::capability_keyboard));
        } else if (idles == 3) {
            if (session.cursor_device()) session.violation("the device outlived its pointer");
            session.queue(event::seat_capabilities(*session.seat(), wire::capability_pointer | wire::capability_keyboard));
        } else if (idles == 4) {
            pointer_enter(session, 638, 240);
        } else {
            session.close_window();
        }
    }
    void finish(Session& session) const {
        if (session.shapes() != std::vector<std::uint32_t>{wire::shape_w_resize, wire::shape_e_resize}) session.violation("each pointer did not get its own shape");
    }
};

// The compositor takes the protocol away: back to the drawn arrow, at once and at each enter after.
struct CursorManagerRemoved {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ++idles;
        if (idles == 1) {
            pointer_enter(session, on_left_edge_x, on_left_edge_y);
        } else if (idles == 2) {
            if (session.registry()) session.queue(event::global_remove(*session.registry(), cursor_shape_name));
        } else if (idles == 3) {
            if (session.cursor_device() || session.cursor_manager()) session.violation("the protocol's objects outlived its global");
            session.pointer_leave();
            pointer_enter(session, 638, 240);
        } else {
            session.close_window();
        }
    }
    void finish(Session& session) const {
        if (session.shapes() != std::vector<std::uint32_t>{wire::shape_w_resize}) session.violation("a shape was asked for after the protocol went");
        if (session.set_cursors() != 2) session.violation("the drawn arrow must come back, at once and at the next enter");
    }
};

// The protocol's global comes after the pointer already exists: from then on it is shapes, and not the arrow.
struct CursorManagerLate {
    static constexpr ClientEnd end = ends_clean;
    static constexpr GlobalSet globals{.cursor_shape = false};
    std::size_t idles{};
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ++idles;
        if (idles == 1) {
            pointer_enter(session, on_left_edge_x, on_left_edge_y);
        } else if (idles == 2) {
            if (session.registry()) session.queue(event::global(*session.registry(), cursor_shape_name, "wp_cursor_shape_manager_v1", 1));
        } else if (idles == 3) {
            if (!session.cursor_device()) session.violation("no device for the pointer that was already there");
            session.pointer_leave();
            pointer_enter(session, 638, 240);
        } else {
            session.close_window();
        }
    }
    void finish(Session& session) const {
        if (session.set_cursors() != 1) session.violation("the arrow must be used only before the protocol came");
        if (session.shapes() != std::vector<std::uint32_t>{wire::shape_e_resize}) session.violation("no shape after the protocol came");
    }
};

// A press held on Help and dragged across the edges: no change, until the release says where it ended.
struct CursorDuringDrag {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ++idles;
        if (idles == 1) {
            pointer_enter(session, on_help_x, on_help_y);
            pointer_button(session, wire::button_left, wire::button_pressed);
            pointer_motion(session, on_left_edge_x, on_left_edge_y);
            pointer_motion(session, 1, 1);
            pointer_motion(session, on_left_edge_x, on_left_edge_y);
        } else if (idles == 2) {
            if (session.shapes() != std::vector<std::uint32_t>{wire::shape_default}) session.violation("the shape changed during a drag");
            pointer_button(session, wire::button_left, wire::button_released);
        } else {
            session.close_window();
        }
    }
    void finish(Session& session) const {
        if (session.shapes() != std::vector<std::uint32_t>{wire::shape_default, wire::shape_w_resize}) session.violation("the release over an edge did not bring its shape");
    }
};

// --- The data grid, through the real backend ---

[[nodiscard]] std::expected<jgui::WaylandBackend, jgui::Error> test_backend();

// A grid of a thousand rows in the frame, as in a program. The script sits in this process, and the client in a child, so
// what each script wants of the grid's end state is asked in the child, by the script's own static function.
struct GridTag;
struct GridLine {
    int number{};
    std::string name{};
};
using WireGrid = jgui::DataGrid<GridTag, GridLine, jgui::NoGridMessage, jgui::Column<"Number", &GridLine::number, 100>, jgui::Column<"Name", &GridLine::name, 100>>;
constexpr std::int32_t grid_rows = 1000;

template <class Script>
[[nodiscard]] int run_grid_client() {
    auto backend = test_backend();
    if (!backend) return 1;
    auto app = jgui::make_application(std::move(*backend), jgui::window(jgui::window_frame(WireGrid{})));
    if (!app) return 1;
    std::vector<GridLine> lines;
    for (std::int32_t i = 0; i < grid_rows; ++i) lines.push_back(GridLine{i, "row " + std::to_string(i)});
    if (!app->ui().get<GridTag>().set_rows(std::move(lines))) return 1;
    if (!ended_as_expected(Script::end, app->run())) return 1;
    if (!Script::grid_is_right(app->ui().get<GridTag>())) {
        std::cerr << "the grid ended in the wrong state, scrolled " << jgui_test::number(app->ui().get<GridTag>().scroll_down()) << '\n';
        return 1;
    }
    return 0;
}

constexpr std::int32_t in_the_body_x = 400;
constexpr std::int32_t in_the_body_y = 200;
constexpr std::int32_t in_the_bar_x = 630;  // the bar is the last 12 of a client area ending at 636

void scroll_frame_of_steps(Session& session, std::int32_t steps) {
    if (!session.pointer()) return;
    session.queue(event::pointer_axis_source(*session.pointer(), wire::axis_source_wheel));
    session.queue(event::pointer_axis_discrete(*session.pointer(), wire::axis_vertical, steps));
    session.queue(event::pointer_axis(*session.pointer(), 0, wire::axis_vertical, Fixed::from_int(steps * 15)));
    session.queue(event::pointer_frame(*session.pointer()));
}

void scroll_frame_of_pixels(Session& session, Fixed pixels) {
    if (!session.pointer()) return;
    session.queue(event::pointer_axis_source(*session.pointer(), wire::axis_source_finger));
    session.queue(event::pointer_axis(*session.pointer(), 0, wire::axis_vertical, pixels));
    session.queue(event::pointer_frame(*session.pointer()));
}

// The first row, going down one column of a 640 wide picture, in this colour, and how many follow it. Both zero when there is none.
struct ColourRun {
    std::int32_t first{};
    std::int32_t length{};
};
[[nodiscard]] ColourRun colour_run_at(std::span<const std::uint32_t> pixels, std::int32_t x, std::uint32_t colour) {
    constexpr std::size_t stride = 640;
    ColourRun run;
    const std::size_t rows = pixels.size() / stride;
    for (std::size_t y = 0; y < rows; ++y) {
        const bool here = pixels[y * stride + static_cast<std::size_t>(x)] == colour;
        if (here && run.length == 0) run.first = static_cast<std::int32_t>(y);
        if (here) ++run.length;
        if (!here && run.length != 0) break;
    }
    return run;
}

// Values no compositor should send, each through the whole path, then a wheel going down until there is no further to go.
struct GridAxisHostile {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0 || !session.pointer()) return;
        ++idles;
        const ObjectId pointer = *session.pointer();
        if (idles == 1) {
            pointer_enter(session, in_the_body_x, in_the_body_y);
            for (const std::int32_t value : {int32_least, int32_most, 0, -1, 1, int32_most, int32_least}) {
                scroll_frame_of_pixels(session, Fixed{value});
                session.queue(event::pointer_axis_discrete(pointer, wire::axis_vertical, value));
                session.queue(event::pointer_axis_discrete(pointer, wire::axis_horizontal, value));
                session.queue(event::pointer_frame(pointer));
                session.queue(event::pointer_axis(pointer, 0, uint32_most, Fixed{value}));
                session.queue(event::pointer_axis_discrete(pointer, 7, value));
                session.queue(event::pointer_axis_stop(pointer, 0, uint32_most));
                session.queue(event::pointer_axis_source(pointer, uint32_most));
                session.queue(event::pointer_frame(pointer));
            }
        } else if (idles == 2) {
            // A flood, and many axis events in one frame: the total stops at what one event can ask for.
            for (std::int32_t i = 0; i < 3000; ++i) session.queue(event::pointer_axis(pointer, 0, wire::axis_vertical, Fixed{int32_most}));
            session.queue(event::pointer_frame(pointer));
            for (std::int32_t i = 0; i < 2000; ++i) scroll_frame_of_steps(session, 1);
        } else {
            session.close_window();
        }
    }
    [[nodiscard]] static bool grid_is_right(const WireGrid& grid) {
        const jgui::Height last = repeated_extent(grid.row_height(), static_cast<std::size_t>(grid_rows)) - grid.body_area().height();
        return grid.scroll_down() == last;
    }
};

// Scrolling with the pointer never in the window, or after it has gone, moves nothing.
struct GridAxisNoEnter {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    std::size_t commits_before{};
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ++idles;
        if (idles == 1) {
            commits_before = session.commits_with_buffer();
            scroll_frame_of_steps(session, 3);
            scroll_frame_of_pixels(session, Fixed::from_int(100));
            pointer_enter(session, in_the_body_x, in_the_body_y);
            session.pointer_leave();
            scroll_frame_of_steps(session, 3);
        } else {
            if (session.commits_with_buffer() != commits_before) session.violation("scrolling with no pointer here drew a frame");
            session.close_window();
        }
    }
    [[nodiscard]] static bool grid_is_right(const WireGrid& grid) { return grid.scroll_down() == jgui::Height{}; }
};

// A click, a wheel notch, then smooth pixels, and half pixels that add up, each read back from the pixels on the wire: a selected
// row's lit band moves up exactly as far as it should.
struct GridWheelThenPixels {
    static constexpr ClientEnd end = ends_clean;
    enum class Step : std::uint8_t { First, Clicked, Wheeled, Smooth, HalfOnce, HalfTwice, Done };
    Step step{Step::First};
    std::optional<ObjectId> latest{};
    std::int32_t row_height{};
    std::int32_t band_top{};
    [[nodiscard]] ColourRun band(Session& session) const {
        if (!latest) return ColourRun{};
        const auto pixels = session.pixels_of(*latest);
        return pixels ? colour_run_at(*pixels, in_the_body_x, jgui::theme::button_pressed.argb) : ColourRun{};
    }
    void on(Session& session, const BufferCommitted& commit) {
        if (latest) session.release(*latest);
        latest = commit.buffer;
    }
    void expect_moved(Session& session, std::int32_t by, const char* what) {
        const ColourRun now = band(session);
        if (now.length == 0 || band_top - now.first != by) session.violation(std::string{what} + ": it went from " + std::to_string(band_top) + " to " + std::to_string(now.first));
        band_top = now.first;
    }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        switch (step) {
            case Step::First:
                pointer_enter(session, in_the_body_x, in_the_body_y);
                left_click(session, in_the_body_x, in_the_body_y);
                step = Step::Clicked;
                return;
            case Step::Clicked: {
                const ColourRun lit = band(session);
                if (lit.length == 0) session.violation("the click lit no row");
                band_top = lit.first;
                row_height = lit.length + 1;  // the last line of a row is its line
                scroll_frame_of_steps(session, 1);
                step = Step::Wheeled;
                return;
            }
            case Step::Wheeled:
                expect_moved(session, row_height * 3, "one notch is three rows");
                scroll_frame_of_pixels(session, Fixed::from_int(10));
                step = Step::Smooth;
                return;
            case Step::Smooth:
                expect_moved(session, 10, "smooth scrolling goes by its pixels");
                scroll_frame_of_pixels(session, Fixed{128});
                step = Step::HalfOnce;
                return;
            case Step::HalfOnce:
                expect_moved(session, 0, "half a pixel moves nothing yet");
                scroll_frame_of_pixels(session, Fixed{128});
                step = Step::HalfTwice;
                return;
            case Step::HalfTwice:
                expect_moved(session, 1, "and the second half makes one");
                session.close_window();
                step = Step::Done;
                return;
            case Step::Done:
            default:
                return;
        }
    }
    [[nodiscard]] static bool grid_is_right(const WireGrid& grid) {
        return grid.scroll_down() == repeated_extent(grid.row_height(), 3) + jgui::Height{11} && grid.selected().has_value();
    }
};

// The window shrinks and then grows with the thumb held, and the pointer goes on dragging.
struct GridResizeDuringThumbDrag {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    std::optional<ObjectId> latest{};
    std::int32_t thumb_y{};
    void on(Session& session, const BufferCommitted& commit) {
        if (latest) session.release(*latest);
        latest = commit.buffer;
    }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ++idles;
        if (idles == 1) {
            const auto pixels = session.pixels_of(*latest);
            const ColourRun thumb = pixels ? colour_run_at(*pixels, in_the_bar_x, jgui::theme::button_edge.argb) : ColourRun{};
            if (thumb.length == 0) session.violation("no thumb was drawn");
            thumb_y = thumb.first + 1;
            pointer_enter(session, in_the_bar_x, thumb_y);
            pointer_button(session, wire::button_left, wire::button_pressed);
            pointer_motion(session, in_the_bar_x, thumb_y + 200);
        } else if (idles == 2) {
            // Each size is drawn before the next thing happens, so the grid really has been that size.
            session.configure(300, 200);
        } else if (idles == 3) {
            pointer_motion(session, in_the_bar_x, 150);
        } else if (idles == 4) {
            session.configure(1, 1);
        } else if (idles == 5) {
            pointer_motion(session, in_the_bar_x, 150);
        } else if (idles == 6) {
            session.configure(640, 480);
        } else if (idles == 7) {
            pointer_motion(session, in_the_bar_x, 470);
            pointer_button(session, wire::button_left, wire::button_released);
        } else if (idles == 8) {
            pointer_motion(session, in_the_body_x, in_the_body_y);
            session.close_window();
        }
    }
    [[nodiscard]] static bool grid_is_right(const WireGrid& grid) {
        const jgui::Height last = repeated_extent(grid.row_height(), static_cast<std::size_t>(grid_rows)) - grid.body_area().height();
        return grid.scroll_down() == last;
    }
};

// The pointer leaves in the middle of a thumb drag and comes back: the drag is over.
struct GridPointerGoneDuringCapture {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    std::optional<ObjectId> latest{};
    void on(Session& session, const BufferCommitted& commit) {
        if (latest) session.release(*latest);
        latest = commit.buffer;
    }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ++idles;
        if (idles == 1) {
            const auto pixels = session.pixels_of(*latest);
            const ColourRun thumb = pixels ? colour_run_at(*pixels, in_the_bar_x, jgui::theme::button_edge.argb) : ColourRun{};
            pointer_enter(session, in_the_bar_x, thumb.first + 1);
            pointer_button(session, wire::button_left, wire::button_pressed);
            session.pointer_leave();
            pointer_enter(session, in_the_bar_x, 400);
            pointer_motion(session, in_the_bar_x, 410);
            pointer_button(session, wire::button_left, wire::button_released);
        } else {
            session.close_window();
        }
    }
    [[nodiscard]] static bool grid_is_right(const WireGrid& grid) { return grid.scroll_down() == jgui::Height{}; }
};

// The seat loses its pointer in the middle of a column drag, then gets one back.
struct GridSeatGoneDuringCapture {
    static constexpr ClientEnd end = ends_clean;
    std::size_t idles{};
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0 || !session.seat()) return;
        ++idles;
        if (idles == 1) {
            pointer_enter(session, 104, 26);  // the first boundary, in the header
            pointer_button(session, wire::button_left, wire::button_pressed);
            pointer_motion(session, 200, 26);
            session.queue(event::seat_capabilities(*session.seat(), wire::capability_keyboard));
        } else if (idles == 2) {
            session.queue(event::seat_capabilities(*session.seat(), wire::capability_pointer | wire::capability_keyboard));
        } else if (idles == 3) {
            pointer_enter(session, 300, 26);
            pointer_motion(session, 400, 26);
        } else {
            session.close_window();
        }
    }
    [[nodiscard]] static bool grid_is_right(const WireGrid& grid) { return grid.column_width(0) == jgui::Width{196}; }
};

// The column boundary's shape over a boundary, and nowhere else, with the frame's edges as they were.
struct GridCursor {
    static constexpr ClientEnd end = ends_clean;
    static constexpr std::size_t steps_before_the_release_is_checked = 7;
    static constexpr std::size_t all_steps = 9;
    std::size_t idles{};
    PacedSteps paced{};
    [[nodiscard]] static auto step(Session& session) {
        return [&session](std::size_t i) {
            switch (i) {
                case 0:
                case 1: pointer_motion(session, 104, 26); break;
                case 2: pointer_motion(session, 104, in_the_body_y); break;
                case 3: pointer_motion(session, on_left_edge_x, on_left_edge_y); break;
                case 4:
                    pointer_motion(session, 104, 26);
                    pointer_button(session, wire::button_left, wire::button_pressed);
                    break;
                case 5: pointer_motion(session, 150, 300); break;
                case 6:
                    pointer_motion(session, 150, 26);
                    pointer_button(session, wire::button_left, wire::button_released);
                    break;
                // Still on the boundary, which came along with the drag: the release must have kept its shape, so this sends nothing.
                case 7: pointer_motion(session, 150, 26); break;
                default: pointer_motion(session, in_the_body_x, in_the_body_y); break;
            }
        };
    }
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Pong&) { paced.on_answer(session, step(session)); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0 || !paced.done()) return;
        ++idles;
        if (idles == 1) {
            pointer_enter(session, in_the_body_x, in_the_body_y);
            paced.run_until(session, steps_before_the_release_is_checked, step(session));
        } else if (idles == 2) {
            paced.run_until(session, all_steps, step(session));
        } else {
            session.close_window();
        }
    }
    void finish(Session& session) const {
        const std::vector<std::uint32_t> wanted{wire::shape_default, wire::shape_col_resize, wire::shape_default, wire::shape_w_resize, wire::shape_col_resize, wire::shape_default};
        if (session.shapes() != wanted) session.violation("the shapes that arrived are not those of the places crossed, once each");
    }
    [[nodiscard]] static bool grid_is_right(const WireGrid& grid) { return grid.column_width(0) == jgui::Width{146}; }
};

// --- The redraw monitor, through the real backend ---

// Real frames, each for its own reason, and a look at what they recorded and at the file, from inside the client.
struct MonitorRealFrames {
    static constexpr ClientEnd end = ends_clean;
    static constexpr bool monitored = true;
    std::size_t idles{};
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ++idles;
        if (idles == 1) {
            pointer_enter(session, on_help_x, on_help_y);
            left_click(session, on_help_x, on_help_y);
        } else if (idles == 2) {
            session.configure(500, 400);
        } else {
            session.close_window();
        }
    }
    template <class Monitor>
    [[nodiscard]] static bool monitor_is_right(const Monitor& monitor, const std::string& text) {
        const auto& frames = monitor.frames();
        if (frames.size() < 3) return false;
        if (frames[0].reason != jgui::RedrawReason::FirstFrame || frames[frames.size() - 1].reason != jgui::RedrawReason::Resize) return false;
        if (frames[frames.size() - 1].area.size() != jgui::Size{jgui::Width{500}, jgui::Height{400}}) return false;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const jgui::FrameRow& row = frames[i];
            if (row.frame_id != i + 1 || row.layout_ns <= 0 || row.acquire_ns <= 0 || row.paint_ns <= 0 || row.hash_ns <= 0 || row.present_ns <= 0) return false;
        }
        bool panel_named = false;
        for (std::size_t i = 0; i < monitor.damage().size(); ++i) panel_named = panel_named || monitor.damage()[i].widget.class_name == "demo::HelpPanel";
        if (!panel_named || monitor.error().has_value()) return false;
        return jgui_test::blocks_all_close(text) && jgui_test::rows_of_section(text, "Frames").size() == frames.size() &&
               jgui_test::rows_of_section(text, "Damage").size() == monitor.damage().size();
    }
};

// The compositor holds every buffer. The fifth frame must wait, and each time it does is a row.
struct MonitorNeverRelease {
    static constexpr ClientEnd end = NeverRelease::end;
    static constexpr bool monitored = true;
    NeverRelease script{};
    template <class Event>
    void on(Session& session, const Event& what)
        requires requires(NeverRelease& inner) { inner.on(session, what); }
    {
        script.on(session, what);
    }
    void finish(Session& session) const { script.finish(session); }
    template <class Monitor>
    [[nodiscard]] static bool monitor_is_right(const Monitor& monitor, const std::string& text) {
        if (monitor.frames().size() != 4 || monitor.deferred_rows().size() == 0) return false;
        for (std::size_t i = 0; i < monitor.deferred_rows().size(); ++i) {
            const jgui::DeferredRow& row = monitor.deferred_rows()[i];
            if (row.next_frame_id != 5 || row.reason != jgui::RedrawReason::Resize) return false;
        }
        return jgui_test::blocks_all_close(text) && jgui_test::rows_of_section(text, "Deferred").size() == monitor.deferred_rows().size();
    }
};

// The client cannot write its file: a directory stands there. The window behaves as ever.
struct MonitorCannotWrite {
    static constexpr ClientEnd end = ends_clean;
    static constexpr bool monitored = true;
    static constexpr bool file_is_blocked = true;
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        session.close_window();
    }
    template <class Monitor>
    [[nodiscard]] static bool monitor_is_right(const Monitor& monitor, const std::string&) {
        return monitor.frames().size() == 1 && monitor.error().has_value() && monitor.error()->message.contains("not a regular file");
    }
};

// The real demo program, started from a working directory of its own and told a false argv[0], with --redraw-monitor. It
// draws its frame and is told to close. Under test is where its file lands, and that a first frame is reached either way.
enum class DemoPlace : std::uint8_t { AsBuilt, CopyUnderABase, CopyWithNoBase };
template <DemoPlace Place>
struct RealDemo {
    static constexpr ClientEnd end = ends_clean;
    static constexpr DemoPlace place = Place;
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        session.close_window();
    }
    void finish(Session& session) const {
        if (session.commits_with_buffer() == 0) session.violation("the demo did not reach its first frame");
    }
};

// What arrives at the compositor, for comparing a run with a monitor and one without.
struct CommitRecord {
    std::vector<std::vector<std::uint32_t>> pictures{};
};

// A window driven through the same things twice, with each commit's pixels read back from the buffer file. The
// compositor is in this process, so the record stays here while the client is in a child.
struct RecordCommits {
    static constexpr ClientEnd end = ends_clean;
    CommitRecord* record{};
    std::size_t idles{};
    void on(Session& session, const BufferCommitted& commit) {
        if (const auto pixels = session.pixels_of(commit.buffer); pixels) record->pictures.emplace_back(pixels->begin(), pixels->end());
        session.release(commit.buffer);
    }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ++idles;
        if (idles == 1) {
            pointer_enter(session, on_help_x, on_help_y);
            left_click(session, on_help_x, on_help_y);
        } else if (idles == 2) {
            pointer_motion(session, on_close_x, on_close_y);
        } else if (idles == 3) {
            session.configure(500, 400);
        } else if (idles == 4) {
            session.configure(500, 400);
        } else {
            session.close_window();
        }
    }
};

// --- The client: the real backend, under the demo's real tree ---

[[nodiscard]] std::expected<void, jgui::Error> run_real_client(bool signal_storm, bool first_frame_only) {
    auto backend = jgui::WaylandBackend::create(jgui::WaylandOptions{
        .title = "jCppGUI", .app_id = "jcppgui.test", .size = jgui::Size{jgui::Width{640}, jgui::Height{480}}});
    if (!backend) return std::unexpected(std::move(backend.error()));
    auto app = jgui::make_application(std::move(*backend), demo::make_ui());
    if (!app) return std::unexpected(std::move(app.error()));
    if (signal_storm) start_signal_storm();
    return first_frame_only ? app->present_first_frame() : app->run();
}

template <class Script>
[[nodiscard]] bool run_plain_scenario() {
    constexpr bool signal_storm = std::same_as<Script, SignalStorm>;
    constexpr bool first_frame_only = requires { requires Script::first_frame_only; };
    GlobalSet globals{};
    if constexpr (requires { Script::globals; }) globals = Script::globals;
    return run_scenario(Script{}, globals, [] {
        const bool as_expected = ended_as_expected(Script::end, run_real_client(signal_storm, first_frame_only));
        if (signal_storm) {
            std::cout << "signals that landed in the client: " << signals_seen << '\n';
            if (signals_seen < 5) return 1;
        }
        return as_expected ? 0 : 1;
    });
}

[[nodiscard]] std::string read_text_file(const std::string& path) {
    std::string text;
    const UniqueFd fd{::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
    struct stat status {};
    if (fd.get() < 0 || ::fstat(fd.get(), &status) != 0) return text;
    text.resize(static_cast<std::size_t>(status.st_size));
    if (!jgui::detail::read_at(fd.get(), std::span{text}, 0)) text.clear();
    return text;
}

[[nodiscard]] std::expected<jgui::WaylandBackend, jgui::Error> test_backend() {
    return jgui::WaylandBackend::create(jgui::WaylandOptions{
        .title = "jCppGUI", .app_id = "jcppgui.test", .size = jgui::Size{jgui::Width{640}, jgui::Height{480}}});
}

// The real client with a real monitor, which writes beside the socket, in the scenario's private directory.
template <class Script>
[[nodiscard]] int run_monitored_client() {
    const char* found = std::getenv("XDG_RUNTIME_DIR");
    if (found == nullptr) return 1;
    const std::string directory{found};
    if constexpr (requires { requires Script::file_is_blocked; }) {
        if (!std::filesystem::create_directory(directory + "/redraw_monitor.jtext")) return 1;
    }
    auto backend = test_backend();
    if (!backend) return 1;
    auto app = jgui::make_application(std::move(*backend), demo::make_ui(), jgui::FrameMonitor<>{jgui::SteadyClock{}, jgui::FileSink{directory}});
    if (!app) return 1;
    if (!ended_as_expected(Script::end, app->run())) return 1;
    if (!Script::monitor_is_right(jgui_test::monitor(*app), read_text_file(directory + "/redraw_monitor.jtext"))) {
        std::cerr << "the monitor did not record what happened\n";
        return 1;
    }
    return 0;
}

// The same script against a client with no monitor and one with: the same frames, pixel for pixel, on the wire.
[[nodiscard]] bool run_monitor_versus_none() {
    CommitRecord without;
    CommitRecord with;
    const bool plain = run_scenario(RecordCommits{.record = &without}, GlobalSet{}, [] {
        return ended_as_expected(RecordCommits::end, run_real_client(false, false)) ? 0 : 1;
    });
    const bool watched = run_scenario(RecordCommits{.record = &with}, GlobalSet{}, [] {
        const char* found = std::getenv("XDG_RUNTIME_DIR");
        if (found == nullptr) return 1;
        auto backend = test_backend();
        if (!backend) return 1;
        auto app = jgui::make_application(std::move(*backend), demo::make_ui(), jgui::FrameMonitor<>{jgui::SteadyClock{}, jgui::FileSink{found}});
        if (!app) return 1;
        return ended_as_expected(RecordCommits::end, app->run()) ? 0 : 1;
    });
    std::cout << "commits without a monitor: " << without.pictures.size() << ", with: " << with.pictures.size() << '\n';
    if (!plain || !watched || without.pictures.size() < 4) return false;
    if (without.pictures.size() != with.pictures.size()) {
        std::cerr << "a monitor changed how many frames reach the compositor\n";
        return false;
    }
    if (without.pictures != with.pictures) {
        std::cerr << "a monitor changed the pixels that reach the compositor\n";
        return false;
    }
    return true;
}

// The real demo with --record-settings: one click on Help, so that an item comes into being, and then the window is closed.
enum class SettingsDemoRun : std::uint8_t { Records, BothFlags, NoBase };

template <SettingsDemoRun Run>
struct SettingsDemo {
    static constexpr ClientEnd end = ends_clean;
    static constexpr SettingsDemoRun settings_run = Run;
    static constexpr std::int32_t across = 640;
    static constexpr std::int32_t down = 480;
    bool clicked{};
    void on(Session& session, const SurfaceReady&) { session.configure(across, down); }
    void on(Session& session, const BufferCommitted& commit) { session.release(commit.buffer); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        if (clicked) {
            session.close_window();
            return;
        }
        clicked = true;
        auto reference = jgui::make_application(jgui::HeadlessBackend{jgui::Size{jgui::Width{across}, jgui::Height{down}}}, demo::make_ui());
        if (!reference || !reference->process_pending()) {
            session.violation("no reference for where Help is");
            return;
        }
        const jgui::Rect help = reference->ui().template get<demo::HelpButton>().bounds();
        const jgui::Point at = jgui_test::inside(help, jgui_test::number(help.width()) / 2, jgui_test::number(help.height()) / 2);
        pointer_enter(session, jgui_test::number(at.left()), jgui_test::number(at.top()));
        left_click(session, jgui_test::number(at.left()), jgui_test::number(at.top()));
    }
};

// What a real run wrote, read back with the viewer's own code, and every line of Items shown in the test's log.
[[nodiscard]] bool settings_file_is_right(const std::filesystem::path& file) {
    const auto read = jgui::read_jtext_file(file.string());
    const viewer::LoadedSettings loaded = viewer::load_settings_file(read);
    std::cout << read_text_file(file.string());
    if (!loaded.items.usable || !loaded.problems.usable) {
        std::cerr << "a section could not be read: " << loaded.items.status << " / " << loaded.problems.status << '\n';
        return false;
    }
    const std::vector<viewer::ItemsRow>& rows = loaded.items.rows;
    if (rows.size() != 6 || !loaded.problems.rows.empty()) {
        std::cerr << "not six items and no problems: " << loaded.items.status << " / " << loaded.problems.status << '\n';
        return false;
    }
    const jgui::Rect whole{jgui::Point{}, jgui::Size{jgui::Width{SettingsDemo<SettingsDemoRun::Records>::across}, jgui::Height{SettingsDemo<SettingsDemoRun::Records>::down}}};
    const bool window_row = rows[0].id == 1 && rows[0].parent_id == 0 && rows[0].class_name.ends_with("Window") && rows[0].name.empty() && rows[0].right == whole.right() && rows[0].bottom == whole.bottom() && rows[0].width == whole.width() && rows[0].height == whole.height();
    const bool button_row = rows[3].id == 4 && rows[3].parent_id == 3 && rows[3].class_name.ends_with("Button") && rows[3].name == "demo::HelpButton" && rows[3].width != jgui::Width{};
    const bool line_row = rows[5].id == 6 && rows[5].parent_id == 5 && rows[5].class_name.ends_with("Label") && rows[5].name == "demo::HelpLine" && rows[5].height != jgui::Height{};
    if (!window_row || !button_row || !line_row) {
        std::cerr << "the rows are not what the window holds\n";
        return false;
    }
    return true;
}

template <SettingsDemoRun Run>
[[nodiscard]] int run_the_real_demo_recording_settings() {
    using std::filesystem::path;
    const char* built = std::getenv("JGUI_TEST_DEMO");
    if (built == nullptr) {
        std::cerr << "JGUI_TEST_DEMO must be set\n";
        return 1;
    }
    const jgui_test::TemporaryDirectory temporary;
    const path top = std::filesystem::canonical(temporary.path());
    if (jgui::application_base_from((top / "program").string())) {
        std::cerr << "a folder above the temporary directory is named like a version, so this test would prove nothing: " << top << '\n';
        return 1;
    }
    const path working_directory = top / "v888" / "here";
    const path false_home = top / "v777";
    std::filesystem::create_directories(working_directory);
    std::filesystem::create_directories(false_home);
    const path base = Run == SettingsDemoRun::NoBase ? top / "plain" : top / "v321";
    const path program = base / "build" / "preset" / "jcppgui-demo";
    std::filesystem::create_directories(program.parent_path());
    std::filesystem::copy_file(built, program);

    const path errors = top / "errors.txt";
    std::vector<std::string> flags{"--record-settings"};
    if (Run == SettingsDemoRun::BothFlags) flags.emplace_back("--redraw-monitor");
    const int status = jgui_test::run_program(program, (false_home / "jcppgui-demo").string(), working_directory, flags, errors);
    const std::string said = read_text_file(errors.string());
    std::cout << "the demo ended with " << status << " and said: " << said << '\n';
    if (!std::filesystem::is_empty(working_directory) || !std::filesystem::is_empty(false_home)) {
        std::cerr << "the working directory or argv[0]'s folder was used\n";
        return 1;
    }
    const path settings = base / jgui::default_settings_file_name;
    const path monitor = base / jgui::default_redraw_file_name;
    if (Run == SettingsDemoRun::NoBase) {
        if (status != 1 || !said.contains("no base directory") || !said.contains(jgui::default_settings_file_name) || std::filesystem::exists(settings)) {
            std::cerr << "the demo with no base directory did not refuse cleanly\n";
            return 1;
        }
        return 0;
    }
    if (status != 0 || !settings_file_is_right(settings)) return 1;
    if (std::filesystem::exists(settings.string() + ".tmp")) {
        std::cerr << "a temporary file was left\n";
        return 1;
    }
    if (Run == SettingsDemoRun::Records && std::filesystem::exists(monitor)) {
        std::cerr << "a monitor's file from a run that did not ask for one\n";
        return 1;
    }
    if (Run == SettingsDemoRun::BothFlags) {
        const viewer::LoadedFile frames = viewer::load_file(jgui::read_jtext_file(monitor.string()));
        if (!frames.frames.usable || frames.frames.rows.size() < 2 || !frames.damage.usable || !frames.run.usable || frames.run.rows.empty() || !frames.run.rows[0].ended) {
            std::cerr << "the monitor's file beside it is not whole: " << frames.frames.status << '\n';
            return 1;
        }
    }
    return 0;
}

// Runs in the scenario's client process, and the demo is a child of that. Every folder here that is named like a version
// is a trap: only the base directory may ever hold the file.
template <DemoPlace Place>
[[nodiscard]] int run_the_real_demo() {
    using std::filesystem::path;
    const char* built = std::getenv("JGUI_TEST_DEMO");
    const char* source = std::getenv("JGUI_TEST_BASE");
    if (built == nullptr || source == nullptr) {
        std::cerr << "JGUI_TEST_DEMO and JGUI_TEST_BASE must be set\n";
        return 1;
    }
    const jgui_test::TemporaryDirectory temporary;
    const path top = std::filesystem::canonical(temporary.path());
    if (jgui::application_base_from((top / "program").string())) {
        std::cerr << "a folder above the temporary directory is named like a version, so this test would prove nothing: " << top << '\n';
        return 1;
    }
    const path working_directory = top / "v888" / "here";
    const path false_home = top / "v777";
    std::filesystem::create_directories(working_directory);
    std::filesystem::create_directories(false_home);

    path program{built};
    path base = std::filesystem::canonical(source);
    if (Place != DemoPlace::AsBuilt) {
        base = Place == DemoPlace::CopyUnderABase ? top / "v321" : top / "plain";
        program = base / "build" / "preset" / "jcppgui-demo";
        std::filesystem::create_directories(program.parent_path());
        std::filesystem::copy_file(built, program);
    }
    const path file = base / jgui::default_redraw_file_name;
    std::error_code absent;
    const auto before = std::filesystem::last_write_time(file, absent);

    const path errors = top / "errors.txt";
    const int status = jgui_test::run_program(program, (false_home / "jcppgui-demo").string(), working_directory, {"--redraw-monitor"}, errors);
    const std::string said = read_text_file(errors.string());
    std::cout << "the demo ended with " << status << " and said: " << said << '\n';

    bool found_elsewhere = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{top}) {
        if (entry.path().filename().string().starts_with(jgui::default_redraw_file_name) && entry.path() != file) {
            std::cerr << "a file where it should not be: " << entry.path() << '\n';
            found_elsewhere = true;
        }
    }
    if (found_elsewhere || !std::filesystem::is_empty(working_directory) || !std::filesystem::is_empty(false_home)) {
        std::cerr << "the working directory or argv[0]'s folder was used\n";
        return 1;
    }
    if (Place == DemoPlace::CopyWithNoBase) {
        if (status != 1 || !said.contains("no base directory") || !said.contains("no folder named v") || std::filesystem::exists(file)) {
            std::cerr << "the demo with no base directory did not refuse cleanly\n";
            return 1;
        }
        return 0;
    }
    std::error_code gone;
    const auto after = std::filesystem::last_write_time(file, gone);
    if (status != 0 || gone || (!absent && after == before)) {
        std::cerr << "no new file at " << file << '\n';
        return 1;
    }
    if (!jgui_test::blocks_all_close(read_text_file(file.string()))) {
        std::cerr << "not a whole file at " << file << '\n';
        return 1;
    }
    std::cout << "the file is at " << file << '\n';
    return 0;
}

// --- The real viewer program, against nothing but this compositor ---

// The window the program should be showing, made here in the compositor's process with the same code and the same file: a headless
// application, sent the same events, whose every pixel can be compared with what arrived. On its own that shows only that the
// program and this agree, so the scripts also look for text they name themselves, drawn on its own with the real font.
class ViewerReference {
public:
    using LoadedFile = std::expected<jgui::JTextFile, jgui::Error>;
    using App = std::remove_cvref_t<decltype(*jgui::make_application(jgui::HeadlessBackend{jgui::Size{}}, viewer::make_ui(viewer::LoadedFile{})))>;

    ViewerReference(const LoadedFile& file, std::int32_t width, std::int32_t height)
        : app_{*jgui::make_application(jgui::HeadlessBackend{jgui::Size{jgui::Width{width}, jgui::Height{height}}}, viewer::make_ui(viewer::load_file(file)))} {
        settle();
    }

    ViewerReference(const LoadedFile& file, const LoadedFile& settings, std::int32_t width, std::int32_t height)
        : app_{*jgui::make_application(jgui::HeadlessBackend{jgui::Size{jgui::Width{width}, jgui::Height{height}}}, viewer::make_ui(viewer::load_file(file), viewer::load_settings_file(settings)))} {
        settle();
    }

    void settle() {
        if (!app_.process_pending()) std::cerr << "the reference window failed\n";
    }
    template <class Event>
    void deliver(const Event& event) {
        jgui_test::backend(app_).inject(event);
        settle();
    }
    template <class Tag>
    void click() {
        const jgui::Point at = centre_of<Tag>();
        deliver(jgui::PointerMove{at});
        deliver(jgui::PointerButton{at, jgui::MouseButton::Left, jgui::ButtonState::Pressed, jgui_test::serial(1)});
        deliver(jgui::PointerButton{at, jgui::MouseButton::Left, jgui::ButtonState::Released, jgui_test::serial(1)});
    }
    template <class Tag>
    [[nodiscard]] auto& get() {
        return app_.ui().template get<Tag>();
    }
    template <class Tag>
    [[nodiscard]] jgui::Point centre_of() {
        const jgui::Rect area = get<Tag>().bounds();
        return jgui_test::inside(area, jgui_test::number(area.width()) / 2, jgui_test::number(area.height()) / 2);
    }

    // Whether what arrived is this window, pixel for pixel.
    [[nodiscard]] bool same_picture(std::span<const std::uint32_t> pixels) {
        const jgui::HeadlessBackend& backend = jgui_test::backend(app_);
        const auto width = static_cast<std::size_t>(jgui_test::number(backend.size().width()));
        const auto height = static_cast<std::size_t>(jgui_test::number(backend.size().height()));
        if (pixels.size() != width * height) return false;
        for (std::size_t y = 0; y < height; ++y)
            for (std::size_t x = 0; x < width; ++x)
                if (pixels[y * width + x] != backend.pixel(jgui::Point{jgui::Left{static_cast<std::int32_t>(x)}, jgui::Top{static_cast<std::int32_t>(y)}})) return false;
        return true;
    }

private:
    App app_;
};

// Whether this text, drawn with the real font in the theme's text colour over that background, is at that place in a picture of that width.
[[nodiscard]] bool text_is_at(std::span<const std::uint32_t> pixels, std::int32_t picture_width, jgui::Point at, std::string_view text, jgui::Color background) {
    auto engine = jgui::TextEngine::create();
    if (!engine) return false;
    auto run = engine->shape(text);
    if (!run) return false;
    const jgui::Size size = run->size();
    std::vector<std::uint32_t> drawn(static_cast<std::size_t>(jgui_test::number(size.width())) * static_cast<std::size_t>(jgui_test::number(size.height())), background.argb);
    auto buffer = jgui::PixelBuffer::create(drawn, size, jgui::PixelBuffer::tight_stride(size));
    if (!buffer) return false;
    {
        const jgui::Canvas canvas{*buffer};
        canvas.draw_text(jgui::Point{}, *run, jgui::theme::text);
    }
    const auto stride = static_cast<std::size_t>(picture_width);
    const auto left = static_cast<std::size_t>(jgui_test::number(at.left()));
    const auto top = static_cast<std::size_t>(jgui_test::number(at.top()));
    const auto across = static_cast<std::size_t>(jgui_test::number(size.width()));
    const auto down = static_cast<std::size_t>(jgui_test::number(size.height()));
    std::size_t differs_from_background = 0;
    for (std::size_t y = 0; y < down; ++y) {
        for (std::size_t x = 0; x < across; ++x) {
            const std::size_t where = (top + y) * stride + left + x;
            if (where >= pixels.size() || left + x >= stride) return false;
            if (pixels[where] != drawn[y * across + x]) return false;
            if (drawn[y * across + x] != background.argb) ++differs_from_background;
        }
    }
    return differs_from_background != 0;
}

// Where a grid draws the header of a column, and the cell of a column in a row on the screen.
template <class Grid>
[[nodiscard]] jgui::Width width_before(const Grid& grid, std::size_t column) {
    jgui::Width width{};
    for (std::size_t i = 0; i < column; ++i) width = width + grid.column_width(i);
    return width;
}
template <class Grid>
[[nodiscard]] jgui::Point header_text_at(const Grid& grid, std::size_t column) {
    const jgui::Rect area = grid.header_area();
    return jgui::Point{jgui::left_after((jgui::left_after(area.left() + width_before(grid, column))) + jgui::theme::cell_pad_x), jgui::top_after(area.top() + jgui::theme::cell_pad_y)};
}
template <class Grid>
[[nodiscard]] jgui::Point cell_text_at(const Grid& grid, std::size_t row, std::size_t column) {
    const jgui::Rect area = grid.body_area();
    const jgui::Top row_top = jgui::top_after(area.top() + repeated_extent(grid.row_height(), row));
    return jgui::Point{jgui::left_after((jgui::left_after(area.left() + width_before(grid, column))) + jgui::theme::cell_pad_x), jgui::top_after(row_top + jgui::theme::cell_pad_y)};
}
[[nodiscard]] inline jgui::Point label_text_at(const jgui::Rect& label) {
    return jgui::Point{jgui::left_after(label.left() + jgui::theme::label_pad_x), jgui::top_after(label.top() + jgui::theme::label_pad_y)};
}

constexpr std::int32_t viewer_width = 900;
constexpr std::int32_t viewer_height = 500;

// What each script keeps: where the file is, as the program was told it, and the buffer that came last.
struct ViewerState {
    std::string path{};
    std::string settings_path{};
    std::optional<ObjectId> latest{};
    std::size_t commits_seen{};

    void on_commit(Session& session, const BufferCommitted& commit) {
        if (latest) session.release(*latest);
        latest = commit.buffer;
        commits_seen = commit.index + 1;
    }
    // Compares what came last with the reference. An empty answer means the picture could not be read, which is a violation.
    [[nodiscard]] std::vector<std::uint32_t> picture(Session& session) const {
        if (!latest) return {};
        auto pixels = session.pixels_of(*latest);
        return pixels ? std::move(*pixels) : std::vector<std::uint32_t>{};
    }
    [[nodiscard]] ViewerReference::LoadedFile file() const { return jgui::read_jtext_file(path); }
    [[nodiscard]] ViewerReference::LoadedFile settings_file() const { return jgui::read_jtext_file(settings_path); }
};

enum class ViewerFile : std::uint8_t { Good, Large, Missing, Malformed, Symlink, PathNotText, GoodInBase, NoBase, SettingsGood, SettingsBadRow, SettingsMissing, SettingsMalformed, OnlySettings, BothInBase };

// What a scenario's file is, what the program is started with, and where it stands. Everything is under its own temporary directory.
struct ViewerSetup {
    std::filesystem::path program{};
    std::string path{};
    std::string settings_path{};
    std::vector<std::string> arguments{};
};

void put_file(const std::filesystem::path& where, std::string_view text) {
    const UniqueFd file{::open(where.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR)};
    if (file.get() >= 0) static_cast<void>(jgui::detail::write_at(file.get(), std::span{text}, 0));
}

[[nodiscard]] ViewerSetup set_up_viewer(ViewerFile kind, const std::filesystem::path& top) {
    ViewerSetup setup;
    const char* built = std::getenv("JGUI_TEST_VIEWER");
    if (built == nullptr) return setup;
    setup.program = built;
    const std::filesystem::path named = top / "some.jtext";
    switch (kind) {
        case ViewerFile::Good: put_file(named, jgui_test::redraw_file_text()); break;
        case ViewerFile::Large: put_file(named, jgui_test::redraw_file_text(65536)); break;
        case ViewerFile::Malformed: put_file(named, "=== jText File ===\n=== Section: Frames ===\n 1. ?# x\n"); break;
        case ViewerFile::Symlink:
            put_file(top / "real.jtext", jgui_test::redraw_file_text());
            std::filesystem::create_symlink(top / "real.jtext", named);
            break;
        case ViewerFile::GoodInBase:
        case ViewerFile::NoBase: {
            const std::filesystem::path base = top / (kind == ViewerFile::GoodInBase ? "v321" : "plain");
            setup.program = base / "build" / "preset" / "jcppgui-viewer";
            std::filesystem::create_directories(setup.program.parent_path());
            std::filesystem::copy_file(built, setup.program);
            if (kind == ViewerFile::GoodInBase) put_file(base / jgui::default_redraw_file_name, jgui_test::redraw_file_text());
            setup.path = (base / jgui::default_redraw_file_name).string();
            return setup;
        }
        case ViewerFile::BothInBase: {
            const std::filesystem::path base = top / "v321";
            setup.program = base / "build" / "preset" / "jcppgui-viewer";
            std::filesystem::create_directories(setup.program.parent_path());
            std::filesystem::copy_file(built, setup.program);
            setup.path = (base / jgui::default_redraw_file_name).string();
            setup.settings_path = (base / jgui::default_settings_file_name).string();
            put_file(setup.path, jgui_test::redraw_file_text());
            put_file(setup.settings_path, jgui_test::settings_file_text());
            return setup;
        }
        case ViewerFile::SettingsGood:
        case ViewerFile::SettingsBadRow:
        case ViewerFile::SettingsMissing:
        case ViewerFile::SettingsMalformed:
        case ViewerFile::OnlySettings: {
            const std::filesystem::path settings = top / "other.jtext";
            if (kind != ViewerFile::OnlySettings) put_file(named, jgui_test::redraw_file_text());
            if (kind == ViewerFile::SettingsGood || kind == ViewerFile::OnlySettings) put_file(settings, jgui_test::settings_file_text());
            if (kind == ViewerFile::SettingsMalformed) put_file(settings, "=== jText File ===\n=== Section: Items ===\n 1. ?# x\n");
            if (kind == ViewerFile::SettingsBadRow) {
                std::string bad = jgui_test::settings_file_text();
                bad.replace(bad.find(jgui_test::settings_test_left), jgui_test::settings_test_left.size(), "nope");
                put_file(settings, bad);
            }
            setup.settings_path = settings.string();
            setup.path = named.string();
            setup.arguments = {setup.path, setup.settings_path};
            return setup;
        }
        case ViewerFile::Missing:
        case ViewerFile::PathNotText:
        default:
            break;
    }
    setup.path = kind == ViewerFile::PathNotText ? "\xFF.jtext" : named.string();
    setup.arguments.push_back(setup.path);
    return setup;
}

constexpr std::size_t frames_reason_column = 2;
constexpr std::size_t damage_tag_column = 3;

// The first picture: every pixel, the title, and text that can only be there if the file was read.
struct ViewerFirstFrame {
    static constexpr ClientEnd end = ends_clean;
    static constexpr ViewerFile viewer_file = ViewerFile::Good;
    ViewerState state{};
    void on(Session& session, const SurfaceReady&) { session.configure(viewer_width, viewer_height); }
    void on(Session& session, const BufferCommitted& commit) { state.on_commit(session, commit); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ViewerReference reference{state.file(), viewer_width, viewer_height};
        const std::vector<std::uint32_t> pixels = state.picture(session);
        if (!reference.same_picture(pixels)) session.violation("the first frame is not the window that was expected");
        const auto& grid = reference.get<viewer::FramesGridTag>();
        if (!text_is_at(pixels, viewer_width, header_text_at(grid, 0), "Frame_ID", jgui::theme::bar)) session.violation("no header text for the first column");
        if (!text_is_at(pixels, viewer_width, header_text_at(grid, frames_reason_column), "Reason", jgui::theme::bar)) session.violation("no header text for the third column");
        if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 0, 0), "1", jgui::theme::background)) session.violation("no id in the first row");
        if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 0, frames_reason_column), "FirstFrame", jgui::theme::background)) session.violation("no reason in the first row");
        if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 1, frames_reason_column), "WidgetDamage", jgui::theme::background)) session.violation("no reason in the second row");
        if (!text_is_at(pixels, viewer_width, label_text_at(reference.get<viewer::StatusLine>().bounds()), "Frames: 200 rows", jgui::theme::background)) session.violation("no status line");
        if (session.title() != "jCppGUI viewer: some.jtext") session.violation("the title does not name the file: " + session.title());
        if (session.app_id() != "jcppgui.viewer") session.violation("another app id: " + session.app_id());
        session.close_window();
    }
};

// A click on each of two buttons, through the wire, and each section in the window after it.
struct ViewerSectionSwitch {
    static constexpr ClientEnd end = ends_clean;
    static constexpr ViewerFile viewer_file = ViewerFile::Good;
    enum class Step : std::uint8_t { First, ClickedDamage, ClickedRun, Done };
    ViewerState state{};
    Step step{Step::First};
    std::vector<std::uint32_t> frames_picture{};
    void on(Session& session, const SurfaceReady&) { session.configure(viewer_width, viewer_height); }
    void on(Session& session, const BufferCommitted& commit) { state.on_commit(session, commit); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ViewerReference reference{state.file(), viewer_width, viewer_height};
        switch (step) {
            case Step::First: {
                frames_picture = state.picture(session);
                const jgui::Point at = reference.centre_of<viewer::DamageButton>();
                pointer_enter(session, jgui_test::number(at.left()), jgui_test::number(at.top()));
                left_click(session, jgui_test::number(at.left()), jgui_test::number(at.top()));
                step = Step::ClickedDamage;
                return;
            }
            case Step::ClickedDamage: {
                reference.click<viewer::DamageButton>();
                const std::vector<std::uint32_t> pixels = state.picture(session);
                if (pixels == frames_picture) session.violation("the click changed nothing");
                if (!reference.same_picture(pixels)) session.violation("the Damage section is not the window that was expected");
                const auto& grid = reference.get<viewer::DamageGridTag>();
                if (!text_is_at(pixels, viewer_width, header_text_at(grid, damage_tag_column), "Tag", jgui::theme::bar)) session.violation("no Damage header");
                if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 0, damage_tag_column), "HelpButton", jgui::theme::background)) session.violation("no Damage row");
                if (!text_is_at(pixels, viewer_width, label_text_at(reference.get<viewer::StatusLine>().bounds()), "Damage: 200 rows", jgui::theme::background)) session.violation("no status for Damage");
                const jgui::Point at = reference.centre_of<viewer::RunButton>();
                left_click(session, jgui_test::number(at.left()), jgui_test::number(at.top()));
                step = Step::ClickedRun;
                return;
            }
            case Step::ClickedRun: {
                reference.click<viewer::DamageButton>();
                reference.click<viewer::RunButton>();
                const std::vector<std::uint32_t> pixels = state.picture(session);
                if (!reference.same_picture(pixels)) session.violation("the Run section is not the window that was expected");
                const auto& grid = reference.get<viewer::RunGridTag>();
                if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 0, 1), "for the viewer", jgui::theme::background)) session.violation("no label in the Run row");
                if (!text_is_at(pixels, viewer_width, label_text_at(reference.get<viewer::StatusLine>().bounds()), "Run: 1 row", jgui::theme::background)) session.violation("no status for Run");
                session.close_window();
                step = Step::Done;
                return;
            }
            case Step::Done:
            default:
                return;
        }
    }
};

// A wheel notch over the grid, through the wire: three rows up. Row 4 is on top after it.
struct ViewerWheelScroll {
    static constexpr ClientEnd end = ends_clean;
    static constexpr ViewerFile viewer_file = ViewerFile::Good;
    ViewerState state{};
    bool scrolled{};
    void on(Session& session, const SurfaceReady&) { session.configure(viewer_width, viewer_height); }
    void on(Session& session, const BufferCommitted& commit) { state.on_commit(session, commit); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ViewerReference reference{state.file(), viewer_width, viewer_height};
        const jgui::Rect body = reference.get<viewer::FramesGridTag>().body_area();
        const jgui::Point at = jgui_test::inside(body, 100, 100);
        if (!scrolled) {
            scrolled = true;
            pointer_enter(session, jgui_test::number(at.left()), jgui_test::number(at.top()));
            scroll_frame_of_steps(session, 1);
            return;
        }
        reference.deliver(jgui::PointerMove{at});
        reference.deliver(jgui::PointerScroll{at, jgui::ScrollDown::of_steps(1), jgui::ScrollAcross{}});
        const std::vector<std::uint32_t> pixels = state.picture(session);
        if (!reference.same_picture(pixels)) session.violation("after the wheel, it is not the window that was expected");
        const auto& grid = reference.get<viewer::FramesGridTag>();
        if (grid.scroll_down() != repeated_extent(grid.row_height(), 3)) session.violation("one notch of the reference is not three rows, so this test proves nothing");
        if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 0, 0), "4", jgui::theme::background)) session.violation("the fourth row is not on top");
        session.close_window();
    }
};

// The grid takes the whole of each new size: bigger, then smaller than its columns.
struct ViewerResize {
    static constexpr ClientEnd end = ends_clean;
    static constexpr ViewerFile viewer_file = ViewerFile::Good;
    static constexpr std::array<std::array<std::int32_t, 2>, 2> sizes{{{1100, 700}, {300, 200}}};
    ViewerState state{};
    std::size_t next{};
    std::int32_t width{viewer_width};
    std::int32_t height{viewer_height};
    void on(Session& session, const SurfaceReady&) { session.configure(viewer_width, viewer_height); }
    void on(Session& session, const BufferCommitted& commit) {
        state.on_commit(session, commit);
        if (commit.width != width || commit.height != height) session.violation("a frame at another size than was configured");
    }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ViewerReference reference{state.file(), width, height};
        if (!reference.same_picture(state.picture(session))) session.violation("at this size, it is not the window that was expected: " + std::to_string(width) + "x" + std::to_string(height));
        const jgui::Rect client = jgui::WindowFrame<jgui::DefaultFrameMetrics>::client_rect(jgui::Size{jgui::Width{width}, jgui::Height{height}});
        const jgui::Rect grid = reference.get<viewer::FramesGridTag>().bounds();
        if (grid.bottom() != client.bottom() || grid.right() != client.right()) session.violation("the grid does not reach the client area's far corner");
        if (next == sizes.size()) {
            session.close_window();
            return;
        }
        width = sizes[next][0];
        height = sizes[next][1];
        ++next;
        session.configure(width, height);
    }
};

// A file that cannot be shown leaves a window that says why. Its buttons still work, and it closes as any window does.
template <ViewerFile Kind>
struct ViewerRefused {
    static constexpr ClientEnd end = ends_clean;
    static constexpr ViewerFile viewer_file = Kind;
    ViewerState state{};
    bool clicked{};
    [[nodiscard]] static constexpr std::string_view says() {
        if (Kind == ViewerFile::Missing) return "cannot look at";
        if (Kind == ViewerFile::Malformed) return "jText line 3: a line stands outside any block";
        if (Kind == ViewerFile::Symlink) return "refused";
        if (Kind == ViewerFile::NoBase) return "the viewer has no base directory";
        return "the path is not text that can be shown";
    }
    [[nodiscard]] ViewerReference::LoadedFile file() const {
        if (Kind == ViewerFile::NoBase) {
            const std::filesystem::path program = std::filesystem::path{state.path}.parent_path() / "build" / "preset" / "jcppgui-viewer";
            return std::unexpected(jgui::Error{jgui::ErrorCode::System, "the viewer has no base directory: no folder named v and three digits contains the executable: " + program.string()});
        }
        if (Kind == ViewerFile::PathNotText) return std::unexpected(jgui::Error{jgui::ErrorCode::InvalidUtf8, std::string{says()}});
        return state.file();
    }
    void on(Session& session, const SurfaceReady&) { session.configure(viewer_width, viewer_height); }
    void on(Session& session, const BufferCommitted& commit) { state.on_commit(session, commit); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ViewerReference reference{file(), viewer_width, viewer_height};
        if (clicked) reference.click<viewer::DamageButton>();
        const std::vector<std::uint32_t> pixels = state.picture(session);
        if (!reference.same_picture(pixels)) session.violation("this is not the window that was expected");
        const jgui::Rect line = reference.get<viewer::StatusLine>().bounds();
        if (!text_is_at(pixels, viewer_width, label_text_at(line), says(), jgui::theme::background)) session.violation("the reason is not where the grid would be");
        if (reference.get<viewer::SectionGrids>().shown()) session.violation("a grid shows for a file that was refused");
        if (session.title() != (Kind == ViewerFile::PathNotText || Kind == ViewerFile::NoBase ? "jCppGUI viewer" : "jCppGUI viewer: some.jtext")) session.violation("another title: " + session.title());
        if (clicked) {
            session.close_window();
            return;
        }
        clicked = true;
        const jgui::Point at = reference.centre_of<viewer::DamageButton>();
        pointer_enter(session, jgui_test::number(at.left()), jgui_test::number(at.top()));
        left_click(session, jgui_test::number(at.left()), jgui_test::number(at.top()));
    }
};

// With no argument, the file in the base directory of a copy of the program, and nothing else.
struct ViewerFileInItsBase {
    static constexpr ClientEnd end = ends_clean;
    static constexpr ViewerFile viewer_file = ViewerFile::GoodInBase;
    ViewerState state{};
    void on(Session& session, const SurfaceReady&) { session.configure(viewer_width, viewer_height); }
    void on(Session& session, const BufferCommitted& commit) { state.on_commit(session, commit); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ViewerReference reference{state.file(), viewer_width, viewer_height};
        const std::vector<std::uint32_t> pixels = state.picture(session);
        if (!reference.same_picture(pixels)) session.violation("this is not the window that was expected");
        if (!text_is_at(pixels, viewer_width, label_text_at(reference.get<viewer::StatusLine>().bounds()), "Frames: 200 rows", jgui::theme::background)) session.violation("the base directory's file was not read");
        if (session.title() != "jCppGUI viewer: redraw_monitor.jtext") session.violation("another title: " + session.title());
        session.close_window();
    }
};

// The largest file the monitor writes, through the real program: its first frame, and a frame after a wheel at its largest step.
struct ViewerLargeFile {
    static constexpr ClientEnd end = ends_clean;
    static constexpr ViewerFile viewer_file = ViewerFile::Large;
    ViewerState state{};
    bool first_checked{};
    void on(Session& session, const SurfaceReady&) { session.configure(viewer_width, viewer_height); }
    void on(Session& session, const BufferCommitted& commit) { state.on_commit(session, commit); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        if (first_checked) {
            session.close_window();
            return;
        }
        first_checked = true;
        ViewerReference reference{state.file(), viewer_width, viewer_height};
        const std::vector<std::uint32_t> pixels = state.picture(session);
        if (!reference.same_picture(pixels)) session.violation("this is not the window that was expected");
        if (!text_is_at(pixels, viewer_width, label_text_at(reference.get<viewer::StatusLine>().bounds()), "Frames: 65536 rows", jgui::theme::background)) session.violation("the status does not say 65536 rows");
        const jgui::Point at = jgui_test::inside(reference.get<viewer::FramesGridTag>().body_area(), 100, 100);
        pointer_enter(session, jgui_test::number(at.left()), jgui_test::number(at.top()));
        scroll_frame_of_steps(session, jgui::max_scroll_steps);
    }
};

constexpr std::size_t items_class_column = 2;
constexpr std::size_t items_left_column = 4;
constexpr std::size_t items_bottom_column = 6;
constexpr std::size_t items_right_column = 7;
constexpr std::size_t problems_what_column = 2;

// Items, then Problems, then back to Frames, through the wire, each compared with the reference pixel for pixel, with text the script
// names itself. What each file holds is that file's business: one file's trouble must show only in its own sections.
template <ViewerFile Kind>
struct ViewerSettings {
    static constexpr ClientEnd end = ends_clean;
    static constexpr ViewerFile viewer_file = Kind;
    static constexpr bool monitor_file_is_fine = Kind != ViewerFile::OnlySettings;
    static constexpr bool settings_are_refused = Kind == ViewerFile::SettingsMissing || Kind == ViewerFile::SettingsMalformed;
    enum class Step : std::uint8_t { First, ClickedItems, ClickedProblems, ClickedFrames, Done };
    ViewerState state{};
    Step step{Step::First};

    [[nodiscard]] static constexpr std::string_view settings_refusal() { return Kind == ViewerFile::SettingsMissing ? "cannot look at" : "jText line 3: a line stands outside any block"; }

    template <class ButtonTag>
    static void click_on(Session& session, ViewerReference& reference) {
        const jgui::Point at = reference.centre_of<ButtonTag>();
        left_click(session, jgui_test::number(at.left()), jgui_test::number(at.top()));
    }

    static void says(Session& session, std::span<const std::uint32_t> pixels, ViewerReference& reference, std::string_view text) {
        if (!text_is_at(pixels, viewer_width, label_text_at(reference.get<viewer::StatusLine>().bounds()), text, jgui::theme::background)) session.violation("the status line does not say: " + std::string{text});
    }

    void check_items(Session& session, std::span<const std::uint32_t> pixels, ViewerReference& reference) const {
        if constexpr (settings_are_refused) {
            says(session, pixels, reference, settings_refusal());
            if (reference.get<viewer::SectionGrids>().shown()) session.violation("a grid shows for a settings file that was refused");
        } else {
            const auto& grid = reference.get<viewer::ItemsGridTag>();
            if (reference.get<viewer::SectionGrids>().shown() != viewer::items_position) session.violation("the Items grid is not shown");
            says(session, pixels, reference, Kind == ViewerFile::SettingsBadRow ? "Items: 2 rows, 1 row left out: a value did not convert" : "Items: 3 rows");
            if (!text_is_at(pixels, viewer_width, header_text_at(grid, 0), "ID", jgui::theme::bar)) session.violation("no ID header");
            if (!text_is_at(pixels, viewer_width, header_text_at(grid, items_class_column), "Class_Name", jgui::theme::bar)) session.violation("no Class_Name header");
            if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 1, 0), "2", jgui::theme::background)) session.violation("no ID in the second row");
            if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 1, items_class_column), "jgui::Button", jgui::theme::background)) session.violation("no class name in the second row");
            if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 1, items_left_column), "7", jgui::theme::background)) session.violation("no Left in the second row");
            if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 1, items_bottom_column), "29", jgui::theme::background)) session.violation("no Bottom in the second row");
            if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 1, items_right_column), "57", jgui::theme::background)) session.violation("no Right in the second row");
        }
    }

    void check_problems(Session& session, std::span<const std::uint32_t> pixels, ViewerReference& reference) const {
        if constexpr (settings_are_refused) {
            says(session, pixels, reference, settings_refusal());
            if (reference.get<viewer::SectionGrids>().shown()) session.violation("a grid shows for a settings file that was refused");
        } else {
            const auto& grid = reference.get<viewer::ProblemsGridTag>();
            says(session, pixels, reference, "Problems: 1 row");
            if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 0, 1), "3", jgui::theme::background)) session.violation("no item in the Problems row");
            if (!text_is_at(pixels, viewer_width, cell_text_at(grid, 0, problems_what_column), "Outside its parent", jgui::theme::background)) session.violation("no reason in the Problems row");
        }
    }

    void check_frames(Session& session, std::span<const std::uint32_t> pixels, ViewerReference& reference) const {
        if constexpr (monitor_file_is_fine) {
            says(session, pixels, reference, "Frames: 200 rows");
            if (reference.get<viewer::SectionGrids>().shown() != viewer::frames_position) session.violation("the Frames grid is not shown");
        } else {
            says(session, pixels, reference, "cannot look at");
            if (reference.get<viewer::SectionGrids>().shown()) session.violation("a grid shows for a monitor file that is missing");
        }
    }

    void on(Session& session, const SurfaceReady&) { session.configure(viewer_width, viewer_height); }
    void on(Session& session, const BufferCommitted& commit) { state.on_commit(session, commit); }
    void on(Session& session, const Idle&) {
        if (session.commits_with_buffer() == 0) return;
        ViewerReference reference{state.file(), state.settings_file(), viewer_width, viewer_height};
        const std::vector<std::uint32_t> pixels = state.picture(session);
        switch (step) {
            case Step::First: {
                if (!reference.same_picture(pixels)) session.violation("the first frame is not the window that was expected");
                check_frames(session, pixels, reference);
                const jgui::Point at = reference.centre_of<viewer::ItemsButton>();
                pointer_enter(session, jgui_test::number(at.left()), jgui_test::number(at.top()));
                click_on<viewer::ItemsButton>(session, reference);
                step = Step::ClickedItems;
                return;
            }
            case Step::ClickedItems:
                reference.click<viewer::ItemsButton>();
                if (!reference.same_picture(pixels)) session.violation("the Items section is not the window that was expected");
                check_items(session, pixels, reference);
                click_on<viewer::ProblemsButton>(session, reference);
                step = Step::ClickedProblems;
                return;
            case Step::ClickedProblems:
                reference.click<viewer::ItemsButton>();
                reference.click<viewer::ProblemsButton>();
                if (!reference.same_picture(pixels)) session.violation("the Problems section is not the window that was expected");
                check_problems(session, pixels, reference);
                click_on<viewer::FramesButton>(session, reference);
                step = Step::ClickedFrames;
                return;
            case Step::ClickedFrames:
                reference.click<viewer::ItemsButton>();
                reference.click<viewer::ProblemsButton>();
                reference.click<viewer::FramesButton>();
                if (!reference.same_picture(pixels)) session.violation("the Frames section is not the window that was expected");
                check_frames(session, pixels, reference);
                session.close_window();
                step = Step::Done;
                return;
            case Step::Done:
            default:
                return;
        }
    }
};

// Runs in the scenario's client process. The real program starts in a directory of its own, and it must end well.
[[nodiscard]] int run_the_real_viewer(const ViewerSetup& setup, const std::filesystem::path& top) {
    const std::filesystem::path errors = top / "errors.txt";
    // Two arguments are a mistake it says at once, before it opens any window.
    if (jgui_test::run_program(setup.program, "jcppgui-viewer", top, {"one", "two", "three"}, errors) != 2) {
        std::cerr << "too many arguments did not end in the usage message\n";
        return 1;
    }
    const int status = jgui_test::run_program(setup.program, "jcppgui-viewer", top, setup.arguments, errors);
    std::cout << "the viewer ended with " << status << " and said: " << read_text_file(errors.string()) << '\n';
    return status;
}

template <class Script>
[[nodiscard]] bool run_viewer_scenario() {
    const jgui_test::TemporaryDirectory temporary;
    const std::filesystem::path top = std::filesystem::canonical(temporary.path());
    if (jgui::application_base_from((top / "program").string())) {
        std::cerr << "a folder above the temporary directory is named like a version, so this test would prove nothing: " << top << '\n';
        return false;
    }
    const ViewerSetup setup = set_up_viewer(Script::viewer_file, top);
    if (setup.program.empty()) {
        std::cerr << "JGUI_TEST_VIEWER must be set\n";
        return false;
    }
    Script script{};
    script.state.path = setup.path;
    script.state.settings_path = setup.settings_path;
    return run_scenario(script, GlobalSet{}, [&] { return run_the_real_viewer(setup, top); });
}

struct MonitorVersusNone {};

template <class Script>
[[nodiscard]] bool run_one() {
    if constexpr (std::same_as<Script, MonitorVersusNone>) {
        return run_monitor_versus_none();
    } else if constexpr (requires { Script::settings_run; }) {
        return run_scenario(Script{}, GlobalSet{}, [] { return run_the_real_demo_recording_settings<Script::settings_run>(); });
    } else if constexpr (requires { Script::viewer_file; }) {
        return run_viewer_scenario<Script>();
    } else if constexpr (requires { Script::place; }) {
        return run_scenario(Script{}, GlobalSet{}, [] { return run_the_real_demo<Script::place>(); });
    } else if constexpr (requires { &Script::grid_is_right; }) {
        return run_scenario(Script{}, GlobalSet{}, [] { return run_grid_client<Script>(); });
    } else if constexpr (requires { requires Script::monitored; }) {
        return run_scenario(Script{}, GlobalSet{}, [] { return run_monitored_client<Script>(); });
    } else {
        return run_plain_scenario<Script>();
    }
}

template <class Script>
struct Named {
    std::string_view name;
};

// Each scenario by name: the names only pick one to run, and each script is a type.
constexpr auto scenarios = std::tuple{
    Named<HappyPath>{"happy_path"},
    Named<KeyboardPath>{"keyboard_path"},
    Named<ConfigureSize<1, 1, 1, 1>>{"configure_1x1"},
    Named<ConfigureSize<0, 0, 640, 480>>{"configure_0x0"},
    Named<ConfigureSize<-5, -7, 640, 480>>{"configure_negative"},
    Named<ConfigureSize<int32_most, int32_most, 8192, 8192>>{"configure_int32_max"},
    Named<ConfigureSize<8192, 1, 8192, 1>>{"configure_8192x1"},
    Named<ConfigureSize<1, 8192, 1, 8192>>{"configure_1x8192"},
    Named<ConfigureSize<119, 119, 119, 119>>{"configure_119x119"},
    Named<ConfigureFlood>{"configure_flood"},
    Named<ReleaseBeforeAttach>{"release_before_attach"},
    Named<ReleaseThreeTimes>{"release_three_times"},
    Named<NeverRelease>{"never_release"},
    Named<ReleaseLate>{"release_late"},
    Named<PoolAttack>{"pool_attack"},
    Named<CapabilitiesFlap>{"capabilities_flap"},
    Named<SeatRemovedWhileHeld>{"seat_removed_while_held"},
    Named<NoSeat>{"no_seat"},
    Named<NoCompositor>{"no_compositor"},
    Named<NoShm>{"no_shm"},
    Named<NoWmBase>{"no_wm_base"},
    Named<OldCompositor>{"old_compositor"},
    Named<OldSeat>{"old_seat"},
    Named<KeymapPipe>{"keymap_pipe"},
    Named<KeymapDirectory>{"keymap_directory"},
    Named<KeymapShort>{"keymap_short"},
    Named<KeymapNoTerminator>{"keymap_1mib_no_terminator"},
    Named<KeymapHugeSize>{"keymap_size_0xffffffff"},
    Named<KeymapFormat7>{"keymap_format_7"},
    Named<KeymapNone>{"keymap_none"},
    Named<KeysExtreme>{"keys_extreme"},
    Named<PointerExtremes>{"pointer_extremes"},
    Named<EventsOfLaterVersions>{"events_of_later_versions"},
    Named<EventFlood>{"event_flood"},
    Named<EventFloodInSync>{"event_flood_in_sync"},
    Named<EventFloodMixed>{"event_flood_mixed"},
    Named<DisconnectAtOnce>{"disconnect_at_once"},
    Named<DisconnectAfterFrame>{"disconnect_after_frame"},
    Named<RawHeader<4>>{"bad_length_too_small"},
    Named<RawHeader<0xFFFC>>{"bad_length_too_large"},
    Named<RawHeader<10>>{"bad_length_not_aligned"},
    Named<RawHeader<8, 1, 99>>{"unknown_opcode"},
    Named<UnknownObject>{"unknown_object"},
    Named<TruncatedThenGone>{"truncated_then_gone"},
    Named<TruncatedDuringConnect>{"truncated_during_connect"},
    Named<TruncatedDuringFirstFrame>{"truncated_during_first_frame"},
    Named<ArgumentsTooShort>{"arguments_too_short"},
    Named<StringNotTerminated>{"string_not_terminated"},
    Named<KeymapWithNoDescriptor>{"keymap_with_no_descriptor"},
    Named<ProtocolErrorOnSurface>{"protocol_error_on_surface"},
    Named<ProtocolErrorOnDisplay>{"protocol_error_on_display"},
    Named<ProtocolErrorOnNothing>{"protocol_error_on_nothing"},
    Named<SignalStorm>{"signal_storm"},
    Named<MonitorRealFrames>{"monitor_real_frames"},
    Named<MonitorNeverRelease>{"monitor_never_release"},
    Named<MonitorVersusNone>{"monitor_versus_none"},
    Named<MonitorCannotWrite>{"monitor_cannot_write"},
    Named<RealDemo<DemoPlace::AsBuilt>>{"demo_as_built"},
    Named<RealDemo<DemoPlace::CopyUnderABase>>{"demo_copy_under_a_base"},
    Named<RealDemo<DemoPlace::CopyWithNoBase>>{"demo_copy_with_no_base"},
    Named<CursorAllEdges>{"cursor_all_edges"},
    Named<CursorNoProtocol>{"cursor_no_protocol"},
    Named<CursorSeatRemoved>{"cursor_seat_removed"},
    Named<CursorPointerFlap>{"cursor_pointer_flap"},
    Named<CursorManagerRemoved>{"cursor_manager_removed"},
    Named<CursorManagerLate>{"cursor_manager_late"},
    Named<CursorDuringDrag>{"cursor_during_drag"},
    Named<GridAxisHostile>{"grid_axis_hostile"},
    Named<GridAxisNoEnter>{"grid_axis_no_enter"},
    Named<GridWheelThenPixels>{"grid_wheel_then_pixels"},
    Named<GridResizeDuringThumbDrag>{"grid_resize_during_thumb_drag"},
    Named<GridPointerGoneDuringCapture>{"grid_pointer_gone_during_capture"},
    Named<GridSeatGoneDuringCapture>{"grid_seat_gone_during_capture"},
    Named<GridCursor>{"grid_cursor"},
    Named<ViewerFirstFrame>{"viewer_first_frame"},
    Named<ViewerSectionSwitch>{"viewer_section_switch"},
    Named<ViewerWheelScroll>{"viewer_wheel_scroll"},
    Named<ViewerResize>{"viewer_resize"},
    Named<ViewerRefused<ViewerFile::Missing>>{"viewer_missing_file"},
    Named<ViewerRefused<ViewerFile::Malformed>>{"viewer_malformed_file"},
    Named<ViewerRefused<ViewerFile::Symlink>>{"viewer_refused_symlink"},
    Named<ViewerRefused<ViewerFile::PathNotText>>{"viewer_path_not_text"},
    Named<ViewerRefused<ViewerFile::NoBase>>{"viewer_copy_with_no_base"},
    Named<ViewerFileInItsBase>{"viewer_file_in_its_base"},
    Named<ViewerLargeFile>{"viewer_65536_rows"},
    Named<SettingsDemo<SettingsDemoRun::Records>>{"settings_demo_records"},
    Named<SettingsDemo<SettingsDemoRun::BothFlags>>{"settings_demo_both_flags"},
    Named<SettingsDemo<SettingsDemoRun::NoBase>>{"settings_demo_no_base"},
    Named<ViewerSettings<ViewerFile::SettingsGood>>{"settings_viewer_items"},
    Named<ViewerSettings<ViewerFile::SettingsBadRow>>{"settings_viewer_bad_row"},
    Named<ViewerSettings<ViewerFile::SettingsMissing>>{"settings_viewer_missing_file"},
    Named<ViewerSettings<ViewerFile::OnlySettings>>{"settings_viewer_only_settings"},
    Named<ViewerSettings<ViewerFile::SettingsMalformed>>{"settings_viewer_malformed_file"},
    Named<ViewerSettings<ViewerFile::BothInBase>>{"settings_viewer_files_in_its_base"},
};

}  // namespace

int main(int argc, char** argv) {
    const auto arguments = jgui::program_arguments(argc, argv);
    if (arguments && arguments->size() == 3 && arguments->at(1) == "--expect-count") {
        // CMake's list and this one must name the same number of scenarios, or some would never run.
        const std::string count = std::to_string(std::tuple_size_v<decltype(scenarios)>);
        if (arguments->at(2) == count) return 0;
        std::cerr << "CMakeLists.txt lists " << arguments->at(2) << " scenarios and this file has " << count << '\n';
        return 1;
    }
    if (!arguments || arguments->size() != 2) {
        std::cerr << "usage: test_fake_compositor <scenario> | --expect-count <n>\n";
        return 2;
    }
    const std::string& wanted = arguments->at(1);
    bool found = false;
    bool passed = false;
    std::apply(
        [&]<class... Script>(const Named<Script>&... named) {
            const auto try_one = [&]<class S>(const Named<S>& entry) {
                if (entry.name == wanted) {
                    found = true;
                    passed = run_one<S>();
                }
            };
            (try_one(named), ...);
        },
        scenarios);
    if (!found) {
        std::cerr << "no such scenario: " << wanted << '\n';
        return 2;
    }
    std::cout << wanted << ": " << (passed ? "passed" : "FAILED") << '\n';
    return passed ? 0 : 1;
}
