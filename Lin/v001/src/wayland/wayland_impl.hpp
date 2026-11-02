// File: src/wayland/wayland_impl.hpp
// Purpose: The Wayland backend's insides: connection, window, buffer files, seat and event loop. Every
//          value from the compositor is treated as untrusted. Listener handlers only record what
//          happened; the toolkit sees it after dispatch has returned. Every C call that can fail goes through a
//          template parameter, so a test can make it fail with no branch here for that.
#pragma once

#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "../c_resource.hpp"
#include "../geometry_access.hpp"
#include "file_io.hpp"
#include "jCppGUI/v001/backend.hpp"
#include "jCppGUI/v001/wayland_backend.hpp"
#include "trampoline.hpp"
#include "untrusted.hpp"
#include "wire_access.hpp"
#include "xdg-shell-client-protocol.h"

namespace jgui::inline v001::detail {

static_assert(WireAccess::number(Key::Return) == XKB_KEY_Return && WireAccess::number(Key::Space) == XKB_KEY_space);
static_assert(keymap_format_xkb_v1 == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);
static_assert(keymap_format_none == WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP);

constexpr std::uint32_t compositor_version = 4;  // for wl_surface.damage_buffer
constexpr std::uint32_t shm_version = 1;
constexpr std::uint32_t wm_base_version = 2;
constexpr std::uint32_t seat_version = 5;  // for release requests and pointer frames

constexpr std::size_t max_wire_string_bytes = 4096;  // libwayland refuses a longer message
constexpr std::size_t max_frame_buffers = 4;
constexpr std::int32_t min_window_side = 120;

[[nodiscard]] constexpr std::size_t sat_add_size(std::size_t a, std::size_t b) noexcept {
    return a > std::numeric_limits<std::size_t>::max() - b ? std::numeric_limits<std::size_t>::max() : a + b;
}

// The requests the objects' destructors just made go out first, so the compositor sees an orderly end. If they cannot,
// nothing is lost: it cleans up after a closed connection anyway.
inline void flush_and_disconnect(wl_display* display) noexcept {
    static_cast<void>(wl_display_flush(display));
    wl_display_disconnect(display);
}
using Display = CResource<wl_display, flush_and_disconnect>;
using Registry = CResource<wl_registry, wl_registry_destroy>;
using Compositor = CResource<wl_compositor, wl_compositor_destroy>;
using Shm = CResource<wl_shm, wl_shm_destroy>;
using ShmPool = CResource<wl_shm_pool, wl_shm_pool_destroy>;
using WaylandBuffer = CResource<wl_buffer, wl_buffer_destroy>;
using WmBase = CResource<xdg_wm_base, xdg_wm_base_destroy>;
using Seat = CResource<wl_seat, wl_seat_release>;
using PointerObject = CResource<wl_pointer, wl_pointer_release>;
using KeyboardObject = CResource<wl_keyboard, wl_keyboard_release>;
using Surface = CResource<wl_surface, wl_surface_destroy>;
using XdgSurface = CResource<xdg_surface, xdg_surface_destroy>;
using Toplevel = CResource<xdg_toplevel, xdg_toplevel_destroy>;
using SyncCallback = CResource<wl_callback, wl_callback_destroy>;

// The calls into the C libraries that can fail by giving back nothing, or by refusing a listener. They are a template
// parameter only so that a test can make one of them fail: production always uses this one, and nothing here
// has a branch for it.
struct LibWayland {
    [[nodiscard]] static wl_display* connect() noexcept { return wl_display_connect(nullptr); }
    [[nodiscard]] static wl_registry* get_registry(wl_display* display) noexcept { return wl_display_get_registry(display); }
    [[nodiscard]] static wl_callback* sync(wl_display* display) noexcept { return wl_display_sync(display); }
    template <class Global>
    [[nodiscard]] static Global* bind(wl_registry* registry, std::uint32_t name, std::uint32_t version) noexcept {
        return bind_global<Global>(registry, name, version);
    }
    template <auto Add, class Proxy, class Owner, class Table>
    [[nodiscard]] static bool listen(Proxy* proxy, const OwnedListener<Owner, Table>& listener, Owner& owner) noexcept {
        return detail::listen<Add>(proxy, listener, owner);
    }
    [[nodiscard]] static wl_surface* create_surface(wl_compositor* compositor) noexcept { return wl_compositor_create_surface(compositor); }
    [[nodiscard]] static xdg_surface* get_xdg_surface(xdg_wm_base* base, wl_surface* surface) noexcept { return xdg_wm_base_get_xdg_surface(base, surface); }
    [[nodiscard]] static xdg_toplevel* get_toplevel(xdg_surface* surface) noexcept { return xdg_surface_get_toplevel(surface); }
    [[nodiscard]] static wl_pointer* get_pointer(wl_seat* seat) noexcept { return wl_seat_get_pointer(seat); }
    [[nodiscard]] static wl_keyboard* get_keyboard(wl_seat* seat) noexcept { return wl_seat_get_keyboard(seat); }
    [[nodiscard]] static wl_shm_pool* create_pool(wl_shm* shm, int fd, std::int32_t bytes) noexcept { return wl_shm_create_pool(shm, fd, bytes); }
    [[nodiscard]] static wl_buffer* create_buffer(wl_shm_pool* pool, std::int32_t width, std::int32_t height, std::int32_t stride) noexcept {
        return wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    }
    [[nodiscard]] static xkb_state* new_keyboard_state(xkb_keymap* keymap) noexcept { return xkb_state_new(keymap); }
    [[nodiscard]] static XkbContext keymap_context() noexcept { return make_keymap_context(); }
};

// One ARGB8888 buffer: a file the compositor reads, which this process never maps. Frames are drawn in
// ordinary memory and copied in. It is never resized: a new size is a new buffer, and an old one goes
// away only after the compositor has released it.
template <class Api>
class ShmBuffer {
public:
    [[nodiscard]] static std::expected<std::unique_ptr<ShmBuffer>, Error> create(wl_shm* shm, Size size);

    ShmBuffer(UniqueFd file, WaylandBuffer buffer, Size size, std::size_t pixel_count) noexcept
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
        return write_at(file_.get(), pixels, 0);
    }

private:
    UniqueFd file_;
    WaylandBuffer buffer_;
    Size size_;
    std::size_t pixel_count_;
    bool busy_{};
};

template <class Api>
std::expected<std::unique_ptr<ShmBuffer<Api>>, Error> ShmBuffer<Api>::create(wl_shm* shm, Size size) {
    // The validation comes first: nothing below does arithmetic on an unchecked size.
    auto bytes = PixelBuffer::byte_size(size);
    if (!bytes) return std::unexpected(std::move(bytes.error()));
    if (*bytes == 0) return make_error(ErrorCode::SizeLimit, "a window buffer cannot be empty");

    auto file = create_buffer_file(*bytes);
    if (!file) return std::unexpected(std::move(file.error()));

    const ShmPool pool{Api::create_pool(shm, file->get(), saturate_to<std::int32_t>(*bytes))};
    if (!pool) return make_error(ErrorCode::System, "wl_shm_create_pool failed");
    WaylandBuffer object{Api::create_buffer(pool.get(), GeometryAccess::number(size.width()), GeometryAccess::number(size.height()), PixelBuffer::tight_stride(size))};
    if (!object) return make_error(ErrorCode::System, "wl_shm_pool_create_buffer failed");

    using Listener = OwnedListener<ShmBuffer, wl_buffer_listener>;
    static constexpr Listener listener{{.release = Listener::template on<&ShmBuffer::on_release>}};
    auto buffer = std::make_unique<ShmBuffer>(std::move(*file), std::move(object), size, *bytes / sizeof(std::uint32_t));
    if (!Api::template listen<wl_buffer_add_listener>(buffer->object(), listener, *buffer))
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
        case ResizeEdge::TopSide: return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
        case ResizeEdge::BottomSide: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
        case ResizeEdge::LeftSide: return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
        case ResizeEdge::RightSide: return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
        case ResizeEdge::TopLeft: return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
        case ResizeEdge::TopRight: return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
        case ResizeEdge::BottomLeft: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
        case ResizeEdge::BottomRight: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
        default: return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
    }
}

template <>
struct GlobalInterface<wl_compositor> {
    static constexpr const wl_interface* value = &wl_compositor_interface;
};
template <>
struct GlobalInterface<wl_shm> {
    static constexpr const wl_interface* value = &wl_shm_interface;
};
template <>
struct GlobalInterface<xdg_wm_base> {
    static constexpr const wl_interface* value = &xdg_wm_base_interface;
};
template <>
struct GlobalInterface<wl_seat> {
    static constexpr const wl_interface* value = &wl_seat_interface;
};

// Whether reading the display may sleep: never, only while nothing is queued for the toolkit, or until something comes.
enum class PumpWait : std::uint8_t { Never, WhenIdle, Always };

// What one roundtrip waits for.
struct SyncWait {
    bool done{};
    void on_done(wl_callback*, std::uint32_t) noexcept { done = true; }
};

using SyncListener = OwnedListener<SyncWait, wl_callback_listener>;
inline constexpr SyncListener sync_listener{wl_callback_listener{.done = SyncListener::on<&SyncWait::on_done>}};

template <class Api>
struct BasicWaylandImpl {
    explicit BasicWaylandImpl(BackendEventQueue& queue, Size fallback) noexcept : events{queue}, chosen_size{fallback} {}

    // --- Listener handlers: each records what happened and nothing here can throw across C ---
    void on_global(wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version) noexcept;
    void on_global_remove(wl_registry*, std::uint32_t name) noexcept;
    void on_ping(xdg_wm_base* base, std::uint32_t serial) noexcept { xdg_wm_base_pong(base, serial); }
    void on_surface_configure(xdg_surface* surface, std::uint32_t serial) noexcept;
    void on_toplevel_configure(xdg_toplevel*, std::int32_t width, std::int32_t height, wl_array*) noexcept;
    void on_toplevel_close(xdg_toplevel*) noexcept { record(CloseEvent{}); }
    void on_toplevel_bounds(xdg_toplevel*, std::int32_t, std::int32_t) noexcept {}
    void on_toplevel_capabilities(xdg_toplevel*, wl_array*) noexcept {}
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
    void on_pointer_axis_value120(wl_pointer*, std::uint32_t, std::int32_t) noexcept {}
    void on_pointer_axis_relative_direction(wl_pointer*, std::uint32_t, std::uint32_t) noexcept {}
    void on_pointer_warp(wl_pointer*, wl_fixed_t, wl_fixed_t) noexcept {}
    void on_keymap(wl_keyboard*, std::uint32_t format, std::int32_t fd, std::uint32_t size) noexcept;
    void on_keyboard_enter(wl_keyboard*, std::uint32_t, wl_surface*, wl_array*) noexcept {}
    void on_keyboard_leave(wl_keyboard*, std::uint32_t, wl_surface*) noexcept {}
    void on_key(wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t key, std::uint32_t state) noexcept;
    void on_modifiers(wl_keyboard*, std::uint32_t, std::uint32_t depressed, std::uint32_t latched,
                      std::uint32_t locked, std::uint32_t group) noexcept;
    void on_repeat_info(wl_keyboard*, std::int32_t, std::int32_t) noexcept {}

    // --- Used from the event loop ---
    [[nodiscard]] std::expected<void, Error> connect(const WaylandOptions& options);
    [[nodiscard]] std::expected<void, Error> pump(PumpWait wait);
    [[nodiscard]] std::expected<void, Error> roundtrip();
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
    std::optional<Error> failure{};

    // Declared first, so it is disconnected after every object below it has been destroyed.
    Display display{};
    Registry registry{};
    Compositor compositor{};
    Shm shm{};
    WmBase wm_base{};
    Seat seat{};
    std::uint32_t seat_name{};
    PointerObject pointer{};
    KeyboardObject keyboard{};
    Surface surface{};
    XdgSurface xdg{};
    Toplevel toplevel{};

    Surface cursor_surface{};
    std::unique_ptr<ShmBuffer<Api>> cursor_buffer{};

    std::vector<std::unique_ptr<ShmBuffer<Api>>> buffers{};
    FrameLedger ledger{};
    std::vector<std::uint32_t> frame_pixels{};  // the picture being drawn, one for all the buffers

    XkbContext xkb_context_object{};
    XkbKeymap xkb_keymap_object{};
    XkbState xkb_state_object{};

    Point pointer_position{};
    Size chosen_size;
    Size pending_size{};
    Size geometry{};
};

// One table each, which names exactly the type it casts back to.
template <class Api>
struct Listeners {
    using Impl = BasicWaylandImpl<Api>;

    using Registry = OwnedListener<Impl, wl_registry_listener>;
    static constexpr Registry registry{{
        .global = Registry::template on<&Impl::on_global>,
        .global_remove = Registry::template on<&Impl::on_global_remove>,
    }};
    using WmBase = OwnedListener<Impl, xdg_wm_base_listener>;
    static constexpr WmBase wm_base{{
        .ping = WmBase::template on<&Impl::on_ping>,
    }};
    using XdgSurface = OwnedListener<Impl, xdg_surface_listener>;
    static constexpr XdgSurface surface{{
        .configure = XdgSurface::template on<&Impl::on_surface_configure>,
    }};
    // Every entry is set, also for events of versions never bound. libwayland does not check an event's version against
    // the object's: it aborts the whole program on an empty entry, so a compositor could end this program by
    // sending one. If a later header adds an event, a missing initializer here is a compile error.
    using Toplevel = OwnedListener<Impl, xdg_toplevel_listener>;
    static constexpr Toplevel toplevel{{
        .configure = Toplevel::template on<&Impl::on_toplevel_configure>,
        .close = Toplevel::template on<&Impl::on_toplevel_close>,
        .configure_bounds = Toplevel::template on<&Impl::on_toplevel_bounds>,
        .wm_capabilities = Toplevel::template on<&Impl::on_toplevel_capabilities>,
    }};
    using Seat = OwnedListener<Impl, wl_seat_listener>;
    static constexpr Seat seat{{
        .capabilities = Seat::template on<&Impl::on_seat_capabilities>,
        .name = Seat::template on<&Impl::on_seat_name>,
    }};
    using Pointer = OwnedListener<Impl, wl_pointer_listener>;
    static constexpr Pointer pointer{{
        .enter = Pointer::template on<&Impl::on_pointer_enter>,
        .leave = Pointer::template on<&Impl::on_pointer_leave>,
        .motion = Pointer::template on<&Impl::on_pointer_motion>,
        .button = Pointer::template on<&Impl::on_pointer_button>,
        .axis = Pointer::template on<&Impl::on_pointer_axis>,
        .frame = Pointer::template on<&Impl::on_pointer_frame>,
        .axis_source = Pointer::template on<&Impl::on_pointer_axis_source>,
        .axis_stop = Pointer::template on<&Impl::on_pointer_axis_stop>,
        .axis_discrete = Pointer::template on<&Impl::on_pointer_axis_discrete>,
        .axis_value120 = Pointer::template on<&Impl::on_pointer_axis_value120>,
        .axis_relative_direction = Pointer::template on<&Impl::on_pointer_axis_relative_direction>,
        .warp = Pointer::template on<&Impl::on_pointer_warp>,
    }};
    using Keyboard = OwnedListener<Impl, wl_keyboard_listener>;
    static constexpr Keyboard keyboard{{
        .keymap = Keyboard::template on<&Impl::on_keymap>,
        .enter = Keyboard::template on<&Impl::on_keyboard_enter>,
        .leave = Keyboard::template on<&Impl::on_keyboard_leave>,
        .key = Keyboard::template on<&Impl::on_key>,
        .modifiers = Keyboard::template on<&Impl::on_modifiers>,
        .repeat_info = Keyboard::template on<&Impl::on_repeat_info>,
    }};
};

template <class Api>
void BasicWaylandImpl<Api>::on_global(wl_registry* object, std::uint32_t name, const char* interface,
                            std::uint32_t version) noexcept {
    // Each name it is compared with is a constant from the generated glue, built into this program.
    std::string kind;
    try {
        auto announced = string_from_c(interface, max_wire_string_bytes);
        if (!announced) {
            fail(announced.error().code, "the compositor announced a global with an unusable name");
            return;
        }
        kind = std::move(*announced);
    } catch (const std::bad_alloc&) {
        fail(ErrorCode::System, "out of memory while reading a global's name");
        return;
    }
    if (kind == wl_compositor_interface.name && !compositor && version >= compositor_version) {
        compositor.reset(Api::template bind<wl_compositor>(object, name, compositor_version));
    } else if (kind == wl_shm_interface.name && !shm) {
        shm.reset(Api::template bind<wl_shm>(object, name, shm_version));
    } else if (kind == xdg_wm_base_interface.name && !wm_base) {
        wm_base.reset(Api::template bind<xdg_wm_base>(object, name, std::min(version, wm_base_version)));
        if (!wm_base) {
            fail(ErrorCode::System, "xdg_wm_base could not be bound");
            return;
        }
        if (!Api::template listen<xdg_wm_base_add_listener>(wm_base.get(), Listeners<Api>::wm_base, *this))
            fail(ErrorCode::System, "the xdg_wm_base listener was refused");
    } else if (kind == wl_seat_interface.name && !seat && version >= seat_version) {
        seat.reset(Api::template bind<wl_seat>(object, name, seat_version));
        if (!seat) {
            fail(ErrorCode::System, "wl_seat could not be bound");
            return;
        }
        seat_name = name;
        if (!Api::template listen<wl_seat_add_listener>(seat.get(), Listeners<Api>::seat, *this))
            fail(ErrorCode::System, "the wl_seat listener was refused");
    }
}

template <class Api>
void BasicWaylandImpl<Api>::on_global_remove(wl_registry*, std::uint32_t name) noexcept {
    if (seat && name == seat_name) {
        pointer.reset();
        keyboard.reset();
        xkb_state_object.reset();
        xkb_keymap_object.reset();
        seat.reset();
        record(PointerLeave{});
    }
}

template <class Api>
void BasicWaylandImpl<Api>::on_toplevel_configure(xdg_toplevel*, std::int32_t width, std::int32_t height, wl_array*) noexcept {
    pending_size = configured_size(width, height, chosen_size);
}

template <class Api>
void BasicWaylandImpl<Api>::on_surface_configure(xdg_surface* object, std::uint32_t serial) noexcept {
    // The ack comes before any commit of the frame that the resize below will produce.
    xdg_surface_ack_configure(object, serial);
    if (!pending_size.empty()) chosen_size = pending_size;
    record(ResizeEvent{chosen_size});
}

template <class Api>
void BasicWaylandImpl<Api>::on_seat_capabilities(wl_seat* object, std::uint32_t capabilities) noexcept {
    const bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    const bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    if (has_pointer && !pointer) {
        pointer.reset(Api::get_pointer(object));
        if (!pointer) {
            fail(ErrorCode::System, "wl_pointer could not be created");
        } else if (!Api::template listen<wl_pointer_add_listener>(pointer.get(), Listeners<Api>::pointer, *this)) {
            fail(ErrorCode::System, "the wl_pointer listener was refused");
        }
    } else if (!has_pointer && pointer) {
        pointer.reset();
        record(PointerLeave{});
    }
    if (has_keyboard && !keyboard) {
        keyboard.reset(Api::get_keyboard(object));
        if (!keyboard) {
            fail(ErrorCode::System, "wl_keyboard could not be created");
        } else if (!Api::template listen<wl_keyboard_add_listener>(keyboard.get(), Listeners<Api>::keyboard, *this)) {
            fail(ErrorCode::System, "the wl_keyboard listener was refused");
        }
    } else if (!has_keyboard && keyboard) {
        keyboard.reset();
        xkb_state_object.reset();
        xkb_keymap_object.reset();
    }
}

template <class Api>
void BasicWaylandImpl<Api>::on_pointer_enter(wl_pointer* object, std::uint32_t serial, wl_surface*, wl_fixed_t x,
                                   wl_fixed_t y) noexcept {
    if (cursor_surface) wl_pointer_set_cursor(object, serial, cursor_surface.get(), 0, 0);
    pointer_position = Point{Left{wl_fixed_to_int(x)}, Top{wl_fixed_to_int(y)}};
    record(PointerMove{pointer_position});
}

template <class Api>
void BasicWaylandImpl<Api>::on_pointer_motion(wl_pointer*, std::uint32_t, wl_fixed_t x, wl_fixed_t y) noexcept {
    pointer_position = Point{Left{wl_fixed_to_int(x)}, Top{wl_fixed_to_int(y)}};
    record(PointerMove{pointer_position});
}

template <class Api>
void BasicWaylandImpl<Api>::on_pointer_button(wl_pointer*, std::uint32_t serial, std::uint32_t, std::uint32_t button,
                                    std::uint32_t state) noexcept {
    const ButtonState mapped =
        state == WL_POINTER_BUTTON_STATE_PRESSED ? ButtonState::Pressed : ButtonState::Released;
    record(PointerButton{pointer_position, WireAccess::make<PointerButtonCode>(button), mapped, WireAccess::make<Serial>(serial)});
}

template <class Api>
void BasicWaylandImpl<Api>::on_keymap(wl_keyboard*, std::uint32_t format, std::int32_t raw_fd, std::uint32_t size) noexcept {
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
    xkb_state_object.reset(Api::new_keyboard_state(xkb_keymap_object.get()));
    if (!xkb_state_object) fail(ErrorCode::Keymap, "xkb could not create a keyboard state");
}

template <class Api>
void BasicWaylandImpl<Api>::on_key(wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t key, std::uint32_t state) noexcept {
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
    record(KeyEvent{WireAccess::make<Keysym>(keysym), modifiers, state == WL_KEYBOARD_KEY_STATE_PRESSED});
}

template <class Api>
void BasicWaylandImpl<Api>::on_modifiers(wl_keyboard*, std::uint32_t, std::uint32_t depressed, std::uint32_t latched,
                               std::uint32_t locked, std::uint32_t group) noexcept {
    if (xkb_state_object) xkb_state_update_mask(xkb_state_object.get(), depressed, latched, locked, 0, 0, group);
}

template <class Api>
Error BasicWaylandImpl<Api>::connection_error() const {
    const int code = wl_display_get_error(display.get());
    std::string message = "the display connection failed (error " + std::to_string(code);
    if (code == EPROTO) {
        const wl_interface* interface = nullptr;
        std::uint32_t id = 0;
        const std::uint32_t protocol_code = wl_display_get_protocol_error(display.get(), &interface, &id);
        // This name comes from the generated glue, not from the compositor, through libwayland's own table.
        message += ", protocol error " + std::to_string(protocol_code) + " on " +
                   (interface != nullptr ? string_from_c(interface->name, max_wire_string_bytes).value_or("an unnamed interface") : "an unknown interface");
    }
    message += ")";
    return Error{code == EPROTO ? ErrorCode::Protocol : ErrorCode::Connect, std::move(message)};
}

template <class Api>
std::expected<void, Error> BasicWaylandImpl<Api>::create_cursor() {
    const auto height = saturate_to<std::int32_t>(cursor_shape.size());
    const auto width = saturate_to<std::int32_t>(cursor_shape.front().size());
    auto buffer = ShmBuffer<Api>::create(shm.get(), Size{Width{width}, Height{height}});
    if (!buffer) return std::unexpected(std::move(buffer.error()));
    cursor_buffer = std::move(*buffer);

    std::vector<std::uint32_t> pixels;
    pixels.reserve(cursor_shape.size() * cursor_shape.front().size());
    for (const std::string_view row : cursor_shape) {
        for (const char cell : row) pixels.push_back(cursor_colour(cell));
    }
    if (auto written = cursor_buffer->write(pixels); !written) return std::unexpected(std::move(written.error()));

    cursor_surface.reset(Api::create_surface(compositor.get()));
    if (!cursor_surface) return make_error(ErrorCode::System, "the cursor surface could not be created");
    wl_surface_attach(cursor_surface.get(), cursor_buffer->object(), 0, 0);
    wl_surface_damage_buffer(cursor_surface.get(), 0, 0, width, height);
    wl_surface_commit(cursor_surface.get());
    return {};
}

template <class Api>
std::expected<void, Error> BasicWaylandImpl<Api>::connect(const WaylandOptions& options) {
    xkb_context_object = Api::keymap_context();
    if (!xkb_context_object) return make_error(ErrorCode::Keymap, "xkb could not create a context");

    display.reset(Api::connect());
    if (!display) return make_error(ErrorCode::Connect, "cannot connect to a Wayland display");

    registry.reset(Api::get_registry(display.get()));
    if (!registry) return make_error(ErrorCode::Connect, "cannot get the Wayland registry");
    if (!Api::template listen<wl_registry_add_listener>(registry.get(), Listeners<Api>::registry, *this))
        return make_error(ErrorCode::System, "the registry's listener was refused");
    if (auto answered = roundtrip(); !answered) return std::unexpected(std::move(answered.error()));

    if (!compositor) return make_error(ErrorCode::MissingGlobal, "the compositor has no wl_compositor version 4");
    if (!shm) return make_error(ErrorCode::MissingGlobal, "the compositor has no wl_shm");
    if (!wm_base) return make_error(ErrorCode::MissingGlobal, "the compositor has no xdg_wm_base");
    if (!seat) return make_error(ErrorCode::MissingGlobal, "the compositor has no wl_seat version 5");

    if (auto cursor = create_cursor(); !cursor) return std::unexpected(std::move(cursor.error()));

    surface.reset(Api::create_surface(compositor.get()));
    if (!surface) return make_error(ErrorCode::System, "the window surface could not be created");
    xdg.reset(Api::get_xdg_surface(wm_base.get(), surface.get()));
    if (!xdg) return make_error(ErrorCode::System, "the xdg surface could not be created");
    if (!Api::template listen<xdg_surface_add_listener>(xdg.get(), Listeners<Api>::surface, *this))
        return make_error(ErrorCode::System, "the xdg surface's listener was refused");
    toplevel.reset(Api::get_toplevel(xdg.get()));
    if (!toplevel) return make_error(ErrorCode::System, "the toplevel could not be created");
    if (!Api::template listen<xdg_toplevel_add_listener>(toplevel.get(), Listeners<Api>::toplevel, *this))
        return make_error(ErrorCode::System, "the toplevel's listener was refused");
    xdg_toplevel_set_title(toplevel.get(), options.title.c_str());
    xdg_toplevel_set_app_id(toplevel.get(), options.app_id.c_str());
    xdg_toplevel_set_min_size(toplevel.get(), min_window_side, min_window_side);
    wl_surface_commit(surface.get());
    return {};
}

template <class Api>
std::expected<void, Error> BasicWaylandImpl<Api>::pump(PumpWait wait) {
    wl_display* d = display.get();
    bool hung_up = false;
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
        const bool may_sleep = wait == PumpWait::Always || (wait == PumpWait::WhenIdle && events.empty());
        const int timeout = (may_sleep && !failure) ? -1 : 0;
        const int ready = ::poll(&watch, 1, timeout);
        if (ready < 0) {
            const int poll_error = errno;
            wl_display_cancel_read(d);
            if (poll_error == EINTR) continue;
            return make_error(ErrorCode::System, "poll on the display failed");
        }
        hung_up = (watch.revents & (POLLERR | POLLHUP)) != 0;
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
    // libwayland reports the end of the connection only when it holds nothing. With part of a message left over, it
    // answers every read as if more could come, and a poll that sees the hang-up would then never sleep again.
    if (hung_up) {
        int waiting = 0;
        if (::ioctl(wl_display_get_fd(d), FIONREAD, &waiting) != 0 || waiting == 0)
            return make_error(ErrorCode::Connect, "the compositor closed the connection");
    }
    return {};
}

// A wait for the compositor to have handled everything so far. libwayland's own version waits with no regard for a
// hang-up, so this goes through pump, which ends on one. A failure in a handler meanwhile ends it too.
template <class Api>
std::expected<void, Error> BasicWaylandImpl<Api>::roundtrip() {
    const SyncCallback callback{Api::sync(display.get())};
    if (!callback) return make_error(ErrorCode::System, "wl_display_sync failed");
    SyncWait wait;
    if (!Api::template listen<wl_callback_add_listener>(callback.get(), sync_listener, wait))
        return make_error(ErrorCode::System, "the sync callback's listener was refused");
    while (!wait.done) {
        if (auto pumped = pump(PumpWait::Always); !pumped) return std::unexpected(std::move(pumped.error()));
    }
    return {};
}

}  // namespace jgui::inline v001::detail
