// File: tests/measure_viewer.cpp
// Purpose: What the viewer costs on the largest file the monitor writes, 65536 rows of each kind, with the real font, the real steady
//          clock and the redraw monitor's own timing, in memory and with no window. Run by hand in a release build, because a debug
//          build's numbers mean nothing. It prints and it checks nothing.
#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "file_io.hpp"
#include "jCppGUI/v001/headless_backend.hpp"
#include "monitor_helpers.hpp"
#include "test_access.hpp"
#include "viewer_helpers.hpp"
#include "viewer_ui.hpp"

namespace {

using namespace jgui;
using Clock = std::chrono::steady_clock;

[[nodiscard]] double ms_since(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
[[nodiscard]] double in_ms(std::int64_t ns) { return static_cast<double>(ns) / 1e6; }

using Monitor = FrameMonitor<SteadyClock, jgui_test::MemorySink>;

constexpr Size window_size{Width{1280}, Height{800}};

}  // namespace

int main() {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path path = std::filesystem::path{directory.path()} / "large.jtext";

    auto started = Clock::now();
    const std::string text = jgui_test::redraw_file_text(65536);
    std::cout << "the writer made " << text.size() << " bytes in " << ms_since(started) << " ms\n";
    {
        const detail::UniqueFd file{::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR)};
        if (file.get() < 0 || !detail::write_at(file.get(), std::span{std::string_view{text}}, 0)) std::cout << "the file could not be written\n";
    }

    started = Clock::now();
    const auto parsed = parse_jtext(text);
    const double parse_ms = ms_since(started);
    if (!parsed) {
        std::cout << parsed.error().message << '\n';
        return 1;
    }
    std::cout << "parsing took " << parse_ms << " ms, including one copy of the text\n";

    started = Clock::now();
    const auto file = read_jtext_file(path.string());
    std::cout << "read_jtext_file, reading and parsing, took " << ms_since(started) << " ms\n";

    started = Clock::now();
    auto loaded = viewer::load_file(file);
    std::cout << "typed rows for all four sections took " << ms_since(started) << " ms: " << loaded.frames.status << '\n';

    started = Clock::now();
    auto app = make_application(HeadlessBackend{window_size}, viewer::make_ui(std::move(loaded)), Monitor{});
    if (!app) {
        std::cout << app.error().message << '\n';
        return 1;
    }
    std::cout << "making the window took " << ms_since(started) << " ms\n";

    started = Clock::now();
    if (!app->process_pending()) std::cout << "the first frame failed\n";
    std::cout << "the first frame took " << ms_since(started) << " ms\n";

    const auto& frames = jgui_test::monitor(*app).frames();
    const std::size_t from = frames.size();
    const Point middle{Left{640}, Top{400}};
    for (int i = 0; i < 200; ++i) {
        jgui_test::backend(*app).inject(PointerScroll{middle, ScrollDown::of_steps(i % 2 == 0 ? 1 : -1), ScrollAcross{}});
        if (!app->process_pending()) std::cout << "a frame failed\n";
    }
    std::vector<std::int64_t> totals;
    for (std::size_t i = from; i < frames.size(); ++i) totals.push_back(frames[i].total_ns);
    std::ranges::sort(totals);
    if (!totals.empty()) std::cout << totals.size() << " frames of a wheel notch: total median " << in_ms(totals[totals.size() / 2]) << " ms, most " << in_ms(totals.back()) << " ms\n";
    return 0;
}
