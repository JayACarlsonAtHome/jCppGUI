// File: tests/fake_compositor/protocol.hpp
// Purpose: The parts of the Wayland and xdg-shell protocols this client uses, as typed opcodes and typed events. The
//          request numbers are checked against libwayland's generated headers in tests/test_injection.cpp.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "wire.hpp"

namespace jgui_test::wire {

// What kind of thing an object is. It decides how its requests are read.
enum class Kind : std::uint8_t {
    Display, Registry, Callback, Compositor, Shm, ShmPool, Buffer, Surface, Seat, Pointer, Keyboard, WmBase, XdgSurface, Toplevel,
};

namespace request {
enum class Display : std::uint16_t { Sync = 0, GetRegistry = 1 };
enum class Registry : std::uint16_t { Bind = 0 };
enum class Compositor : std::uint16_t { CreateSurface = 0 };
enum class Shm : std::uint16_t { CreatePool = 0 };
enum class ShmPool : std::uint16_t { CreateBuffer = 0, Destroy = 1, Resize = 2 };
enum class Buffer : std::uint16_t { Destroy = 0 };
enum class Surface : std::uint16_t { Destroy = 0, Attach = 1, Damage = 2, Frame = 3, Commit = 6, DamageBuffer = 9 };
enum class Seat : std::uint16_t { GetPointer = 0, GetKeyboard = 1, GetTouch = 2, Release = 3 };
enum class Pointer : std::uint16_t { SetCursor = 0, Release = 1 };
enum class Keyboard : std::uint16_t { Release = 0 };
enum class WmBase : std::uint16_t { Destroy = 0, GetXdgSurface = 2, Pong = 3 };
enum class XdgSurface : std::uint16_t { Destroy = 0, GetToplevel = 1, SetWindowGeometry = 3, AckConfigure = 4 };
enum class Toplevel : std::uint16_t { Destroy = 0, SetTitle = 2, SetAppId = 3, Move = 5, Resize = 6, SetMinSize = 8 };
}  // namespace request

inline constexpr std::int32_t format_argb8888 = 0;
inline constexpr std::uint32_t capability_pointer = 1;
inline constexpr std::uint32_t capability_keyboard = 2;
inline constexpr std::uint32_t keymap_no_keymap = 0;
inline constexpr std::uint32_t keymap_xkb_v1 = 1;
inline constexpr std::uint32_t button_left = 0x110;
inline constexpr std::uint32_t button_pressed = 1;
inline constexpr std::uint32_t button_released = 0;
inline constexpr std::uint32_t resize_edge_left = 4;

// Each event as a typed function, giving the bytes to send. A descriptor goes alongside and is not in these.
namespace event {

[[nodiscard]] constexpr std::vector<std::byte> display_error(ObjectId object, std::uint32_t code, std::string_view message) {
    return MessageWriter{display_id, 0}.object(object).u32(code).string(message).finish();
}
[[nodiscard]] constexpr std::vector<std::byte> delete_id(std::uint32_t id) { return MessageWriter{display_id, 1}.u32(id).finish(); }
[[nodiscard]] constexpr std::vector<std::byte> global(ObjectId registry, std::uint32_t name, std::string_view interface, std::uint32_t version) {
    return MessageWriter{registry, 0}.u32(name).string(interface).u32(version).finish();
}
[[nodiscard]] constexpr std::vector<std::byte> global_remove(ObjectId registry, std::uint32_t name) { return MessageWriter{registry, 1}.u32(name).finish(); }
[[nodiscard]] constexpr std::vector<std::byte> callback_done(ObjectId callback, std::uint32_t data) { return MessageWriter{callback, 0}.u32(data).finish(); }
[[nodiscard]] constexpr std::vector<std::byte> shm_format(ObjectId shm, std::uint32_t format) { return MessageWriter{shm, 0}.u32(format).finish(); }
[[nodiscard]] constexpr std::vector<std::byte> buffer_release(ObjectId buffer) { return MessageWriter{buffer, 0}.finish(); }
[[nodiscard]] constexpr std::vector<std::byte> ping(ObjectId base, std::uint32_t serial) { return MessageWriter{base, 0}.u32(serial).finish(); }
[[nodiscard]] constexpr std::vector<std::byte> surface_configure(ObjectId surface, std::uint32_t serial) { return MessageWriter{surface, 0}.u32(serial).finish(); }
[[nodiscard]] constexpr std::vector<std::byte> toplevel_configure(ObjectId toplevel, std::int32_t width, std::int32_t height) {
    return MessageWriter{toplevel, 0}.i32(width).i32(height).array(std::span<const std::uint32_t>{}).finish();
}
[[nodiscard]] constexpr std::vector<std::byte> toplevel_close(ObjectId toplevel) { return MessageWriter{toplevel, 1}.finish(); }
[[nodiscard]] constexpr std::vector<std::byte> seat_capabilities(ObjectId seat, std::uint32_t capabilities) { return MessageWriter{seat, 0}.u32(capabilities).finish(); }
[[nodiscard]] constexpr std::vector<std::byte> seat_name(ObjectId seat, std::string_view name) { return MessageWriter{seat, 1}.string(name).finish(); }
[[nodiscard]] constexpr std::vector<std::byte> pointer_enter(ObjectId pointer, std::uint32_t serial, ObjectId surface, Fixed x, Fixed y) {
    return MessageWriter{pointer, 0}.u32(serial).object(surface).fixed(x).fixed(y).finish();
}
[[nodiscard]] constexpr std::vector<std::byte> pointer_leave(ObjectId pointer, std::uint32_t serial, ObjectId surface) {
    return MessageWriter{pointer, 1}.u32(serial).object(surface).finish();
}
[[nodiscard]] constexpr std::vector<std::byte> pointer_motion(ObjectId pointer, std::uint32_t time, Fixed x, Fixed y) {
    return MessageWriter{pointer, 2}.u32(time).fixed(x).fixed(y).finish();
}
[[nodiscard]] constexpr std::vector<std::byte> pointer_button(ObjectId pointer, std::uint32_t serial, std::uint32_t time, std::uint32_t button, std::uint32_t state) {
    return MessageWriter{pointer, 3}.u32(serial).u32(time).u32(button).u32(state).finish();
}
[[nodiscard]] constexpr std::vector<std::byte> pointer_frame(ObjectId pointer) { return MessageWriter{pointer, 5}.finish(); }
[[nodiscard]] constexpr std::vector<std::byte> keyboard_keymap(ObjectId keyboard, std::uint32_t format, std::uint32_t size) {
    return MessageWriter{keyboard, 0}.u32(format).u32(size).finish();
}
[[nodiscard]] constexpr std::vector<std::byte> keyboard_enter(ObjectId keyboard, std::uint32_t serial, ObjectId surface) {
    return MessageWriter{keyboard, 1}.u32(serial).object(surface).array(std::span<const std::uint32_t>{}).finish();
}
[[nodiscard]] constexpr std::vector<std::byte> keyboard_key(ObjectId keyboard, std::uint32_t serial, std::uint32_t time, std::uint32_t key, std::uint32_t state) {
    return MessageWriter{keyboard, 3}.u32(serial).u32(time).u32(key).u32(state).finish();
}
[[nodiscard]] constexpr std::vector<std::byte> keyboard_modifiers(ObjectId keyboard, std::uint32_t serial, std::uint32_t depressed, std::uint32_t latched,
                                                                  std::uint32_t locked, std::uint32_t group) {
    return MessageWriter{keyboard, 4}.u32(serial).u32(depressed).u32(latched).u32(locked).u32(group).finish();
}

// Golden bytes, from the protocol's own rules: object, then size and opcode, then the arguments.
consteval bool toplevel_configure_is_on_the_wire_as_specified() {
    const auto bytes = toplevel_configure(ObjectId{5}, 640, -1);
    const std::array<unsigned, 20> expected{5, 0, 0, 0, 0, 0, 20, 0, 0x80, 2, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0};
    if (bytes.size() != expected.size()) return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (std::to_integer<unsigned>(bytes[i]) != expected[i]) return false;
    return true;
}
static_assert(toplevel_configure_is_on_the_wire_as_specified());

}  // namespace event

}  // namespace jgui_test::wire
