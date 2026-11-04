// File: src/files/folder_names.hpp
// Purpose: What matching and deleting both need of a folder: every name in it, read through a descriptor that is already open, so a link swapped in cannot send them elsewhere.
#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "c_resource.hpp"
#include "jCppGUI/v001/error.hpp"

namespace jgui::inline v001::detail {

inline constexpr std::size_t most_names_in_a_folder = 1'000'000;

[[nodiscard]] inline std::string reason_of(int code) { return std::generic_category().message(code); }

// Every name in a folder but its own two, sorted as bytes.
[[nodiscard]] inline std::expected<std::vector<std::string>, Error> names_in(int folder) {
    const int own = ::openat(folder, ".", O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_CLOEXEC);
    if (own < 0) return make_error(ErrorCode::System, reason_of(errno));
    const CResource<DIR, ::closedir> listing{::fdopendir(own)};
    if (!listing) {
        const int why = errno;
        static_cast<void>(::close(own));
        return make_error(ErrorCode::System, reason_of(why));
    }
    std::vector<std::string> names;
    while (true) {
        errno = 0;
        const dirent* found = ::readdir(listing.get());
        if (found == nullptr) {
            if (errno != 0) return make_error(ErrorCode::System, reason_of(errno));
            break;
        }
        const std::string_view name{static_cast<const char*>(found->d_name)};
        if (name == "." || name == "..") continue;
        if (names.size() == most_names_in_a_folder) return make_error(ErrorCode::SizeLimit, "it holds too many things to check");
        names.emplace_back(name);
    }
    std::ranges::sort(names);
    return names;
}

}  // namespace jgui::inline v001::detail
