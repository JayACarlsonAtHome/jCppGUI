// File: tests/compile_fail/warn_sign_conversion.cpp
// Purpose: An implicit signed to unsigned conversion must fail to compile under the project's warning flags.
[[nodiscard]] static unsigned to_unsigned(int value) {
#ifdef JGUI_BREAK
    return value;
#else
    return value < 0 ? 0U : static_cast<unsigned>(value);
#endif
}

int main(int argc, char**) {
    return to_unsigned(argc) == 0U ? 1 : 0;
}
