// File: tests/check.hpp
// Purpose: The small test helper: counts checks, prints failures with their line, gives the exit code.
#pragma once

#include <cstdio>
#include <source_location>

namespace jgui_test {

class Checker {
public:
    void operator()(bool condition, const char* what, std::source_location where = std::source_location::current()) {
        ++checks_;
        if (!condition) {
            ++failures_;
            std::fprintf(stderr, "FAILED %s:%u: %s\n", where.file_name(), where.line(), what);
        }
    }

    [[nodiscard]] int finish() const {
        std::printf("%d checks, %d failures\n", checks_, failures_);
        return failures_ == 0 ? 0 : 1;
    }

private:
    int checks_{};
    int failures_{};
};

}  // namespace jgui_test
