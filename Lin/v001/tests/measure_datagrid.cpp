// File: tests/measure_datagrid.cpp
// Purpose: What a frame of the grid costs, with the real font, the real steady clock and the redraw monitor's own timing, in
//          memory and with no window: at 100 rows and at 1,000,000, and when every cell holds very long text. Run by hand in
//          a release build, because a debug build's numbers mean nothing. It prints and it checks nothing.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "jCppGUI/v001/headless_backend.hpp"
#include "jCppGUI/v001/jgui.hpp"
#include "monitor_helpers.hpp"
#include "test_access.hpp"

namespace {

using namespace jgui;

struct Line {
    std::int64_t number{};
    std::string name{};
    std::string note{};
    bool on{};
    Width wide{};
    Left left{};
};
struct GridTag;
using Grid = DataGrid<GridTag, Line, NoGridMessage, Column<"Number", &Line::number, 120>, Column<"Name", &Line::name, 240>, Column<"Note", &Line::note, 320>, Column<"On", &Line::on, 80>,
                      Column<"Wide", &Line::wide, 120>, Column<"Left", &Line::left, 120>>;

using Monitor = FrameMonitor<SteadyClock, jgui_test::MemorySink>;

constexpr Size window_size{Width{1280}, Height{800}};

[[nodiscard]] std::vector<Line> make_lines(std::int32_t count, bool long_text) {
    std::vector<Line> lines;
    lines.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        std::string note = long_text ? std::string(100000, 'w') : "note number " + std::to_string((i * 7919) % 100000);
        lines.push_back(Line{(std::int64_t{i} * 7919) % 1000003, "name " + std::to_string(i), std::move(note), i % 2 == 0, Width{i}, Left{-i}});
    }
    return lines;
}

struct Numbers {
    std::int64_t median{};
    std::int64_t most{};
};

[[nodiscard]] Numbers numbers_of(std::vector<std::int64_t> values) {
    if (values.empty()) return Numbers{};
    std::ranges::sort(values);
    return Numbers{values[values.size() / 2], values.back()};
}

[[nodiscard]] double in_ms(std::int64_t ns) { return static_cast<double>(ns) / 1e6; }

// Each row is one kind of frame, driven by the same events. The first frame shapes the headers too, and is left out.
template <class App, class Drive>
void measure(App& app, const char* name, Drive drive) {
    auto& frames = jgui_test::monitor(app).frames();
    const std::size_t from = frames.size();
    constexpr int runs = 200;
    for (int i = 0; i < runs; ++i) {
        jgui_test::backend(app).inject(drive(i));
        if (!app.process_pending()) std::cout << "a frame failed\n";
    }
    std::vector<std::int64_t> layout;
    std::vector<std::int64_t> paint;
    std::vector<std::int64_t> present;
    std::vector<std::int64_t> total;
    for (std::size_t i = from; i < frames.size(); ++i) {
        layout.push_back(frames[i].layout_ns);
        paint.push_back(frames[i].paint_ns);
        present.push_back(frames[i].present_ns);
        total.push_back(frames[i].total_ns);
    }
    const Numbers l = numbers_of(std::move(layout));
    const Numbers p = numbers_of(std::move(paint));
    const Numbers t = numbers_of(std::move(total));
    std::cout << name << ": " << (frames.size() - from) << " frames, layout median " << in_ms(l.median) << " ms, paint median " << in_ms(p.median) << " ms most " << in_ms(p.most) << " ms, total median "
              << in_ms(t.median) << " ms most " << in_ms(t.most) << " ms\n";
}

void measure_rows(std::int32_t count, bool long_text) {
    auto app = make_application(HeadlessBackend{window_size}, window(window_frame(Grid{})), Monitor{});
    if (!app) {
        std::cout << "no application: " << app.error().message << '\n';
        return;
    }
    auto& grid = app->ui().get<GridTag>();
    const auto started = std::chrono::steady_clock::now();
    if (!grid.set_rows(make_lines(count, long_text))) std::cout << "rows refused\n";
    const auto given = std::chrono::steady_clock::now();
    if (!app->process_pending()) std::cout << "first frame failed\n";
    std::cout << "--- " << count << " rows" << (long_text ? " with 100000 bytes in each Note" : "") << ", " << jgui_test::number(window_size.width()) << "x" << jgui_test::number(window_size.height())
              << ": making them and set_rows took " << std::chrono::duration<double, std::milli>(given - started).count() << " ms, " << grid.cells_shaped() << " cells shaped\n";
    const Point middle{Left{640}, Top{400}};
    measure(*app, "a wheel notch, each way in turn", [&](int i) { return PointerScroll{middle, ScrollDown::of_steps(i % 2 == 0 ? 1 : -1), ScrollAcross{}}; });
    measure(*app, "a press on a row, a new one each time", [&](int i) {
        return PointerButton{Point{Left{640}, Top{100 + (i % 20) * 16}}, MouseButton::Left, i % 2 == 0 ? ButtonState::Pressed : ButtonState::Released, jgui_test::serial(1)};
    });
    if (long_text) return;
    const auto timed_sort = [&](std::int32_t x, const char* name) {
        const auto before = std::chrono::steady_clock::now();
        jgui_test::backend(*app).inject(PointerButton{Point{Left{x}, Top{30}}, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)});
        jgui_test::backend(*app).inject(PointerButton{Point{Left{x}, Top{30}}, MouseButton::Left, ButtonState::Released, jgui_test::serial(1)});
        if (!app->process_pending()) std::cout << "a sort failed\n";
        std::cout << name << ": " << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count() << " ms, with its frame\n";
    };
    timed_sort(60, "sort by a number");
    timed_sort(200, "sort by a string");
}

}  // namespace

int main() {
    measure_rows(100, false);
    measure_rows(1'000'000, false);
    measure_rows(1000, true);
    return 0;
}
