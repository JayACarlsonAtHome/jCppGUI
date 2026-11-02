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
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "fake_compositor/harness.hpp"
#include "help_ui.hpp"
#include "jCppGUI/v001/wayland_backend.hpp"

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
    if (session.pointer() && session.window_surface())
        session.queue(event::pointer_enter(*session.pointer(), session.next_serial(), *session.window_surface(), Fixed::from_int(x), Fixed::from_int(y)));
}
void pointer_motion(Session& session, std::int32_t x, std::int32_t y) {
    if (session.pointer()) session.queue(event::pointer_motion(*session.pointer(), 0, Fixed::from_int(x), Fixed::from_int(y)));
}
void pointer_button(Session& session, std::uint32_t button, std::uint32_t state) {
    if (session.pointer()) session.queue(event::pointer_button(*session.pointer(), session.next_serial(), 0, button, state));
}
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
                session.queue(event::pointer_enter(pointer, session.next_serial(), surface, Fixed{x}, Fixed{y}));
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

// More events at once than the toolkit's queue holds, if libwayland ever lets them all through in one read.
struct EventFlood {
    static constexpr ClientEnd end = ends_clean;
    static constexpr std::int32_t count = 70000;
    bool flooded{};
    void on(Session& session, const BufferCommitted& commit) {
        session.release(commit.buffer);
        if (flooded || !session.pointer()) return;
        flooded = true;
        for (std::int32_t i = 0; i < count; ++i) session.queue(event::pointer_motion(*session.pointer(), 0, Fixed::from_int(nowhere_x), Fixed::from_int(nowhere_y)));
        session.close_window();
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
[[nodiscard]] bool run_one() {
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
