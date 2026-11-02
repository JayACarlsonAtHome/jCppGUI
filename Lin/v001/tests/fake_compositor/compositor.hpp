// File: tests/fake_compositor/compositor.hpp
// Purpose: A scripted Wayland compositor for tests, in our own C++. It speaks the wire protocol over a Unix socket with no
//          libwayland. A script chooses what it sends and when, as a type with overloads of on(Session&, event).
//          Whatever the script does, the compositor also holds the client to the protocol: each thing it sees wrong
//          goes on the session's list of violations, which fails the scenario.
#pragma once

#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "file_io.hpp"
#include "protocol.hpp"
#include "socket_io.hpp"
#include "wire.hpp"

namespace jgui_test::fake {

using wire::Fixed;
using wire::Header;
using wire::Kind;
using wire::MessageReader;
using wire::ObjectId;
using wire::UniqueFd;
namespace event = wire::event;
namespace request = wire::request;

// Which globals this compositor announces, and at which versions.
struct GlobalSet {
    bool compositor{true};
    std::uint32_t compositor_version{4};
    bool shm{true};
    bool wm_base{true};
    bool seat{true};
    std::uint32_t seat_version{5};
};

inline constexpr std::uint32_t compositor_name = 1;
inline constexpr std::uint32_t shm_name = 2;
inline constexpr std::uint32_t wm_base_name = 3;
inline constexpr std::uint32_t seat_name = 4;

// What a script can hear about. Each is its own type, and a script overloads on() for those it wants.
struct SeatBound {};
struct SurfaceReady {};  // the window's first commit, which asks for its first configure
struct PointerCreated {};
struct KeyboardCreated {};
struct PointerReleased {};
struct KeyboardReleased {};
struct SeatReleased {};
struct PoolCreated {
    std::size_t pool;
};
struct BufferCreated {
    ObjectId buffer;
    std::size_t pool;
};
struct BufferCommitted {
    ObjectId buffer;
    std::int32_t width{};
    std::int32_t height{};
    std::size_t index{};  // counts from zero, over the window's commits with a buffer
};
struct ConfigureAcked {
    std::uint32_t serial{};
    std::size_t count{};
};
struct MoveRequested {
    std::uint32_t serial{};
};
struct ResizeRequested {
    std::uint32_t serial{};
    std::uint32_t edge{};
};
struct Pong {
    std::uint32_t serial{};
};
struct Idle {
    std::size_t times{};  // how many quiet spells there have been in a row
};
struct ClientGone {};

// A stream of bytes is enough to tell whether a client wrote to a file it should not have.
[[nodiscard]] constexpr std::uint64_t fnv1a(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325U;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 0x100000001b3U;
    }
    return hash;
}
static_assert(fnv1a(std::span<const std::uint8_t>{}) == 0xcbf29ce484222325U);

// Reading a whole 256 MiB buffer on each commit is more than a test needs: larger ones go unchecked for this.
inline constexpr std::size_t largest_buffer_checked = std::size_t{16} << 20U;

struct Pool {
    UniqueFd file;
    std::int32_t bytes{};
};

struct BufferRecord {
    std::size_t pool{};
    std::int32_t offset{};
    std::int32_t width{};
    std::int32_t height{};
    std::int32_t stride{};
    bool held{};
    std::uint64_t held_hash{};
    bool hash_known{};
};

struct SurfaceRecord {
    std::optional<ObjectId> pending_buffer{};
    bool attached_since_commit{};
    bool damaged_since_attach{};
    bool committed{};
    bool has_xdg{};
};

class Session {
public:
    Session(UniqueFd client, GlobalSet globals) : client_{std::move(client)}, globals_{globals} { objects_.insert_or_assign(wire::display_id.value, Kind::Display); }

    // --- What a script can do ---
    void queue(std::vector<std::byte> bytes) {
        if (!hang_up_) outgoing_.insert(outgoing_.end(), bytes.begin(), bytes.end());
    }
    void queue_with_fd(std::vector<std::byte> bytes, UniqueFd fd) {
        pending_fds_.push_back(PendingFd{outgoing_.size(), std::move(fd)});
        queue(std::move(bytes));
    }

    [[nodiscard]] std::uint32_t next_serial() noexcept { return ++serial_; }

    // A configure for the toplevel and its surface, which the client must answer with an ack.
    std::uint32_t configure(std::int32_t width, std::int32_t height) {
        if (!toplevel_ || !xdg_surface_) {
            violation("the script configured before the window exists");
            return 0;
        }
        const std::uint32_t serial = next_serial();
        queue(event::toplevel_configure(*toplevel_, width, height));
        queue(event::surface_configure(*xdg_surface_, serial));
        sent_configures_.insert_or_assign(serial, std::pair{width, height});
        return serial;
    }

    void close_window() {
        close_sent_ = true;
        if (toplevel_) queue(event::toplevel_close(*toplevel_));
    }

    // Tells the client the compositor is done with a buffer, after checking nothing wrote to it meanwhile.
    void release(ObjectId buffer) {
        check_unchanged(buffer);
        if (const auto found = buffers_.find(buffer.value); found != buffers_.end()) found->second.held = false;
        queue(event::buffer_release(buffer));
    }
    // Sends a release with no check and no change in what is recorded, for scenarios about lying to the client.
    void send_release_anyway(ObjectId buffer) { queue(event::buffer_release(buffer)); }

    // Ends the connection as soon as what is queued has gone. Nothing queued after this is sent.
    void hang_up() noexcept { hang_up_ = true; }

    void violation(std::string text) { violations_.push_back(std::move(text)); }

    // --- What a script can look at ---
    [[nodiscard]] std::optional<ObjectId> registry() const noexcept { return registry_; }
    [[nodiscard]] std::optional<ObjectId> wm_base() const noexcept { return wm_base_; }
    [[nodiscard]] std::optional<ObjectId> seat() const noexcept { return seat_; }
    [[nodiscard]] std::optional<ObjectId> pointer() const noexcept { return pointer_; }
    [[nodiscard]] std::optional<ObjectId> keyboard() const noexcept { return keyboard_; }
    [[nodiscard]] std::optional<ObjectId> window_surface() const noexcept { return window_surface_; }
    [[nodiscard]] std::optional<ObjectId> xdg_surface() const noexcept { return xdg_surface_; }
    [[nodiscard]] std::optional<ObjectId> toplevel() const noexcept { return toplevel_; }
    [[nodiscard]] std::size_t commits_with_buffer() const noexcept { return commits_with_buffer_; }
    [[nodiscard]] std::size_t acks() const noexcept { return acks_; }
    [[nodiscard]] std::size_t buffers_held() const {
        return static_cast<std::size_t>(std::ranges::count_if(buffers_, [](const auto& entry) { return entry.second.held; }));
    }
    [[nodiscard]] std::size_t buffers_alive() const noexcept { return buffers_.size(); }
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    [[nodiscard]] const std::string& app_id() const noexcept { return app_id_; }
    [[nodiscard]] const std::vector<std::string>& violations() const noexcept { return violations_; }
    [[nodiscard]] bool client_gone() const noexcept { return client_gone_; }
    [[nodiscard]] Pool& pool(std::size_t index) { return pools_.at(index); }

    // The pixels a buffer holds now, read from its file, in rows.
    [[nodiscard]] std::expected<std::vector<std::uint32_t>, jgui::Error> pixels_of(ObjectId buffer) {
        const auto found = buffers_.find(buffer.value);
        if (found == buffers_.end()) return jgui::make_error(jgui::ErrorCode::System, "no such buffer");
        const BufferRecord& record = found->second;
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(record.width) * static_cast<std::size_t>(record.height));
        if (auto read = jgui::detail::read_at<std::uint32_t>(pools_.at(record.pool).file.get(), pixels, record.offset); !read) return std::unexpected(std::move(read.error()));
        return pixels;
    }

    // After an orderly end, nothing with a destroy request may be left. These have no destroy request of their own.
    void check_teardown() {
        for (const auto& [id, kind] : objects_) {
            if (kind != Kind::Display && kind != Kind::Registry && kind != Kind::Compositor && kind != Kind::Shm)
                violation("left alive when the client ended: object " + std::to_string(id) + " of kind " + std::to_string(static_cast<int>(kind)));
        }
    }

    // --- Below this line, only the compositor loop ---
    [[nodiscard]] int socket() const noexcept { return client_.get(); }
    [[nodiscard]] bool wants_to_send() const noexcept { return sent_up_to_ < outgoing_.size(); }
    [[nodiscard]] bool hanging_up() const noexcept { return hang_up_; }
    void mark_gone() noexcept { client_gone_ = true; }
    void drop_outgoing() noexcept {
        outgoing_.clear();
        pending_fds_.clear();
        sent_up_to_ = 0;
    }
    void disconnect() noexcept {
        client_ = UniqueFd{};
        client_gone_ = true;
    }

    // Sends what the socket will take. A descriptor travels with the first byte of its own message.
    [[nodiscard]] bool flush() {
        while (sent_up_to_ < outgoing_.size()) {
            int fd = -1;
            std::size_t limit = outgoing_.size();
            if (!pending_fds_.empty()) {
                if (pending_fds_.front().at == sent_up_to_) {
                    fd = pending_fds_.front().fd.get();
                    if (pending_fds_.size() > 1) limit = pending_fds_[1].at;
                } else {
                    limit = pending_fds_.front().at;
                }
            }
            const auto sent = wire::send_some(client_.get(), std::span<const std::byte>{outgoing_}.subspan(sent_up_to_, limit - sent_up_to_), fd);
            if (!sent) return false;
            if (*sent == 0) return true;
            if (fd >= 0) pending_fds_.pop_front();
            sent_up_to_ += *sent;
        }
        outgoing_.clear();
        sent_up_to_ = 0;
        return true;
    }

    // What arrived but has not been handled, with the descriptors that came with it.
    std::vector<std::byte> incoming{};
    std::deque<UniqueFd> incoming_fds{};

    // --- The rules the client is held to, checked as its requests arrive ---
    void on_compositor(request::Compositor which, MessageReader& reader);
    void on_buffer(ObjectId self, request::Buffer which, MessageReader& reader);

    [[nodiscard]] std::optional<Kind> kind_of(ObjectId id) const {
        const auto found = objects_.find(id.value);
        return found == objects_.end() ? std::nullopt : std::optional<Kind>{found->second};
    }

    // Read in the order they are listed, and refused unless the message ends just there.
    template <class... T>
    [[nodiscard]] std::optional<std::tuple<T...>> arguments(MessageReader& reader, std::string_view what);

    void add_object(ObjectId id, Kind kind) {
        if (id.value == 0 || id.value >= 0xFF000000U) violation("a new object has an id outside the client's range: " + std::to_string(id.value));
        if (objects_.contains(id.value)) violation("a new object reuses a live id: " + std::to_string(id.value));
        objects_.insert_or_assign(id.value, kind);
    }

    void remove_object(ObjectId id) {
        objects_.erase(id.value);
        queue(event::delete_id(id.value));
    }

    // A commit on the window surface. Answers what the script should hear.
    [[nodiscard]] std::optional<BufferCommitted> take_window_commit(SurfaceRecord& surface);

    void check_unchanged(ObjectId buffer) {
        const auto found = buffers_.find(buffer.value);
        if (found == buffers_.end() || !found->second.held || !found->second.hash_known) return;
        const auto now = hash_of(found->second);
        if (now && *now != found->second.held_hash) violation("the client wrote to a buffer the compositor holds");
    }

    void check_all_held_unchanged() {
        for (const auto& [id, record] : buffers_) {
            if (record.held) check_unchanged(ObjectId{id});
        }
    }

    [[nodiscard]] std::optional<std::uint64_t> hash_of(const BufferRecord& record) {
        const std::size_t bytes = static_cast<std::size_t>(record.stride) * static_cast<std::size_t>(record.height);
        if (bytes > largest_buffer_checked) return std::nullopt;
        std::vector<std::uint8_t> content(bytes);
        if (!jgui::detail::read_at<std::uint8_t>(pools_.at(record.pool).file.get(), content, record.offset)) return std::nullopt;
        return fnv1a(content);
    }

    void announce_globals(ObjectId registry_id) {
        if (globals_.compositor) queue(event::global(registry_id, compositor_name, "wl_compositor", globals_.compositor_version));
        if (globals_.shm) queue(event::global(registry_id, shm_name, "wl_shm", 1));
        if (globals_.wm_base) queue(event::global(registry_id, wm_base_name, "xdg_wm_base", 2));
        if (globals_.seat) queue(event::global(registry_id, seat_name, "wl_seat", globals_.seat_version));
    }

    struct PendingFd {
        std::size_t at{};
        UniqueFd fd;
    };

    UniqueFd client_;
    GlobalSet globals_;
    std::map<std::uint32_t, Kind> objects_{};
    std::map<std::uint32_t, BufferRecord> buffers_{};
    std::map<std::uint32_t, SurfaceRecord> surfaces_{};
    std::deque<Pool> pools_{};
    std::map<std::uint32_t, std::size_t> objects_pool_{};
    std::vector<std::byte> outgoing_{};
    std::size_t sent_up_to_{};
    std::deque<PendingFd> pending_fds_{};
    std::vector<std::string> violations_{};
    std::map<std::uint32_t, std::pair<std::int32_t, std::int32_t>> sent_configures_{};
    std::uint32_t serial_{};
    std::uint32_t last_acked_{};
    std::size_t acks_{};
    std::size_t commits_with_buffer_{};
    std::optional<std::pair<std::int32_t, std::int32_t>> acked_size_{};
    std::optional<std::pair<std::int32_t, std::int32_t>> geometry_{};
    std::optional<ObjectId> registry_{};
    std::optional<ObjectId> wm_base_{};
    std::optional<ObjectId> seat_{};
    std::optional<ObjectId> pointer_{};
    std::optional<ObjectId> keyboard_{};
    std::optional<ObjectId> window_surface_{};
    std::optional<ObjectId> xdg_surface_{};
    std::optional<ObjectId> toplevel_{};
    std::string title_{};
    std::string app_id_{};
    bool destroyed_a_held_buffer_{};
    bool close_sent_{};
    bool hang_up_{};
    bool client_gone_{};
};

namespace detail {
template <class T>
[[nodiscard]] constexpr std::expected<T, wire::Refusal> read_one(MessageReader& reader) {
    if constexpr (std::same_as<T, std::uint32_t>) {
        return reader.u32();
    } else if constexpr (std::same_as<T, std::int32_t>) {
        return reader.i32();
    } else if constexpr (std::same_as<T, ObjectId>) {
        return reader.object();
    } else {
        static_assert(std::same_as<T, std::string>, "this argument type is not on the wire");
        return reader.string();
    }
}
}  // namespace detail

template <class... T>
std::optional<std::tuple<T...>> Session::arguments(MessageReader& reader, std::string_view what) {
    bool good = true;
    const auto one = [&]<class V>(std::type_identity<V>) -> V {
        if (!good) return V{};
        auto value = detail::read_one<V>(reader);
        if (!value) {
            good = false;
            return V{};
        }
        return std::move(*value);
    };
    static_cast<void>(one);  // an empty list of arguments does not use it
    std::tuple<T...> values{one(std::type_identity<T>{})...};
    if (!good || !reader.at_end()) {
        violation("a request with bad arguments: " + std::string{what});
        return std::nullopt;
    }
    return std::optional<std::tuple<T...>>{std::move(values)};
}

inline void Session::on_compositor(request::Compositor which, MessageReader& reader) {
    switch (which) {
        case request::Compositor::CreateSurface: {
            const auto args = arguments<ObjectId>(reader, "wl_compositor.create_surface");
            if (!args) return;
            add_object(std::get<0>(*args), Kind::Surface);
            surfaces_.insert_or_assign(std::get<0>(*args).value, SurfaceRecord{});
            return;
        }
        default: violation("an unexpected request on wl_compositor"); return;
    }
}

inline void Session::on_buffer(ObjectId self, request::Buffer which, MessageReader& reader) {
    switch (which) {
        case request::Buffer::Destroy: {
            if (!arguments<>(reader, "wl_buffer.destroy")) return;
            const auto found = buffers_.find(self.value);
            // A running program destroys a buffer only after its release. At the end of the program it may go earlier,
            // because the compositor reads only until the connection ends. So it counts only if the client goes on.
            if (found != buffers_.end() && found->second.held) destroyed_a_held_buffer_ = true;
            buffers_.erase(self.value);
            remove_object(self);
            return;
        }
        default: violation("an unexpected request on wl_buffer"); return;
    }
}

inline std::optional<BufferCommitted> Session::take_window_commit(SurfaceRecord& surface) {
    if (!surface.attached_since_commit || !surface.pending_buffer) return std::nullopt;
    const ObjectId id = *surface.pending_buffer;
    surface.attached_since_commit = false;
    const auto found = buffers_.find(id.value);
    if (destroyed_a_held_buffer_) violation("a buffer the compositor holds was destroyed, and the client went on");
    if (found == buffers_.end()) {
        violation("a commit of a buffer that does not exist");
        return std::nullopt;
    }
    BufferRecord& record = found->second;
    if (!acked_size_) violation("a buffer committed before any configure was acknowledged");
    if (acked_size_ && acked_size_->first > 0 && acked_size_->first <= 8192 && acked_size_->second > 0 && acked_size_->second <= 8192 &&
        (acked_size_->first != record.width || acked_size_->second != record.height))
        violation("a buffer committed at another size than the configure last acknowledged");
    if (geometry_ && (geometry_->first != record.width || geometry_->second != record.height)) violation("a window geometry that does not match the buffer");
    if (!surface.damaged_since_attach) violation("a new buffer committed with no damage");
    surface.damaged_since_attach = false;
    check_unchanged(id);
    record.held = true;
    const auto hash = hash_of(record);
    record.hash_known = hash.has_value();
    record.held_hash = hash.value_or(0);
    return BufferCommitted{id, record.width, record.height, commits_with_buffer_++};
}

// The loop: it reads requests, holds the client to the rules, tells the script, and sends what the script queued.
template <class Script>
class Compositor {
public:
    Compositor(Session& session, Script& script) noexcept : session_{session}, script_{script} {}

    // Runs until the client is gone or the deadline passes. False if the deadline passed.
    [[nodiscard]] bool run(std::chrono::milliseconds deadline, std::chrono::milliseconds quiet_spell) {
        const auto end = std::chrono::steady_clock::now() + deadline;
        std::size_t quiet_spells = 0;
        while (!session_.client_gone()) {
            if (std::chrono::steady_clock::now() >= end) return false;
            // A peer that stopped reading may still have requests on the way, so sending stops and reading goes on.
            if (!session_.flush()) session_.drop_outgoing();
            if (session_.hanging_up() && !session_.wants_to_send()) {
                session_.disconnect();
                break;
            }
            const auto wanted = static_cast<short>(POLLIN | (session_.wants_to_send() ? POLLOUT : 0));
            pollfd watch{session_.socket(), wanted, 0};
            const int ready = ::poll(&watch, 1, static_cast<int>(quiet_spell.count()));
            if (ready < 0) continue;
            if (ready == 0) {
                ++quiet_spells;
                tell(Idle{quiet_spells});
                continue;
            }
            quiet_spells = 0;
            if ((watch.revents & (POLLIN | POLLHUP | POLLERR)) != 0) read_and_handle();
        }
        session_.check_all_held_unchanged();
        tell(ClientGone{});
        return true;
    }

private:
    template <class E>
    void tell(const E& what) {
        if constexpr (requires { script_.on(session_, what); }) {
            script_.on(session_, what);
        } else {
            standard(what);
        }
    }

    // What happens when a script has nothing to say about an event.
    void standard(const SeatBound&) {
        if (session_.seat()) {
            session_.queue(event::seat_capabilities(*session_.seat(), wire::capability_pointer | wire::capability_keyboard));
            session_.queue(event::seat_name(*session_.seat(), "test"));
        }
    }
    void standard(const SurfaceReady&) { session_.configure(640, 480); }
    void standard(const BufferCommitted& commit) {
        session_.release(commit.buffer);
        session_.close_window();
    }
    template <class E>
    void standard(const E&) {}

    void read_and_handle() {
        std::array<std::byte, 16384> chunk{};
        std::vector<UniqueFd> descriptors;
        const auto got = wire::receive_some(session_.socket(), chunk, descriptors);
        if (!got) {
            session_.mark_gone();
            return;
        }
        for (UniqueFd& fd : descriptors) session_.incoming_fds.push_back(std::move(fd));
        const std::span<const std::byte> fresh = std::span<const std::byte>{chunk}.first(got->bytes);
        session_.incoming.insert(session_.incoming.end(), fresh.begin(), fresh.end());
        std::size_t used = 0;
        while (true) {
            const std::span<const std::byte> rest = std::span<const std::byte>{session_.incoming}.subspan(used);
            const auto header = wire::read_header(rest);
            if (!header) {
                if (header.error() != wire::Refusal::TooShortForHeader && header.error() != wire::Refusal::Truncated) {
                    session_.violation("the client sent a malformed message");
                    session_.incoming.clear();
                    used = 0;
                }
                break;
            }
            MessageReader reader{rest.subspan(wire::header_bytes, header->size - wire::header_bytes)};
            handle(*header, reader);
            used += header->size;
        }
        session_.incoming.erase(session_.incoming.begin(), session_.incoming.begin() + static_cast<std::ptrdiff_t>(used));
        if (got->closed) session_.mark_gone();
    }

    void unexpected(std::string_view interface, std::uint16_t opcode) {
        session_.violation("an unexpected request " + std::to_string(opcode) + " on " + std::string{interface});
    }

    void handle(const Header& header, MessageReader& reader) {
        const auto kind = session_.kind_of(header.object);
        if (!kind) {
            session_.violation("a request on an object that does not exist: " + std::to_string(header.object.value));
            return;
        }
        switch (*kind) {
            case Kind::Display: on_display(header, reader); return;
            case Kind::Registry: on_registry(header, reader); return;
            case Kind::Compositor: session_.on_compositor(static_cast<request::Compositor>(header.opcode), reader); return;
            case Kind::Shm: on_shm(header, reader); return;
            case Kind::ShmPool: on_pool(header, reader); return;
            case Kind::Buffer: session_.on_buffer(header.object, static_cast<request::Buffer>(header.opcode), reader); return;
            case Kind::Surface: on_surface(header, reader); return;
            case Kind::Seat: on_seat(header, reader); return;
            case Kind::Pointer: on_pointer(header, reader); return;
            case Kind::Keyboard: on_keyboard(header, reader); return;
            case Kind::WmBase: on_wm_base(header, reader); return;
            case Kind::XdgSurface: on_xdg_surface(header, reader); return;
            case Kind::Toplevel: on_toplevel(header, reader); return;
            case Kind::Callback:
            default: unexpected("an object with no requests", header.opcode); return;
        }
    }

    void on_display(const Header& header, MessageReader& reader) {
        switch (static_cast<request::Display>(header.opcode)) {
            case request::Display::Sync: {
                const auto args = session_.arguments<ObjectId>(reader, "wl_display.sync");
                if (!args) return;
                session_.add_object(std::get<0>(*args), Kind::Callback);
                session_.queue(event::callback_done(std::get<0>(*args), session_.next_serial()));
                session_.remove_object(std::get<0>(*args));
                return;
            }
            case request::Display::GetRegistry: {
                const auto args = session_.arguments<ObjectId>(reader, "wl_display.get_registry");
                if (!args) return;
                session_.add_object(std::get<0>(*args), Kind::Registry);
                session_.registry_ = std::get<0>(*args);
                session_.announce_globals(std::get<0>(*args));
                return;
            }
            default: unexpected("wl_display", header.opcode); return;
        }
    }

    void on_registry(const Header& header, MessageReader& reader) {
        if (static_cast<request::Registry>(header.opcode) != request::Registry::Bind) {
            unexpected("wl_registry", header.opcode);
            return;
        }
        const auto args = session_.arguments<std::uint32_t, std::string, std::uint32_t, ObjectId>(reader, "wl_registry.bind");
        if (!args) return;
        const auto& [name, interface, version, id] = *args;
        const auto bound = [&](std::uint32_t expected_name, std::string_view expected_interface, std::uint32_t most, Kind kind) {
            if (name != expected_name || interface != expected_interface) return false;
            if (version == 0 || version > most) session_.violation("a bind above the version announced: " + std::string{expected_interface});
            session_.add_object(id, kind);
            return true;
        };
        if (bound(compositor_name, "wl_compositor", session_.globals_.compositor_version, Kind::Compositor)) return;
        if (bound(shm_name, "wl_shm", 1, Kind::Shm)) {
            session_.queue(event::shm_format(id, 0));
            session_.queue(event::shm_format(id, 1));
            return;
        }
        if (bound(wm_base_name, "xdg_wm_base", 2, Kind::WmBase)) {
            session_.wm_base_ = id;
            return;
        }
        if (bound(seat_name, "wl_seat", session_.globals_.seat_version, Kind::Seat)) {
            session_.seat_ = id;
            tell(SeatBound{});
            return;
        }
        session_.violation("a bind of a global that was not announced: " + interface);
    }

    void on_shm(const Header& header, MessageReader& reader) {
        if (static_cast<request::Shm>(header.opcode) != request::Shm::CreatePool) {
            unexpected("wl_shm", header.opcode);
            return;
        }
        const auto args = session_.arguments<ObjectId, std::int32_t>(reader, "wl_shm.create_pool");
        if (!args) return;
        if (session_.incoming_fds.empty()) {
            session_.violation("wl_shm.create_pool with no descriptor");
            return;
        }
        UniqueFd file = std::move(session_.incoming_fds.front());
        session_.incoming_fds.pop_front();
        const auto [id, bytes] = *args;
        struct stat status {};
        if (::fstat(file.get(), &status) != 0 || status.st_size < bytes) session_.violation("a pool larger than its file");
        if (bytes <= 0) session_.violation("a pool of no size");
        session_.add_object(id, Kind::ShmPool);
        session_.pools_.push_back(Pool{std::move(file), bytes});
        session_.objects_pool_.insert_or_assign(id.value, session_.pools_.size() - 1);
        tell(PoolCreated{session_.pools_.size() - 1});
    }

    void on_pool(const Header& header, MessageReader& reader) {
        switch (static_cast<request::ShmPool>(header.opcode)) {
            case request::ShmPool::CreateBuffer: {
                const auto args = session_.arguments<ObjectId, std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::uint32_t>(reader, "wl_shm_pool.create_buffer");
                if (!args) return;
                const auto [id, offset, width, height, stride, format] = *args;
                const std::size_t pool = session_.objects_pool_.at(header.object.value);
                if (width <= 0 || height <= 0 || stride != width * 4) session_.violation("a buffer whose size or stride is wrong");
                if (format != 0) session_.violation("a buffer that is not ARGB8888");
                const std::int64_t last = std::int64_t{offset} + std::int64_t{stride} * std::int64_t{height};
                if (offset < 0 || last > session_.pools_.at(pool).bytes) session_.violation("a buffer outside its pool");
                session_.add_object(id, Kind::Buffer);
                session_.buffers_.insert_or_assign(id.value, BufferRecord{pool, offset, width, height, stride});
                tell(BufferCreated{id, pool});
                return;
            }
            case request::ShmPool::Destroy:
                if (!session_.arguments<>(reader, "wl_shm_pool.destroy")) return;
                session_.objects_pool_.erase(header.object.value);
                session_.remove_object(header.object);
                return;
            case request::ShmPool::Resize:
            default: unexpected("wl_shm_pool: this client never resizes a pool", header.opcode); return;
        }
    }

    void on_surface(const Header& header, MessageReader& reader) {
        const auto found = session_.surfaces_.find(header.object.value);
        if (found == session_.surfaces_.end()) {
            session_.violation("a request on a surface that was not recorded");
            return;
        }
        SurfaceRecord& surface = found->second;
        switch (static_cast<request::Surface>(header.opcode)) {
            case request::Surface::Attach: {
                const auto args = session_.arguments<ObjectId, std::int32_t, std::int32_t>(reader, "wl_surface.attach");
                if (!args) return;
                const auto [buffer, x, y] = *args;
                if (x != 0 || y != 0) session_.violation("an attach with an offset");
                const auto record = session_.buffers_.find(buffer.value);
                if (record == session_.buffers_.end()) {
                    session_.violation("an attach of a buffer that does not exist");
                    return;
                }
                if (record->second.held) session_.violation("an attach of a buffer the compositor holds");
                if (surface.has_xdg && !session_.acked_size_) session_.violation("an attach before any configure was acknowledged");
                surface.pending_buffer = buffer;
                surface.attached_since_commit = true;
                surface.damaged_since_attach = false;
                return;
            }
            case request::Surface::DamageBuffer: {
                const auto args = session_.arguments<std::int32_t, std::int32_t, std::int32_t, std::int32_t>(reader, "wl_surface.damage_buffer");
                if (!args) return;
                surface.damaged_since_attach = true;
                return;
            }
            case request::Surface::Commit: {
                if (!session_.arguments<>(reader, "wl_surface.commit")) return;
                if (surface.has_xdg && !session_.toplevel_) session_.violation("the window surface was committed with no role");
                const bool first_commit = !surface.committed;
                surface.committed = true;
                if (!surface.has_xdg) {
                    // The cursor: nothing here asks for a configure, and its buffer stays attached.
                    surface.attached_since_commit = false;
                    return;
                }
                if (first_commit && surface.attached_since_commit) session_.violation("the first commit of a window must have no buffer");
                if (first_commit) {
                    tell(SurfaceReady{});
                    return;
                }
                session_.check_all_held_unchanged();
                if (const auto commit = session_.take_window_commit(surface); commit) tell(*commit);
                return;
            }
            case request::Surface::Destroy:
                if (!session_.arguments<>(reader, "wl_surface.destroy")) return;
                if (surface.has_xdg && session_.xdg_surface_) session_.violation("a surface destroyed before its xdg surface");
                session_.surfaces_.erase(found);
                session_.remove_object(header.object);
                return;
            case request::Surface::Damage:
            case request::Surface::Frame:
            default: unexpected("wl_surface", header.opcode); return;
        }
    }

    void on_seat(const Header& header, MessageReader& reader) {
        switch (static_cast<request::Seat>(header.opcode)) {
            case request::Seat::GetPointer: {
                const auto args = session_.arguments<ObjectId>(reader, "wl_seat.get_pointer");
                if (!args) return;
                session_.add_object(std::get<0>(*args), Kind::Pointer);
                session_.pointer_ = std::get<0>(*args);
                tell(PointerCreated{});
                return;
            }
            case request::Seat::GetKeyboard: {
                const auto args = session_.arguments<ObjectId>(reader, "wl_seat.get_keyboard");
                if (!args) return;
                session_.add_object(std::get<0>(*args), Kind::Keyboard);
                session_.keyboard_ = std::get<0>(*args);
                tell(KeyboardCreated{});
                return;
            }
            case request::Seat::Release:
                if (!session_.arguments<>(reader, "wl_seat.release")) return;
                if (session_.pointer_ || session_.keyboard_) session_.violation("a seat released before its pointer and keyboard");
                session_.seat_.reset();
                session_.remove_object(header.object);
                tell(SeatReleased{});
                return;
            case request::Seat::GetTouch:
            default: unexpected("wl_seat", header.opcode); return;
        }
    }

    void on_pointer(const Header& header, MessageReader& reader) {
        switch (static_cast<request::Pointer>(header.opcode)) {
            case request::Pointer::SetCursor: {
                const auto args = session_.arguments<std::uint32_t, ObjectId, std::int32_t, std::int32_t>(reader, "wl_pointer.set_cursor");
                if (!args) return;
                if (std::get<0>(*args) == 0 || std::get<0>(*args) > session_.serial_) session_.violation("set_cursor with a serial never sent");
                if (!session_.surfaces_.contains(std::get<1>(*args).value)) session_.violation("set_cursor with a surface that does not exist");
                return;
            }
            case request::Pointer::Release:
                if (!session_.arguments<>(reader, "wl_pointer.release")) return;
                session_.pointer_.reset();
                session_.remove_object(header.object);
                tell(PointerReleased{});
                return;
            default: unexpected("wl_pointer", header.opcode); return;
        }
    }

    void on_keyboard(const Header& header, MessageReader& reader) {
        if (static_cast<request::Keyboard>(header.opcode) != request::Keyboard::Release) {
            unexpected("wl_keyboard", header.opcode);
            return;
        }
        if (!session_.arguments<>(reader, "wl_keyboard.release")) return;
        session_.keyboard_.reset();
        session_.remove_object(header.object);
        tell(KeyboardReleased{});
    }

    void on_wm_base(const Header& header, MessageReader& reader) {
        switch (static_cast<request::WmBase>(header.opcode)) {
            case request::WmBase::GetXdgSurface: {
                const auto args = session_.arguments<ObjectId, ObjectId>(reader, "xdg_wm_base.get_xdg_surface");
                if (!args) return;
                const auto found = session_.surfaces_.find(std::get<1>(*args).value);
                if (found == session_.surfaces_.end()) {
                    session_.violation("get_xdg_surface on a surface that does not exist");
                    return;
                }
                if (found->second.committed) session_.violation("get_xdg_surface on a surface already committed");
                found->second.has_xdg = true;
                session_.add_object(std::get<0>(*args), Kind::XdgSurface);
                session_.xdg_surface_ = std::get<0>(*args);
                session_.window_surface_ = std::get<1>(*args);
                return;
            }
            case request::WmBase::Pong: {
                const auto args = session_.arguments<std::uint32_t>(reader, "xdg_wm_base.pong");
                if (args) tell(Pong{std::get<0>(*args)});
                return;
            }
            case request::WmBase::Destroy:
                if (!session_.arguments<>(reader, "xdg_wm_base.destroy")) return;
                if (session_.xdg_surface_) session_.violation("xdg_wm_base destroyed before its surfaces");
                session_.wm_base_.reset();
                session_.remove_object(header.object);
                return;
            default: unexpected("xdg_wm_base", header.opcode); return;
        }
    }

    void on_xdg_surface(const Header& header, MessageReader& reader) {
        switch (static_cast<request::XdgSurface>(header.opcode)) {
            case request::XdgSurface::GetToplevel: {
                const auto args = session_.arguments<ObjectId>(reader, "xdg_surface.get_toplevel");
                if (!args) return;
                if (session_.toplevel_) session_.violation("a second toplevel");
                session_.add_object(std::get<0>(*args), Kind::Toplevel);
                session_.toplevel_ = std::get<0>(*args);
                return;
            }
            case request::XdgSurface::SetWindowGeometry: {
                const auto args = session_.arguments<std::int32_t, std::int32_t, std::int32_t, std::int32_t>(reader, "xdg_surface.set_window_geometry");
                if (!args) return;
                const auto [x, y, width, height] = *args;
                if (x != 0 || y != 0 || width <= 0 || height <= 0) session_.violation("an impossible window geometry");
                session_.geometry_ = std::pair{width, height};
                return;
            }
            case request::XdgSurface::AckConfigure: {
                const auto args = session_.arguments<std::uint32_t>(reader, "xdg_surface.ack_configure");
                if (!args) return;
                const std::uint32_t serial = std::get<0>(*args);
                const auto sent = session_.sent_configures_.find(serial);
                if (sent == session_.sent_configures_.end()) {
                    session_.violation("an ack of a configure never sent");
                    return;
                }
                if (serial <= session_.last_acked_) session_.violation("configures acknowledged out of order");
                session_.last_acked_ = serial;
                session_.acked_size_ = sent->second;
                ++session_.acks_;
                tell(ConfigureAcked{serial, session_.acks_});
                return;
            }
            case request::XdgSurface::Destroy:
                if (!session_.arguments<>(reader, "xdg_surface.destroy")) return;
                if (session_.toplevel_) session_.violation("an xdg surface destroyed before its toplevel");
                session_.xdg_surface_.reset();
                session_.remove_object(header.object);
                return;
            default: unexpected("xdg_surface", header.opcode); return;
        }
    }

    void on_toplevel(const Header& header, MessageReader& reader) {
        switch (static_cast<request::Toplevel>(header.opcode)) {
            case request::Toplevel::SetTitle: {
                const auto args = session_.arguments<std::string>(reader, "xdg_toplevel.set_title");
                if (args) session_.title_ = std::get<0>(*args);
                return;
            }
            case request::Toplevel::SetAppId: {
                const auto args = session_.arguments<std::string>(reader, "xdg_toplevel.set_app_id");
                if (args) session_.app_id_ = std::get<0>(*args);
                return;
            }
            case request::Toplevel::SetMinSize: {
                const auto args = session_.arguments<std::int32_t, std::int32_t>(reader, "xdg_toplevel.set_min_size");
                if (args && (std::get<0>(*args) < 0 || std::get<1>(*args) < 0)) session_.violation("a negative minimum size");
                return;
            }
            case request::Toplevel::Move: {
                const auto args = session_.arguments<ObjectId, std::uint32_t>(reader, "xdg_toplevel.move");
                if (args) tell(MoveRequested{std::get<1>(*args)});
                return;
            }
            case request::Toplevel::Resize: {
                const auto args = session_.arguments<ObjectId, std::uint32_t, std::uint32_t>(reader, "xdg_toplevel.resize");
                if (args) tell(ResizeRequested{std::get<1>(*args), std::get<2>(*args)});
                return;
            }
            case request::Toplevel::Destroy:
                if (!session_.arguments<>(reader, "xdg_toplevel.destroy")) return;
                session_.toplevel_.reset();
                session_.remove_object(header.object);
                return;
            default: unexpected("xdg_toplevel", header.opcode); return;
        }
    }

    Session& session_;
    Script& script_;
};

}  // namespace jgui_test::fake
