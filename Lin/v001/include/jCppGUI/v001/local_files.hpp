// File: include/jCppGUI/v001/local_files.hpp
// Purpose: The file source for this machine's own disks. A program that uses it links jgui_files, and jgui.hpp does not include it. What holds a copy's
//          descriptors stays out of this header, as WaylandImpl does, behind one concrete type with a stateless deleter.
#pragma once

#include <expected>
#include <memory>
#include <span>
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

// Matches two things, and everything in them, a little at a time, and only reads. Links are never followed. Files must have the same SHA-256, folders the same names, and links must say the same.
// Times, permissions and owners are not compared. A thing that cannot be read, a pipe or device, and one and the same thing on both sides are all findings, and never a match.
class LocalMatchJob {
public:
    struct Impl;
    struct ImplDeleter {
        void operator()(Impl* impl) const noexcept;
    };

    explicit LocalMatchJob(std::unique_ptr<Impl, ImplDeleter> impl) noexcept;
    [[nodiscard]] std::expected<bool, Error> step();
    [[nodiscard]] MatchProgress progress() const noexcept;
    [[nodiscard]] std::span<const MatchFinding> findings() const noexcept;

private:
    std::unique_ptr<Impl, ImplDeleter> impl_;
};

[[nodiscard]] std::expected<LocalMatchJob, Error> start_local_match(const std::string& first, const std::string& second);

// Finds out what deleting something would remove, and changes nothing. Links are counted and not followed. Something that cannot be looked at, or another mounted disk anywhere inside, is an Error.
class LocalCountJob {
public:
    struct Impl;
    struct ImplDeleter {
        void operator()(Impl* impl) const noexcept;
    };

    explicit LocalCountJob(std::unique_ptr<Impl, ImplDeleter> impl) noexcept;
    [[nodiscard]] std::expected<bool, Error> step();
    [[nodiscard]] DeleteCount counted() const noexcept;

private:
    std::unique_ptr<Impl, ImplDeleter> impl_;
};

[[nodiscard]] std::expected<LocalCountJob, Error> start_local_count(const std::string& path);

// Moves one thing, whole, to the freedesktop.org trash of its own disk, by a rename, after writing what a desktop's trash needs to put it back. It never copies and never removes: if that disk has no trash this
// can use, it is refused and the thing stays. data_home is the folder holding the home trash, used for what is on the same disk. The trash and the folders in it must be this user's, and not open to everyone to write to.
[[nodiscard]] std::expected<void, Error> move_to_trash_under(const std::string& path, const std::string& data_home);

// The same, with data_home from XDG_DATA_HOME, or from HOME as the specification says.
[[nodiscard]] std::expected<void, Error> move_to_trash(const std::string& path);

// A whole regular file, of at most most_diff_file_bytes, without following a link at its name.
[[nodiscard]] std::expected<std::string, Error> read_text_file(const std::string& path);

// Sets when something last changed to now, and only that: nothing is made, and a link's own time is set, not that of what it points to.
[[nodiscard]] std::expected<void, Error> touch_local_file(const std::string& path);

// Opens a terminal in this folder running this command, each word of it one argument with no shell between, and returns without waiting. The first terminal that can be started is used: TERMINAL's if that is set,
// then those in known_terminals. It goes on by itself when this program ends. Refused when no terminal can be started.
[[nodiscard]] std::expected<void, Error> run_in_local_terminal(const std::string& folder, std::span<const std::string> command);

// What EDITOR says, or nothing.
[[nodiscard]] std::string local_editor();

struct LocalFiles {
    using CopyJob = LocalCopyJob;
    using MatchJob = LocalMatchJob;
    using CountJob = LocalCountJob;
    [[nodiscard]] std::expected<void, Error> touch(const std::string& path) const { return touch_local_file(path); }
    [[nodiscard]] std::string editor() const { return local_editor(); }
    [[nodiscard]] std::expected<void, Error> run_in_terminal(const std::string& folder, std::span<const std::string> command) const { return run_in_local_terminal(folder, command); }
    [[nodiscard]] std::expected<LocalCountJob, Error> start_count(const std::string& path) const { return start_local_count(path); }
    [[nodiscard]] std::expected<void, Error> move_to_trash(const std::string& path) const { return ::jgui::move_to_trash(path); }
    [[nodiscard]] std::expected<std::string, Error> read_text(const std::string& path) const { return read_text_file(path); }
    [[nodiscard]] std::expected<LocalMatchJob, Error> start_match(const std::string& first, const std::string& second) const { return start_local_match(first, second); }
    [[nodiscard]] std::expected<DirectoryListing, Error> read(const std::string& path) const { return read_directory(path); }
    [[nodiscard]] std::expected<LocalCopyJob, Error> start_copy(const std::string& from, const std::string& into) const { return start_local_copy(from, into); }
};

static_assert(FileSource<LocalFiles>);

}  // namespace jgui::inline v001
