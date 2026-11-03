// File: tests/compile_fail/vector_pool_at_run_time.cpp
// Purpose: A vector-backed pool moves its items as it grows, so using one at run time must not compile,
//          directly or through the Dynamic node above it.
#include <vector>

#include "common.hpp"

int main() {
#ifdef JGUI_BREAK
    jgui::Dynamic<First, jgui::Label<First>, std::vector> node;
#else
    jgui::Dynamic<First, jgui::Label<First>> node;
#endif
    const auto created = node.create(jgui::CheckedText{"text"});
    return created.has_value() ? 0 : 1;
}
