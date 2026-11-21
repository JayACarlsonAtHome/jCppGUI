// File: src/base_directory.cpp
// Purpose: Asks the kernel where the running executable is. The path rule's one exception is the literal below, in this
//          file only, and tests/check_source_rules.sh enforces that. PROCESS.md has the rule.
#include "jCppGUI/v001/base_directory.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace jgui::inline v001 {

std::expected<std::string, Error> application_base_directory() {
    std::error_code failed;
    const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", failed);
    if (failed) return make_error(ErrorCode::System, "cannot read where the executable is: " + failed.message());
    const auto base = application_base_from(executable.native());
    if (!base) return make_error(base.error().code, base.error().text() + ": " + executable.native());
    return std::string{*base};
}

}  // namespace jgui::inline v001
