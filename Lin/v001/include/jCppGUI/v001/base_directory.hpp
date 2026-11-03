// File: include/jCppGUI/v001/base_directory.hpp
// Purpose: The application's base directory: the vNNN folder that contains the running executable. The rule is a pure
//          function, so it runs in static_assert. The one call that asks the system where the executable is lives in src/.
#pragma once

#include <algorithm>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "error.hpp"

namespace jgui::inline v001 {

namespace detail {
// Exactly "v" and three digits, so not v01, v0001, vabc, V001, xv001 or v001x.
[[nodiscard]] constexpr bool is_version_folder_name(std::string_view name) noexcept {
    return name.size() == 4 && name.front() == 'v' && std::ranges::all_of(name.substr(1), [](char c) { return c >= '0' && c <= '9'; });
}
}  // namespace detail

// The nearest folder above an executable whose name is a version, as a view of the text given. The file name itself never
// counts, so a program named v001 is only a program. Anything else is an Error and never a guess. That covers a path
// that is not absolute, and one with no such folder. It also covers a path that is not in the plain form the kernel
// reports: a repeated or trailing slash, or a "." or ".." name, because walking up through a ".." by its text could
// name the wrong folder. And the kernel's mark on an executable that has been replaced or removed while running.
[[nodiscard]] constexpr std::expected<std::string_view, Error> application_base_from(std::string_view executable) {
    if (executable.empty()) return make_error(ErrorCode::System, "the executable's path is empty");
    if (executable.front() != '/') return make_error(ErrorCode::System, "the executable's path is relative");
    if (executable.contains('\0')) return make_error(ErrorCode::System, "the executable's path holds a NUL");
    if (executable.ends_with(" (deleted)")) return make_error(ErrorCode::System, "the executable was replaced or removed while running");
    std::optional<std::size_t> base_end;
    std::size_t at = 1;
    while (true) {
        const std::size_t slash = executable.find('/', at);
        const std::string_view name = executable.substr(at, slash == std::string_view::npos ? slash : slash - at);
        if (name.empty() || name == "." || name == "..") return make_error(ErrorCode::System, "the executable's path is not in plain form");
        if (slash == std::string_view::npos) break;
        if (detail::is_version_folder_name(name)) base_end = slash;
        at = slash + 1;
    }
    if (!base_end) return make_error(ErrorCode::System, "no folder named v and three digits contains the executable");
    return executable.substr(0, *base_end);
}

// Applies that rule to where the system says this program's executable really is, whatever it was started as and from
// wherever. A link to the executable counts as the executable it leads to. Defined in src/base_directory.cpp.
[[nodiscard]] std::expected<std::string, Error> application_base_directory();

}  // namespace jgui::inline v001
