// File: tests/compile_fail/wrong_handle.cpp
// Purpose: A Handle<A> passed to a Pool<B> must not compile.
#include "common.hpp"

int main() {
    jgui::Pool<int> ints;
    jgui::Pool<long> longs;
    const jgui::Handle<int> handle = ints.emplace(1).value();
#ifdef JGUI_BREAK
    static_cast<void>(longs.find(handle));
#else
    static_cast<void>(ints.find(handle));
#endif
    static_cast<void>(longs);
}
