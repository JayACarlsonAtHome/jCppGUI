// File: tests/test_fake_compositor_rules.cpp
// Purpose: A checker that never fires proves nothing, so this breaks each rule the test compositor holds a client to, in
//          turn, by sending it hand-made requests through a real socket, and requires the violation that names it. There
//          is no client process here: the requests are all waiting in the socket when the compositor runs.
#include <sys/mman.h>  // for memfd_create

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "check.hpp"
#include "fake_compositor/compositor.hpp"

namespace {

using namespace jgui_test::fake;
namespace wire = jgui_test::wire;
using wire::MessageWriter;

// Ids as the requests below use them. The test keeps to these, so what breaks a rule is the one thing added to each.
constexpr ObjectId registry{2};
constexpr ObjectId compositor_object{3};
constexpr ObjectId shm{4};
constexpr ObjectId wm_base{5};
constexpr ObjectId seat{6};
constexpr ObjectId surface{7};
constexpr ObjectId xdg_surface{8};
constexpr ObjectId toplevel{9};
constexpr ObjectId pool{10};
constexpr ObjectId buffer{11};
constexpr ObjectId pointer{13};
constexpr ObjectId cursor_manager{14};
constexpr ObjectId cursor_device{15};

// Sends hand-made requests through one end of a socket pair.
class Client {
public:
    explicit Client(UniqueFd socket) : socket_{std::move(socket)} {}

    Client& request(std::vector<std::byte> bytes, int fd = -1) {
        const auto sent = wire::send_some(socket_.get(), bytes, fd);
        if (!sent || *sent != bytes.size()) failed_ = true;
        return *this;
    }
    Client& bind(std::uint32_t name, std::string_view interface, std::uint32_t version, ObjectId id) {
        return request(MessageWriter{registry, 0}.u32(name).string(interface).u32(version).object(id).finish());
    }
    // Everything up to a window that has made its first commit and is ready for a configure.
    Client& up_to_window() {
        request(MessageWriter{wire::display_id, 1}.object(registry).finish());
        bind(compositor_name, "wl_compositor", 4, compositor_object);
        bind(shm_name, "wl_shm", 1, shm);
        bind(wm_base_name, "xdg_wm_base", 2, wm_base);
        bind(seat_name, "wl_seat", 5, seat);
        request(MessageWriter{compositor_object, 0}.object(surface).finish());
        request(MessageWriter{wm_base, 2}.object(xdg_surface).object(surface).finish());
        request(MessageWriter{xdg_surface, 1}.object(toplevel).finish());
        return request(MessageWriter{surface, 6}.finish());
    }
    Client& pool_of(std::string_view file_bytes, std::int32_t size) {
        const UniqueFd file{::memfd_create("jcppgui-rules", MFD_CLOEXEC)};
        static_cast<void>(jgui::detail::write_at(file.get(), std::span{file_bytes}, 0));
        return request(MessageWriter{shm, 0}.object(pool).i32(size).finish(), file.get());
    }
    Client& a_buffer(ObjectId id, std::int32_t width, std::int32_t height, std::int32_t stride, std::uint32_t format = 0, std::int32_t offset = 0) {
        return request(MessageWriter{pool, 0}.object(id).i32(offset).i32(width).i32(height).i32(stride).u32(format).finish());
    }
    Client& attach_damage_commit(ObjectId id) {
        request(MessageWriter{surface, 1}.object(id).i32(0).i32(0).finish());
        request(MessageWriter{surface, 9}.i32(0).i32(0).i32(2).i32(2).finish());
        return request(MessageWriter{surface, 6}.finish());
    }
    // A pointer, and a cursor shape device for it, on top of a window.
    Client& up_to_cursor_device() {
        up_to_window().request(MessageWriter{seat, 0}.object(pointer).finish());
        bind(cursor_shape_name, "wp_cursor_shape_manager_v1", 1, cursor_manager);
        return request(MessageWriter{cursor_manager, 1}.object(cursor_device).object(pointer).finish());
    }
    Client& set_shape(std::uint32_t serial, std::uint32_t shape) { return request(MessageWriter{cursor_device, 1}.u32(serial).u32(shape).finish()); }
    Client& ack(std::uint32_t serial) { return request(MessageWriter{xdg_surface, 4}.u32(serial).finish()); }
    void finish() { socket_ = UniqueFd{}; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    UniqueFd socket_;
    bool failed_{};
};

// Answers the first commit with a 2x2 configure, whose serial comes out as 1. Holds what is committed. Nothing else.
struct PlainScript {
    void on(Session& session, const SurfaceReady&) { static_cast<void>(session.configure(2, 2)); }
    void on(Session&, const BufferCommitted&) {}
    void on(Session&, const SeatBound&) {}
};

// Writes to the buffer's file as though the client had, while it is held, and then releases it.
struct WritesWhileHeld {
    void on(Session& session, const SurfaceReady&) { static_cast<void>(session.configure(2, 2)); }
    void on(Session&, const SeatBound&) {}
    void on(Session& session, const BufferCommitted& commit) {
        const std::array<std::uint32_t, 1> changed{0x12345678U};
        static_cast<void>(jgui::detail::write_at<std::uint32_t>(session.pool(0).file.get(), changed, 0));
        session.release(commit.buffer);
    }
};

// The window as PlainScript has it, with the pointer coming in as soon as there is one: its serial comes out as 2.
struct EntersAtOnce {
    void on(Session& session, const SurfaceReady&) { static_cast<void>(session.configure(2, 2)); }
    void on(Session&, const BufferCommitted&) {}
    void on(Session&, const SeatBound&) {}
    void on(Session& session, const PointerCreated&) { session.pointer_enter(wire::Fixed::from_int(1), wire::Fixed::from_int(1)); }
};
constexpr std::uint32_t enter_serial = 2;

// The same, and the pointer has left again by the time anything is asked of it.
struct EntersAndLeaves {
    void on(Session& session, const SurfaceReady&) { static_cast<void>(session.configure(2, 2)); }
    void on(Session&, const BufferCommitted&) {}
    void on(Session&, const SeatBound&) {}
    void on(Session& session, const PointerCreated&) {
        session.pointer_enter(wire::Fixed::from_int(1), wire::Fixed::from_int(1));
        session.pointer_leave();
    }
};

template <class Script = PlainScript, class Requests>
[[nodiscard]] std::vector<std::string> violations_from(Requests requests, std::size_t* commits = nullptr) {
    auto pair = wire::socket_pair();
    if (!pair) return {"no socket pair"};
    Client client{std::move(pair->theirs)};
    requests(client);
    if (client.failed()) return {"the requests did not all go"};
    client.finish();
    Session session{std::move(pair->ours), GlobalSet{}};
    Script script{};
    Compositor<Script> compositor{session, script};
    if (!compositor.run(std::chrono::milliseconds{10000}, std::chrono::milliseconds{1000})) return {"it ran out of time"};
    if (commits != nullptr) *commits = session.commits_with_buffer();
    return session.violations();
}

template <class Script = PlainScript, class Requests>
void must_see(jgui_test::Checker& check, std::string_view wanted, Requests requests) {
    bool found = false;
    const auto seen = violations_from<Script>(requests);
    for (const std::string& violation : seen) found = found || violation.contains(wanted);
    if (!found) {
        std::cerr << "wanted a violation about \"" << wanted << "\", and saw:\n";
        for (const std::string& violation : seen) std::cerr << "  " << violation << '\n';
    }
    check(found, "a rule was broken and the compositor did not say so");
}

constexpr std::string_view pixels = "pppppppppppppppp";

}  // namespace

int main() {
    jgui_test::Checker check;

    // The control: this much is all legal, and the same steps are in the cases below.
    std::size_t commits = 0;
    const auto clean = violations_from(
        [&](Client& c) {
        c.up_to_window().ack(1).pool_of(pixels, 16).a_buffer(buffer, 2, 2, 8);
        c.request(MessageWriter{xdg_surface, 3}.i32(0).i32(0).i32(2).i32(2).finish());
        c.attach_damage_commit(buffer);
        },
        &commits);
    check(commits == 1, "and the control really got as far as a commit");
    for (const std::string& violation : clean) std::cerr << "unexpected: " << violation << '\n';
    check(clean.empty(), "a correct client is not accused of anything");

    must_see(check, "does not exist", [](Client& c) { c.request(MessageWriter{ObjectId{99}, 0}.finish()); });
    must_see(check, "unexpected request", [](Client& c) { c.request(MessageWriter{wire::display_id, 77}.finish()); });
    must_see(check, "malformed", [](Client& c) { c.request(MessageWriter{wire::display_id, 0}.finish()).request(std::vector<std::byte>(8, std::byte{0xFF})); });
    must_see(check, "bad arguments", [](Client& c) { c.request(MessageWriter{wire::display_id, 0}.finish()); });
    must_see(check, "bad arguments", [](Client& c) { c.request(MessageWriter{wire::display_id, 1}.object(registry).u32(9).finish()); });
    must_see(check, "reuses a live id", [](Client& c) {
        c.request(MessageWriter{wire::display_id, 1}.object(registry).finish());
        c.request(MessageWriter{wire::display_id, 1}.object(registry).finish());
    });
    must_see(check, "outside the client's range", [](Client& c) { c.request(MessageWriter{wire::display_id, 1}.object(ObjectId{0xFF000001}).finish()); });
    must_see(check, "not announced", [](Client& c) {
        c.request(MessageWriter{wire::display_id, 1}.object(registry).finish());
        c.bind(77, "wl_output", 1, compositor_object);
    });
    must_see(check, "above the version", [](Client& c) {
        c.request(MessageWriter{wire::display_id, 1}.object(registry).finish());
        c.bind(compositor_name, "wl_compositor", 5, compositor_object);
    });

    must_see(check, "first commit of a window must have no buffer", [&](Client& c) {
        c.request(MessageWriter{wire::display_id, 1}.object(registry).finish());
        c.bind(compositor_name, "wl_compositor", 4, compositor_object);
        c.bind(shm_name, "wl_shm", 1, shm);
        c.bind(wm_base_name, "xdg_wm_base", 2, wm_base);
        c.request(MessageWriter{compositor_object, 0}.object(surface).finish());
        c.request(MessageWriter{wm_base, 2}.object(xdg_surface).object(surface).finish());
        c.request(MessageWriter{xdg_surface, 1}.object(toplevel).finish());
        c.pool_of(pixels, 16).a_buffer(buffer, 2, 2, 8).attach_damage_commit(buffer);
    });
    must_see(check, "before any configure was acknowledged", [&](Client& c) {
        c.up_to_window().pool_of(pixels, 16).a_buffer(buffer, 2, 2, 8).attach_damage_commit(buffer);
    });
    must_see(check, "never sent", [](Client& c) { c.up_to_window().ack(99); });
    must_see(check, "out of order", [](Client& c) { c.up_to_window().ack(1).ack(1); });
    must_see(check, "another size", [&](Client& c) {
        c.up_to_window().ack(1).pool_of(std::string(64, 'p'), 64).a_buffer(buffer, 4, 4, 16).attach_damage_commit(buffer);
    });
    must_see(check, "no damage", [&](Client& c) {
        c.up_to_window().ack(1).pool_of(pixels, 16).a_buffer(buffer, 2, 2, 8);
        c.request(MessageWriter{surface, 1}.object(buffer).i32(0).i32(0).finish()).request(MessageWriter{surface, 6}.finish());
    });
    must_see(check, "window geometry", [&](Client& c) {
        c.up_to_window().ack(1).pool_of(pixels, 16).a_buffer(buffer, 2, 2, 8);
        c.request(MessageWriter{xdg_surface, 3}.i32(0).i32(0).i32(3).i32(3).finish());
        c.attach_damage_commit(buffer);
    });
    must_see(check, "impossible window geometry", [](Client& c) { c.up_to_window().request(MessageWriter{xdg_surface, 3}.i32(0).i32(0).i32(0).i32(3).finish()); });
    must_see(check, "an attach of a buffer the compositor holds", [&](Client& c) {
        c.up_to_window().ack(1).pool_of(pixels, 16).a_buffer(buffer, 2, 2, 8).attach_damage_commit(buffer).attach_damage_commit(buffer);
    });
    must_see(check, "a buffer the compositor holds was destroyed", [&](Client& c) {
        c.up_to_window().ack(1).pool_of(pixels, 16).a_buffer(buffer, 2, 2, 8).attach_damage_commit(buffer);
        c.request(MessageWriter{buffer, 0}.finish());
        c.a_buffer(ObjectId{50}, 2, 2, 8).attach_damage_commit(ObjectId{50});
    });
    must_see<WritesWhileHeld>(check, "wrote to a buffer", [&](Client& c) {
        c.up_to_window().ack(1).pool_of(pixels, 16).a_buffer(buffer, 2, 2, 8).attach_damage_commit(buffer);
    });
    must_see(check, "does not exist", [](Client& c) { c.up_to_window().ack(1).request(MessageWriter{surface, 1}.object(buffer).i32(0).i32(0).finish()); });
    must_see(check, "offset", [&](Client& c) { c.up_to_window().ack(1).pool_of(pixels, 16).a_buffer(buffer, 2, 2, 8).request(MessageWriter{surface, 1}.object(buffer).i32(1).i32(0).finish()); });
    must_see(check, "size or stride", [&](Client& c) { c.up_to_window().pool_of(pixels, 16).a_buffer(buffer, 2, 2, 12); });
    must_see(check, "not ARGB8888", [&](Client& c) { c.up_to_window().pool_of(pixels, 16).a_buffer(buffer, 2, 2, 8, 1); });
    must_see(check, "outside its pool", [&](Client& c) { c.up_to_window().pool_of(pixels, 16).a_buffer(buffer, 2, 2, 8, 0, 4); });
    must_see(check, "larger than its file", [&](Client& c) { c.up_to_window().pool_of(pixels, 32); });
    must_see(check, "no descriptor", [](Client& c) { c.up_to_window().request(MessageWriter{shm, 0}.object(pool).i32(16).finish()); });
    must_see(check, "never resizes", [&](Client& c) { c.up_to_window().pool_of(pixels, 16).request(MessageWriter{pool, 2}.i32(32).finish()); });
    must_see(check, "before its xdg surface", [](Client& c) { c.up_to_window().request(MessageWriter{surface, 0}.finish()); });
    must_see(check, "before its toplevel", [](Client& c) { c.up_to_window().request(MessageWriter{xdg_surface, 0}.finish()); });
    must_see(check, "before its surfaces", [](Client& c) { c.up_to_window().request(MessageWriter{wm_base, 0}.finish()); });
    must_see(check, "second toplevel", [](Client& c) { c.up_to_window().request(MessageWriter{xdg_surface, 1}.object(ObjectId{50}).finish()); });
    must_see(check, "before its pointer and keyboard", [](Client& c) { c.up_to_window().request(MessageWriter{seat, 0}.object(pointer).finish()).request(MessageWriter{seat, 3}.finish()); });
    must_see(check, "serial never sent", [](Client& c) {
        c.up_to_window().request(MessageWriter{seat, 0}.object(pointer).finish());
        c.request(MessageWriter{pointer, 0}.u32(99).object(surface).i32(0).i32(0).finish());
    });
    must_see(check, "already committed", [](Client& c) { c.up_to_window().request(MessageWriter{compositor_object, 0}.object(ObjectId{50}).finish()).request(MessageWriter{ObjectId{50}, 6}.finish()).request(MessageWriter{wm_base, 2}.object(ObjectId{51}).object(ObjectId{50}).finish()); });

    // --- The cursor shape protocol ---
    const auto cursor_clean = violations_from<EntersAtOnce>([](Client& c) {
        c.up_to_cursor_device().set_shape(enter_serial, wire::shape_default).set_shape(enter_serial, wire::shape_last_of_version_1);
        c.request(MessageWriter{cursor_device, 0}.finish()).request(MessageWriter{pointer, 1}.finish()).request(MessageWriter{cursor_manager, 0}.finish());
    });
    for (const std::string& violation : cursor_clean) std::cerr << "unexpected: " << violation << '\n';
    check(cursor_clean.empty(), "a correct client of the cursor shape protocol is not accused of anything");
    must_see<EntersAtOnce>(check, "other than the latest enter", [](Client& c) { c.up_to_cursor_device().set_shape(enter_serial + 1, wire::shape_default); });
    must_see<EntersAtOnce>(check, "other than the latest enter", [](Client& c) { c.up_to_cursor_device().set_shape(1, wire::shape_default); });
    must_see(check, "other than the latest enter", [](Client& c) { c.up_to_cursor_device().set_shape(1, wire::shape_default); });
    must_see<EntersAndLeaves>(check, "other than the latest enter", [](Client& c) { c.up_to_cursor_device().set_shape(enter_serial, wire::shape_default); });
    must_see<EntersAtOnce>(check, "invalid cursor shape", [](Client& c) { c.up_to_cursor_device().set_shape(enter_serial, 0); });
    must_see<EntersAtOnce>(check, "invalid cursor shape", [](Client& c) { c.up_to_cursor_device().set_shape(enter_serial, wire::shape_last_of_version_1 + 1); });
    must_see(check, "other than the current pointer", [](Client& c) {
        c.up_to_window().request(MessageWriter{seat, 0}.object(pointer).finish());
        c.bind(cursor_shape_name, "wp_cursor_shape_manager_v1", 1, cursor_manager);
        c.request(MessageWriter{cursor_manager, 1}.object(cursor_device).object(surface).finish());
    });
    must_see(check, "other than the current pointer", [](Client& c) {
        c.up_to_window();
        c.bind(cursor_shape_name, "wp_cursor_shape_manager_v1", 1, cursor_manager);
        c.request(MessageWriter{cursor_manager, 1}.object(cursor_device).object(pointer).finish());
    });
    must_see(check, "second cursor shape device", [](Client& c) { c.up_to_cursor_device().request(MessageWriter{cursor_manager, 1}.object(ObjectId{50}).object(pointer).finish()); });
    must_see(check, "released before its cursor shape device", [](Client& c) { c.up_to_cursor_device().request(MessageWriter{pointer, 1}.finish()); });
    must_see(check, "destroyed before its device", [](Client& c) { c.up_to_cursor_device().request(MessageWriter{cursor_manager, 0}.finish()); });
    must_see<EntersAtOnce>(check, "beside a cursor shape device", [](Client& c) { c.up_to_cursor_device().request(MessageWriter{pointer, 0}.u32(enter_serial).object(surface).i32(0).i32(0).finish()); });
    must_see(check, "no tablet", [](Client& c) { c.up_to_cursor_device().request(MessageWriter{cursor_manager, 2}.object(ObjectId{50}).object(pointer).finish()); });
    must_see(check, "above the version", [](Client& c) {
        c.request(MessageWriter{wire::display_id, 1}.object(registry).finish());
        c.bind(cursor_shape_name, "wp_cursor_shape_manager_v1", 2, cursor_manager);
    });
    return check.finish();
}
