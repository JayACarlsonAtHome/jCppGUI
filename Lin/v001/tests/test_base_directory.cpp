// File: tests/test_base_directory.cpp
// Purpose: The application's base directory. The rule runs in static_assert. At run time this program starts copies and
//          links of itself in temporary folders, each from a working directory that would mislead anything looking there.
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "check.hpp"
#include "jCppGUI/v001/base_directory.hpp"
#include "jCppGUI/v001/program_arguments.hpp"
#include "jCppGUI/v001/redraw_monitor.hpp"
#include "monitor_helpers.hpp"

namespace {

using jgui::application_base_from;
using jgui_test::TemporaryDirectory;
using std::filesystem::path;

[[nodiscard]] constexpr bool gives(std::string_view executable, std::string_view base) {
    const auto found = application_base_from(executable);
    return found.has_value() && *found == base;
}
[[nodiscard]] constexpr bool refuses(std::string_view executable, std::string_view reason) {
    const auto found = application_base_from(executable);
    return !found.has_value() && found.error().message.contains(reason);
}

constexpr std::string_view relative = "relative";
constexpr std::string_view none_above = "no folder named v";
constexpr std::string_view not_plain = "not in plain form";
constexpr std::string_view replaced = "replaced or removed";

// --- What a version folder's name is ---
static_assert(jgui::detail::is_version_folder_name("v001") && jgui::detail::is_version_folder_name("v000") && jgui::detail::is_version_folder_name("v999"));
static_assert(!jgui::detail::is_version_folder_name("") && !jgui::detail::is_version_folder_name("v") && !jgui::detail::is_version_folder_name("v01"));
static_assert(!jgui::detail::is_version_folder_name("v0001") && !jgui::detail::is_version_folder_name("vabc") && !jgui::detail::is_version_folder_name("V001"));
static_assert(!jgui::detail::is_version_folder_name("xv001") && !jgui::detail::is_version_folder_name("v001x") && !jgui::detail::is_version_folder_name("v00a"));
static_assert(!jgui::detail::is_version_folder_name("v0/1") && !jgui::detail::is_version_folder_name("1001") && !jgui::detail::is_version_folder_name("v 01"));

// --- The base ---
static_assert(gives("/anything/jCppGUI/Lin/v001/build/gcc-release/jcppgui-demo", "/anything/jCppGUI/Lin/v001"));
static_assert(gives("/a/v001/app", "/a/v001"));
static_assert(gives("/v001/app", "/v001"));
static_assert(gives("/a/v000/app", "/a/v000") && gives("/a/v999/app", "/a/v999"));
// The nearest wins.
static_assert(gives("/a/v001/x/v002/build/app", "/a/v001/x/v002"));
static_assert(gives("/a/v001/v002/app", "/a/v001/v002"));
// The file name never counts.
static_assert(gives("/a/v001/x/v002", "/a/v001"));
static_assert(gives("/a/v001/v001", "/a/v001"));
static_assert(refuses("/a/b/v001", none_above) && refuses("/v001", none_above));
// A name that only looks like one is passed over, and so is no base at all when nothing else is there.
static_assert(gives("/a/v001/v0001/app", "/a/v001") && gives("/a/v001/V002/xv003/v004x/app", "/a/v001"));
static_assert(refuses("/a/v01/app", none_above) && refuses("/a/v0001/app", none_above) && refuses("/a/vabc/app", none_above));
static_assert(refuses("/a/V001/app", none_above) && refuses("/a/xv001/app", none_above) && refuses("/a/v001x/app", none_above));
static_assert(refuses("/a/b/app", none_above) && refuses("/app", none_above));

// --- A path that is not absolute ---
static_assert(refuses("", "empty"));
static_assert(refuses("app", relative) && refuses("v001/app", relative) && refuses("./v001/app", relative) && refuses("../v001/app", relative));

// --- Only the plain form the kernel reports: a repeated or trailing slash, or a dot name, is refused and not tidied up ---
static_assert(refuses("/", not_plain) && refuses("//", not_plain));
static_assert(refuses("//a/v001/app", not_plain) && refuses("/a//v001/app", not_plain) && refuses("/a/v001//app", not_plain));
static_assert(refuses("/a/v001/app/", not_plain) && refuses("/a/v001/", not_plain) && refuses("/v001/", not_plain));
static_assert(refuses("/a/v001/./app", not_plain) && refuses("/a/./v001/app", not_plain) && refuses("/a/v001/.", not_plain));
static_assert(refuses("/a/v001/../app", not_plain) && refuses("/a/v001/build/../app", not_plain) && refuses("/a/v001/..", not_plain));
static_assert(gives("/a/v001/.hidden/app", "/a/v001") && gives("/a/v001/..x/app", "/a/v001") && gives("/a/v001/.../.app", "/a/v001"));
static_assert(refuses(std::string_view{"/a/v001/\0/app", 13}, "NUL"));

// --- The kernel's mark on an executable that has gone. Only at the very end of the path is it that mark ---
static_assert(refuses("/a/v001/build/app (deleted)", replaced));
static_assert(refuses("/a/b/app (deleted)", replaced));
static_assert(gives("/a/v001/build (deleted)/app", "/a/v001") && gives("/a/v001/app (deleted) x", "/a/v001") && gives("/a/v001/app(deleted)", "/a/v001"));
static_assert(refuses("/a/v001 (deleted)/app", none_above));

// What a child does, given how it was started. "where" only looks, for a program standing in the real source tree. "base"
// and "error" also save through a default FileSink, so the sink's refusal and its file are under test too.
[[nodiscard]] int child_checks(const std::string& gone, const std::string& change, const std::string& expect, const std::string& value) {
    std::error_code ignored;
    if (gone == "remove") {
        std::filesystem::remove(change, ignored);
    } else if (gone == "replace") {
        const std::string fresh = change + ".new";
        std::filesystem::copy_file(change, fresh, ignored);
        std::filesystem::rename(fresh, change, ignored);
    }
    const auto base = jgui::application_base_directory();
    if (base) {
        std::cout << "base: " << *base << '\n';
    } else {
        std::cout << "error: " << base.error().message << '\n';
    }
    if (expect == "where") return base && *base == value ? 0 : 1;
    jgui::FileSink sink;
    if (sink.file_name() != jgui::default_redraw_file_name) return 1;
    const auto saved = sink.save("x\n");
    if (expect == "base") return base && *base == value && saved ? 0 : 1;
    return !base && base.error().message.contains(value) && !saved && saved.error().message.contains("no base directory") && saved.error().message.contains(value) ? 0 : 1;
}

[[nodiscard]] bool holds_the_file(const path& directory) {
    return std::filesystem::exists(directory / jgui::default_redraw_file_name);
}

void place_copy(const path& from, const path& to) {
    std::filesystem::create_directories(to.parent_path());
    std::filesystem::copy_file(from, to);
}

void test_real_location(jgui_test::Checker& check, const path& self, const path& source) {
    const TemporaryDirectory temporary;
    const path top = std::filesystem::canonical(temporary.path());
    if (application_base_from((top / "program").string())) {
        // Not a pass: with such a folder above, the refusals below could not happen and the rest would prove nothing.
        check(false, "a folder above the temporary directory is named like a version");
        return;
    }
    // The working directory has a version folder above it, and argv[0] names another. A guess from either would show.
    const path here = top / "v888" / "here";
    const path false_home = top / "v777";
    std::filesystem::create_directories(here);
    std::filesystem::create_directories(false_home);
    const std::string false_name = (false_home / "app").string();
    const auto run = [&](const path& program, const std::string& gone, const path& change, const std::string& expect, const std::string& value) {
        return jgui_test::run_program(program, false_name, here, {"--child", gone, change.string(), expect, value});
    };
    const auto nothing_strayed = [&] { return std::filesystem::is_empty(here) && std::filesystem::is_empty(false_home); };

    // As built, under the real tree. Only looking, so nothing is written there.
    check(run(self, "stay", {}, "where", std::filesystem::canonical(source).string()) == 0, "as built, the base is this version's folder");
    check(nothing_strayed(), "and nothing is written where it was started");

    const path base = top / "v321";
    const path copy = base / "build" / "preset" / "app";
    place_copy(self, copy);
    check(run(copy, "stay", {}, "base", base.string()) == 0, "a copy under another version folder has that one as its base");
    check(holds_the_file(base) && !holds_the_file(copy.parent_path()) && nothing_strayed(), "and the file lands there and nowhere else");

    const path nested = base / "x" / "v322";
    const path deep_copy = nested / "build" / "app";
    place_copy(self, deep_copy);
    check(run(deep_copy, "stay", {}, "base", nested.string()) == 0 && holds_the_file(nested), "the nearest version folder wins");

    const path plain = top / "plain" / "build";
    const path lost = plain / "app";
    place_copy(self, lost);
    check(run(lost, "stay", {}, "error", std::string{none_above}) == 0, "a copy with no version folder above refuses, and the sink refuses with the reason");
    check(!holds_the_file(plain) && !holds_the_file(top / "plain") && !holds_the_file(top) && nothing_strayed(), "and nothing is written");

    // A symbolic link is followed by the kernel, so it is the executable's real location that counts.
    const path link = plain / "link";
    std::filesystem::create_symlink(copy, link);
    check(run(link, "stay", {}, "where", base.string()) == 0, "a symbolic link with no version folder above finds the real executable's base");

    // A hard link is a real name of its own, and that is the name the kernel reports.
    const path hard = plain / "hard";
    std::filesystem::create_hard_link(copy, hard);
    check(run(hard, "stay", {}, "error", std::string{none_above}) == 0, "a hard link with no version folder above refuses");

    // Gone while running: under a version folder, so only the kernel's mark can be the reason for the refusal.
    const path removed = base / "removed" / "app";
    place_copy(self, removed);
    check(run(removed, "remove", removed, "error", std::string{replaced}) == 0, "an executable removed while running refuses");
    check(!std::filesystem::exists(removed) && !holds_the_file(removed.parent_path()), "with its file really gone, and nothing written beside it");
    const path overwritten = base / "overwritten" / "app";
    place_copy(self, overwritten);
    check(run(overwritten, "replace", overwritten, "error", std::string{replaced}) == 0, "an executable replaced while running refuses");
    check(std::filesystem::exists(overwritten), "with a new file really standing at its name");

    // Those removals and refusals wrote nothing over the file from the first copy.
    check(nothing_strayed(), "and at the end, nothing was ever written where it was started");
}

}  // namespace

int main(int argc, char** argv) {
    const auto arguments = jgui::program_arguments(argc, argv);
    if (!arguments) {
        std::cerr << arguments.error().message << '\n';
        return 2;
    }
    if (arguments->size() == 6 && arguments->at(1) == "--child") return child_checks(arguments->at(2), arguments->at(3), arguments->at(4), arguments->at(5));

    jgui_test::Checker check;
    const char* self = std::getenv("JGUI_TEST_SELF");
    const char* source = std::getenv("JGUI_TEST_BASE");
    check(self != nullptr && source != nullptr, "CMake says where this program and the source tree are");
    if (self != nullptr && source != nullptr) test_real_location(check, self, source);
    return check.finish();
}
