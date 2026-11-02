// File: src/program_arguments.cpp
// Purpose: Turns main's argc and argv into C++ values, through the one helper that reads C pointer and
//          count pairs.
#include "jCppGUI/v001/program_arguments.hpp"

#include <utility>

#include "c_resource.hpp"
#include "jCppGUI/v001/geometry.hpp"

namespace jgui::inline v001 {

std::expected<std::vector<std::string_view>, Error> program_arguments(int argc, const char* const* argv) {
    if (argc < 0) return make_error(ErrorCode::SizeLimit, "the argument count is negative");
    auto pointers = detail::copy_from_c(argv, saturate_to<std::size_t>(argc), max_program_arguments);
    if (!pointers) return std::unexpected(std::move(pointers.error()));
    std::vector<std::string_view> arguments;
    arguments.reserve(pointers->size());
    for (const char* argument : *pointers) {
        if (argument == nullptr) return make_error(ErrorCode::System, "an argument is a null pointer");
        arguments.emplace_back(argument);
    }
    return arguments;
}

}  // namespace jgui::inline v001
