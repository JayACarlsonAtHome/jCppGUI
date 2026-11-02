// File: include/jCppGUI/v001/program_arguments.hpp
// Purpose: main's arguments as C++ values, so a program never has to read argc and argv as a C pointer
//          and count. Defined in src/, beside the one helper that reads such pairs.
#pragma once

#include <cstddef>
#include <expected>
#include <string_view>
#include <vector>

#include "error.hpp"

namespace jgui::inline v001 {

inline constexpr std::size_t max_program_arguments = 4096;

// Refuses a negative count, a count over the limit and a null argument. The views look at the argument
// strings themselves, which stay valid for the whole of main.
[[nodiscard]] std::expected<std::vector<std::string_view>, Error> program_arguments(int argc, const char* const* argv);

}  // namespace jgui::inline v001
