// File: tests/compile_fail/warn_narrowing.cpp
// Purpose: An implicit int to short conversion must fail to compile under the project's warning flags.
#include <limits>

[[nodiscard]] static short narrow(int value) {
#ifdef JGUI_BREAK
    return value;
#else
    return value > std::numeric_limits<short>::max() ? std::numeric_limits<short>::max() : static_cast<short>(value);
#endif
}

int main(int argc, char**) {
    return narrow(argc);
}
