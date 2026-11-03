// File: tests/compile_fail/caption_unchecked_at_run_time.cpp
// Purpose: Text known only at run time cannot reach a widget unchecked. It goes through CheckedText::check.
#include <string>
#include <string_view>

#include "common.hpp"

int main() {
    const std::string held = "from outside";
    const std::string_view text{held};
#ifdef JGUI_BREAK
    auto ui = jgui::label<First>(text);
    static_cast<void>(ui);
#else
    const auto checked = jgui::CheckedText::check(text);
    if (checked) {
        auto ui = jgui::label<First>(*checked);
        static_cast<void>(ui);
    }
#endif
}
