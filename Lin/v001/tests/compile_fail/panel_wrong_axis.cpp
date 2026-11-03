// File: tests/compile_fail/panel_wrong_axis.cpp
// Purpose: Panels sized along one axis must not share out a length of the other.
#include <array>
#include <span>

#include "common.hpp"

int main() {
    using WidePanel = jgui::PanelSize<jgui::detail::WidthTag>;
    const std::array<WidePanel, 1> panels{WidePanel{}};
#ifdef JGUI_BREAK
    return jgui::split_remaining_space(jgui::Height{5}, std::span<const WidePanel>{panels}).empty() ? 1 : 0;
#else
    return jgui::split_remaining_space(jgui::Width{5}, std::span<const WidePanel>{panels}).empty() ? 1 : 0;
#endif
}
