// File: include/jCppGUI/v001/program_arguments.hpp
// Purpose: main's arguments as C++ values, so a program never has to read argc and argv as a C pointer
//          and count. Defined in src/, beside the one helper that reads such pairs.
#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

#include "error.hpp"

namespace jgui::inline v001 {

inline constexpr std::size_t max_program_arguments = 4096;
inline constexpr std::size_t max_program_argument_bytes = std::size_t{128} * 1024;  // Linux's own limit on one argument

// Refuses a negative count, a count over the limit, a null argument and an argument over its limit. Each
// argument is copied, so nothing keeps looking at what the C side gave.
[[nodiscard]] std::expected<std::vector<std::string>, Error> program_arguments(int argc, const char* const* argv);

}  // namespace jgui::inline v001
