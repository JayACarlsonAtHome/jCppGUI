// File: tests/fuzz/fuzz_backend.cpp
// Purpose: Fuzzes the backend's input path: the real WaylandBackend reads what the input says, through a real socket,
//          in the test compositor's wire format, so every listener handler sees it. The input is read as records of
//          an object, an event and arguments, so most of it gets past libwayland's own checks and reaches our code.
//          libwayland itself is not instrumented: see docs/SECURITY.md.
#include <sys/mman.h>  // for memfd_create
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "c_resource.hpp"
#include "fake_compositor/protocol.hpp"
#include "fake_compositor/socket_io.hpp"
#include "file_io.hpp"
#include "jCppGUI/v001/wayland_backend.hpp"

namespace {

namespace wire = jgui_test::wire;
using wire::ObjectId;
using wire::UniqueFd;

// libwayland hands out ids in order. Nothing is deleted before these are all made, so they do not vary.
constexpr ObjectId registry{2};
constexpr ObjectId sync_callback{3};
constexpr ObjectId seat{7};
constexpr ObjectId keyboard{15};
constexpr std::array<std::uint32_t, 16> known_objects{1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 3, 0xDEAD};

struct NoSink {
    void on(const jgui::CloseEvent&) {}
    void on(const jgui::ResizeEvent&) {}
    void on(const jgui::PointerMove&) {}
    void on(const jgui::PointerButton&) {}
    void on(const jgui::PointerLeave&) {}
    void on(const jgui::PointerScroll&) {}
    void on(const jgui::KeyEvent&) {}
};

[[nodiscard]] bool send_all(int socket, const std::vector<std::byte>& bytes, int fd = -1) {
    const auto sent = wire::send_some(socket, bytes, fd);
    return sent && *sent == bytes.size();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t limit = std::size_t{64} * 1024;
    const auto input = jgui::detail::copy_from_c(data, size, limit);
    if (!input) return 0;

    auto pair = wire::socket_pair();
    if (!pair) std::abort();
    // libwayland takes this copy of the descriptor over, and closes it.
    if (::setenv("WAYLAND_SOCKET", std::to_string(::dup(pair->theirs.get())).c_str(), 1) != 0) std::abort();
    pair->theirs = UniqueFd{};

    // What the backend's first roundtrip needs is already waiting when it connects.
    std::vector<std::byte> greeting;
    const auto add = [&](std::vector<std::byte> message) { greeting.insert(greeting.end(), message.begin(), message.end()); };
    add(wire::event::global(registry, 1, "wl_compositor", 4));
    add(wire::event::global(registry, 2, "wl_shm", 1));
    add(wire::event::global(registry, 3, "xdg_wm_base", 2));
    add(wire::event::global(registry, 4, "wl_seat", 5));
    add(wire::event::callback_done(sync_callback, 0));
    if (!send_all(pair->ours.get(), greeting)) std::abort();

    auto backend = jgui::WaylandBackend::create(jgui::WaylandOptions{.title = "t", .app_id = "a", .size = jgui::Size{jgui::Width{64}, jgui::Height{64}}});
    if (!backend) std::abort();

    NoSink sink;
    if (!send_all(pair->ours.get(), wire::event::seat_capabilities(seat, wire::capability_pointer | wire::capability_keyboard))) std::abort();
    if (!backend->process_pending(sink)) std::abort();

    // Each record: which object, which event, how many argument words, and the words. A record whose top bit of the
    // object byte is set is a keymap instead, and the rest of the input is its text.
    std::size_t at = 0;
    while (at + 3 <= input->size()) {
        const std::uint8_t which = (*input)[at];
        const std::uint8_t opcode = (*input)[at + 1];
        const std::uint8_t words = (*input)[at + 2] % 8U;
        at += 3;
        if ((which & 0x80U) != 0) {
            const std::span<const std::uint8_t> text = std::span{*input}.subspan(at);
            const UniqueFd file{::memfd_create("jcppgui-fuzz-backend", MFD_CLOEXEC)};
            if (file.get() < 0 || !jgui::detail::write_at<std::uint8_t>(file.get(), text, 0)) std::abort();
            if (!send_all(pair->ours.get(), wire::event::keyboard_keymap(keyboard, wire::keymap_xkb_v1, static_cast<std::uint32_t>(text.size())), file.get())) return 0;
            at = input->size();
        } else {
            wire::MessageWriter writer{ObjectId{known_objects[which % known_objects.size()]}, static_cast<std::uint16_t>(opcode % 8U)};
            for (std::uint8_t word = 0; word < words && at + 4 <= input->size(); ++word, at += 4) {
                writer.u32(std::uint32_t{(*input)[at]} | (std::uint32_t{(*input)[at + 1]} << 8U) | (std::uint32_t{(*input)[at + 2]} << 16U) | (std::uint32_t{(*input)[at + 3]} << 24U));
            }
            if (!send_all(pair->ours.get(), writer.finish())) return 0;
        }
        if (!backend->process_pending(sink)) return 0;
    }
    return 0;
}
