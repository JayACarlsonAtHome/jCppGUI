// File: include/jCppGUI/v001/local_files.hpp
// Purpose: The file source for this machine's own disks. A program that uses it links jgui_files, and jgui.hpp does not include it. What holds a copy's
//          descriptors stays out of this header, as WaylandImpl does, behind one concrete type with a stateless deleter.
#pragma once

#include <expected>
#include <memory>
#include <string>

#include "error.hpp"
#include "file_listing.hpp"

namespace jgui::inline v001 {

// What a folder holds, with folders first. What stands at the path must itself be a folder and not a link to one, and nothing in it is followed.
// A name that cannot be shown is counted and left out. More things than a grid can hold is refused.
[[nodiscard]] std::expected<DirectoryListing, Error> read_directory(const std::string& path);

// The full path of a folder to start in, as the system resolves it, relative to where the program started, with every link in it followed. This is the one
// place a link is followed, because the person running the program named it. Something that is not a folder, or that a pane could not show, is refused.
[[nodiscard]] std::expected<std::string, Error> starting_folder(const std::string& path);

// Copies one thing, and everything in it, into a folder, a chunk at a time. It never replaces anything, and never follows a link: a link is copied as a link. A pipe,
// a device or a socket is counted and left. A file not finished is removed, and folders already made stay. Set-user-ID bits, the owner and extended attributes are not copied.
// Each file, once written, is flushed to the disk, dropped from the cache where the system allows, and read back from both sides, and it counts only if the two SHA-256 sums match.
class LocalCopyJob {
public:
    struct Impl;
    struct ImplDeleter {
        void operator()(Impl* impl) const noexcept;
    };

    explicit LocalCopyJob(std::unique_ptr<Impl, ImplDeleter> impl) noexcept;
    [[nodiscard]] std::expected<bool, Error> step();
    [[nodiscard]] CopyProgress progress() const noexcept;

private:
    std::unique_ptr<Impl, ImplDeleter> impl_;
};

// Starts a copy of what is at from, under its own name, into that folder. A name already there in the folder is refused before anything is made.
[[nodiscard]] std::expected<LocalCopyJob, Error> start_local_copy(const std::string& from, const std::string& into);

struct LocalFiles {
    using CopyJob = LocalCopyJob;
    [[nodiscard]] std::expected<DirectoryListing, Error> read(const std::string& path) const { return read_directory(path); }
    [[nodiscard]] std::expected<LocalCopyJob, Error> start_copy(const std::string& from, const std::string& into) const { return start_local_copy(from, into); }
};

static_assert(FileSource<LocalFiles>);

}  // namespace jgui::inline v001
