// File: tests/compile_fail/warn_shadow.cpp
// Purpose: A variable that shadows another must fail to compile under the project's warning flags.
int main(int argc, char**) {
    int total = argc;
    if (argc > 1) {
#ifdef JGUI_BREAK
        const int total = argc + 1;
        return total;
#else
        const int more = argc + 1;
        return more;
#endif
    }
    return total;
}
