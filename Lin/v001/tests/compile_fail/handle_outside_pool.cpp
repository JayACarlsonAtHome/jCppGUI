// File: tests/compile_fail/handle_outside_pool.cpp
// Purpose: A Handle<T> made by anything other than its pool must not compile. Copying one is fine.
#include "common.hpp"

int main() {
    jgui::Pool<int> pool;
    const jgui::Handle<int> made = pool.emplace(1).value();
#ifdef JGUI_BREAK
    const jgui::Handle<int> forged{0U, 0U};
#else
    const jgui::Handle<int> forged{made};
#endif
    static_cast<void>(pool.find(forged));
}
