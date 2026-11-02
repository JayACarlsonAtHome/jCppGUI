// File: tests/compile_fail/callback_runs_key_from_number.cpp
// Purpose: A key can be named at compile time, but no run-time number can become a wire value above the backend.
#include <cstdint>

#include "common.hpp"

int main(int argc, char**) {
#ifdef JGUI_BREAK
    const jgui::Keysym key{static_cast<std::uint32_t>(argc)};
#else
    const jgui::Keysym key{0x41};
    static_cast<void>(argc);
#endif
    static_cast<void>(key);
}
