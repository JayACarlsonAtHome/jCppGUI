// File: src/program_arguments.cpp
// Purpose: Turns main's argc and argv into C++ values, through the one helper that reads C pointer and
//          count pairs.
#include "jCppGUI/v001/program_arguments.hpp"

#include <utility>

#include "c_resource.hpp"
#include "jCppGUI/v001/geometry.hpp"

namespace jgui::inline v001 {

std::expected<std::vector<std::string>, Error> program_arguments(int argc, const char* const* argv) {
    if (argc < 0) return make_error(ErrorCode::SizeLimit, "the argument count is negative");
    auto pointers = detail::copy_from_c(argv, saturate_to<std::size_t>(argc), max_program_arguments);
    if (!pointers) return std::unexpected(std::move(pointers.error()));
    std::vector<std::string> arguments;
    arguments.reserve(pointers->size());
    for (const char* argument : *pointers) {
        auto copied = detail::string_from_c(argument, max_program_argument_bytes);
        if (!copied) return std::unexpected(std::move(copied.error()));
        arguments.push_back(std::move(*copied));
    }
    return arguments;
}

}  // namespace jgui::inline v001
