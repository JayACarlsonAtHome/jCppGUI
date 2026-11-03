// File: tests/viewer_helpers.hpp
// Purpose: What the viewer's tests share: a redraw monitor's file with known rows in it, from the real writer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jCppGUI/v001/redraw_monitor.hpp"
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

}  // namespace jgui_test
