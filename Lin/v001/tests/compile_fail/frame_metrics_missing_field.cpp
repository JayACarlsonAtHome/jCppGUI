// File: tests/compile_fail/frame_metrics_missing_field.cpp
// Purpose: A metrics type missing a field, used for a window frame, must fail on the FrameMetrics concept.
#include <cstdint>

#include "common.hpp"

struct Metrics {
    static constexpr std::int32_t edge_width = 4;
    static constexpr std::int32_t corner_length = 12;
    static constexpr std::int32_t title_height = 24;
#ifndef JGUI_BREAK
    static constexpr std::int32_t close_side = 16;
#endif
};

int main() {
    auto ui = jgui::window(jgui::window_frame<Metrics>(Plain<First>{}));
    static_cast<void>(ui);
}
