// File: tests/compile_fail/empty_receivers.cpp
// Purpose: A message with an empty To<> must not compile, even if it is never sent.
#include "common.hpp"

#ifdef JGUI_BREAK
using Nobody = jgui::Message<"nobody", jgui::To<>>;
#else
using Nobody = jgui::Message<"nobody", jgui::To<First>>;
#endif

int main() {
    const Nobody message{};
    static_cast<void>(message);
}
