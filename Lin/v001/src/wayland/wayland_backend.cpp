// File: src/wayland/wayland_backend.cpp
// Purpose: The Wayland backend: connection, window, buffer files, seat and event loop. Every
//          value from the compositor is treated as untrusted. Listener handlers only record what
//          happened; the toolkit sees it after dispatch has returned.
#include "jCppGUI/v001/wayland_backend.hpp"

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "../c_resource.hpp"
#include "file_io.hpp"
#include "trampoline.hpp"
#include "untrusted.hpp"
#include "wire_access.hpp"
#include "xdg-shell-client-protocol.h"

namespace jgui::inline v001 {

static_assert(detail::WireAccess::number(Key::Return) == XKB_KEY_Return && detail::WireAccess::number(Key::Space) == XKB_KEY_space);
static_assert(detail::keymap_format_xkb_v1 == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);
static_assert(detail::keymap_format_none == WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP);

namespace {

constexpr std::uint32_t compositor_version = 4;  // for wl_surface.damage_buffer
constexpr std::uint32_t shm_version = 1;
constexpr std::uint32_t wm_base_version = 2;
constexpr std::uint32_t seat_version = 5;  // for release requests and pointer frames

constexpr std::size_t max_frame_buffers = 4;
constexpr std::int32_t min_window_side = 120;

[[nodiscard]] constexpr std::size_t sat_add_size(std::size_t a, std::size_t b) noexcept {
    return a > std::numeric_limits<std::size_t>::max() - b ? std::numeric_limits<std::size_t>::max() : a + b;
}

using Display = detail::CResource<wl_display, wl_display_disconnect>;
using Registry = detail::CResource<wl_registry, wl_registry_destroy>;
using Compositor = detail::CResource<wl_compositor, wl_compositor_destroy>;
using Shm = detail::CResource<wl_shm, wl_shm_destroy>;
using ShmPool = detail::CResource<wl_shm_pool, wl_shm_pool_destroy>;
using WaylandBuffer = detail::CResource<wl_buffer, wl_buffer_destroy>;
using WmBase = detail::CResource<xdg_wm_base, xdg_wm_base_destroy>;
using Seat = detail::CResource<wl_seat, wl_seat_release>;
using PointerObject = detail::CResource<wl_pointer, wl_pointer_release>;
using KeyboardObject = detail::CResource<wl_keyboard, wl_keyboard_release>;
using Surface = detail::CResource<wl_surface, wl_surface_destroy>;
using XdgSurface = detail::CResource<xdg_surface, xdg_surface_destroy>;
using Toplevel = detail::CResource<xdg_toplevel, xdg_toplevel_destroy>;

// One ARGB8888 buffer: a file the compositor reads, which this process never maps. Frames are drawn in
// ordinary memory and copied in. It is never resized: a new size is a new buffer, and an old one goes
// away only after the compositor has released it.
class ShmBuffer {
public:
    [[nodiscard]] static std::expected<std::unique_ptr<ShmBuffer>, Error> create(wl_shm* shm, Size size);

    ShmBuffer(detail::UniqueFd file, WaylandBuffer buffer, Size size, std::size_t pixel_count) noexcept
        : file_{std::move(file)}, buffer_{std::move(buffer)}, size_{size}, pixel_count_{pixel_count} {}

    void on_release(wl_buffer*) noexcept { busy_ = false; }

    [[nodiscard]] Size size() const noexcept { return size_; }
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    void mark_busy() noexcept { busy_ = true; }
    [[nodiscard]] wl_buffer* object() const noexcept { return buffer_.get(); }
    [[nodiscard]] std::size_t byte_count() const noexcept { return pixel_count_ * sizeof(std::uint32_t); }

    // Copies one whole picture to the file. Refused while the compositor still holds the file.
    [[nodiscard]] std::expected<void, Error> write(std::span<const std::uint32_t> pixels) const {
        if (busy_) return make_error(ErrorCode::System, "a buffer the compositor holds cannot be written");
        if (pixels.size() != pixel_count_) return make_error(ErrorCode::SizeLimit, "the picture does not fit this buffer");
        return detail::write_at(file_.get(), pixels, 0);
    }

private:
    detail::UniqueFd file_;
    WaylandBuffer buffer_;
    Size size_;
    std::size_t pixel_count_;
    bool busy_{};
};

constexpr wl_buffer_listener buffer_listener{
    .release = &detail::Trampoline<&ShmBuffer::on_release>::call,
};

std::expected<std::unique_ptr<ShmBuffer>, Error> ShmBuffer::create(wl_shm* shm, Size size) {
    // The validation comes first: nothing below does arithmetic on an unchecked size.
    auto bytes = PixelBuffer::byte_size(size);
    if (!bytes) return std::unexpected(std::move(bytes.error()));
    if (*bytes == 0) return make_error(ErrorCode::SizeLimit, "a window buffer cannot be empty");

    auto file = detail::create_buffer_file(*bytes);
    if (!file) return std::unexpected(std::move(file.error()));

    const ShmPool pool{wl_shm_create_pool(shm, file->get(), saturate_to<std::int32_t>(*bytes))};
    if (!pool) return make_error(ErrorCode::System, "wl_shm_create_pool failed");
    WaylandBuffer object{wl_shm_pool_create_buffer(pool.get(), 0, size.width(), size.height(),
                                                   sat_mul(size.width(), 4), WL_SHM_FORMAT_ARGB8888)};
    if (!object) return make_error(ErrorCode::System, "wl_shm_pool_create_buffer failed");

    auto buffer = std::make_unique<ShmBuffer>(std::move(*file), std::move(object), size, *bytes / sizeof(std::uint32_t));
    if (!detail::listen<wl_buffer_add_listener>(buffer->object(), buffer_listener, *buffer))
        return make_error(ErrorCode::System, "the buffer's listener was refused");
    return buffer;
}

// The arrow drawn for the pointer: X is the outline and a dot the fill. Without a cursor of our own
// the pointer's look over the window is left undefined by the protocol.
constexpr std::array<std::string_view, 17> cursor_shape{
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ",
    "X.....X     ", "X......X    ", "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ",
    "X.X X..X    ", "XX  X..X    ", "X    X..X   ", "     X..X   ", "      XX    ",
};

static_assert(std::ranges::all_of(cursor_shape, [](std::string_view row) { return row.size() == cursor_shape.front().size(); }),
              "every cursor row must have the same width");

[[nodiscard]] constexpr std::uint32_t cursor_colour(char cell) noexcept {
    if (cell == 'X') return 0xFF000000U;
    if (cell == '.') return 0xFFFFFFFFU;
    return 0x00000000U;
}
static_assert(cursor_colour('X') == 0xFF000000U && cursor_colour('.') == 0xFFFFFFFFU && cursor_colour(' ') == 0U);

[[nodiscard]] constexpr std::uint32_t resize_edge_value(ResizeEdge edge) noexcept {
    switch (edge) {
        case ResizeEdge::Top: return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
        case ResizeEdge::Bottom: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
        case ResizeEdge::Left: return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
        case ResizeEdge::Right: return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
        case ResizeEdge::TopLeft: return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
        case ResizeEdge::TopRight: return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
        case ResizeEdge::BottomLeft: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
        case ResizeEdge::BottomRight: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
        default: return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
    }
}

}  // namespace

namespace detail {

struct WaylandImpl {
    explicit WaylandImpl(BackendEventQueue& queue, Size fallback) noexcept : events{queue}, chosen_size{fallback} {}

    // --- Listener handlers: each records what happened and nothing here can throw across C ---
    void on_global(wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version) noexcept;
    void on_global_remove(wl_registry*, std::uint32_t name) noexcept;
    void on_ping(xdg_wm_base* base, std::uint32_t serial) noexcept { xdg_wm_base_pong(base, serial); }
    void on_surface_configure(xdg_surface* surface, std::uint32_t serial) noexcept;
    void on_toplevel_configure(xdg_toplevel*, std::int32_t width, std::int32_t height, wl_array*) noexcept;
    void on_toplevel_close(xdg_toplevel*) noexcept { record(CloseEvent{}); }
    void on_seat_capabilities(wl_seat* object, std::uint32_t capabilities) noexcept;
    void on_seat_name(wl_seat*, const char*) noexcept {}
    void on_pointer_enter(wl_pointer* object, std::uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) noexcept;
    void on_pointer_leave(wl_pointer*, std::uint32_t, wl_surface*) noexcept { record(PointerLeave{}); }
    void on_pointer_motion(wl_pointer*, std::uint32_t, wl_fixed_t x, wl_fixed_t y) noexcept;
    void on_pointer_button(wl_pointer*, std::uint32_t serial, std::uint32_t, std::uint32_t button,
                           std::uint32_t state) noexcept;
    void on_pointer_axis(wl_pointer*, std::uint32_t, std::uint32_t, wl_fixed_t) noexcept {}
    void on_pointer_frame(wl_pointer*) noexcept {}
    void on_pointer_axis_source(wl_pointer*, std::uint32_t) noexcept {}
    void on_pointer_axis_stop(wl_pointer*, std::uint32_t, std::uint32_t) noexcept {}
    void on_pointer_axis_discrete(wl_pointer*, std::uint32_t, std::int32_t) noexcept {}
    void on_keymap(wl_keyboard*, std::uint32_t format, std::int32_t fd, std::uint32_t size) noexcept;
    void on_keyboard_enter(wl_keyboard*, std::uint32_t, wl_surface*, wl_array*) noexcept {}
    void on_keyboard_leave(wl_keyboard*, std::uint32_t, wl_surface*) noexcept {}
    void on_key(wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t key, std::uint32_t state) noexcept;
    void on_modifiers(wl_keyboard*, std::uint32_t, std::uint32_t depressed, std::uint32_t latched,
                      std::uint32_t locked, std::uint32_t group) noexcept;
    void on_repeat_info(wl_keyboard*, std::int32_t, std::int32_t) noexcept {}

    // --- Used from the event loop ---
    [[nodiscard]] std::expected<void, Error> connect(const WaylandOptions& options);
    [[nodiscard]] std::expected<void, Error> pump(bool may_block);
    [[nodiscard]] std::expected<void, Error> create_cursor();
    [[nodiscard]] std::optional<Error> take_failure() noexcept { return std::exchange(failure, std::nullopt); }
    [[nodiscard]] Error connection_error() const;

    // Whether a frame of this size can start, given what the compositor holds now.
    [[nodiscard]] FrameRoom room_for(std::size_t frame_bytes, Size size) const noexcept {
        HeldBuffers held;
        bool reuses_a_buffer = false;
        for (const auto& buffer : buffers) {
            if (buffer->busy()) {
                held.bytes = sat_add_size(held.bytes, buffer->byte_count());
                ++held.count;
            } else if (buffer->size() == size) {
                reuses_a_buffer = true;
            }
        }
        return frame_room(frame_bytes, held, reuses_a_buffer, max_frame_buffers, window_pixel_budget);
    }

    template <class E>
    void record(const E& event) noexcept {
        try {
            if (!events.push(event)) fail(ErrorCode::SizeLimit, "the display sent more events than the queue limit");
        } catch (const std::bad_alloc&) {
            fail(ErrorCode::System, "out of memory while queueing a display event");
        }
    }

    void fail(ErrorCode code, const char* what) noexcept {
        if (failure) return;
        try {
            failure = Error{code, what};
        } catch (const std::bad_alloc&) {
            failure = Error{code, {}};
        }
    }

    BackendEventQueue& events;
    std::optional<Error> failure;

    // Declared first, so it is disconnected after every object below it has been destroyed.
    Display display;
    Registry registry;
    Compositor compositor;
    Shm shm;
    WmBase wm_base;
    Seat seat;
    std::uint32_t seat_name{};
    PointerObject pointer;
    KeyboardObject keyboard;
    Surface surface;
    XdgSurface xdg;
    Toplevel toplevel;

    Surface cursor_surface;
    std::unique_ptr<ShmBuffer> cursor_buffer;

    std::vector<std::unique_ptr<ShmBuffer>> buffers;
    FrameLedger ledger;
    std::vector<std::uint32_t> frame_pixels;  // the picture being drawn, one for all the buffers

    XkbContext xkb_context_object;
    XkbKeymap xkb_keymap_object;
    XkbState xkb_state_object;

    Point pointer_position{};
    Size chosen_size;
    Size pending_size{};
    Size geometry{};
};

void WaylandImplDeleter::operator()(WaylandImpl* impl) const noexcept {
    std::default_delete<WaylandImpl>{}(impl);
}

}  // namespace detail

namespace {

using detail::Trampoline;
using detail::WaylandImpl;

constexpr wl_registry_listener registry_listener{
    .global = &Trampoline<&WaylandImpl::on_global>::call,
    .global_remove = &Trampoline<&WaylandImpl::on_global_remove>::call,
};
constexpr xdg_wm_base_listener wm_base_listener{
    .ping = &Trampoline<&WaylandImpl::on_ping>::call,
};
constexpr xdg_surface_listener surface_listener{
    .configure = &Trampoline<&WaylandImpl::on_surface_configure>::call,
};
// The two listeners below grow as the protocol does. Only the events of the versions we bind can ever
// arrive, so those are all that is set, and this still compiles against a later libwayland.
constexpr xdg_toplevel_listener toplevel_listener = [] {
    xdg_toplevel_listener listener{};
    listener.configure = &Trampoline<&WaylandImpl::on_toplevel_configure>::call;
    listener.close = &Trampoline<&WaylandImpl::on_toplevel_close>::call;
    return listener;
}();
constexpr wl_seat_listener seat_listener{
    .capabilities = &Trampoline<&WaylandImpl::on_seat_capabilities>::call,
    .name = &Trampoline<&WaylandImpl::on_seat_name>::call,
};
constexpr wl_pointer_listener pointer_listener = [] {
    wl_pointer_listener listener{};
    listener.enter = &Trampoline<&WaylandImpl::on_pointer_enter>::call;
    listener.leave = &Trampoline<&WaylandImpl::on_pointer_leave>::call;
    listener.motion = &Trampoline<&WaylandImpl::on_pointer_motion>::call;
    listener.button = &Trampoline<&WaylandImpl::on_pointer_button>::call;
    listener.axis = &Trampoline<&WaylandImpl::on_pointer_axis>::call;
    listener.frame = &Trampoline<&WaylandImpl::on_pointer_frame>::call;
    listener.axis_source = &Trampoline<&WaylandImpl::on_pointer_axis_source>::call;
    listener.axis_stop = &Trampoline<&WaylandImpl::on_pointer_axis_stop>::call;
    listener.axis_discrete = &Trampoline<&WaylandImpl::on_pointer_axis_discrete>::call;
    return listener;
}();
constexpr wl_keyboard_listener keyboard_listener{
    .keymap = &Trampoline<&WaylandImpl::on_keymap>::call,
    .enter = &Trampoline<&WaylandImpl::on_keyboard_enter>::call,
    .leave = &Trampoline<&WaylandImpl::on_keyboard_leave>::call,
    .key = &Trampoline<&WaylandImpl::on_key>::call,
    .modifiers = &Trampoline<&WaylandImpl::on_modifiers>::call,
    .repeat_info = &Trampoline<&WaylandImpl::on_repeat_info>::call,
};

}  // namespace

namespace detail {

void WaylandImpl::on_global(wl_registry* object, std::uint32_t name, const char* interface,
                            std::uint32_t version) noexcept {
    const std::string_view kind{interface};
    if (kind == wl_compositor_interface.name && !compositor && version >= compositor_version) {
        compositor.reset(bind_global<wl_compositor>(object, name, &wl_compositor_interface, compositor_version));
    } else if (kind == wl_shm_interface.name && !shm) {
        shm.reset(bind_global<wl_shm>(object, name, &wl_shm_interface, shm_version));
    } else if (kind == xdg_wm_base_interface.name && !wm_base) {
        wm_base.reset(bind_global<xdg_wm_base>(object, name, &xdg_wm_base_interface, std::min(version, wm_base_version)));
        if (!wm_base) {
            fail(ErrorCode::System, "xdg_wm_base could not be bound");
            return;
        }
        if (!listen<xdg_wm_base_add_listener>(wm_base.get(), wm_base_listener, *this))
            fail(ErrorCode::System, "the xdg_wm_base listener was refused");
    } else if (kind == wl_seat_interface.name && !seat && version >= seat_version) {
        seat.reset(bind_global<wl_seat>(object, name, &wl_seat_interface, seat_version));
        if (!seat) {
            fail(ErrorCode::System, "wl_seat could not be bound");
            return;
        }
        seat_name = name;
        if (!listen<wl_seat_add_listener>(seat.get(), seat_listener, *this))
            fail(ErrorCode::System, "the wl_seat listener was refused");
    }
}

void WaylandImpl::on_global_remove(wl_registry*, std::uint32_t name) noexcept {
    if (seat && name == seat_name) {
        pointer.reset();
        keyboard.reset();
        seat.reset();
        record(PointerLeave{});
    }
}

void WaylandImpl::on_toplevel_configure(xdg_toplevel*, std::int32_t width, std::int32_t height, wl_array*) noexcept {
    pending_size = configured_size(width, height, chosen_size);
}

void WaylandImpl::on_surface_configure(xdg_surface* object, std::uint32_t serial) noexcept {
    // The ack comes before any commit of the frame that the resize below will produce.
    xdg_surface_ack_configure(object, serial);
    if (!pending_size.empty()) chosen_size = pending_size;
    record(ResizeEvent{chosen_size});
}

void WaylandImpl::on_seat_capabilities(wl_seat* object, std::uint32_t capabilities) noexcept {
    const bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    const bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    if (has_pointer && !pointer) {
        pointer.reset(wl_seat_get_pointer(object));
        if (!pointer) {
            fail(ErrorCode::System, "wl_pointer could not be created");
        } else if (!listen<wl_pointer_add_listener>(pointer.get(), pointer_listener, *this)) {
            fail(ErrorCode::System, "the wl_pointer listener was refused");
        }
    } else if (!has_pointer && pointer) {
        pointer.reset();
        record(PointerLeave{});
    }
    if (has_keyboard && !keyboard) {
        keyboard.reset(wl_seat_get_keyboard(object));
        if (!keyboard) {
            fail(ErrorCode::System, "wl_keyboard could not be created");
        } else if (!listen<wl_keyboard_add_listener>(keyboard.get(), keyboard_listener, *this)) {
            fail(ErrorCode::System, "the wl_keyboard listener was refused");
        }
    } else if (!has_keyboard && keyboard) {
        keyboard.reset();
        xkb_state_object.reset();
        xkb_keymap_object.reset();
    }
}

void WaylandImpl::on_pointer_enter(wl_pointer* object, std::uint32_t serial, wl_surface*, wl_fixed_t x,
                                   wl_fixed_t y) noexcept {
    if (cursor_surface) wl_pointer_set_cursor(object, serial, cursor_surface.get(), 0, 0);
    pointer_position = Point{wl_fixed_to_int(x), wl_fixed_to_int(y)};
    record(PointerMove{pointer_position});
}

void WaylandImpl::on_pointer_motion(wl_pointer*, std::uint32_t, wl_fixed_t x, wl_fixed_t y) noexcept {
    pointer_position = Point{wl_fixed_to_int(x), wl_fixed_to_int(y)};
    record(PointerMove{pointer_position});
}

void WaylandImpl::on_pointer_button(wl_pointer*, std::uint32_t serial, std::uint32_t, std::uint32_t button,
                                    std::uint32_t state) noexcept {
    const ButtonState mapped =
        state == WL_POINTER_BUTTON_STATE_PRESSED ? ButtonState::Pressed : ButtonState::Released;
    record(PointerButton{pointer_position, PointerButtonCode{button}, mapped, Serial{serial}});
}

void WaylandImpl::on_keymap(wl_keyboard*, std::uint32_t format, std::int32_t raw_fd, std::uint32_t size) noexcept {
    // Taking ownership first means the received descriptor is closed on every path.
    UniqueFd fd{raw_fd};
    xkb_state_object.reset();
    xkb_keymap_object.reset();
    // The protocol allows a keyboard with no keymap. It is treated as having no keys.
    if (format == keymap_format_none) return;
    try {
        auto keymap = load_keymap(xkb_context_object.get(), format, std::move(fd), size);
        if (!keymap) {
            fail(keymap.error().code, "the compositor sent a keymap that was refused");
            return;
        }
        xkb_keymap_object = std::move(*keymap);
    } catch (const std::bad_alloc&) {
        fail(ErrorCode::System, "out of memory while loading the keymap");
        return;
    }
    xkb_state_object.reset(xkb_state_new(xkb_keymap_object.get()));
    if (!xkb_state_object) fail(ErrorCode::Keymap, "xkb could not create a keyboard state");
}

void WaylandImpl::on_key(wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t key, std::uint32_t state) noexcept {
    if (!xkb_state_object) return;
    const std::optional<std::uint32_t> code = evdev_to_xkb(key);
    if (!code) return;
    xkb_state* xkb = xkb_state_object.get();
    const std::uint32_t keysym = xkb_state_key_get_one_sym(xkb, *code);
    ModifierMask modifiers{};
    const auto add = [&](const char* name, ModifierMask bit) {
        if (xkb_state_mod_name_is_active(xkb, name, XKB_STATE_MODS_EFFECTIVE) > 0) modifiers = modifiers | bit;
    };
    add(XKB_MOD_NAME_SHIFT, Modifier::Shift);
    add(XKB_MOD_NAME_CTRL, Modifier::Ctrl);
    add(XKB_MOD_NAME_ALT, Modifier::Alt);
    add(XKB_MOD_NAME_LOGO, Modifier::Super);
    record(KeyEvent{Keysym{keysym}, modifiers, state == WL_KEYBOARD_KEY_STATE_PRESSED});
}

void WaylandImpl::on_modifiers(wl_keyboard*, std::uint32_t, std::uint32_t depressed, std::uint32_t latched,
                               std::uint32_t locked, std::uint32_t group) noexcept {
    if (xkb_state_object) xkb_state_update_mask(xkb_state_object.get(), depressed, latched, locked, 0, 0, group);
}

Error WaylandImpl::connection_error() const {
    const int code = wl_display_get_error(display.get());
    std::string message = "the display connection failed (error " + std::to_string(code);
    if (code == EPROTO) {
        const wl_interface* interface = nullptr;
        std::uint32_t id = 0;
        const std::uint32_t protocol_code = wl_display_get_protocol_error(display.get(), &interface, &id);
        message += ", protocol error " + std::to_string(protocol_code) + " on " +
                   (interface != nullptr ? interface->name : "an unknown interface");
    }
    message += ")";
    return Error{code == EPROTO ? ErrorCode::Protocol : ErrorCode::Connect, std::move(message)};
}

std::expected<void, Error> WaylandImpl::create_cursor() {
    const auto height = saturate_to<std::int32_t>(cursor_shape.size());
    const auto width = saturate_to<std::int32_t>(cursor_shape.front().size());
    auto buffer = ShmBuffer::create(shm.get(), Size{width, height});
    if (!buffer) return std::unexpected(std::move(buffer.error()));
    cursor_buffer = std::move(*buffer);

    std::vector<std::uint32_t> pixels;
    pixels.reserve(cursor_shape.size() * cursor_shape.front().size());
    for (const std::string_view row : cursor_shape) {
        for (const char cell : row) pixels.push_back(cursor_colour(cell));
    }
    if (auto written = cursor_buffer->write(pixels); !written) return written;

    cursor_surface.reset(wl_compositor_create_surface(compositor.get()));
    if (!cursor_surface) return make_error(ErrorCode::System, "the cursor surface could not be created");
    wl_surface_attach(cursor_surface.get(), cursor_buffer->object(), 0, 0);
    wl_surface_damage_buffer(cursor_surface.get(), 0, 0, width, height);
    wl_surface_commit(cursor_surface.get());
    return {};
}

std::expected<void, Error> WaylandImpl::connect(const WaylandOptions& options) {
    xkb_context_object = make_keymap_context();
    if (!xkb_context_object) return make_error(ErrorCode::Keymap, "xkb could not create a context");

    display.reset(wl_display_connect(nullptr));
    if (!display) return make_error(ErrorCode::Connect, "cannot connect to a Wayland display");

    registry.reset(wl_display_get_registry(display.get()));
    if (!registry) return make_error(ErrorCode::Connect, "cannot get the Wayland registry");
    if (!listen<wl_registry_add_listener>(registry.get(), registry_listener, *this))
        return make_error(ErrorCode::System, "the registry's listener was refused");
    if (wl_display_roundtrip(display.get()) < 0) return std::unexpected(connection_error());

    if (!compositor) return make_error(ErrorCode::MissingGlobal, "the compositor has no wl_compositor version 4");
    if (!shm) return make_error(ErrorCode::MissingGlobal, "the compositor has no wl_shm");
    if (!wm_base) return make_error(ErrorCode::MissingGlobal, "the compositor has no xdg_wm_base");
    if (!seat) return make_error(ErrorCode::MissingGlobal, "the compositor has no wl_seat version 5");

    if (auto cursor = create_cursor(); !cursor) return cursor;

    surface.reset(wl_compositor_create_surface(compositor.get()));
    if (!surface) return make_error(ErrorCode::System, "the window surface could not be created");
    xdg.reset(xdg_wm_base_get_xdg_surface(wm_base.get(), surface.get()));
    if (!xdg) return make_error(ErrorCode::System, "the xdg surface could not be created");
    if (!listen<xdg_surface_add_listener>(xdg.get(), surface_listener, *this))
        return make_error(ErrorCode::System, "the xdg surface's listener was refused");
    toplevel.reset(xdg_surface_get_toplevel(xdg.get()));
    if (!toplevel) return make_error(ErrorCode::System, "the toplevel could not be created");
    if (!listen<xdg_toplevel_add_listener>(toplevel.get(), toplevel_listener, *this))
        return make_error(ErrorCode::System, "the toplevel's listener was refused");
    xdg_toplevel_set_title(toplevel.get(), options.title.c_str());
    xdg_toplevel_set_app_id(toplevel.get(), options.app_id.c_str());
    xdg_toplevel_set_min_size(toplevel.get(), min_window_side, min_window_side);
    wl_surface_commit(surface.get());
    return {};
}

std::expected<void, Error> WaylandImpl::pump(bool may_block) {
    wl_display* d = display.get();
    while (true) {
        while (wl_display_prepare_read(d) != 0) {
            if (wl_display_dispatch_pending(d) < 0) return std::unexpected(connection_error());
        }
        // From here until read or cancel, this thread holds a read intent.
        short wanted = POLLIN;
        if (wl_display_flush(d) < 0) {
            if (errno != EAGAIN && errno != EINTR) {
                wl_display_cancel_read(d);
                return std::unexpected(connection_error());
            }
            if (errno == EAGAIN) wanted = static_cast<short>(POLLIN | POLLOUT);
        }
        pollfd watch{wl_display_get_fd(d), wanted, 0};
        const int timeout = (may_block && events.empty() && !failure) ? -1 : 0;
        const int ready = ::poll(&watch, 1, timeout);
        if (ready < 0) {
            const int poll_error = errno;
            wl_display_cancel_read(d);
            if (poll_error == EINTR) continue;
            return make_error(ErrorCode::System, "poll on the display failed");
        }
        if ((watch.revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
            if (wl_display_read_events(d) < 0 && errno != EAGAIN) return std::unexpected(connection_error());
        } else {
            wl_display_cancel_read(d);
        }
        if (wl_display_dispatch_pending(d) < 0) return std::unexpected(connection_error());
        break;
    }
    if (std::optional<Error> error = take_failure(); error) return std::unexpected(std::move(*error));
    if (wl_display_get_error(d) != 0) return std::unexpected(connection_error());
    return {};
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
    if (auto connected = impl->connect(options); !connected) return std::unexpected(std::move(connected.error()));
    return WaylandBackend{std::move(events), std::move(impl)};
}

std::expected<void, Error> WaylandBackend::pump(bool may_block) { return impl_->pump(may_block); }

bool WaylandBackend::can_present(Size size) const {
    // A size that is refused goes on to acquire_frame, which reports it.
    const auto bytes = PixelBuffer::byte_size(size);
    return !bytes || impl_->room_for(*bytes, size) != detail::FrameRoom::Wait;
}

std::expected<Frame<WaylandBackend>, Error> WaylandBackend::acquire_frame(Size size) {
    // First: what follows may free the buffer an earlier frame chose, and may then fail.
    const std::uint64_t generation = impl_->ledger.begin();
    auto bytes = PixelBuffer::byte_size(size);
    if (!bytes) return std::unexpected(std::move(bytes.error()));
    const detail::FrameRoom room = impl_->room_for(*bytes, size);
    if (room == detail::FrameRoom::Never) return make_error(ErrorCode::SizeLimit, "one frame of this size is over the memory budget");
    if (room == detail::FrameRoom::Wait) return make_error(ErrorCode::WouldBlock, "the compositor holds too much for this frame yet");

    // Everything the compositor does not hold goes, except one buffer that fits.
    auto& buffers = impl_->buffers;
    bool kept_one = false;
    std::erase_if(buffers, [&](const std::unique_ptr<ShmBuffer>& buffer) {
        if (buffer->busy()) return false;
        if (kept_one || buffer->size() != size) return true;
        kept_one = true;
        return false;
    });
    // The old pixels go before anything new is allocated, so the budget also holds at the peak.
    const std::size_t pixel_count = *bytes / sizeof(std::uint32_t);
    if (impl_->frame_pixels.capacity() != pixel_count) std::vector<std::uint32_t>{}.swap(impl_->frame_pixels);
    std::size_t chosen = buffers.size();
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        if (!buffers[i]->busy()) chosen = i;
    }
    if (chosen == buffers.size()) {
        auto created = ShmBuffer::create(impl_->shm.get(), size);
        if (!created) return std::unexpected(std::move(created.error()));
        buffers.push_back(std::move(*created));
    }
    try {
        impl_->frame_pixels.resize(pixel_count);
    } catch (const std::bad_alloc&) {
        return make_error(ErrorCode::System, "out of memory for the frame's pixels");
    }
    return Frame<WaylandBackend>{generation, chosen, size};
}

std::expected<PixelBuffer, Error> WaylandBackend::frame_pixels(const Frame<WaylandBackend>& frame) {
    if (!impl_->ledger.current(frame.generation_)) return make_error(ErrorCode::StaleFrame, "this frame is no longer the current one");
    return PixelBuffer::create(impl_->frame_pixels, frame.size_, sat_mul(frame.size_.width(), 4));
}

std::expected<void, Error> WaylandBackend::present(Frame<WaylandBackend> frame, Rect damage) {
    if (!impl_->ledger.current(frame.generation_)) return make_error(ErrorCode::StaleFrame, "this frame is no longer the current one");
    impl_->ledger.finish();
    // A current frame's buffer is still in the list, because only a new acquire removes any.
    ShmBuffer& buffer = *impl_->buffers.at(frame.buffer_);
    if (auto written = buffer.write(impl_->frame_pixels); !written) return written;
    wl_surface* surface = impl_->surface.get();
    if (impl_->geometry != buffer.size()) {
        impl_->geometry = buffer.size();
        xdg_surface_set_window_geometry(impl_->xdg.get(), 0, 0, impl_->geometry.width(), impl_->geometry.height());
    }
    buffer.mark_busy();
    wl_surface_attach(surface, buffer.object(), 0, 0);
    wl_surface_damage_buffer(surface, damage.x(), damage.y(), damage.width(), damage.height());
    wl_surface_commit(surface);
    return {};
}

std::expected<void, Error> WaylandBackend::sync() {
    if (wl_display_roundtrip(impl_->display.get()) < 0) return std::unexpected(impl_->connection_error());
    if (std::optional<Error> error = impl_->take_failure(); error) return std::unexpected(std::move(*error));
    return {};
}

void WaylandBackend::begin_move(Serial serial) noexcept {
    if (impl_->seat) xdg_toplevel_move(impl_->toplevel.get(), impl_->seat.get(), detail::WireAccess::number(serial));
}

void WaylandBackend::begin_resize(Serial serial, ResizeEdge edge) noexcept {
    if (impl_->seat)
        xdg_toplevel_resize(impl_->toplevel.get(), impl_->seat.get(), detail::WireAccess::number(serial), resize_edge_value(edge));
}

}  // namespace jgui::inline v001
