// File: tests/test_injection.cpp
// Purpose: What neither a real nor a fake compositor can make happen: a C library giving back nothing, or refusing a
//          listener, one call at a time, through the real implementation under a template parameter. Also the
//          system calls behind a buffer file, a real short write and what a signal really does to them.
#include <fcntl.h>
#include <sys/mman.h>  // for memfd_create
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "check.hpp"
#include "fake_compositor/harness.hpp"
#include "file_io.hpp"
#include "jCppGUI/v001/backend.hpp"
#include "untrusted.hpp"
#include "wayland_impl.hpp"

namespace {

using jgui::ErrorCode;
using jgui::detail::BasicWaylandImpl;
using jgui::detail::LibWayland;
using jgui::detail::PumpWait;
using jgui::detail::UniqueFd;
namespace fake = jgui_test::fake;
namespace wire = jgui_test::wire;
namespace request = wire::request;

// --- The test compositor's request numbers against libwayland's own generated ones ---
static_assert(std::to_underlying(request::Display::Sync) == WL_DISPLAY_SYNC && std::to_underlying(request::Display::GetRegistry) == WL_DISPLAY_GET_REGISTRY);
static_assert(std::to_underlying(request::Registry::Bind) == WL_REGISTRY_BIND);
static_assert(std::to_underlying(request::Compositor::CreateSurface) == WL_COMPOSITOR_CREATE_SURFACE);
static_assert(std::to_underlying(request::Shm::CreatePool) == WL_SHM_CREATE_POOL);
static_assert(std::to_underlying(request::ShmPool::CreateBuffer) == WL_SHM_POOL_CREATE_BUFFER && std::to_underlying(request::ShmPool::Destroy) == WL_SHM_POOL_DESTROY && std::to_underlying(request::ShmPool::Resize) == WL_SHM_POOL_RESIZE);
static_assert(std::to_underlying(request::Buffer::Destroy) == WL_BUFFER_DESTROY);
static_assert(std::to_underlying(request::Surface::Destroy) == WL_SURFACE_DESTROY && std::to_underlying(request::Surface::Attach) == WL_SURFACE_ATTACH && std::to_underlying(request::Surface::Damage) == WL_SURFACE_DAMAGE);
static_assert(std::to_underlying(request::Surface::Frame) == WL_SURFACE_FRAME && std::to_underlying(request::Surface::Commit) == WL_SURFACE_COMMIT && std::to_underlying(request::Surface::DamageBuffer) == WL_SURFACE_DAMAGE_BUFFER);
static_assert(std::to_underlying(request::Seat::GetPointer) == WL_SEAT_GET_POINTER && std::to_underlying(request::Seat::GetKeyboard) == WL_SEAT_GET_KEYBOARD && std::to_underlying(request::Seat::GetTouch) == WL_SEAT_GET_TOUCH && std::to_underlying(request::Seat::Release) == WL_SEAT_RELEASE);
static_assert(std::to_underlying(request::Pointer::SetCursor) == WL_POINTER_SET_CURSOR && std::to_underlying(request::Pointer::Release) == WL_POINTER_RELEASE);
static_assert(std::to_underlying(request::Keyboard::Release) == WL_KEYBOARD_RELEASE);
static_assert(std::to_underlying(request::WmBase::Destroy) == XDG_WM_BASE_DESTROY && std::to_underlying(request::WmBase::GetXdgSurface) == XDG_WM_BASE_GET_XDG_SURFACE && std::to_underlying(request::WmBase::Pong) == XDG_WM_BASE_PONG);
static_assert(std::to_underlying(request::XdgSurface::Destroy) == XDG_SURFACE_DESTROY && std::to_underlying(request::XdgSurface::GetToplevel) == XDG_SURFACE_GET_TOPLEVEL);
static_assert(std::to_underlying(request::XdgSurface::SetWindowGeometry) == XDG_SURFACE_SET_WINDOW_GEOMETRY && std::to_underlying(request::XdgSurface::AckConfigure) == XDG_SURFACE_ACK_CONFIGURE);
static_assert(std::to_underlying(request::Toplevel::Destroy) == XDG_TOPLEVEL_DESTROY && std::to_underlying(request::Toplevel::SetTitle) == XDG_TOPLEVEL_SET_TITLE && std::to_underlying(request::Toplevel::SetAppId) == XDG_TOPLEVEL_SET_APP_ID);
static_assert(std::to_underlying(request::Toplevel::Move) == XDG_TOPLEVEL_MOVE && std::to_underlying(request::Toplevel::Resize) == XDG_TOPLEVEL_RESIZE && std::to_underlying(request::Toplevel::SetMinSize) == XDG_TOPLEVEL_SET_MIN_SIZE);
static_assert(wire::button_left == 0x110 && wire::resize_edge_left == XDG_TOPLEVEL_RESIZE_EDGE_LEFT);
static_assert(wire::capability_pointer == WL_SEAT_CAPABILITY_POINTER && wire::capability_keyboard == WL_SEAT_CAPABILITY_KEYBOARD);
static_assert(wire::keymap_xkb_v1 == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && wire::keymap_no_keymap == WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP);
static_assert(wire::format_argb8888 == WL_SHM_FORMAT_ARGB8888);

// --- One failing C call at a time ---

enum class Site : std::uint8_t {
    Nothing, Connect, Registry, Sync,
    BindCompositor, BindShm, BindWmBase, BindSeat,
    ListenRegistry, ListenSync, ListenWmBase, ListenSeat, ListenPointer, ListenKeyboard, ListenSurface, ListenToplevel, ListenBuffer,
    CursorSurface, WindowSurface, XdgSurface, Toplevel, Pointer, Keyboard, Pool, Buffer, KeyboardState, KeymapContext,
};

// The real calls, except the one that gives back nothing or refuses.
template <Site Fails>
struct Failing {
    static inline int surfaces_made = 0;

    [[nodiscard]] static wl_display* connect() noexcept { return Fails == Site::Connect ? nullptr : LibWayland::connect(); }
    [[nodiscard]] static wl_registry* get_registry(wl_display* display) noexcept { return Fails == Site::Registry ? nullptr : LibWayland::get_registry(display); }
    [[nodiscard]] static wl_callback* sync(wl_display* display) noexcept { return Fails == Site::Sync ? nullptr : LibWayland::sync(display); }

    template <class Global>
    [[nodiscard]] static Global* bind(wl_registry* registry, std::uint32_t name, std::uint32_t version) noexcept {
        if constexpr ((Fails == Site::BindCompositor && std::same_as<Global, wl_compositor>) || (Fails == Site::BindShm && std::same_as<Global, wl_shm>) ||
                      (Fails == Site::BindWmBase && std::same_as<Global, xdg_wm_base>) || (Fails == Site::BindSeat && std::same_as<Global, wl_seat>)) {
            static_cast<void>(registry);
            static_cast<void>(name);
            static_cast<void>(version);
            return nullptr;
        } else {
            return LibWayland::template bind<Global>(registry, name, version);
        }
    }

    template <auto Add, class Proxy, class Owner, class Table>
    [[nodiscard]] static bool listen(Proxy* proxy, const jgui::detail::OwnedListener<Owner, Table>& listener, Owner& owner) noexcept {
        if constexpr ((Fails == Site::ListenRegistry && std::same_as<Proxy, wl_registry>) || (Fails == Site::ListenSync && std::same_as<Proxy, wl_callback>) ||
                      (Fails == Site::ListenWmBase && std::same_as<Proxy, xdg_wm_base>) || (Fails == Site::ListenSeat && std::same_as<Proxy, wl_seat>) ||
                      (Fails == Site::ListenPointer && std::same_as<Proxy, wl_pointer>) || (Fails == Site::ListenKeyboard && std::same_as<Proxy, wl_keyboard>) ||
                      (Fails == Site::ListenSurface && std::same_as<Proxy, xdg_surface>) || (Fails == Site::ListenToplevel && std::same_as<Proxy, xdg_toplevel>) ||
                      (Fails == Site::ListenBuffer && std::same_as<Proxy, wl_buffer>)) {
            static_cast<void>(proxy);
            static_cast<void>(listener);
            static_cast<void>(owner);
            return false;
        } else {
            return LibWayland::template listen<Add>(proxy, listener, owner);
        }
    }

    // The cursor's surface is made first, and the window's next.
    [[nodiscard]] static wl_surface* create_surface(wl_compositor* compositor) noexcept {
        ++surfaces_made;
        if ((Fails == Site::CursorSurface && surfaces_made == 1) || (Fails == Site::WindowSurface && surfaces_made == 2)) return nullptr;
        return LibWayland::create_surface(compositor);
    }
    [[nodiscard]] static xdg_surface* get_xdg_surface(xdg_wm_base* base, wl_surface* surface) noexcept { return Fails == Site::XdgSurface ? nullptr : LibWayland::get_xdg_surface(base, surface); }
    [[nodiscard]] static xdg_toplevel* get_toplevel(xdg_surface* surface) noexcept { return Fails == Site::Toplevel ? nullptr : LibWayland::get_toplevel(surface); }
    [[nodiscard]] static wl_pointer* get_pointer(wl_seat* seat) noexcept { return Fails == Site::Pointer ? nullptr : LibWayland::get_pointer(seat); }
    [[nodiscard]] static wl_keyboard* get_keyboard(wl_seat* seat) noexcept { return Fails == Site::Keyboard ? nullptr : LibWayland::get_keyboard(seat); }
    [[nodiscard]] static wl_shm_pool* create_pool(wl_shm* shm, int fd, std::int32_t bytes) noexcept { return Fails == Site::Pool ? nullptr : LibWayland::create_pool(shm, fd, bytes); }
    [[nodiscard]] static wl_buffer* create_buffer(wl_shm_pool* pool, std::int32_t width, std::int32_t height, std::int32_t stride) noexcept {
        return Fails == Site::Buffer ? nullptr : LibWayland::create_buffer(pool, width, height, stride);
    }
    [[nodiscard]] static xkb_state* new_keyboard_state(xkb_keymap* keymap) noexcept { return Fails == Site::KeyboardState ? nullptr : LibWayland::new_keyboard_state(keymap); }
    [[nodiscard]] static jgui::detail::XkbContext keymap_context() noexcept { return Fails == Site::KeymapContext ? jgui::detail::XkbContext{} : LibWayland::keymap_context(); }
};

// Sends a usable keymap, and goes when nothing more can happen.
struct InjectionScript {
    static constexpr fake::ClientEnd end = fake::ends_with_some_error;
    void on(fake::Session& session, const fake::KeyboardCreated&) {
        const auto* path = std::getenv("JGUI_TEST_KEYMAP");
        if (path == nullptr || !session.keyboard()) return;
        UniqueFd source{::open(path, O_RDONLY | O_CLOEXEC)};
        struct stat status {};
        if (source.get() < 0 || ::fstat(source.get(), &status) != 0) return;
        std::string text(static_cast<std::size_t>(status.st_size), '\0');
        if (!jgui::detail::read_at(source.get(), std::span{text}, 0)) return;
        text.push_back('\0');
        UniqueFd copy{::memfd_create("jcppgui-injection-keymap", MFD_CLOEXEC)};
        static_cast<void>(jgui::detail::write_at(copy.get(), std::span<const char>{text}, 0));
        session.queue_with_fd(wire::event::keyboard_keymap(*session.keyboard(), wire::keymap_xkb_v1, static_cast<std::uint32_t>(text.size())), std::move(copy));
    }
    void on(fake::Session&, const fake::SurfaceReady&) {}
    void on(fake::Session& session, const fake::Idle&) { session.hang_up(); }
};

struct NoSink {
    void on(const jgui::CloseEvent&) {}
    void on(const jgui::ResizeEvent&) {}
    void on(const jgui::PointerMove&) {}
    void on(const jgui::PointerButton&) {}
    void on(const jgui::PointerLeave&) {}
    void on(const jgui::KeyEvent&) {}
};

template <Site Fails>
[[nodiscard]] std::expected<void, jgui::Error> connect_and_pump() {
    jgui::BackendEventQueue queue;
    BasicWaylandImpl<Failing<Fails>> impl{queue, jgui::Size{jgui::Width{64}, jgui::Height{64}}};
    const jgui::WaylandOptions options{.title = "t", .app_id = "a", .size = jgui::Size{jgui::Width{64}, jgui::Height{64}}};
    if (auto connected = impl.connect(options); !connected) return std::unexpected(std::move(connected.error()));
    NoSink sink;
    while (true) {
        if (auto pumped = impl.pump(PumpWait::WhenIdle); !pumped) return std::unexpected(std::move(pumped.error()));
        queue.drain(sink);
    }
}

template <Site Fails>
void refused_with(jgui_test::Checker& check, std::string_view name, ErrorCode code, std::string_view text) {
    const fake::ClientEnd expected{false, code, text, false};
    const bool passed = fake::run_scenario(InjectionScript{}, fake::GlobalSet{}, [&] { return fake::ended_as_expected(expected, connect_and_pump<Fails>()) ? 0 : 1; });
    std::cout << name << ": " << (passed ? "a clean Error" : "FAILED") << '\n';
    check(passed, "a failing C call did not end in the Error it should have");
}

// These fail before any connection, so there is no compositor to run.
template <Site Fails>
void refused_before_connecting(jgui_test::Checker& check, ErrorCode code, const char* what) {
    const pid_t child = ::fork();
    if (child == 0) {
        const auto result = connect_and_pump<Fails>();
        ::_exit(!result && result.error().code == code ? 0 : 1);
    }
    int status = 0;
    check(child > 0 && ::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, what);
}

void test_failing_c_calls(jgui_test::Checker& check) {
    refused_before_connecting<Site::Connect>(check, ErrorCode::Connect, "no connection is a clean Error");
    refused_before_connecting<Site::KeymapContext>(check, ErrorCode::Keymap, "no xkb context is a clean Error");
    constexpr ErrorCode system = ErrorCode::System;
    // The control: nothing fails, so it runs until the compositor goes.
    refused_with<Site::Nothing>(check, "nothing fails", ErrorCode::Connect, "connection");
    refused_with<Site::Registry>(check, "wl_display_get_registry", ErrorCode::Connect, "registry");
    refused_with<Site::Sync>(check, "wl_display_sync", system, "wl_display_sync");
    refused_with<Site::BindCompositor>(check, "bind wl_compositor", ErrorCode::MissingGlobal, "wl_compositor");
    refused_with<Site::BindShm>(check, "bind wl_shm", ErrorCode::MissingGlobal, "wl_shm");
    refused_with<Site::BindWmBase>(check, "bind xdg_wm_base", system, "xdg_wm_base");
    refused_with<Site::BindSeat>(check, "bind wl_seat", system, "wl_seat");
    refused_with<Site::ListenRegistry>(check, "registry listener", system, "registry");
    refused_with<Site::ListenSync>(check, "sync listener", system, "sync");
    refused_with<Site::ListenWmBase>(check, "xdg_wm_base listener", system, "xdg_wm_base");
    refused_with<Site::ListenSeat>(check, "wl_seat listener", system, "wl_seat");
    refused_with<Site::ListenPointer>(check, "wl_pointer listener", system, "wl_pointer");
    refused_with<Site::ListenKeyboard>(check, "wl_keyboard listener", system, "wl_keyboard");
    refused_with<Site::ListenSurface>(check, "xdg_surface listener", system, "xdg surface");
    refused_with<Site::ListenToplevel>(check, "xdg_toplevel listener", system, "toplevel");
    refused_with<Site::ListenBuffer>(check, "wl_buffer listener", system, "buffer");
    refused_with<Site::CursorSurface>(check, "the cursor's surface", system, "cursor");
    refused_with<Site::WindowSurface>(check, "the window's surface", system, "window surface");
    refused_with<Site::XdgSurface>(check, "xdg_wm_base_get_xdg_surface", system, "xdg surface");
    refused_with<Site::Toplevel>(check, "xdg_surface_get_toplevel", system, "toplevel");
    refused_with<Site::Pointer>(check, "wl_seat_get_pointer", system, "wl_pointer");
    refused_with<Site::Keyboard>(check, "wl_seat_get_keyboard", system, "wl_keyboard");
    refused_with<Site::Pool>(check, "wl_shm_create_pool", system, "wl_shm_create_pool");
    refused_with<Site::Buffer>(check, "wl_shm_pool_create_buffer", system, "wl_shm_pool_create_buffer");
    refused_with<Site::KeyboardState>(check, "xkb_state_new", ErrorCode::Keymap, "keyboard state");
}

// --- The buffer file's system calls ---

constexpr unsigned int noexec_flags = MFD_CLOEXEC | MFD_NOEXEC_SEAL;

// A kernel too old for MFD_NOEXEC_SEAL refuses it with EINVAL.
struct OldKernel {
    static inline int calls = 0;
    static inline unsigned int last_flags = 0;
    [[nodiscard]] static int memfd_create(const char* name, unsigned int flags) noexcept {
        ++calls;
        last_flags = flags;
        if (flags == noexec_flags) {
            errno = EINVAL;
            return -1;
        }
        return ::memfd_create(name, flags);
    }
    [[nodiscard]] static int reserve(int fd, ::off_t bytes) noexcept { return jgui::detail::BufferFileCalls::reserve(fd, bytes); }
    [[nodiscard]] static int add_seals(int fd, int seals) noexcept { return jgui::detail::BufferFileCalls::add_seals(fd, seals); }
};

template <int First, int Second>
struct NoMemfd {
    static inline int calls = 0;
    [[nodiscard]] static int memfd_create(const char*, unsigned int) noexcept {
        errno = ++calls == 1 ? First : Second;
        return -1;
    }
    [[nodiscard]] static int reserve(int, ::off_t) noexcept { return 0; }
    [[nodiscard]] static int add_seals(int, int) noexcept { return 0; }
};

struct ReserveInterrupted {
    static inline int calls = 0;
    [[nodiscard]] static int memfd_create(const char* name, unsigned int flags) noexcept { return ::memfd_create(name, flags); }
    [[nodiscard]] static int reserve(int fd, ::off_t bytes) noexcept { return ++calls <= 3 ? EINTR : jgui::detail::BufferFileCalls::reserve(fd, bytes); }
    [[nodiscard]] static int add_seals(int fd, int seals) noexcept { return jgui::detail::BufferFileCalls::add_seals(fd, seals); }
};

struct NoSpace {
    [[nodiscard]] static int memfd_create(const char* name, unsigned int flags) noexcept { return ::memfd_create(name, flags); }
    [[nodiscard]] static int reserve(int, ::off_t) noexcept { return ENOSPC; }
    [[nodiscard]] static int add_seals(int fd, int seals) noexcept { return jgui::detail::BufferFileCalls::add_seals(fd, seals); }
};

struct NoSeals {
    [[nodiscard]] static int memfd_create(const char* name, unsigned int flags) noexcept { return ::memfd_create(name, flags); }
    [[nodiscard]] static int reserve(int fd, ::off_t bytes) noexcept { return jgui::detail::BufferFileCalls::reserve(fd, bytes); }
    [[nodiscard]] static int add_seals(int, int) noexcept {
        errno = EPERM;
        return -1;
    }
};

void test_buffer_file_calls(jgui_test::Checker& check) {
    const auto old_kernel = jgui::detail::create_buffer_file<OldKernel>(4096);
    check(old_kernel.has_value() && OldKernel::calls == 2, "EINVAL falls back to a second call");
    check((OldKernel::last_flags & MFD_ALLOW_SEALING) != 0 && (OldKernel::last_flags & MFD_CLOEXEC) != 0 && (OldKernel::last_flags & MFD_NOEXEC_SEAL) == 0, "with flags an old kernel knows");
    if (old_kernel) {
        const int seals = ::fcntl(old_kernel->get(), F_GET_SEALS);
        check(seals >= 0 && (seals & F_SEAL_SHRINK) != 0 && (seals & F_SEAL_GROW) != 0 && (seals & F_SEAL_SEAL) != 0, "and the file is still sealed");
        check(::ftruncate(old_kernel->get(), 0) != 0, "so it cannot be cut short");
    }
    using Both = NoMemfd<EINVAL, EMFILE>;
    const auto both = jgui::detail::create_buffer_file<Both>(4096);
    check(!both.has_value() && both.error().code == ErrorCode::System && Both::calls == 2, "both calls failing is an Error");
    using OtherError = NoMemfd<EMFILE, 0>;
    const auto other = jgui::detail::create_buffer_file<OtherError>(4096);
    check(!other.has_value() && OtherError::calls == 1, "any other error does not try again");
    check(jgui::detail::create_buffer_file<ReserveInterrupted>(4096).has_value() && ReserveInterrupted::calls == 4, "an interrupted reservation is repeated");
    check(!jgui::detail::create_buffer_file<NoSpace>(4096).has_value(), "no space is an Error");
    check(!jgui::detail::create_buffer_file<NoSeals>(4096).has_value(), "a file that cannot be sealed is an Error");
}

// --- A real short write: the file size limit lets a write go only part of the way ---

void test_short_write(jgui_test::Checker& check) {
    constexpr std::size_t limit = 4096;
    constexpr std::size_t wanted = 3 * limit;
    const pid_t child = ::fork();
    if (child == 0) {
        static_cast<void>(std::signal(SIGXFSZ, SIG_IGN));
        rlimit file_limit{limit, limit};
        if (::setrlimit(RLIMIT_FSIZE, &file_limit) != 0) ::_exit(2);
        const UniqueFd fd{::memfd_create("jcppgui-short-write", MFD_CLOEXEC)};
        std::vector<std::uint8_t> out(wanted, 0x5A);
        // The first call moves exactly up to the limit, the short transfer. The loop goes on from there, and the next
        // call fails with EFBIG, so the helper reports it and has written just what fitted.
        const auto written = jgui::detail::write_at<std::uint8_t>(fd.get(), out, 0);
        if (written) ::_exit(3);
        struct stat status {};
        if (::fstat(fd.get(), &status) != 0 || status.st_size != static_cast<::off_t>(limit)) ::_exit(4);
        std::vector<std::uint8_t> back(limit);
        if (!jgui::detail::read_at<std::uint8_t>(fd.get(), back, 0) || back != std::vector<std::uint8_t>(limit, 0x5A)) ::_exit(5);
        // A read that the file cannot fill: a short read, then the end of the file.
        std::vector<std::uint8_t> too_much(wanted);
        if (jgui::detail::read_at<std::uint8_t>(fd.get(), too_much, 0)) ::_exit(6);
        if (too_much.front() != 0x5A || too_much[limit - 1] != 0x5A) ::_exit(7);
        ::_exit(0);
    }
    int status = 0;
    check(child > 0 && ::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, "a real short write and a real short read end in an Error, with what fitted in place");
}

// --- What a signal really does ---

volatile std::sig_atomic_t alarms = 0;
void count_alarm(int) { alarms = alarms + 1; }

void start_alarms() {
    static_cast<void>(std::signal(SIGALRM, &count_alarm));
    itimerval timer{};
    timer.it_interval.tv_usec = 20;
    timer.it_value.tv_usec = 20;
    ::setitimer(ITIMER_REAL, &timer, nullptr);
}
void stop_alarms() {
    const itimerval off{};
    ::setitimer(ITIMER_REAL, &off, nullptr);
    static_cast<void>(std::signal(SIGALRM, SIG_DFL));
}

// The real reservation, under a fast timer. The kernel's tmpfs code can stop this with EINTR, and create_buffer_file
// repeats it from the start, so each one is counted. A timer faster than the whole reservation could then keep it from ever
// ending, so the alarms stop after the first few. On the kernel tried, 7.2, reserving 256 MiB took too little time
// for this ever to happen: the count printed was 0, so this path has run only with made-up results, above.
struct CountsInterrupts {
    static inline int interrupted = 0;
    [[nodiscard]] static int memfd_create(const char* name, unsigned int flags) noexcept { return ::memfd_create(name, flags); }
    [[nodiscard]] static int reserve(int fd, ::off_t bytes) noexcept {
        const int result = jgui::detail::BufferFileCalls::reserve(fd, bytes);
        if (result == EINTR && ++interrupted == 3) stop_alarms();
        return result;
    }
    [[nodiscard]] static int add_seals(int fd, int seals) noexcept { return jgui::detail::BufferFileCalls::add_seals(fd, seals); }
};

void test_real_signals(jgui_test::Checker& check) {
    constexpr std::size_t bytes = std::size_t{256} << 20U;
    start_alarms();
    const auto file = jgui::detail::create_buffer_file<CountsInterrupts>(bytes);
    stop_alarms();
    check(file.has_value(), "the largest buffer file is made, with a timer running");
    std::cout << "posix_fallocate on tmpfs: " << CountsInterrupts::interrupted << " real EINTR, " << alarms << " signals\n";
    if (!file) return;

    // The same timer against whole reads and writes of that file. The kernel lets only a fatal signal stop those on
    // tmpfs, so EINTR is not expected here, and the loop's handling of it is proved with made-up calls in
    // tests/test_untrusted.cpp. What must hold is that they all finish correctly.
    const auto before = alarms;
    std::vector<std::uint32_t> pixels(bytes / sizeof(std::uint32_t), 0x01020304U);
    start_alarms();
    const auto written = jgui::detail::write_at<std::uint32_t>(file->get(), pixels, 0);
    pixels.assign(pixels.size(), 0);
    const auto read = jgui::detail::read_at<std::uint32_t>(file->get(), pixels, 0);
    stop_alarms();
    check(written.has_value() && read.has_value() && pixels.front() == 0x01020304U && pixels.back() == 0x01020304U, "a whole write and read go through under the timer");
    std::cout << "pread and pwrite under " << (alarms - before) << " signals: complete\n";
}

}  // namespace

int main() {
    jgui_test::Checker check;
    test_buffer_file_calls(check);
    test_short_write(check);
    test_real_signals(check);
    test_failing_c_calls(check);
    return check.finish();
}
