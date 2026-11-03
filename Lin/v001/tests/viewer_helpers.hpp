// File: tests/viewer_helpers.hpp
// Purpose: What the viewer's tests share: a redraw monitor's file and a settings record with known rows in them, from the real writers.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "jCppGUI/v001/redraw_monitor.hpp"
#include "jCppGUI/v001/settings_recorder.hpp"
#include "monitor_helpers.hpp"

namespace jgui_test {

inline constexpr std::int32_t viewer_test_frames = 200;

// That many frames, each with one damaged widget, a time something had to wait, and a run that ended.
[[nodiscard]] constexpr std::string redraw_file_text(std::int32_t frames = viewer_test_frames) {
    jgui::FrameMonitor monitor{TestClock{}, MemorySink{}, jgui::FrameMonitorOptions{.label = "for the viewer"}};
    std::vector<std::uint32_t> pixels(4, 0);
    const jgui::Size size{jgui::Width{2}, jgui::Height{2}};
    auto buffer = jgui::PixelBuffer::create(pixels, size, jgui::PixelBuffer::tight_stride(size));
    for (std::int32_t i = 0; i < frames; ++i) {
        monitor.clock().queue({1000, 11, 22, 33, 44, 55});
        monitor.frame_begins(i == 0 ? jgui::RedrawReason::FirstFrame : jgui::RedrawReason::WidgetDamage, jgui::Rect{jgui::Point{}, jgui::Size{jgui::Width{640}, jgui::Height{480}}});
        monitor.widget_damaged(jgui::DamagedWidget{"Button", "HelpButton", 3});
        monitor.phases_begin();
        monitor.laid_out();
        monitor.acquired();
        monitor.painted(*buffer);
        monitor.presented();
    }
    monitor.deferred(jgui::RedrawReason::Resize, jgui::Size{jgui::Width{800}, jgui::Height{600}});
    monitor.finish();
    return monitor.sink().saved;
}

inline constexpr std::string_view settings_test_left = "4242";

// A settings record from the real writer: three items, the last with a place it has no right to, so one problem.
[[nodiscard]] constexpr std::string settings_file_text() {
    jgui::SettingsRecorder recorder{TestClock{}, MemorySink{}};
    const jgui::Rect whole{jgui::Point{}, jgui::Size{jgui::Width{640}, jgui::Height{480}}};
    recorder.items_begin(3);
    recorder.item_found(jgui::ItemFound{1, 0, "jgui::Window", "", whole});
    recorder.item_found(jgui::ItemFound{2, 1, "jgui::Button", "demo::HelpButton", jgui::Rect{jgui::Point{jgui::Left{7}, jgui::Top{9}}, jgui::Size{jgui::Width{50}, jgui::Height{20}}}});
    recorder.item_found(jgui::ItemFound{3, 1, "jgui::Label", "demo::Stray", jgui::Rect{jgui::Point{jgui::Left{4242}, jgui::Top{1}}, jgui::Size{jgui::Width{10}, jgui::Height{10}}}});
    recorder.items_end();
    recorder.finish();
    return recorder.sink().saved;
}

}  // namespace jgui_test
