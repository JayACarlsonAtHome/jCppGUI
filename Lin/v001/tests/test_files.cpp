// File: tests/test_files.cpp
// Purpose: The file manager. Names, paths, dates and sizes, then the panes in static_assert with a made-up disk, from events to state and pixels: above all that
//          what is done in one pane does nothing to the other, and what a copy or a drag asks of the disk. At run time, reading and copying against real folders, and copies that do not match.
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "check.hpp"
#include "file_io.hpp"
#include "files_ui.hpp"
#include "jCppGUI/v001/local_files.hpp"
#include "jCppGUI/v001/program_arguments.hpp"
#include "monitor_helpers.hpp"
#include "test_access.hpp"

namespace {

using namespace jgui;
using jgui_test::BlockShaper;

// --- Names and paths ---
static_assert(file_name_can_show("a b.txt") && file_name_can_show("\xC3\xA9t\xC3\xA9") && file_name_can_show("\xC2\xA0"));
static_assert(!file_name_can_show("") && !file_name_can_show(".") && !file_name_can_show("..") && !file_name_can_show("a/b"));
static_assert(!file_name_can_show("a\nb") && !file_name_can_show("a\x7F") && !file_name_can_show("\xFF") && !file_name_can_show("\xC2\x85"), "a newline, DEL, bad UTF-8 and a C1 control");
static_assert(file_name_can_show(std::string(255, 'x')) && !file_name_can_show(std::string(256, 'x')));
static_assert(file_path_can_show("/") && file_path_can_show("/a") && file_path_can_show("/a/b c"));
static_assert(!file_path_can_show("") && !file_path_can_show("a/b") && !file_path_can_show("/a/") && !file_path_can_show("//a") && !file_path_can_show("/a/../b") && !file_path_can_show("/./a") && !file_path_can_show("/a\nb"));
static_assert(file_path_can_show("/" + std::string(4095, 'x')) && !file_path_can_show("/" + std::string(4096, 'x')));
static_assert(parent_of_path("/a/b") == "/a" && parent_of_path("/a") == "/" && parent_of_path("/") == "/");
static_assert(child_of_path("/", "a") == "/a" && child_of_path("/a", "b") == "/a/b");
static_assert(compare_file_names("a", "B") < 0 && compare_file_names("B", "a") > 0 && compare_file_names("a", "ab") < 0 && compare_file_names("a", "a") == 0 && compare_file_names("A", "a") < 0);

// --- Sizes and dates ---
static_assert(CellFormat<FileBytes>::text(FileBytes{}).empty());
static_assert(CellFormat<FileBytes>::text(FileBytes{true, 0}) == "0" && CellFormat<FileBytes>::text(FileBytes{true, 999}) == "999" && CellFormat<FileBytes>::text(FileBytes{true, 1000}) == "1,000");
static_assert(CellFormat<FileBytes>::text(FileBytes{true, 18'446'744'073'709'551'615U}) == "18,446,744,073,709,551,615");
static_assert(CellFormat<FileTime>::text(FileTime{}).empty());
static_assert(CellFormat<FileTime>::text(FileTime{true, 0}) == "1970-01-01 00:00");
static_assert(CellFormat<FileTime>::text(FileTime{true, -1}) == "1969-12-31 23:59");
static_assert(CellFormat<FileTime>::text(FileTime{true, 951'782'400}) == "2000-02-29 00:00", "a leap day");
static_assert(CellFormat<FileTime>::text(FileTime{true, 1793664000}) == "2026-11-03 00:00");
static_assert(!CellFormat<FileTime>::text(FileTime{true, std::numeric_limits<std::int64_t>::max()}).empty() && !CellFormat<FileTime>::text(FileTime{true, std::numeric_limits<std::int64_t>::min()}).empty(), "the ends of the range, in constant evaluation, so no overflow");

// --- Names in a tree ---
static_assert(CellFormat<TreeName>::text(TreeName{"a", 0, TreeMark::Closed}) == "[+] a");
static_assert(CellFormat<TreeName>::text(TreeName{"a", 1, TreeMark::Open}) == "    [-] a");
static_assert(CellFormat<TreeName>::text(TreeName{"a", 1, TreeMark::None}) == "        a");

[[nodiscard]] constexpr bool folders_come_first() {
    std::vector<FileEntry> entries{{"b", FileKind::File, {}, {}}, {"Z", FileKind::Folder, {}, {}}, {"a", FileKind::Folder, {}, {}}, {"A", FileKind::File, {}, {}}};
    sort_file_entries(entries);
    return entries[0].name == "a" && entries[1].name == "Z" && entries[2].name == "A" && entries[3].name == "b";
}
static_assert(folders_come_first());

// --- A made-up disk ---
//   /            top, deep
//   /top         one, two, note.txt
//   /top/one     inner, f1
//   /top/one/inner  f2
//   /top/two     f3
//   /other       g1
//   /locked      cannot be read
// What a test can see was asked of the made-up disk, and what the disk has become. Copies arrive when their last step is done.
struct FakeDisk {
    std::vector<std::string> gone{};  // taken away by something else, because nothing here can
    std::vector<std::string> arrived{};
    std::vector<std::string> matched{};
    int steps_to_copy{3};
    int steps_taken{};
    int jobs_ended{};
    bool step_fails{};
    bool start_fails{};
    bool delete_fails{};
    bool touch_fails{};
    bool no_terminal{};
    std::string editor{};
    bool no_picture_folder{};
    bool picture_fails{};
    std::vector<std::vector<std::uint8_t>> pictures{};
    std::vector<std::string> touched{};
    std::vector<std::vector<std::string>> terminals{};
};

struct FakeJob {
    FakeDisk* disk{};
    std::string target{};
    int left{};
    [[nodiscard]] constexpr std::expected<bool, Error> step() {
        ++disk->steps_taken;
        if (disk->step_fails) return make_error(ErrorCode::System, "the disk is full");
        if (--left > 0) return false;
        disk->arrived.push_back(target);
        return true;
    }
    [[nodiscard]] constexpr CopyProgress progress() const noexcept { return CopyProgress{.files = 1, .bytes = 1000, .bytes_checked = 1000}; }
};

// Two steps, and a path with "bad" in it does not match.
struct FakeMatchJob {
    FakeDisk* disk{};
    std::string first{};
    std::string second{};
    int left{2};
    std::vector<MatchFinding> found{};
    [[nodiscard]] constexpr std::expected<bool, Error> step() {
        if (disk != nullptr) disk->matched.push_back(first + " " + second);
        if (--left > 0) return false;
        if (first.contains("bad") || second.contains("bad")) found.push_back(MatchFinding{std::string{last_name_of(first)}, "The contents differ by SHA-256"});
        return true;
    }
    [[nodiscard]] constexpr MatchProgress progress() const noexcept {
        return found.empty() ? MatchProgress{.files = 1, .bytes = 10} : MatchProgress{.differences = 1};
    }
    [[nodiscard]] constexpr std::span<const MatchFinding> findings() const noexcept { return found; }
};

// Two steps, and it finds one file for each thing and one folder for each thing with no dot in its name. A path with "locked" in it cannot be counted.
struct FakeCountJob {
    std::string path{};
    int left{2};
    [[nodiscard]] constexpr std::expected<bool, Error> step() {
        if (path.contains("locked")) return make_error(ErrorCode::System, "cannot open it");
        return --left <= 0;
    }
    [[nodiscard]] constexpr DeleteCount counted() const noexcept { return DeleteCount{1, path.contains('.') ? 0U : 1U, 10}; }
};

struct FakeFiles {
    using CopyJob = FakeJob;
    using MatchJob = FakeMatchJob;
    using CountJob = FakeCountJob;
    FakeDisk* disk{};

    [[nodiscard]] constexpr std::expected<FakeCountJob, Error> start_count(const std::string& path) const { return FakeCountJob{path}; }
    [[nodiscard]] constexpr std::string editor() const { return disk == nullptr ? std::string{} : disk->editor; }
    [[nodiscard]] constexpr std::expected<TouchOutcome, Error> touch(const std::string& folder, const std::string& name) const {
        if (disk->touch_fails) return make_error(ErrorCode::System, "cannot touch it");
        const std::string path = child_of_path(folder, name);
        disk->touched.push_back(path);
        const auto there = this->read(folder);
        if (there && std::ranges::contains(there->entries, name, &FileEntry::name)) return TouchOutcome::TimeSet;
        disk->arrived.push_back(path);
        return TouchOutcome::Made;
    }
    [[nodiscard]] constexpr std::expected<void, Error> rename(const std::string& path, const std::string& new_name) const {
        const auto there = this->read(parent_of_path(path));
        if (there && std::ranges::contains(there->entries, new_name, &FileEntry::name)) return make_error(ErrorCode::System, new_name + " is already there");
        disk->gone.push_back(path);
        disk->arrived.push_back(child_of_path(parent_of_path(path), new_name));
        return {};
    }
    [[nodiscard]] constexpr std::expected<std::string, Error> picture_folder() const {
        if (disk->no_picture_folder) return make_error(ErrorCode::System, "there is no home");
        return std::string{"/shots"};
    }
    [[nodiscard]] constexpr std::expected<std::string, Error> save_picture(const std::string& folder, const std::string& name, std::span<const std::uint8_t> png) const {
        if (disk->picture_fails) return make_error(ErrorCode::System, "the disk is full");
        disk->pictures.emplace_back(png.begin(), png.end());
        disk->arrived.push_back(child_of_path(folder, name));
        return child_of_path(folder, name);
    }
    [[nodiscard]] constexpr std::expected<void, Error> run_in_terminal(const std::string& folder, std::span<const std::string> command) const {
        if (disk->no_terminal) return make_error(ErrorCode::System, "no terminal");
        std::vector<std::string> what{folder};
        what.insert(what.end(), command.begin(), command.end());
        disk->terminals.push_back(std::move(what));
        return {};
    }
    [[nodiscard]] constexpr std::expected<void, Error> move_to_trash(const std::string& path) const {
        if (disk->delete_fails) return make_error(ErrorCode::System, "cannot delete it");
        disk->gone.push_back(path);
        return {};
    }
    [[nodiscard]] constexpr std::expected<std::string, Error> read_text(const std::string& path) const {
        if (path.ends_with("f1")) return std::string{"one\ntwo\nthree\n"};
        if (path.ends_with("f2")) return std::string{"one\nTWO\nthree\nfour\n"};
        if (path.ends_with("f3")) return std::string{"one\ntwo\nthree\n"};
        if (path.ends_with("note.txt")) return std::string{"a\0b", 3};
        return make_error(ErrorCode::System, last_name_of(path).empty() ? std::string{"no name"} : std::string{last_name_of(path)} + " is not a file");
    }

    [[nodiscard]] constexpr std::expected<FakeMatchJob, Error> start_match(const std::string& first, const std::string& second) const {
        if (first.contains("locked")) return make_error(ErrorCode::System, "cannot open it");
        return FakeMatchJob{disk, first, second};
    }

    [[nodiscard]] static constexpr FileEntry folder(std::string_view name) { return FileEntry{std::string{name}, FileKind::Folder, {}, FileTime{true, 0}}; }
    [[nodiscard]] static constexpr FileEntry file(std::string_view name, std::uint64_t size) { return FileEntry{std::string{name}, FileKind::File, FileBytes{true, size}, FileTime{true, 0}}; }

    [[nodiscard]] static constexpr std::expected<DirectoryListing, Error> on_the_disk(const std::string& path) {
        if (path == "/") return DirectoryListing{{folder("locked"), folder("other"), folder("top")}, 0};
        if (path == "/top") return DirectoryListing{{folder("one"), folder("two"), file("note.txt", 5)}, 1};
        if (path == "/top/one") return DirectoryListing{{folder("inner"), file("f1", 1000)}, 0};
        if (path == "/top/one/inner") return DirectoryListing{{file("f2", 2)}, 0};
        if (path == "/top/two") return DirectoryListing{{file("f3", 3)}, 0};
        if (path == "/other") return DirectoryListing{{file("g1", 7)}, 0};
        if (path == "/shots") return DirectoryListing{{}, 0};
        return make_error(ErrorCode::System, "cannot open " + path);
    }

    [[nodiscard]] constexpr std::expected<DirectoryListing, Error> read(const std::string& path) const {
        auto listing = on_the_disk(path);
        if (!listing || disk == nullptr) return listing;
        std::erase_if(listing->entries, [&](const FileEntry& entry) { return std::ranges::contains(disk->gone, child_of_path(path, entry.name)); });
        for (const std::string& target : disk->arrived)
            if (parent_of_path(target) == path) listing->entries.push_back(file(last_name_of(target), 1));
        sort_file_entries(listing->entries);
        return listing;
    }

    [[nodiscard]] constexpr std::expected<FakeJob, Error> start_copy(const std::string& from, const std::string& into) const {
        if (disk->start_fails) return make_error(ErrorCode::System, "that name is already there");
        return FakeJob{disk, child_of_path(into, last_name_of(from)), disk->steps_to_copy};
    }
};
static_assert(FileSource<FakeFiles>);

using Left_ = files::LeftPane;
using Right_ = files::RightPane;
constexpr Size window_size{Width{1400}, Height{400}};
constexpr Rect whole_window{Point{}, window_size};

[[nodiscard]] constexpr auto ui_on(FakeDisk& disk, files::StartFolder left = std::string{"/top"}, files::StartFolder right = std::string{"/top"}) {
    return files::make_ui(FakeFiles{&disk}, left, right);
}

[[nodiscard]] constexpr auto fake_ui(files::StartFolder left = std::string{"/top"}, files::StartFolder right = std::string{"/top"}) {
    return files::make_ui(FakeFiles{}, left, right);
}

template <class Tree>
constexpr bool lay_out(Tree& tree, Rect area = whole_window) {
    detail::TestAccess::set_size(tree, area.size());
    return jgui_test::layout(tree, area, BlockShaper{}).has_value();
}

template <class Pane>
[[nodiscard]] constexpr std::vector<std::string> names_of(const Pane& pane) {
    std::vector<std::string> names;
    for (const FileRow& row : pane.rows()) names.push_back(row.name.text);
    return names;
}

[[nodiscard]] constexpr bool same_names(const std::vector<std::string>& names, std::initializer_list<std::string_view> wanted) {
    return std::ranges::equal(names, wanted);
}

// The middle of the name in a row of the grid, away from any mark at this depth, and the middle of its mark.
template <class Grid>
[[nodiscard]] constexpr Point row_point(const Grid& grid, std::size_t position, Width from_column_left) {
    const Top top = (grid.body_area().top() + repeated_extent(grid.row_height(), position)) - grid.scroll_down();
    return Point{left_after(grid.column_left(file_name_column) + from_column_left), top_after(top + Height{1})};
}

// With BlockShaper a character is 2 wide, so a mark or a level of indent is 8, after the cell's padding of 4.
inline constexpr Width on_a_top_mark{6};
inline constexpr Width past_a_top_mark{20};
inline constexpr Width on_a_second_mark{14};

template <class Tree>
constexpr void press(Tree& tree, Point at) {
    jgui_test::notify(tree, ClearFocus{});
    jgui_test::dispatch(tree, PointerPress{at, MouseButton::Left, jgui_test::serial(1)});
    jgui_test::dispatch(tree, PointerRelease{at, MouseButton::Left});
    static_cast<void>(lay_out(tree));
}

template <class Tree>
constexpr void key(Tree& tree, Keysym sym) {
    jgui_test::dispatch(tree, KeyEvent{sym, {}, true});
    static_cast<void>(lay_out(tree));
}

template <class P>
using GridOf = FilePaneGrid<P>;

[[nodiscard]] constexpr bool each_starts_in_its_own_folder() {
    auto tree = fake_ui(std::string{"/top"}, std::string{"/other"});
    const auto& left = tree.template get<Left_>();
    const auto& right = tree.template get<Right_>();
    return left.folder() == "/top" && right.folder() == "/other" && same_names(names_of(left), {"one", "two", "note.txt"}) && same_names(names_of(right), {"g1"}) &&
           tree.template get<FilePanePath<Left_>>().text() == "/top" && left.status() == "3 things, and 1 with a name that cannot be shown" && right.status() == "1 thing";
}
static_assert(each_starts_in_its_own_folder());

[[nodiscard]] constexpr bool a_start_that_failed_says_why_there_only() {
    auto tree = fake_ui(std::unexpected(Error{ErrorCode::System, "no such folder"}), std::string{"/other"});
    if (!lay_out(tree)) return false;
    return tree.template get<Left_>().status() == "no such folder" && tree.template get<Left_>().rows().empty() && tree.template get<Left_>().folder().empty() && same_names(names_of(tree.template get<Right_>()), {"g1"});
}
static_assert(a_start_that_failed_says_why_there_only());

// The panes get half each and share nothing, down to the pixel.
[[nodiscard]] constexpr bool the_panes_split_the_room() {
    auto tree = fake_ui();
    if (!lay_out(tree)) return false;
    const Rect room = tree.template get<files::Panes>().bounds();
    return room.height() > Height{} && tree.template get<Left_>().bounds() == left_half_of(room) && tree.template get<Right_>().bounds() == right_half_of(room) && tree.template get<GridOf<Left_>>().bounds().intersect(tree.template get<GridOf<Right_>>().bounds()).empty();
}
static_assert(the_panes_split_the_room());

// The heart of it. Every kind of action in the left pane, with the right looked at after each.
template <class Tree>
[[nodiscard]] constexpr bool right_untouched(Tree& tree) {
    const auto& right = tree.template get<Right_>();
    const auto& grid = tree.template get<GridOf<Right_>>();
    return right.folder() == "/top" && same_names(names_of(right), {"one", "two", "note.txt"}) && !grid.selected() && !grid.focused() && grid.scroll_down() == Height{} && grid.sort_keys().empty();
}

[[nodiscard]] constexpr bool the_left_pane_leaves_the_right_alone() {
    auto tree = fake_ui();
    if (!lay_out(tree)) return false;
    auto& grid = tree.template get<GridOf<Left_>>();
    // Selects a row.
    press(tree, row_point(grid, 0, past_a_top_mark));
    if (!grid.selected() || !grid.focused() || !right_untouched(tree)) return false;
    // Opens it, from the keys, then closes it.
    key(tree, Key::Plus);
    if (!same_names(names_of(tree.template get<Left_>()), {"one", "inner", "f1", "two", "note.txt"}) || !right_untouched(tree)) return false;
    key(tree, Key::Minus);
    if (!same_names(names_of(tree.template get<Left_>()), {"one", "two", "note.txt"}) || !right_untouched(tree)) return false;
    // Sorts it.
    if (!grid.set_sort_keys(std::array{GridSortKey{0, true}}) || !right_untouched(tree)) return false;
    // Goes somewhere else, by each button.
    const auto click = [&](const auto& button) { press(tree, Point{left_after(button.bounds().left() + Width{1}), top_after(button.bounds().top() + Height{1})}); };
    click(tree.template get<FilePaneUpButton<Left_>>());
    if (tree.template get<Left_>().folder() != "/" || !right_untouched(tree)) return false;
    click(tree.template get<FilePaneReloadButton<Left_>>());
    if (tree.template get<Left_>().folder() != "/" || !right_untouched(tree)) return false;
    press(tree, row_point(grid, 1, past_a_top_mark));
    click(tree.template get<FilePaneOpenButton<Left_>>());
    return tree.template get<Left_>().folder() == "/other" && right_untouched(tree);
}
static_assert(the_left_pane_leaves_the_right_alone());

// And the other way about, with both moving: a key goes only to the pane that has the focus.
[[nodiscard]] constexpr bool a_key_goes_only_to_the_focused_pane() {
    auto tree = fake_ui();
    if (!lay_out(tree)) return false;
    auto& left_grid = tree.template get<GridOf<Left_>>();
    auto& right_grid = tree.template get<GridOf<Right_>>();
    press(tree, row_point(left_grid, 0, past_a_top_mark));
    press(tree, row_point(right_grid, 1, past_a_top_mark));
    if (left_grid.focused() || !right_grid.focused()) return false;
    if (left_grid.selected() != GridRow{0} || right_grid.selected() != GridRow{1}) return false;
    key(tree, Key::Return);
    if (!same_names(names_of(tree.template get<Right_>()), {"one", "two", "f3", "note.txt"}) || !same_names(names_of(tree.template get<Left_>()), {"one", "two", "note.txt"})) return false;
    key(tree, Key::BackSpace);
    return tree.template get<Right_>().folder() == "/" && tree.template get<Left_>().folder() == "/top";
}
static_assert(a_key_goes_only_to_the_focused_pane());

[[nodiscard]] constexpr bool a_mark_opens_and_a_name_only_selects() {
    auto tree = fake_ui();
    if (!lay_out(tree)) return false;
    auto& grid = tree.template get<GridOf<Left_>>();
    const auto& pane = tree.template get<Left_>();
    press(tree, row_point(grid, 0, past_a_top_mark));
    if (pane.rows().size() != 3 || grid.selected() != GridRow{0}) return false;
    press(tree, row_point(grid, 0, on_a_top_mark));
    if (!same_names(names_of(pane), {"one", "inner", "f1", "two", "note.txt"}) || grid.selected() != GridRow{0}) return false;
    if (pane.rows()[0].name.mark != TreeMark::Open || pane.rows()[1].name.depth != 1 || pane.status() != "2 things") return false;
    // The mark one level in is further across, and the first level's place on that row is not a mark.
    press(tree, row_point(grid, 1, on_a_top_mark));
    if (pane.rows().size() != 5 || grid.selected() != GridRow{1}) return false;
    press(tree, row_point(grid, 1, on_a_second_mark));
    if (!same_names(names_of(pane), {"one", "inner", "f2", "f1", "two", "note.txt"}) || pane.rows()[2].name.depth != 2) return false;
    // A file's row has no mark.
    press(tree, row_point(grid, 5, on_a_top_mark));
    if (pane.rows().size() != 6 || grid.selected() != GridRow{5}) return false;
    // Closing the top one takes everything under it, however deep.
    press(tree, row_point(grid, 0, on_a_top_mark));
    return same_names(names_of(pane), {"one", "two", "note.txt"}) && pane.rows()[0].name.mark == TreeMark::Closed && grid.selected() == GridRow{0};
}
static_assert(a_mark_opens_and_a_name_only_selects());

// A mark is where it is drawn, also scrolled sideways, and not at all when the name is not at the left of its column.
[[nodiscard]] constexpr bool a_mark_follows_the_column() {
    auto tree = fake_ui();
    const Rect narrow{Point{}, Size{Width{300}, Height{400}}};
    if (!lay_out(tree, narrow)) return false;
    auto& grid = tree.template get<GridOf<Left_>>();
    const auto& pane = tree.template get<Left_>();
    jgui_test::dispatch(tree, PointerScroll{Point{left_after(grid.bounds().left() + Width{10}), top_after(grid.body_area().top() + Height{2})}, ScrollDown{}, ScrollAcross::of_steps(1)});
    static_cast<void>(lay_out(tree, narrow));
    if (grid.scroll_across() == Width{}) return false;
    if (grid.column_left(file_name_column) >= grid.body_area().left()) return false;
    // The mark itself has gone off the left, so this press there is on the name.
    jgui_test::dispatch(tree, PointerPress{row_point(grid, 0, Width{60}), MouseButton::Left, jgui_test::serial(1)});
    jgui_test::dispatch(tree, PointerRelease{row_point(grid, 0, Width{60}), MouseButton::Left});
    if (pane.rows().size() != 3) return false;
    auto centred = fake_ui();
    if (!lay_out(centred)) return false;
    auto& other = centred.template get<GridOf<Left_>>();
    if (!other.set_column_align(file_name_column, ColumnAlign::AtRight)) return false;
    press(centred, row_point(other, 0, on_a_top_mark));
    return centred.template get<Left_>().rows().size() == 3;
}
static_assert(a_mark_follows_the_column());

// Keys: + opens only, - closes only, Return does either, and they ask for a selected row. Ctrl is the grid's.
[[nodiscard]] constexpr bool the_keys_do_what_they_say() {
    auto tree = fake_ui();
    if (!lay_out(tree)) return false;
    auto& grid = tree.template get<GridOf<Left_>>();
    const auto& pane = tree.template get<Left_>();
    press(tree, grid.bounds().origin());  // focus, with nothing selected
    key(tree, Key::Plus);
    key(tree, Key::Return);
    if (pane.rows().size() != 3) return false;
    press(tree, row_point(grid, 0, past_a_top_mark));
    key(tree, Key::Minus);
    if (pane.rows().size() != 3) return false;
    key(tree, Key::KeypadPlus);
    if (pane.rows().size() != 5) return false;
    key(tree, Key::Plus);
    if (pane.rows().size() != 5) return false;
    key(tree, Key::KeypadMinus);
    if (pane.rows().size() != 3) return false;
    key(tree, Key::Return);
    if (pane.rows().size() != 5) return false;
    key(tree, Key::Return);
    if (pane.rows().size() != 3) return false;
    jgui_test::dispatch(tree, KeyEvent{Key::Plus, Modifier::Ctrl, true});
    if (pane.rows().size() != 3) return false;
    jgui_test::dispatch(tree, KeyEvent{Key::Plus, {}, false});
    if (pane.rows().size() != 3) return false;
    // On a file, nothing, and the arrows still move the selection.
    key(tree, Key::Down);
    key(tree, Key::Down);
    if (grid.selected() != GridRow{2}) return false;
    key(tree, Key::Return);
    key(tree, Key::Plus);
    return pane.rows().size() == 3;
}
static_assert(the_keys_do_what_they_say());

// With no focus, keys do nothing here. At the top, Up stays there.
[[nodiscard]] constexpr bool up_and_no_focus() {
    auto tree = fake_ui();
    if (!lay_out(tree)) return false;
    key(tree, Key::BackSpace);
    if (tree.template get<Left_>().folder() != "/top" || tree.template get<Right_>().folder() != "/top") return false;
    auto& pane = tree.template get<Left_>();
    pane.go_up();
    if (pane.folder() != "/") return false;
    pane.go_up();
    return pane.folder() == "/" && same_names(names_of(pane), {"locked", "other", "top"});
}
static_assert(up_and_no_focus());

// A folder that cannot be read costs nothing but a line.
[[nodiscard]] constexpr bool a_refusal_changes_only_the_status() {
    auto tree = fake_ui(std::string{"/"}, std::string{"/"});
    if (!lay_out(tree)) return false;
    auto& grid = tree.template get<GridOf<Left_>>();
    auto& pane = tree.template get<Left_>();
    press(tree, row_point(grid, 0, on_a_top_mark));
    if (pane.status() != "cannot open /locked" || pane.rows().size() != 3 || pane.rows()[0].name.mark != TreeMark::Closed) return false;
    pane.open_selected();
    if (pane.folder() != "/" || pane.status() != "cannot open /locked") return false;
    pane.open("/gone");
    if (pane.folder() != "/" || pane.status() != "cannot open /gone" || pane.rows().size() != 3) return false;
    pane.open("relative");
    if (pane.folder() != "/" || pane.status() != "That path cannot be used") return false;
    pane.open("/a/../b");
    if (pane.folder() != "/") return false;
    // The right one has heard nothing of this.
    return tree.template get<Right_>().status() == "3 things";
}
static_assert(a_refusal_changes_only_the_status());

[[nodiscard]] constexpr bool show_this_folder_wants_a_folder() {
    auto tree = fake_ui();
    if (!lay_out(tree)) return false;
    auto& pane = tree.template get<Left_>();
    pane.open_selected();
    if (pane.status() != "Select a folder first" || pane.folder() != "/top") return false;
    tree.template get<GridOf<Left_>>().select_row(GridRow{2});
    pane.open_selected();
    if (pane.status() != "Select a folder first" || pane.folder() != "/top") return false;
    // A folder inside an open one has its whole path.
    pane.toggle(0);
    tree.template get<GridOf<Left_>>().select_row(GridRow{1});
    pane.open_selected();
    if (pane.folder() != "/top/one/inner" || !same_names(names_of(pane), {"f2"})) return false;
    // A new folder starts clean, with nothing selected.
    return !tree.template get<GridOf<Left_>>().selected();
}
static_assert(show_this_folder_wants_a_folder());

// Opening and closing keep the sort and the selection. A row's path comes from the tree, whatever order rows show in.
[[nodiscard]] constexpr bool opening_keeps_the_sort() {
    auto tree = fake_ui();
    if (!lay_out(tree)) return false;
    auto& grid = tree.template get<GridOf<Left_>>();
    auto& pane = tree.template get<Left_>();
    if (!grid.set_sort_keys(std::array{GridSortKey{0, true}})) return false;
    grid.select_row(GridRow{1});  // two
    pane.toggle(1);
    if (grid.sort_keys().size() != 1 || !grid.sort_keys()[0].descending || grid.selected() != GridRow{1}) return false;
    if (!same_names(names_of(pane), {"one", "two", "f3", "note.txt"})) return false;
    // In name order going down: two, one, note.txt, f3.
    const auto order = grid.display_order();
    if (order[0] != 1 || order[1] != 0 || order[2] != 3 || order[3] != 2) return false;
    // Inside a sort, the row at the top of the screen is two, and one's children still land under it in the tree.
    pane.toggle(0);
    if (!same_names(names_of(pane), {"one", "inner", "f1", "two", "f3", "note.txt"})) return false;
    if (grid.selected() != GridRow{3}) return false;
    pane.toggle(1);  // inner, whose path needs the tree's order
    if (!same_names(names_of(pane), {"one", "inner", "f2", "f1", "two", "f3", "note.txt"}) || grid.selected() != GridRow{4}) return false;
    // Closing moves it back, and a selected row closed away selects the folder that went.
    pane.toggle(0);
    if (grid.selected() != GridRow{1}) return false;
    pane.toggle(0);
    grid.select_row(GridRow{2});
    pane.toggle(0);
    return grid.selected() == GridRow{0};
}
static_assert(opening_keeps_the_sort());

// The new select_row and row_at on a grid of their own.
[[nodiscard]] constexpr bool the_grid_selects_and_answers() {
    auto tree = fake_ui();
    if (!lay_out(tree)) return false;
    auto& grid = tree.template get<GridOf<Left_>>();
    if (grid.row_at(row_point(grid, 1, past_a_top_mark)) != GridRow{1}) return false;
    if (grid.row_at(row_point(grid, 3, past_a_top_mark)) || grid.row_at(grid.header_area().origin()) || grid.row_at(Point{})) return false;
    jgui_test::clear_damage(tree);
    grid.select_row(GridRow{2});
    if (grid.selected() != GridRow{2} || !jgui_test::damaged(tree)) return false;
    jgui_test::clear_damage(tree);
    grid.select_row(GridRow{2});
    if (jgui_test::damaged(tree)) return false;
    grid.select_row(GridRow{99});
    if (grid.selected() || !jgui_test::damaged(tree)) return false;
    grid.select_row(std::nullopt);
    return !grid.selected();
}
static_assert(the_grid_selects_and_answers());

// What pulls it up: panes in a OneShown, kept while another child shows, and costing nothing then.
struct MainTag;
struct PulledUp;
struct PulledLeft;
struct PulledRight;
struct PulledPanes;
struct PullButton;
struct PulledTransfers;

struct ShowFiles {
    template <class U>
    constexpr void operator()(U& u) const {
        auto& shown = u.template get<PulledUp>();
        shown.show(shown.shown() ? std::nullopt : std::optional<std::size_t>{0});
    }
};

[[nodiscard]] constexpr bool the_panes_can_be_pulled_up() {
    auto tree = window(window_frame(menu_bar(button<PullButton>("Files", ShowFiles{})), file_transfers<PulledTransfers>(FakeFiles{}), one_shown<PulledUp>(two_panes<PulledPanes>(file_pane<PulledLeft, PulledTransfers>(FakeFiles{}), file_pane<PulledRight, PulledTransfers>(FakeFiles{})))));
    tree.template get<PulledLeft>().open("/top");
    tree.template get<PulledRight>().open("/other");
    if (!lay_out(tree)) return false;
    if (tree.template get<PulledUp>().shown() || tree.template get<PulledLeft>().bounds() != Rect{}) return false;
    const auto click_button = [&] {
        const Rect area = tree.template get<PullButton>().bounds();
        const Point at{left_after(area.left() + Width{1}), top_after(area.top() + Height{1})};
        jgui_test::dispatch(tree, PointerPress{at, MouseButton::Left, jgui_test::serial(1)});
        jgui_test::dispatch(tree, PointerRelease{at, MouseButton::Left});
        static_cast<void>(lay_out(tree));
    };
    click_button();
    if (tree.template get<PulledUp>().shown() != std::size_t{0} || tree.template get<PulledLeft>().bounds().empty()) return false;
    tree.template get<PulledLeft>().toggle(0);
    click_button();
    if (tree.template get<PulledUp>().shown()) return false;
    click_button();
    return tree.template get<PulledLeft>().rows().size() == 5 && tree.template get<PulledRight>().folder() == "/other";
}
static_assert(the_panes_can_be_pulled_up());

// --- Copy and paste, and drags, on the made-up disk ---

template <class Tree>
constexpr void turn(Tree& tree, MonotonicTime now = MonotonicTime{}) {
    jgui_test::notify(tree, TimersDue{now});
    static_cast<void>(lay_out(tree));
}

template <class Tree, class Tag>
constexpr void click_on(Tree& tree) {
    const Rect area = tree.template get<Tag>().bounds();
    press(tree, Point{left_after(area.left() + Width{1}), top_after(area.top() + Height{1})});
}

template <class Tree>
[[nodiscard]] constexpr auto& transfers_of(Tree& tree) { return tree.template get<files::Transfers>(); }

[[nodiscard]] constexpr bool copy_on_one_side_paste_on_the_other() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::string{"/top"}, std::string{"/other"});
    if (!lay_out(tree)) return false;
    auto& left_grid = tree.template get<GridOf<Left_>>();
    press(tree, row_point(left_grid, 2, past_a_top_mark));  // note.txt
    click_on<decltype(tree), FilePaneCopyButton<Left_>>(tree);
    if (!std::ranges::equal(transfers_of(tree).copied(), std::vector<std::string>{"/top/note.txt"}) || transfers_of(tree).line() != "Ready to paste /top/note.txt") return false;
    // Nothing is on its way until the paste, and with nothing selected on the right it goes to the folder at the top there.
    turn(tree);
    if (disk.steps_taken != 0) return false;
    click_on<decltype(tree), FilePanePasteButton<Right_>>(tree);
    if (!transfers_of(tree).copying()) return false;
    // One step a turn, each asking for the next turn at once, and the panes do not look again until the end.
    turn(tree);
    if (disk.steps_taken != 1 || tree.template get<Right_>().rows().size() != 1) return false;
    turn(tree);
    if (disk.steps_taken != 2 || !transfers_of(tree).copying()) return false;
    turn(tree);
    if (transfers_of(tree).copying() || disk.arrived != std::vector<std::string>{"/other/note.txt"}) return false;
    if (transfers_of(tree).line() != "Copied note.txt to /other, and every file matched by SHA-256: 1 file, 1,000 bytes") return false;
    // The right sees it, in the turn after, and the left keeps what it had.
    turn(tree);
    if (!same_names(names_of(tree.template get<Right_>()), {"g1", "note.txt"})) return false;
    return same_names(names_of(tree.template get<Left_>()), {"one", "two", "note.txt"}) && left_grid.selected() == GridRow{2};
}
static_assert(copy_on_one_side_paste_on_the_other());

[[nodiscard]] constexpr bool the_keys_copy_and_paste() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::string{"/top"}, std::string{"/top"});
    if (!lay_out(tree)) return false;
    press(tree, row_point(tree.template get<GridOf<Left_>>(), 2, past_a_top_mark));
    jgui_test::dispatch(tree, KeyEvent{Key::LowerC, Modifier::Ctrl, true});
    if (!std::ranges::equal(transfers_of(tree).copied(), std::vector<std::string>{"/top/note.txt"})) return false;
    // A folder selected takes it inside. Caps Lock makes no difference.
    press(tree, row_point(tree.template get<GridOf<Right_>>(), 1, past_a_top_mark));
    jgui_test::dispatch(tree, KeyEvent{Key::UpperV, Modifier::Ctrl, true});
    if (!transfers_of(tree).copying()) return false;
    for (int i = 0; i < 3; ++i) turn(tree);
    if (disk.arrived != std::vector<std::string>{"/top/two/note.txt"}) return false;
    // A file selected takes it to the folder that file is in, and with no Ctrl, a letter is nothing.
    disk.arrived.clear();
    press(tree, row_point(tree.template get<GridOf<Right_>>(), 0, past_a_top_mark));
    jgui_test::dispatch(tree, KeyEvent{Key::LowerV, {}, true});
    return !transfers_of(tree).copying();
}
static_assert(the_keys_copy_and_paste());

[[nodiscard]] constexpr bool what_a_copy_refuses() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& transfers = transfers_of(tree);
    click_on<decltype(tree), FilePanePasteButton<Left_>>(tree);
    if (transfers.line() != "Nothing has been copied yet") return false;
    click_on<decltype(tree), FilePaneCopyButton<Left_>>(tree);
    if (tree.template get<Left_>().status() != "Select something to copy first" || !transfers.copied().empty()) return false;
    // A folder into itself or anything inside it never reaches the disk.
    transfers.copy({std::string{"/top/one"}});
    transfers.paste_into("/top/one");
    if (transfers.copying() || transfers.line() != "A folder cannot go inside itself") return false;
    transfers.paste_into("/top/one/inner");
    if (transfers.copying() || transfers.line() != "A folder cannot go inside itself") return false;
    // A name that is taken is the source's to refuse, and nothing starts.
    disk.start_fails = true;
    transfers.paste_into("/other");
    if (transfers.copying() || transfers.line() != "that name is already there") return false;
    disk.start_fails = false;
    // One copy at a time.
    transfers.paste_into("/other");
    if (!transfers.copying()) return false;
    transfers.paste_into("/top/two");
    return transfers.line() == "A copy is still going. Escape stops it.";
}
static_assert(what_a_copy_refuses());

[[nodiscard]] constexpr bool a_copy_can_be_stopped_or_fail() {
    FakeDisk disk;
    disk.steps_to_copy = 100;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& transfers = transfers_of(tree);
    transfers.copy({std::string{"/top/note.txt"}});
    transfers.paste_into("/other");
    turn(tree);
    jgui_test::dispatch(tree, KeyEvent{Key::Escape, {}, true});
    if (transfers.copying() || !transfers.line().starts_with("Stopped: what was copied so far is in /other")) return false;
    const int steps = disk.steps_taken;
    turn(tree);
    if (disk.steps_taken != steps || !disk.arrived.empty()) return false;
    // What it says when the disk says no, and it does not carry on.
    transfers.paste_into("/other");
    disk.step_fails = true;
    turn(tree);
    if (transfers.copying() || !transfers.line().starts_with("Stopped: the disk is full.")) return false;
    // With neither going, Escape is nobody's.
    return !jgui_test::dispatch(tree, KeyEvent{Key::Escape, {}, true});
}
static_assert(a_copy_can_be_stopped_or_fail());

template <class Tree>
constexpr void drag(Tree& tree, Point from, Point to) {
    jgui_test::notify(tree, ClearFocus{});
    jgui_test::dispatch(tree, PointerPress{from, MouseButton::Left, jgui_test::serial(1)});
    jgui_test::dispatch(tree, PointerMove{to, jgui_test::capture_active(tree)});
    jgui_test::dispatch(tree, PointerRelease{to, MouseButton::Left});
    static_cast<void>(lay_out(tree));
}

[[nodiscard]] constexpr bool a_drag_copies_across() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::string{"/top"}, std::string{"/other"});
    if (!lay_out(tree)) return false;
    auto& left = tree.template get<GridOf<Left_>>();
    auto& right = tree.template get<GridOf<Right_>>();
    auto& transfers = transfers_of(tree);
    // Past the last row on the right is the folder at the top there.
    const Point below{left_after(right.body_area().left() + Width{10}), right.body_area().bottom() - Height{5}};
    drag(tree, row_point(left, 2, past_a_top_mark), below);
    if (!transfers.copying() || transfers.dragging() || jgui_test::capture_active(tree) || !disk.arrived.empty()) return false;
    // It goes a step a turn, as a paste does, and what was last copied for a paste is not touched.
    if (!transfers.copied().empty()) return false;
    for (int i = 0; i < 3; ++i) turn(tree);
    if (disk.arrived != std::vector<std::string>{"/other/note.txt"} || !transfers.line().starts_with("Copied note.txt to /other, and every file matched by SHA-256: ")) return false;
    // The original stays, and the copy shows on the other side.
    return same_names(names_of(tree.template get<Left_>()), {"one", "two", "note.txt"}) && same_names(names_of(tree.template get<Right_>()), {"g1", "note.txt"});
}
static_assert(a_drag_copies_across());

// Escape stops a copy that a drop started, as it stops one a paste started.
[[nodiscard]] constexpr bool a_copy_from_a_drag_can_be_stopped() {
    FakeDisk disk;
    disk.steps_to_copy = 100;
    auto tree = ui_on(disk, std::string{"/top"}, std::string{"/other"});
    if (!lay_out(tree)) return false;
    auto& transfers = transfers_of(tree);
    drag(tree, row_point(tree.template get<GridOf<Left_>>(), 2, past_a_top_mark), row_point(tree.template get<GridOf<Right_>>(), 0, past_a_top_mark));
    if (!transfers.copying()) return false;
    turn(tree);
    jgui_test::dispatch(tree, KeyEvent{Key::Escape, {}, true});
    return !transfers.copying() && transfers.line().starts_with("Stopped: what was copied so far is in /other") && disk.arrived.empty();
}
static_assert(a_copy_from_a_drag_can_be_stopped());

[[nodiscard]] constexpr bool a_drag_lands_on_what_is_under_it() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& left = tree.template get<GridOf<Left_>>();
    auto& right = tree.template get<GridOf<Right_>>();
    // Onto a file: the folder that file is in, which here is where it came from, so nothing is copied.
    drag(tree, row_point(left, 0, past_a_top_mark), row_point(right, 2, past_a_top_mark));
    if (transfers_of(tree).copying() || transfers_of(tree).line() != "Nothing was copied: it is already there") return false;
    // Onto a folder on the other side, right to left, which is the other order for the panes to be told.
    drag(tree, row_point(right, 2, past_a_top_mark), row_point(left, 1, past_a_top_mark));
    for (int i = 0; i < 3; ++i) turn(tree);
    return disk.arrived == std::vector<std::string>{"/top/two/note.txt"};
}
static_assert(a_drag_lands_on_what_is_under_it());

[[nodiscard]] constexpr bool what_a_drag_leaves_alone() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& left = tree.template get<GridOf<Left_>>();
    auto& transfers = transfers_of(tree);
    const Point start = row_point(left, 2, past_a_top_mark);
    // A click, and a click with a shaky hand, copy nothing.
    press(tree, start);
    drag(tree, start, Point{left_after(start.left() + Width{3}), top_after(start.top() + Height{3})});
    if (transfers.copying() || transfers.dragging() || left.selected() != GridRow{2}) return false;
    // Into the folder it is already in.
    drag(tree, start, row_point(tree.template get<GridOf<Right_>>(), 2, past_a_top_mark));
    if (transfers.copying() || transfers.line() != "Nothing was copied: it is already there") return false;
    // Let go outside any list.
    drag(tree, start, tree.template get<FilePaneUpButton<Left_>>().bounds().origin());
    if (transfers.copying() || transfers.line() != "Nothing was copied: let go over a list to copy something") return false;
    // A folder onto itself.
    drag(tree, row_point(left, 0, past_a_top_mark), row_point(left, 0, past_a_top_mark + Width{20}));
    if (transfers.copying()) return false;
    drag(tree, row_point(left, 0, past_a_top_mark), row_point(tree.template get<GridOf<Right_>>(), 0, past_a_top_mark));
    if (transfers.copying() || transfers.line() != "A folder cannot go inside itself") return false;
    // Escape during it, which holds until the pointer is let go, and does not start again.
    jgui_test::notify(tree, ClearFocus{});
    jgui_test::dispatch(tree, PointerPress{start, MouseButton::Left, jgui_test::serial(1)});
    const Point over = row_point(tree.template get<GridOf<Right_>>(), 1, past_a_top_mark);
    jgui_test::dispatch(tree, PointerMove{over, true});
    if (!transfers.dragging() || transfers.line() != "To copy note.txt to /top/two, let go there, or press Escape to stop") return false;
    jgui_test::dispatch(tree, KeyEvent{Key::Escape, {}, true});
    jgui_test::dispatch(tree, PointerMove{Point{left_after(over.left() + Width{1}), over.top()}, true});
    if (transfers.dragging()) return false;
    jgui_test::dispatch(tree, PointerRelease{over, MouseButton::Left});
    if (transfers.copying() || jgui_test::capture_active(tree)) return false;
    // The pointer gone during one.
    jgui_test::notify(tree, ClearFocus{});
    jgui_test::dispatch(tree, PointerPress{start, MouseButton::Left, jgui_test::serial(1)});
    jgui_test::dispatch(tree, PointerMove{over, true});
    jgui_test::notify(tree, PointerLeave{});
    if (transfers.dragging() || jgui_test::capture_active(tree)) return false;
    jgui_test::dispatch(tree, PointerRelease{over, MouseButton::Left});
    if (transfers.copying()) return false;
    // The disk saying no.
    disk.start_fails = true;
    drag(tree, start, over);
    if (transfers.copying() || transfers.line() != "that name is already there") return false;
    disk.start_fails = false;
    // One copy at a time, by a drop as by a paste.
    drag(tree, start, over);
    if (!transfers.copying()) return false;
    drag(tree, start, over);
    return transfers.line() == "A copy is still going. Escape stops it." && disk.arrived.empty();
}
static_assert(what_a_drag_leaves_alone());

template <class Tree, class Grid>
constexpr void press_with(Tree& tree, Grid& grid, std::size_t row, ModifierMask held) {
    jgui_test::dispatch(tree, PointerPress{row_point(grid, row, past_a_top_mark), MouseButton::Left, jgui_test::serial(1), held});
    jgui_test::dispatch(tree, PointerRelease{row_point(grid, row, past_a_top_mark), MouseButton::Left});
    static_cast<void>(lay_out(tree));
}

[[nodiscard]] constexpr bool chosen_are(auto& grid, std::initializer_list<std::uint32_t> rows) { return std::ranges::equal(grid.selected_rows(), rows); }

// Ctrl and Shift on the rows, with the keys, and that the other side knows nothing of it.
[[nodiscard]] constexpr bool several_can_be_selected() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& left = tree.template get<GridOf<Left_>>();
    auto& right = tree.template get<GridOf<Right_>>();
    press_with(tree, left, 0, {});
    if (!chosen_are(left, {0})) return false;
    // Ctrl adds, and takes away, and the current row follows.
    press_with(tree, left, 2, Modifier::Ctrl);
    if (!chosen_are(left, {0, 2}) || left.selected() != GridRow{2}) return false;
    press_with(tree, left, 2, Modifier::Ctrl);
    if (!chosen_are(left, {0}) || left.selected() != GridRow{0}) return false;
    press_with(tree, left, 0, Modifier::Ctrl);
    if (!chosen_are(left, {}) || left.selected()) return false;
    // Shift takes everything from the last one picked, both ways, replacing what was there.
    press_with(tree, left, 2, {});
    press_with(tree, left, 0, Modifier::Shift);
    if (!chosen_are(left, {0, 1, 2}) || left.selected() != GridRow{0}) return false;
    press_with(tree, left, 1, Modifier::Shift);
    if (!chosen_are(left, {1, 2})) return false;
    // Shift with no row picked yet is a plain press.
    if (!chosen_are(right, {})) return false;
    press_with(tree, right, 1, Modifier::Shift);
    if (!chosen_are(right, {1}) || !chosen_are(left, {1, 2})) return false;
    // A plain click on one of several comes down to that one when it is let go, and not before.
    jgui_test::dispatch(tree, PointerPress{row_point(left, 2, past_a_top_mark), MouseButton::Left, jgui_test::serial(1)});
    if (!chosen_are(left, {1, 2})) return false;
    jgui_test::dispatch(tree, PointerRelease{row_point(left, 2, past_a_top_mark), MouseButton::Left});
    if (!chosen_are(left, {2}) || jgui_test::capture_active(tree)) return false;
    // The keys: Shift and an arrow reaches, and an arrow alone comes back to one. Ctrl+A is everything on the side with the focus.
    jgui_test::dispatch(tree, KeyEvent{Key::Up, Modifier::Shift, true});
    if (!chosen_are(left, {1, 2}) || left.selected() != GridRow{1}) return false;
    jgui_test::dispatch(tree, KeyEvent{Key::Up, {}, true});
    if (!chosen_are(left, {0})) return false;
    jgui_test::dispatch(tree, KeyEvent{Key::LowerA, Modifier::Ctrl, true});
    if (!chosen_are(left, {0, 1, 2}) || left.selected() != GridRow{0} || !chosen_are(right, {1})) return false;
    // Sorted another way, it stays on the same rows, and Shift goes by the order they show.
    press_with(tree, left, 0, {});
    if (!left.set_sort_keys(std::array{GridSortKey{file_name_column, true}}).has_value()) return false;
    static_cast<void>(lay_out(tree));
    if (!chosen_are(left, {0})) return false;
    const std::span<const std::uint32_t> order = left.display_order();
    if (order.front() == 0) return false;  // or this shows nothing
    const std::size_t picked = static_cast<std::size_t>(std::ranges::find(order, std::uint32_t{0}) - order.begin());
    std::vector<std::uint32_t> expected(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(picked) + 1);
    std::ranges::sort(expected);
    jgui_test::dispatch(tree, PointerPress{row_point(left, 0, past_a_top_mark), MouseButton::Left, jgui_test::serial(1), Modifier::Shift});
    jgui_test::dispatch(tree, PointerRelease{row_point(left, 0, past_a_top_mark), MouseButton::Left});
    return std::ranges::equal(left.selected_rows(), expected);
}
static_assert(several_can_be_selected());

// Opening and closing a folder keeps several selected, each on its own row.
[[nodiscard]] constexpr bool opening_and_closing_keeps_several() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& pane = tree.template get<Left_>();
    auto& grid = tree.template get<GridOf<Left_>>();
    press_with(tree, grid, 1, {});  // two
    press_with(tree, grid, 2, Modifier::Ctrl);  // note.txt
    pane.toggle(0);  // one, above them both
    if (!same_names(names_of(pane), {"one", "inner", "f1", "two", "note.txt"}) || !chosen_are(grid, {3, 4}) || grid.selected() != GridRow{4}) return false;
    // A row inside what closes selects the folder, once.
    press_with(tree, grid, 1, Modifier::Ctrl);  // inner
    press_with(tree, grid, 2, Modifier::Ctrl);  // f1
    pane.toggle(0);
    if (!chosen_are(grid, {0, 1, 2}) || grid.selected() != GridRow{0}) return false;
    // Looking again keeps them all, by their paths, and drops what has gone.
    disk.gone.push_back("/top/two");
    pane.reload();
    return same_names(names_of(pane), {"one", "note.txt"}) && chosen_are(grid, {0, 1});
}
static_assert(opening_and_closing_keeps_several());

[[nodiscard]] constexpr bool several_are_copied_together() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::string{"/top"}, std::string{"/other"});
    if (!lay_out(tree)) return false;
    auto& left = tree.template get<GridOf<Left_>>();
    auto& pane = tree.template get<Left_>();
    auto& transfers = transfers_of(tree);
    // A folder that is open, with something inside it selected as well, goes once, as the folder.
    pane.toggle(1);  // two
    press_with(tree, left, 1, {});
    press_with(tree, left, 2, Modifier::Ctrl);  // f3, inside it
    press_with(tree, left, 3, Modifier::Ctrl);  // note.txt
    click_on<decltype(tree), FilePaneCopyButton<Left_>>(tree);
    if (!std::ranges::equal(transfers.copied(), std::vector<std::string>{"/top/two", "/top/note.txt"}) || transfers.line() != "Ready to paste 2 things") return false;
    click_on<decltype(tree), FilePanePasteButton<Right_>>(tree);
    if (!transfers.copying()) return false;
    // One after the other, to the end.
    for (int i = 0; i < 3; ++i) turn(tree);
    if (disk.arrived != std::vector<std::string>{"/other/two"} || !transfers.copying()) return false;
    for (int i = 0; i < 3; ++i) turn(tree);
    if (disk.arrived != std::vector<std::string>{"/other/two", "/other/note.txt"} || transfers.copying()) return false;
    if (transfers.line() != "Copied 2 things to /other, and every file matched by SHA-256: 2 files, 2,000 bytes") return false;
    return same_names(names_of(tree.template get<Right_>()), {"g1", "note.txt", "two"});
}
static_assert(several_are_copied_together());

[[nodiscard]] constexpr bool several_can_be_dragged() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::string{"/top"}, std::string{"/other"});
    if (!lay_out(tree)) return false;
    auto& left = tree.template get<GridOf<Left_>>();
    auto& right = tree.template get<GridOf<Right_>>();
    auto& transfers = transfers_of(tree);
    press_with(tree, left, 0, {});
    press_with(tree, left, 2, Modifier::Ctrl);
    // A drag from one of them takes both, and they stay selected.
    jgui_test::notify(tree, ClearFocus{});
    jgui_test::dispatch(tree, PointerPress{row_point(left, 2, past_a_top_mark), MouseButton::Left, jgui_test::serial(1)});
    jgui_test::dispatch(tree, PointerMove{row_point(right, 0, past_a_top_mark), true});
    if (transfers.line() != "To copy 2 things to /other, let go there, or press Escape to stop") return false;
    jgui_test::dispatch(tree, PointerRelease{row_point(right, 0, past_a_top_mark), MouseButton::Left});
    if (!transfers.copying() || !chosen_are(left, {0, 2}) || jgui_test::capture_active(tree)) return false;
    for (int i = 0; i < 6; ++i) turn(tree);
    if (disk.arrived != std::vector<std::string>{"/other/one", "/other/note.txt"}) return false;
    // A drag stopped by Escape leaves them selected too.
    press_with(tree, left, 0, {});
    press_with(tree, left, 2, Modifier::Ctrl);
    jgui_test::dispatch(tree, PointerPress{row_point(left, 2, past_a_top_mark), MouseButton::Left, jgui_test::serial(1)});
    jgui_test::dispatch(tree, PointerMove{row_point(right, 0, past_a_top_mark), true});
    jgui_test::dispatch(tree, KeyEvent{Key::Escape, {}, true});
    jgui_test::dispatch(tree, PointerRelease{row_point(right, 0, past_a_top_mark), MouseButton::Left});
    if (transfers.copying() || !chosen_are(left, {0, 2})) return false;
    // One of them already where they would go stops the lot, before anything is made.
    disk.arrived.clear();
    drag(tree, row_point(left, 2, past_a_top_mark), row_point(left, 2, past_a_top_mark + Width{20}));
    return !transfers.copying() && disk.arrived.empty() && transfers.line() == "Nothing was copied: one is already there";
}
static_assert(several_can_be_dragged());

// One that cannot go stops the rest, and what went stays, and the line says how far it got.
[[nodiscard]] constexpr bool when_one_of_several_cannot_go() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& transfers = transfers_of(tree);
    // The same name twice is refused before anything is made.
    transfers.copy({std::string{"/a/same"}, std::string{"/b/same"}});
    transfers.paste_into("/other");
    if (transfers.copying() || transfers.line() != "Nothing was copied: two of those have the same name, same") return false;
    // One inside itself refuses the whole of it.
    transfers.copy({std::string{"/top/note.txt"}, std::string{"/top/one"}});
    transfers.paste_into("/top/one");
    if (transfers.copying() || transfers.line() != "A folder cannot go inside itself") return false;
    // A failure part way.
    transfers.copy({std::string{"/top/note.txt"}, std::string{"/top/one"}, std::string{"/top/two"}});
    transfers.paste_into("/other");
    for (int i = 0; i < 3; ++i) turn(tree);
    if (disk.arrived != std::vector<std::string>{"/other/note.txt"} || !transfers.copying()) return false;
    disk.step_fails = true;
    turn(tree);
    if (transfers.copying() || !transfers.line().starts_with("Stopped: the disk is full. 1 of 3 done. What was copied so far is in /other. ")) return false;
    disk.step_fails = false;
    // Escape part way.
    disk.arrived.clear();
    transfers.paste_into("/other");
    for (int i = 0; i < 3; ++i) turn(tree);
    jgui_test::dispatch(tree, KeyEvent{Key::Escape, {}, true});
    if (transfers.copying() || !transfers.line().starts_with("Stopped: 1 of 3 done. What was copied so far is in /other. ")) return false;
    // The next one's name taken, found only when its turn comes.
    disk.arrived.clear();
    transfers.paste_into("/other");
    for (int i = 0; i < 2; ++i) turn(tree);
    disk.start_fails = true;
    turn(tree);
    return !transfers.copying() && transfers.line().starts_with("Stopped: that name is already there. 1 of 3 done. ") && disk.arrived == std::vector<std::string>{"/other/note.txt"};
}
static_assert(when_one_of_several_cannot_go());

[[nodiscard]] constexpr ComparedItem item_at(std::string_view relative, FileKind kind, std::uint64_t size = 0, std::int64_t seconds = 0, bool closed = false) {
    return ComparedItem{std::string{relative}, kind, FileBytes{kind == FileKind::File, size}, FileTime{true, seconds}, closed};
}

// What two things are to each other.
static_assert(match_of_two(item_at("a", FileKind::File, 5, 10), item_at("a", FileKind::File, 5, 10)) == FileMatch::SameSizeAndTime);
static_assert(match_of_two(item_at("a", FileKind::File, 5, 11), item_at("a", FileKind::File, 5, 10)) == FileMatch::NewerHere);
static_assert(match_of_two(item_at("a", FileKind::File, 9, 10), item_at("a", FileKind::File, 5, 11)) == FileMatch::OlderHere);
static_assert(match_of_two(item_at("a", FileKind::File, 9, 10), item_at("a", FileKind::File, 5, 10)) == FileMatch::SizeDiffers);
static_assert(match_of_two(item_at("a", FileKind::File, 5, 10), item_at("a", FileKind::Folder)) == FileMatch::KindDiffers);
static_assert(match_of_two(item_at("a", FileKind::Folder), item_at("a", FileKind::Folder, 0, 99)) == FileMatch::InBoth);
static_assert(match_of_two(item_at("a", FileKind::Link), item_at("a", FileKind::Link)) == FileMatch::InBoth);
// A time or a size not known says nothing.
static_assert(match_of_two(ComparedItem{"a", FileKind::File, FileBytes{false, 0}, FileTime{true, 1}, false}, item_at("a", FileKind::File, 5, 1)) == FileMatch::InBoth);
static_assert(match_of_two(ComparedItem{"a", FileKind::File, FileBytes{true, 5}, FileTime{false, 0}, false}, item_at("a", FileKind::File, 5, 1)) == FileMatch::InBoth);

// Something missing is only here, unless nobody has looked where it would be.
[[nodiscard]] constexpr bool what_is_only_here() {
    const std::array there{item_at("closed", FileKind::Folder, 0, 0, true), item_at("file", FileKind::File, 1, 1), item_at("open", FileKind::Folder), item_at("open/inner", FileKind::File, 1, 1)};
    const auto match = [&](std::string_view relative) { return match_against(item_at(relative, FileKind::File, 1, 1), there); };
    return match("not_there") == FileMatch::OnlyHere && match("file") == FileMatch::SameSizeAndTime && match("open/inner") == FileMatch::SameSizeAndTime
        && match("open/not_there") == FileMatch::OnlyHere && match("closed/x") == FileMatch::NotCompared && match("closed/x/y/z") == FileMatch::NotCompared
        && match("file/x") == FileMatch::OnlyHere && match("gone/x/y") == FileMatch::OnlyHere && match("clos") == FileMatch::OnlyHere;
}
static_assert(what_is_only_here());

[[nodiscard]] constexpr std::vector<FileMatch> matches_of(std::span<const FileRow> rows) {
    std::vector<FileMatch> out;
    for (const FileRow& row : rows) out.push_back(row.match);
    return out;
}

// From the button, each side marked from its own view, in tree order and sorted however it is, and nothing asked of the disk.
[[nodiscard]] constexpr bool the_panes_are_compared() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::string{"/top"}, std::string{"/other"});
    if (!lay_out(tree)) return false;
    auto& left = tree.template get<Left_>();
    auto& right = tree.template get<Right_>();
    const auto matches = [](const auto& pane) { return matches_of(pane.rows()); };
    // Nothing is marked before it is asked for.
    if (matches(left) != std::vector<FileMatch>(3, FileMatch::NotCompared)) return false;
    click_on<decltype(tree), files::CompareButton>(tree);
    if (matches(left) != std::vector<FileMatch>(3, FileMatch::OnlyHere) || matches(right) != std::vector<FileMatch>{FileMatch::OnlyHere}) return false;
    if (transfers_of(tree).line() != "Compared what shows: 3 only on the left, 1 only on the right, 0 different, 0 same size and time. Contents were not read. To match by contents, select two or more first.") return false;
    if (!disk.arrived.empty() || disk.steps_taken != 0) return false;
    // The same folder on both sides, one with a folder open: its rows are not compared, and the selection stays.
    right.open("/top");
    left.toggle(1);  // two
    tree.template get<GridOf<Left_>>().select_row(GridRow{3});  // note.txt
    click_on<decltype(tree), files::CompareButton>(tree);
    if (matches(left) != std::vector<FileMatch>{FileMatch::InBoth, FileMatch::InBoth, FileMatch::NotCompared, FileMatch::SameSizeAndTime}) return false;
    if (matches(right) != std::vector<FileMatch>{FileMatch::InBoth, FileMatch::InBoth, FileMatch::SameSizeAndTime}) return false;
    if (tree.template get<GridOf<Left_>>().selected() != GridRow{3} || left.rows()[1].name.mark != TreeMark::Open) return false;
    // Reading again takes the marks away.
    left.reload();
    return matches(left) == std::vector<FileMatch>(4, FileMatch::NotCompared) && matches(right)[2] == FileMatch::SameSizeAndTime;
}
static_assert(the_panes_are_compared());

// With no folder on a side there is nothing to compare with.
[[nodiscard]] constexpr bool comparing_needs_both_sides() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::string{"/top"}, files::StartFolder{std::unexpected(Error{ErrorCode::System, "no"})});
    if (!lay_out(tree)) return false;
    click_on<decltype(tree), files::CompareButton>(tree);
    return transfers_of(tree).line() == "Comparing needs a folder on each side" && tree.template get<Left_>().rows()[0].match == FileMatch::NotCompared;
}
static_assert(comparing_needs_both_sides());

// Comparing "/" as a top works from the same place as any other folder.
[[nodiscard]] constexpr bool comparing_at_the_root() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::string{"/"}, std::string{"/"});
    if (!lay_out(tree)) return false;
    click_on<decltype(tree), files::CompareButton>(tree);
    return std::ranges::all_of(tree.template get<Left_>().rows(), [](const FileRow& row) { return row.match == FileMatch::InBoth; });
}
static_assert(comparing_at_the_root());

// --- Matching what is selected ---
[[nodiscard]] constexpr bool selections_become_pairs() {
    using Paths = std::vector<std::string>;
    // One on each side: those two, whatever they are called.
    MatchPlan plan = plan_matches(Paths{"/a/x"}, Paths{"/b/y"});
    if (plan.pairs.size() != 1 || plan.pairs[0].first != "/a/x" || plan.pairs[0].second != "/b/y" || !plan.unpaired.empty()) return false;
    // All on one side: each against the first.
    plan = plan_matches(Paths{"/a/x", "/a/y", "/a/z"}, Paths{});
    if (plan.pairs.size() != 2 || plan.pairs[0].second != "/a/y" || plan.pairs[1].first != "/a/x" || plan.pairs[1].second != "/a/z") return false;
    plan = plan_matches(Paths{}, Paths{"/b/x", "/b/y"});
    if (plan.pairs.size() != 1 || plan.pairs[0].first != "/b/x") return false;
    // Several across the sides: by name, and one with no partner is already a finding.
    plan = plan_matches(Paths{"/a/x", "/a/only_left"}, Paths{"/b/deep/x", "/b/only_right"});
    if (plan.pairs.size() != 1 || plan.pairs[0].second != "/b/deep/x" || plan.unpaired.size() != 2) return false;
    if (plan.unpaired[0].where != "only_left" || plan.unpaired[1].where != "only_right") return false;
    // Two of a name on a side could go with either, so nothing starts.
    plan = plan_matches(Paths{"/a/x", "/a/d/x"}, Paths{"/b/x"});
    return plan.pairs.empty() && plan.refused == "Nothing was matched: two selected on one side have the same name, x";
}
static_assert(selections_become_pairs());

template <class Tree>
[[nodiscard]] constexpr auto& popup_of(Tree& tree) { return tree.template get<files::MatchPopup>(); }

template <class Tree>
constexpr void until_matched(Tree& tree) {
    for (int i = 0; i < 50 && (transfers_of(tree).matching() || i == 0); ++i) turn(tree);
    turn(tree);
}

// Selected on each side, the button matches them, and the popup says they all match. Until it is closed nothing below it hears anything.
[[nodiscard]] constexpr bool the_selected_are_matched() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::string{"/top"}, std::string{"/other"});
    static_cast<void>(lay_out(tree));
    auto& left_grid = tree.template get<FilePaneGrid<Left_>>();
    auto& right_grid = tree.template get<FilePaneGrid<Right_>>();
    left_grid.select_rows(std::vector<std::uint32_t>{2}, GridRow{2});    // note.txt
    right_grid.select_rows(std::vector<std::uint32_t>{0}, GridRow{0});  // g1, under another name
    click_on<decltype(tree), files::CompareButton>(tree);
    if (!transfers_of(tree).matching() || popup_of(tree).is_open()) return false;
    until_matched(tree);
    if (transfers_of(tree).matching() || !popup_of(tree).is_open()) return false;
    if (disk.matched.empty() || disk.matched.front() != "/top/note.txt /other/g1") return false;
    if (popup_of(tree).heading() != "They all match" || !popup_of(tree).findings().empty()) return false;
    if (!popup_of(tree).detail_text().contains("1 file matched by SHA-256") || !popup_of(tree).detail_text().contains("not compared")) return false;
    // A press below goes nowhere: it would have selected a row, and now it does not.
    const auto before = left_grid.selected_rows().size();
    press(tree, row_point(left_grid, 0, past_a_top_mark));
    if (left_grid.selected_rows().size() != before || left_grid.selected()->index != 2 || !popup_of(tree).is_open()) return false;
    // Nor do keys.
    jgui_test::dispatch(tree, KeyEvent{Key::LowerA, Modifier::Ctrl, true});
    if (left_grid.selected_rows().size() != before) return false;
    // OK closes it, and the panes hear again. Nothing was marked or copied.
    click_on<decltype(tree), FileMatchPopupButton<files::MatchPopup>>(tree);
    if (popup_of(tree).is_open() || !disk.arrived.empty() || tree.template get<Left_>().rows()[2].match != FileMatch::NotCompared) return false;
    press(tree, row_point(left_grid, 0, past_a_top_mark));
    return left_grid.selected()->index == 0;
}
static_assert(the_selected_are_matched());

// What does not match is listed, the others still go, and it is never called a match.
[[nodiscard]] constexpr bool what_does_not_match_is_listed() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::string{"/"}, std::string{"/other"});
    static_cast<void>(lay_out(tree));
    auto& left_grid = tree.template get<FilePaneGrid<Left_>>();
    left_grid.select_rows(std::vector<std::uint32_t>{0, 1, 2}, GridRow{0});  // locked, other, top: all on one side, so against the first, which cannot be opened
    click_on<decltype(tree), files::CompareButton>(tree);
    until_matched(tree);
    if (!popup_of(tree).is_open() || popup_of(tree).heading() != "2 things do not match") return false;
    if (popup_of(tree).findings().size() != 2 || popup_of(tree).findings()[0].why != "cannot open it") return false;
    // Escape closes it, and comparing again with fewer than two selected goes back to what shows.
    jgui_test::dispatch(tree, KeyEvent{Key::Escape, {}, true});
    if (popup_of(tree).is_open()) return false;
    left_grid.select_rows(std::vector<std::uint32_t>{1}, GridRow{1});
    click_on<decltype(tree), files::CompareButton>(tree);
    return !transfers_of(tree).matching() && transfers_of(tree).line().starts_with("Compared what shows");
}
static_assert(what_does_not_match_is_listed());

// Stopped part way, it does not say they match, a copy and a match do not run together, and a refusal starts nothing.
[[nodiscard]] constexpr bool a_match_can_be_stopped() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::string{"/top"}, std::string{"/top"});
    static_cast<void>(lay_out(tree));
    auto& left_grid = tree.template get<FilePaneGrid<Left_>>();
    left_grid.select_rows(std::vector<std::uint32_t>{0, 1}, GridRow{0});
    click_on<decltype(tree), files::CompareButton>(tree);
    if (!transfers_of(tree).matching()) return false;
    click_on<decltype(tree), FilePaneCopyButton<Left_>>(tree);
    click_on<decltype(tree), FilePanePasteButton<Right_>>(tree);
    if (transfers_of(tree).copying() || transfers_of(tree).line() != "A match is still going. Escape stops it.") return false;
    turn(tree);
    jgui_test::dispatch(tree, KeyEvent{Key::Escape, {}, true});
    if (transfers_of(tree).matching()) return false;
    turn(tree);
    if (!popup_of(tree).is_open() || popup_of(tree).heading().contains("They all match") || !popup_of(tree).heading().starts_with("Stopped before the end")) return false;
    jgui_test::dispatch(tree, KeyEvent{Key::Return, {}, true});
    if (popup_of(tree).is_open()) return false;
    transfers_of(tree).start_matching(MatchPlan{.refused = "No"});
    return !transfers_of(tree).matching() && transfers_of(tree).line() == "No";
}
static_assert(a_match_can_be_stopped());


// --- Deleting, on the made-up disk ---

template <class Tree>
[[nodiscard]] constexpr auto& delete_popup_of(Tree& tree) { return tree.template get<files::DeletePopup>(); }

template <class Tree>
constexpr void until_asked(Tree& tree) {
    for (int i = 0; i < 20 && !delete_popup_of(tree).is_open() && transfers_of(tree).counting(); ++i) turn(tree);
}

template <class Tree>
constexpr void until_deleted(Tree& tree) {
    for (int i = 0; i < 20 && transfers_of(tree).deleting(); ++i) turn(tree);
    turn(tree);
}

// Nothing goes until the popup's own button is clicked: not on the count, not on Return, not on Escape, and not on Cancel.
[[nodiscard]] constexpr bool only_the_button_deletes() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& grid = tree.template get<GridOf<Left_>>();
    grid.select_row(GridRow{2});  // note.txt
    const auto ask = [&] {
        click_on<decltype(tree), FilePaneDeleteButton<Left_>>(tree);
        until_asked(tree);
    };
    ask();
    if (!delete_popup_of(tree).is_open() || delete_popup_of(tree).heading() != "Are you sure?" || delete_popup_of(tree).count_text() != "File count to be deleted: 1") return false;
    if (!delete_popup_of(tree).what_text().contains("from note.txt in /top") || !disk.gone.empty() || !transfers_of(tree).awaiting_delete_answer()) return false;
    for (const Keysym key : {Key::Return, Key::Escape}) {
        jgui_test::dispatch(tree, KeyEvent{key, {}, true});
        if (delete_popup_of(tree).is_open() || !disk.gone.empty() || transfers_of(tree).line() != "Nothing was deleted") return false;
        ask();
    }
    // A letter does nothing at all: it reaches nothing behind the popup, and the popup stays.
    jgui_test::dispatch(tree, KeyEvent{Key::LowerA, {}, true});
    if (!delete_popup_of(tree).is_open() || !disk.gone.empty()) return false;
    static_cast<void>(lay_out(tree));
    click_on<decltype(tree), FileDeletePopupCancel<files::DeletePopup>>(tree);
    if (delete_popup_of(tree).is_open() || !disk.gone.empty() || transfers_of(tree).awaiting_delete_answer()) return false;
    ask();
    static_cast<void>(lay_out(tree));
    click_on<decltype(tree), FileDeletePopupConfirm<files::DeletePopup>>(tree);
    if (delete_popup_of(tree).is_open() || !transfers_of(tree).deleting() || !disk.gone.empty()) return false;
    until_deleted(tree);
    if (disk.gone != std::vector<std::string>{"/top/note.txt"} || transfers_of(tree).line() != "Moved to the trash: 1 file, 0 folders, 10 bytes") return false;
    // Both sides show the same folder, and both have looked again.
    return same_names(names_of(tree.template get<Left_>()), {"one", "two"}) && same_names(names_of(tree.template get<Right_>()), {"one", "two"});
}
static_assert(only_the_button_deletes());

// A pane's Delete takes only its own side's selection, however much is selected on the other.
[[nodiscard]] constexpr bool only_this_side_is_deleted() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    click_on<decltype(tree), FilePaneDeleteButton<Left_>>(tree);
    if (delete_popup_of(tree).is_open() || transfers_of(tree).counting() || tree.template get<Left_>().status() != "Select something to delete first") return false;
    tree.template get<GridOf<Right_>>().select_rows(std::vector<std::uint32_t>{0, 1}, GridRow{0});
    tree.template get<GridOf<Left_>>().select_row(GridRow{2});
    click_on<decltype(tree), FilePaneDeleteButton<Left_>>(tree);
    until_asked(tree);
    if (delete_popup_of(tree).count_text() != "File count to be deleted: 1") return false;
    static_cast<void>(lay_out(tree));
    click_on<decltype(tree), FileDeletePopupConfirm<files::DeletePopup>>(tree);
    until_deleted(tree);
    if (disk.gone != std::vector<std::string>{"/top/note.txt"}) return false;
    // Several on a side are all counted together, with folders on their own.
    click_on<decltype(tree), FilePaneDeleteButton<Right_>>(tree);
    until_asked(tree);
    return delete_popup_of(tree).count_text() == "File count to be deleted: 2" && delete_popup_of(tree).what_text().starts_with("Also 2 folders, and 20 bytes, from 2 selected things in /top");
}
static_assert(only_this_side_is_deleted());

// A count that cannot finish deletes nothing and asks nothing, and stopping a count is the same.
[[nodiscard]] constexpr bool a_count_that_fails_deletes_nothing() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::string{"/"}, std::string{"/top"});
    if (!lay_out(tree)) return false;
    tree.template get<GridOf<Left_>>().select_rows(std::vector<std::uint32_t>{0, 1}, GridRow{0});  // locked, other
    click_on<decltype(tree), FilePaneDeleteButton<Left_>>(tree);
    for (int i = 0; i < 5; ++i) turn(tree);
    if (delete_popup_of(tree).is_open() || transfers_of(tree).counting() || transfers_of(tree).line() != "Nothing was deleted: cannot open it" || !disk.gone.empty()) return false;
    tree.template get<GridOf<Left_>>().select_row(GridRow{1});
    click_on<decltype(tree), FilePaneDeleteButton<Left_>>(tree);
    if (!transfers_of(tree).counting()) return false;
    jgui_test::dispatch(tree, KeyEvent{Key::Escape, {}, true});
    for (int i = 0; i < 5; ++i) turn(tree);
    return !delete_popup_of(tree).is_open() && !transfers_of(tree).counting() && transfers_of(tree).line() == "Counting was stopped, and nothing was deleted" && disk.gone.empty();
}
static_assert(a_count_that_fails_deletes_nothing());

// Stopped or failing part way, the line says how many went, and what is selected after that stays.
[[nodiscard]] constexpr bool a_delete_can_stop_part_way() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& grid = tree.template get<GridOf<Left_>>();
    const auto say_yes = [&] {
        click_on<decltype(tree), FilePaneDeleteButton<Left_>>(tree);
        until_asked(tree);
        static_cast<void>(lay_out(tree));
        click_on<decltype(tree), FileDeletePopupConfirm<files::DeletePopup>>(tree);
    };
    grid.select_rows(std::vector<std::uint32_t>{0, 1}, GridRow{0});
    say_yes();
    turn(tree);
    jgui_test::dispatch(tree, KeyEvent{Key::Escape, {}, true});
    if (transfers_of(tree).deleting() || transfers_of(tree).line() != "Stopped: Moved 1 of 2 selected to the trash" || disk.gone != std::vector<std::string>{"/top/one"}) return false;
    turn(tree);
    grid.select_row(GridRow{0});  // two
    disk.delete_fails = true;
    say_yes();
    turn(tree);
    if (transfers_of(tree).deleting() || transfers_of(tree).line() != "Stopped: cannot delete it. Moved 0 of 1 selected to the trash" || disk.gone.size() != 1) return false;
    disk.delete_fails = false;
    turn(tree);
    // While one thing goes on, nothing else starts. One selected on each side, so Diff and Compare both get as far as asking.
    grid.select_row(GridRow{0});
    tree.template get<GridOf<Right_>>().select_row(GridRow{1});
    say_yes();
    click_on<decltype(tree), files::DiffButton>(tree);
    click_on<decltype(tree), files::CompareButton>(tree);
    if (transfers_of(tree).matching() || transfers_of(tree).diffing() || transfers_of(tree).line() != "A delete is still going. Escape stops it.") return false;
    transfers_of(tree).copy({"/top/note.txt"});
    transfers_of(tree).paste_into("/other");
    if (transfers_of(tree).copying() || transfers_of(tree).line() != "A delete is still going. Escape stops it.") return false;
    until_deleted(tree);
    return disk.gone == std::vector<std::string>{"/top/one", "/top/two"} && transfers_of(tree).line() == "Moved to the trash: 1 file, 1 folder, 10 bytes";
}
static_assert(a_delete_can_stop_part_way());

// --- What a trash is told ---

static_assert(trash_escaped_path("/a b/c%d/\xC3\xA9.txt") == "/a%20b/c%25d/%C3%A9.txt");
static_assert(trash_escaped_path("/A-z_0.9~/x") == "/A-z_0.9~/x");
static_assert(trash_info_text("/a b", TrashDate{2026, 11, 4, 9, 5, 7}) == "[Trash Info]\nPath=/a%20b\nDeletionDate=2026-11-04T09:05:07\n");
static_assert(trash_name_to_try("a.txt", 0) == "a.txt" && trash_name_to_try("a.txt", 2) == "a.txt.2");
static_assert(trash_name_to_try(std::string(255, 'x'), 0).size() + trash_info_suffix.size() == 255, "room for the info file's name");
static_assert(trash_name_to_try(std::string(255, 'x'), 12).size() + trash_info_suffix.size() == 255, "and for a number");
static_assert(valid_utf8(trash_name_to_try(std::string(244, 'x') + "\xC3\xA9\xC3\xA9\xC3\xA9", 0)), "cut on a whole character");
static_assert(path_is_in_a_trash("/h/.local/share/Trash/files/a", "/h/.local/share/Trash") && path_is_in_a_trash("/h/.local/share/Trash", "/h/.local/share/Trash"));
static_assert(path_is_in_a_trash("/disk/d/.Trash-1000/files/a", "/h/T") && path_is_in_a_trash("/disk/d/.Trash", "/h/T"));
static_assert(!path_is_in_a_trash("/disk/d/.Trashy/a", "/h/T") && !path_is_in_a_trash("/h/.local/share/Trash2", "/h/.local/share/Trash") && !path_is_in_a_trash("/disk/d/a", "/h/T"));

// --- Cat, Edit and Touch, on the made-up disk ---

using Words = std::vector<std::string>;
static_assert(editor_words("") == Words{"vim"} && editor_words("  \t ") == Words{"vim"}, "nothing set, and vim is there even when only a terminal works");
static_assert(editor_words("nano") == Words{"nano"} && editor_words("  emacs   -nw\t--foo ") == Words{"emacs", "-nw", "--foo"});
static_assert(!editor_words("sh -c 'x'") && !editor_words("a\"b") && !editor_words("a\\b"), "what would need a shell to read is refused");
static_assert(*terminal_tool_command(TerminalTool::Edit, Words{"/a b"}, "emacs -nw") == Words{"emacs", "-nw", "--", "/a b"}, "the name stays one word, after where options end");
static_assert(*terminal_tool_command(TerminalTool::Edit, Words{"/a"}, "") == Words{"vim", "--", "/a"});
static_assert(terminal_tool_command(TerminalTool::Cat, Words{"/a"}, "'bad")->front() == "sh", "Cat does not care what EDITOR says");

static_assert(absolute_path_folders("/a:/b c:/d") == Words{"/a", "/b c", "/d"});
static_assert(absolute_path_folders("").empty() && absolute_path_folders("::").empty() && absolute_path_folders(".:bin:./x").empty(), "nothing and relative folders are not looked in");
static_assert(absolute_path_folders(":/a::rel:/b:") == Words{"/a", "/b"});

static_assert(terminal_arguments(TerminalStyle{"ptyxis", {"--"}, 1}, std::vector<std::string>{"vim", "--", "/a b"}) == std::vector<std::string>{"ptyxis", "--", "vim", "--", "/a b"});
static_assert(terminal_arguments(TerminalStyle{"foot", {}, 0}, std::vector<std::string>{"vim"}) == std::vector<std::string>{"foot", "vim"});

[[nodiscard]] constexpr bool cat_and_edit_open_a_terminal() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& pane = tree.template get<Left_>();
    click_on<decltype(tree), FilePaneCatButton<Left_>>(tree);
    if (!disk.terminals.empty() || pane.status() != "Select a file first") return false;
    // A folder is refused, by name.
    tree.template get<GridOf<Left_>>().select_row(GridRow{0});
    click_on<decltype(tree), FilePaneEditButton<Left_>>(tree);
    if (!disk.terminals.empty() || pane.status() != "Only files can be shown or edited, and one is not one") return false;
    tree.template get<GridOf<Left_>>().select_row(GridRow{2});
    click_on<decltype(tree), FilePaneCatButton<Left_>>(tree);
    if (disk.terminals.size() != 1 || disk.terminals[0] != std::vector<std::string>{"/top", "sh", "-c", std::string{cat_and_wait_script}, "sh", "/top/note.txt"}) return false;
    if (transfers_of(tree).line() != "Showing note.txt in a terminal") return false;
    click_on<decltype(tree), FilePaneEditButton<Left_>>(tree);
    if (disk.terminals.size() != 2 || disk.terminals[1] != std::vector<std::string>{"/top", "vim", "--", "/top/note.txt"} || transfers_of(tree).line() != "Editing note.txt in a terminal") return false;
    disk.editor = "emacs -nw";
    click_on<decltype(tree), FilePaneEditButton<Left_>>(tree);
    if (disk.terminals.size() != 3 || disk.terminals[2] != std::vector<std::string>{"/top", "emacs", "-nw", "--", "/top/note.txt"}) return false;
    disk.editor = "sh -c 'x'";
    click_on<decltype(tree), FilePaneEditButton<Left_>>(tree);
    if (disk.terminals.size() != 3 || !transfers_of(tree).line().starts_with("EDITOR holds a quote")) return false;
    disk.editor.clear();
    disk.no_terminal = true;
    click_on<decltype(tree), FilePaneEditButton<Left_>>(tree);
    return disk.terminals.size() == 3 && transfers_of(tree).line() == "no terminal";
}
static_assert(cat_and_edit_open_a_terminal());

// --- Typing a name ---

struct EntryTag;

[[nodiscard]] constexpr KeyEvent typing(std::string_view text) { return KeyEvent{Key::Space, {}, true, KeyText::of(text)}; }
[[nodiscard]] constexpr KeyEvent pressing(Keysym key) { return KeyEvent{key, {}, true}; }

[[nodiscard]] constexpr bool an_entry_edits_by_whole_characters() {
    TextEntry<EntryTag> entry{5};
    if (!entry.take_key(typing("a")) || !entry.take_key(typing("é")) || entry.text() != "aé" || entry.caret() != 3) return false;
    // Backwards over the two bytes of one character, and forwards again, and one typed goes where the caret is.
    if (!entry.take_key(pressing(Key::Left)) || entry.caret() != 1 || !entry.take_key(pressing(Key::Right)) || entry.caret() != 3) return false;
    static_cast<void>(entry.take_key(pressing(Key::Home)));
    if (!entry.take_key(typing("b")) || entry.text() != "baé" || entry.caret() != 1) return false;
    if (!entry.take_key(pressing(Key::Delete)) || entry.text() != "bé") return false;
    static_cast<void>(entry.take_key(pressing(Key::End)));
    if (!entry.take_key(pressing(Key::BackSpace)) || entry.text() != "b" || entry.caret() != 1) return false;
    // At either end these do nothing, and are still its keys.
    if (!entry.take_key(pressing(Key::Delete)) || entry.text() != "b") return false;
    static_cast<void>(entry.take_key(pressing(Key::Home)));
    return entry.take_key(pressing(Key::BackSpace)) && entry.text() == "b" && entry.caret() == 0;
}
static_assert(an_entry_edits_by_whole_characters());

[[nodiscard]] constexpr bool an_entry_takes_only_what_can_show() {
    TextEntry<EntryTag> entry{4};
    // A control character, a backspace's own byte, invalid UTF-8 and nothing at all.
    for (const std::string_view bad : {std::string_view{"\n"}, std::string_view{"\b"}, std::string_view{"\x7f"}, std::string_view{"\xC2\x85"}, std::string_view{"\xFF"}, std::string_view{"\xC3"}, std::string_view{""}}) {
        if (entry.take_key(typing(bad)) || !entry.text().empty()) return false;
    }
    if (entry.set_text("a\nb") || entry.set_text("12345") || !entry.text().empty()) return false;
    // A character that does not fit is not cut.
    if (!entry.set_text("abc") || entry.take_key(typing("é")) || entry.text() != "abc" || !entry.take_key(typing("d")) || entry.take_key(typing("e")) || entry.text() != "abcd") return false;
    // A key held with a command types nothing, and letting go of one does nothing.
    TextEntry<EntryTag> other{9};
    static_cast<void>(other.take_key(KeyEvent{Key::LowerC, Modifier::Ctrl, true, KeyText::of("c")}));
    static_cast<void>(other.take_key(KeyEvent{Key::LowerC, Modifier::Alt, true, KeyText::of("c")}));
    static_cast<void>(other.take_key(KeyEvent{Key::LowerC, Modifier::Super, true, KeyText::of("c")}));
    static_cast<void>(other.take_key(KeyEvent{Key::LowerC, {}, false, KeyText::of("c")}));
    if (!other.text().empty()) return false;
    if (!other.take_key(KeyEvent{Key::UpperC, Modifier::Shift, true, KeyText::of("C")}) || other.text() != "C") return false;
    // More than a key can carry is nothing, never a part.
    return KeyText::of(std::string(most_key_text_bytes + 1, 'x')).view().empty() && KeyText::of(std::string(most_key_text_bytes, 'x')).view().size() == most_key_text_bytes;
}
static_assert(an_entry_takes_only_what_can_show());

static_assert(check_given_name("/top", "a b.txt") && check_given_name("/", "-rf"), "a name that looks like an option is still a name, because no shell reads it");
static_assert(check_given_name("/top", "").error().message == "Type a name first");
static_assert(check_given_name("/top", "a/b").error().message == "A name cannot hold a slash" && check_given_name("/top", "/").error().message == "A name cannot hold a slash");
static_assert(!check_given_name("/top", ".") && !check_given_name("/top", "..") && !check_given_name("/top", "a\nb") && !check_given_name("/top", "\xFF"));
static_assert(!check_given_name("/top", std::string(most_file_name_bytes + 1, 'x')) && check_given_name("/top", std::string(most_file_name_bytes, 'x')));
static_assert(!check_given_name("relative", "a") && !check_given_name("/" + std::string(most_file_path_bytes - 2, 'x'), "ab"), "a folder no pane could hold, and a path over the limit");

template <class Tree>
[[nodiscard]] constexpr auto& name_popup_of(Tree& tree) {
    return tree.template get<files::NamePopup>();
}

template <class Tree>
constexpr void type_in(Tree& tree, std::string_view text) {
    for (std::size_t at = 0; at < text.size(); ++at) {
        jgui_test::dispatch(tree, typing(text.substr(at, 1)));
        static_cast<void>(lay_out(tree));
    }
}

[[nodiscard]] constexpr bool touch_asks_for_a_name_and_makes_the_file() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& popup = name_popup_of(tree);
    if (popup.is_open()) return false;
    // With nothing selected, the folder at the top, and nothing is done until it is given a name.
    click_on<decltype(tree), FilePaneTouchButton<Left_>>(tree);
    turn(tree);
    if (!popup.is_open() || !disk.touched.empty() || !popup.typed().empty() || popup.where() != "In /top") return false;
    key(tree, Key::Return);
    if (!popup.is_open() || popup.problem() != "Type a name first" || !disk.touched.empty()) return false;
    type_in(tree, "a/b");
    if (!popup.problem().empty()) return false;  // what is typed clears the reason
    key(tree, Key::Return);
    if (!popup.is_open() || popup.problem() != "A name cannot hold a slash" || !disk.touched.empty()) return false;
    key(tree, Key::BackSpace);
    key(tree, Key::BackSpace);
    key(tree, Key::BackSpace);
    type_in(tree, "new.txt");
    // The keys went to the box and nothing behind it: the pane's own Backspace would have gone up.
    if (tree.template get<Left_>().folder() != "/top") return false;
    key(tree, Key::Return);
    if (popup.is_open() || disk.touched != std::vector<std::string>{"/top/new.txt"} || transfers_of(tree).line() != "Made new.txt") return false;
    turn(tree);
    if (!std::ranges::contains(names_of(tree.template get<Left_>()), "new.txt") || !std::ranges::contains(names_of(tree.template get<Right_>()), "new.txt")) return false;
    // A name already there gets the time of now, as touch does. The keypad's Enter says yes too.
    click_on<decltype(tree), FilePaneTouchButton<Left_>>(tree);
    turn(tree);
    if (!popup.typed().empty()) return false;  // what was typed last time is gone
    type_in(tree, "note.txt");
    key(tree, Key::KeypadEnter);
    if (popup.is_open() || disk.touched.back() != "/top/note.txt" || transfers_of(tree).line() != "Set the time of note.txt") return false;
    // In a selected folder, or the folder a selected row is in.
    tree.template get<GridOf<Left_>>().select_row(GridRow{0});
    click_on<decltype(tree), FilePaneTouchButton<Left_>>(tree);
    turn(tree);
    if (popup.where() != "In /top/one") return false;
    // Escape changes nothing.
    type_in(tree, "x");
    const std::size_t before = disk.touched.size();
    key(tree, Key::Escape);
    if (popup.is_open() || disk.touched.size() != before || transfers_of(tree).line() != "Nothing was changed") return false;
    // What the source refuses stays open, to be corrected.
    disk.touch_fails = true;
    click_on<decltype(tree), FilePaneTouchButton<Left_>>(tree);
    turn(tree);
    type_in(tree, "y");
    key(tree, Key::Return);
    if (!popup.is_open() || popup.problem() != "cannot touch it") return false;
    key(tree, Key::Escape);
    // Not while something else is going on.
    disk.touch_fails = false;
    transfers_of(tree).copy({"/top/note.txt"});
    transfers_of(tree).paste_into("/other");
    click_on<decltype(tree), FilePaneTouchButton<Left_>>(tree);
    if (transfers_of(tree).line() != "A copy is still going. Escape stops it.") return false;
    turn(tree);
    return !popup.is_open();
}
static_assert(touch_asks_for_a_name_and_makes_the_file());

[[nodiscard]] constexpr bool touch_needs_a_folder() {
    FakeDisk disk;
    auto tree = ui_on(disk, std::unexpected(Error{ErrorCode::System, "no folder"}));
    if (!lay_out(tree)) return false;
    click_on<decltype(tree), FilePaneTouchButton<Left_>>(tree);
    turn(tree);
    return !name_popup_of(tree).is_open() && tree.template get<Left_>().status() == "This side has no folder to touch in";
}
static_assert(touch_needs_a_folder());

[[nodiscard]] constexpr bool rename_takes_one_thing_and_starts_from_its_name() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& popup = name_popup_of(tree);
    auto& pane = tree.template get<Left_>();
    click_on<decltype(tree), FilePaneRenameButton<Left_>>(tree);
    turn(tree);
    if (popup.is_open() || pane.status() != "Select one thing to rename") return false;
    tree.template get<GridOf<Left_>>().select_rows(std::vector<std::uint32_t>{0, 2}, GridRow{0});
    click_on<decltype(tree), FilePaneRenameButton<Left_>>(tree);
    turn(tree);
    if (popup.is_open() || pane.status() != "Select one thing to rename") return false;
    tree.template get<GridOf<Left_>>().select_row(GridRow{2});
    click_on<decltype(tree), FilePaneRenameButton<Left_>>(tree);
    turn(tree);
    if (!popup.is_open() || popup.typed() != "note.txt" || popup.where() != "In /top") return false;
    // The name it has already changes nothing, and asks nothing of the disk.
    key(tree, Key::Return);
    if (popup.is_open() || !disk.gone.empty() || transfers_of(tree).line() != "Nothing was changed") return false;
    // A name that is taken is refused, stays open, and nothing moves.
    click_on<decltype(tree), FilePaneRenameButton<Left_>>(tree);
    turn(tree);
    for (int i = 0; i < 8; ++i) key(tree, Key::BackSpace);
    type_in(tree, "one");
    key(tree, Key::Return);
    if (!popup.is_open() || popup.problem() != "one is already there" || !disk.gone.empty()) return false;
    for (int i = 0; i < 3; ++i) key(tree, Key::BackSpace);
    type_in(tree, "renamed.txt");
    key(tree, Key::Return);
    if (popup.is_open() || disk.gone != std::vector<std::string>{"/top/note.txt"} || disk.arrived != std::vector<std::string>{"/top/renamed.txt"}) return false;
    if (transfers_of(tree).line() != "Renamed note.txt to renamed.txt") return false;
    turn(tree);
    return !std::ranges::contains(names_of(pane), "note.txt") && std::ranges::contains(names_of(pane), "renamed.txt");
}
static_assert(rename_takes_one_thing_and_starts_from_its_name());

// --- A screenshot ---

static_assert(png_crc32(std::array<std::uint8_t, 4>{'I', 'E', 'N', 'D'}) == 0xAE426082U, "the well-known checksum of a PNG's last chunk");
static_assert(png_adler32(std::array<std::uint8_t, 9>{'W', 'i', 'k', 'i', 'p', 'e', 'd', 'i', 'a'}) == 0x11E60398U, "the example the checksum's own description gives");
static_assert(picture_file_name("shot") == "shot.png" && picture_file_name("shot.png") == "shot.png" && picture_file_name("a.txt") == "a.txt.png");

[[nodiscard]] constexpr WindowPicture one_pixel_picture() {
    std::array<std::uint32_t, 1> pixels{0xFF123456U};
    return PixelBuffer::create(pixels, Size{Width{1}, Height{1}}, 4)->picture();
}

// One pixel, whole file. These bytes came from Python's own zlib, and Pillow read them back as that colour, so they do not depend on what they test.
inline constexpr std::array<std::uint8_t, 72> one_pixel_png{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0F, 0x49, 0x44, 0x41, 0x54, 0x78, 0x01, 0x01, 0x04, 0x00, 0xFB, 0xFF, 0x00, 0x12, 0x34, 0x56, 0x00, 0xF8, 0x00, 0x9D, 0xF8, 0xD7, 0x64, 0x8C, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

[[nodiscard]] constexpr bool same_bytes(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) { return std::ranges::equal(a, b); }

static_assert(same_bytes(*png_of(one_pixel_picture()), one_pixel_png));

// A gap between rows is not part of a picture, and nothing at all cannot be a PNG.
[[nodiscard]] constexpr bool a_picture_leaves_out_the_gaps() {
    std::array<std::uint32_t, 6> pixels{1, 2, 99, 3, 4, 99};
    const auto buffer = PixelBuffer::create(pixels, Size{Width{2}, Height{2}}, 12);
    const WindowPicture picture = buffer->picture();
    if (picture.columns() != 2 || picture.rows() != 2 || !std::ranges::equal(picture.argb(), std::array<std::uint32_t, 4>{1, 2, 3, 4})) return false;
    const auto empty = PixelBuffer::create(std::span<std::uint32_t>{}, Size{}, 0);
    return !png_of(empty->picture());
}
static_assert(a_picture_leaves_out_the_gaps());

[[nodiscard]] constexpr bool a_screenshot_asks_for_a_name_first() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& popup = name_popup_of(tree);
    const auto asked_for_a_picture = [&] { return detail::TestAccess::take_requests(tree).picture; };
    const auto click_it = [&] {
        click_on<decltype(tree), files::ScreenShotButton>(tree);
        turn(tree);
    };
    // Nothing is taken until there is a name, so the question is not in the picture.
    click_it();
    if (!popup.is_open() || popup.where() != "In /shots" || !popup.typed().empty() || asked_for_a_picture()) return false;
    key(tree, Key::Return);
    if (!popup.is_open() || popup.problem() != "Type a name first" || asked_for_a_picture()) return false;
    type_in(tree, "a/b");
    key(tree, Key::Return);
    if (!popup.is_open() || popup.problem() != "A name cannot hold a slash" || asked_for_a_picture()) return false;
    key(tree, Key::BackSpace);
    key(tree, Key::BackSpace);
    key(tree, Key::BackSpace);
    type_in(tree, "shot");
    key(tree, Key::Return);
    if (popup.is_open() || !asked_for_a_picture() || !disk.pictures.empty()) return false;
    // A frame later, it is saved under that name, and the panes keep what a comparison marked.
    const std::uint64_t changes_before = transfers_of(tree).changes();
    detail::TestAccess::keep_picture(tree, one_pixel_picture());
    turn(tree);
    if (disk.pictures.size() != 1 || !same_bytes(disk.pictures[0], one_pixel_png) || disk.arrived != std::vector<std::string>{"/shots/shot.png"}) return false;
    if (transfers_of(tree).line() != "Saved a screenshot of this window to /shots/shot.png" || transfers_of(tree).changes() != changes_before) return false;
    // A picture no one named goes nowhere.
    detail::TestAccess::keep_picture(tree, one_pixel_picture());
    turn(tree);
    if (disk.pictures.size() != 1) return false;
    // A name that is taken stays open to be corrected, with or without the suffix.
    click_it();
    type_in(tree, "shot");
    key(tree, Key::Return);
    if (!popup.is_open() || popup.problem() != "shot.png is already there, and nothing was replaced" || asked_for_a_picture()) return false;
    // Escape takes nothing.
    key(tree, Key::Escape);
    if (popup.is_open() || asked_for_a_picture() || transfers_of(tree).line() != "Nothing was changed") return false;
    // The name given with .png is not told twice, and a disk that fails says so.
    disk.picture_fails = true;
    click_it();
    type_in(tree, "two.png");
    key(tree, Key::Return);
    if (popup.is_open() || !asked_for_a_picture()) return false;
    detail::TestAccess::keep_picture(tree, one_pixel_picture());
    turn(tree);
    if (disk.pictures.size() != 1 || transfers_of(tree).line() != "No screenshot was saved: the disk is full") return false;
    // With nowhere to put it, nothing is asked.
    disk.no_picture_folder = true;
    click_it();
    return !popup.is_open() && transfers_of(tree).line() == "No screenshot was saved: there is no home";
}
static_assert(a_screenshot_asks_for_a_name_first());

// --- A line of text that can be copied ---

// A click on a status line asks for all of it to be copied, long or not, and shows it did until the text changes. A press elsewhere asks nothing.
[[nodiscard]] constexpr bool status_lines_can_be_copied() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& line = tree.template get<FileTransfersLine<files::Transfers>>();
    auto& status = tree.template get<FilePaneStatus<Left_>>();
    auto& path = tree.template get<FilePanePath<Right_>>();
    const auto take = [&] { return detail::TestAccess::take_requests(tree); };
    static_cast<void>(take());
    const std::string long_message(3000, 'x');
    transfers_of(tree).tell(long_message);
    static_cast<void>(lay_out(tree));
    click_on<decltype(tree), FileTransfersLine<files::Transfers>>(tree);
    const WindowRequests first = take();
    if (!first.copy || first.copy->text.view() != long_message || !line.just_copied()) return false;
    transfers_of(tree).tell("Something new");
    if (line.just_copied()) return false;
    click_on<decltype(tree), FilePaneStatus<Left_>>(tree);
    const WindowRequests second = take();
    if (!second.copy || second.copy->text.view() != "3 things, and 1 with a name that cannot be shown" || !status.just_copied()) return false;
    click_on<decltype(tree), FilePanePath<Right_>>(tree);
    const WindowRequests third = take();
    if (!third.copy || third.copy->text.view() != "/top" || !path.just_copied()) return false;
    // Going off it stops showing that it was copied, and nothing is asked again.
    jgui_test::dispatch(tree, PointerMove{Point{Left{1}, Top{1}}});
    if (path.just_copied() || take().copy) return false;
    // A press on anything else asks nothing.
    click_on<decltype(tree), FilePaneReloadButton<Left_>>(tree);
    return !take().copy;
}
static_assert(status_lines_can_be_copied());

// --- The sanitizer ---

[[nodiscard]] constexpr ClipboardFindings found_in(std::string_view text) { return inspect_clipboard_text(text).found; }
[[nodiscard]] constexpr std::string cleaned(std::string_view text) { return std::string{inspect_clipboard_text(text).text.view()}; }

static_assert(found_in("").none() && found_in("Moved to the trash: 1 file, 0 folders, 10 bytes").none() && found_in("/disk/a b/c-d_e.txt (1)").none(), "ordinary messages and paths are left alone");
static_assert(found_in("\xC3\xA9t\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80").none() && cleaned("\xC3\xA9t\xC3\xA9") == "\xC3\xA9t\xC3\xA9", "letters and an emoji");
static_assert(found_in("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9").none(), "a joiner, which emoji and some languages need");
static_assert(found_in("rm -rf ~; x").shell == 1 && cleaned("rm -rf ~; x") == "rm -rf ~; x", "a shell character is named and kept");
static_assert(found_in("$(x) `y` a|b a&b a<b a>b").shell == 7);
static_assert(found_in("a\nb").control == 1 && cleaned("a\nb") == "a\xEF\xBF\xBD" "b", "a newline could press Enter");
static_assert(found_in("a\rb").control == 1 && found_in("a\tb").control == 1);
static_assert(found_in("\x1B[31m").control == 1 && !cleaned("\x1B[31m").contains('\x1B'), "a terminal escape");
static_assert(found_in("\x7F").control == 1 && found_in("\xC2\x85").control == 1 && found_in("\xE2\x80\xA8").control == 1, "DEL, a C1 control and a line separator");
static_assert(found_in("x\xE2\x80\xAEy").direction == 1 && found_in("\xE2\x81\xA6").direction == 1 && found_in("\xE2\x80\x8F").direction == 1 && found_in("\xD8\x9C").direction == 1, "what changes the order text shows in");
static_assert(found_in("\xE2\x80\x8B").invisible == 1 && found_in("\xEF\xBB\xBF").invisible == 1 && found_in("\xC2\xAD").invisible == 1 && found_in("\xF3\xA0\x80\x81").invisible == 1, "what does not show, Unicode tags too");
static_assert(found_in("a\xFF" "b").not_text == 1 && found_in("\xC0\x80").not_text == 2 && found_in("\xE2\x82").not_text == 2 && found_in("\xED\xA0\x80").not_text == 3, "bytes that are not text, a cut character and a surrogate");
static_assert(valid_utf8(cleaned("a\xFF\xC0\xE2\x82 b")), "what comes out is always text");
static_assert(found_in(std::string(4096, 'x')).none() && found_in(std::string(4097, 'x')).cut && cleaned(std::string(5000, 'x')).size() == 4096, "the limit");
static_assert(valid_utf8(cleaned(std::string(4095, 'x') + "\xC3\xA9")) && found_in(std::string(4095, 'x') + "\xC3\xA9").cut, "cut on a whole character");
static_assert(clipboard_findings_text(found_in("a;b\n"))== "Characters a shell would act on, such as ; | & $: 1; Control characters, such as a newline or a terminal escape: 1");

template <class Tree>
[[nodiscard]] constexpr auto& clipboard_popup_of(Tree& tree) { return tree.template get<files::ClipboardPopup>(); }

// Text with something in it does not reach the clipboard until the popup's Continue is clicked, and then only as cleaned. Every key and Abort stop it there.
[[nodiscard]] constexpr bool the_user_decides_before_it_goes() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& line = tree.template get<FileTransfersLine<files::Transfers>>();
    const auto take = [&] { return detail::TestAccess::take_requests(tree); };
    const auto ask = [&] {
        transfers_of(tree).tell("name; rm \xE2\x80\xAE x");
        static_cast<void>(lay_out(tree));
        click_on<decltype(tree), FileTransfersLine<files::Transfers>>(tree);
        turn(tree);
    };
    ask();
    if (!clipboard_popup_of(tree).is_open() || take().copy || line.just_copied()) return false;
    if (!clipboard_popup_of(tree).found_text().contains("shell") || !clipboard_popup_of(tree).found_text().contains("order") || !clipboard_popup_of(tree).text_shown().contains("rm") || clipboard_popup_of(tree).text_shown().contains("\xE2\x80\xAE")) return false;
    for (const Keysym key : {Key::Return, Key::Escape}) {
        jgui_test::dispatch(tree, KeyEvent{key, {}, true});
        if (clipboard_popup_of(tree).is_open() || take().copy || transfers_of(tree).line() != "Nothing was copied") return false;
        ask();
    }
    // No other key does anything, and nothing behind it can be reached.
    jgui_test::dispatch(tree, KeyEvent{Key::LowerA, {}, true});
    if (!clipboard_popup_of(tree).is_open() || take().copy) return false;
    click_on<decltype(tree), FilePaneReloadButton<Left_>>(tree);
    if (!clipboard_popup_of(tree).is_open() || take().copy) return false;
    static_cast<void>(lay_out(tree));
    click_on<decltype(tree), FileClipboardPopupAbort<files::ClipboardPopup>>(tree);
    if (clipboard_popup_of(tree).is_open() || take().copy || transfers_of(tree).line() != "Nothing was copied") return false;
    ask();
    static_cast<void>(lay_out(tree));
    click_on<decltype(tree), FileClipboardPopupContinue<files::ClipboardPopup>>(tree);
    const WindowRequests went = take();
    if (clipboard_popup_of(tree).is_open() || !went.copy || !went.copy->text.view().contains("; rm") || went.copy->text.view().contains("\xE2\x80\xAE")) return false;
    // Asked once, it is not asked again, and the next clean line goes straight through.
    turn(tree);
    if (clipboard_popup_of(tree).is_open() || take().copy) return false;
    transfers_of(tree).tell("plain");
    static_cast<void>(lay_out(tree));
    click_on<decltype(tree), FileTransfersLine<files::Transfers>>(tree);
    return take().copy.has_value() && line.just_copied();
}
static_assert(the_user_decides_before_it_goes());

// --- Diffing lines ---

static_assert(shown_line("plain\n") == "plain");
static_assert(shown_line("no end") == "no end (no newline at the end)");
static_assert(shown_line("a\tb\n") == "a    b");
static_assert(shown_line("a\r\n") == "a\xE2\x90\x8D", "a carriage return shows");
static_assert(shown_line("a\x1B[31mb\n") == "a\xEF\xBF\xBD[31mb", "a terminal escape does not reach the screen as one");
static_assert(shown_line("a\xC2\x85z\n") == "a\xEF\xBF\xBDz", "a C1 control");
static_assert(shown_line("x\xE2\x80\xAEy\n") == "x\xEF\xBF\xBDy", "a right-to-left override cannot make text show other than it is");
static_assert(shown_line("x\xE2\x81\xA6y\n") == "x\xEF\xBF\xBDy", "nor an isolate");
static_assert(shown_line("x\xE2\x80\xA8y\n") == "x\xEF\xBF\xBDy", "nor a line separator");
static_assert(shown_line("\xC3\xA9t\xC3\xA9\n") == "\xC3\xA9t\xC3\xA9", "real letters stay");
static_assert(shown_line("\xE2\x82\xAC\n") == "\xE2\x82\xAC", "and a euro sign");
static_assert(shown_line("bad\xFF\n") == "(a line that is not UTF-8, 4 bytes)");
static_assert(shown_line(std::string(5000, 'x') + "\n").size() <= max_caption_bytes);
static_assert(valid_utf8(shown_line(std::string(5000, '\t') + "\xC3\xA9\n")));

[[nodiscard]] constexpr DiffOutcome diff_of(std::string first, std::string second) {
    auto differ = LineDiffer::start(std::move(first), std::move(second));
    if (!differ) return DiffOutcome{.too_different = true};
    while (!differ->step()) {}
    return differ->outcome();
}

static_assert(diff_of("", "").rows.empty() && diff_of("a\nb\n", "a\nb\n").rows.empty(), "nothing differs, and nothing is listed");

[[nodiscard]] constexpr bool one_line_changed() {
    const DiffOutcome out = diff_of("one\ntwo\nthree\n", "one\nTWO\nthree\n");
    if (out.left_only != 1 || out.right_only != 1 || out.rows.size() != 4) return false;
    if (out.rows[0].text != "one" || out.rows[0].left != FileBytes{true, 1} || out.rows[0].right != FileBytes{true, 1} || !out.rows[0].only_in.empty()) return false;
    if (out.rows[1].text != "two" || out.rows[1].only_in != "Left" || out.rows[1].left != FileBytes{true, 2} || out.rows[1].right.known) return false;
    if (out.rows[2].text != "TWO" || out.rows[2].only_in != "Right" || out.rows[2].left.known || out.rows[2].right != FileBytes{true, 2}) return false;
    return out.rows[3].text == "three" && out.rows[3].left == FileBytes{true, 3} && out.rows[3].right == FileBytes{true, 3};
}
static_assert(one_line_changed());

static_assert(diff_of("", "a\n").right_only == 1 && diff_of("a\n", "").left_only == 1, "one side empty");
static_assert(diff_of("a\n", "a").left_only == 1 && diff_of("a\n", "a").right_only == 1, "a last newline is a difference: the two files are not the same");
static_assert(diff_of("a\r\n", "a\n").left_only == 1, "and so is a line ending");
static_assert(diff_of("x\ny\nz\n", "a\nb\n").left_only == 3 && diff_of("x\ny\nz\n", "a\nb\n").right_only == 2, "nothing in common");

// Lines the same that are far from a change are counted and not listed.
[[nodiscard]] constexpr bool far_lines_are_left_out() {
    std::string first;
    for (int i = 0; i < 30; ++i) first += "line " + decimal_text(static_cast<std::uint64_t>(i), false) + "\n";
    std::string second = first;
    second.replace(second.find("line 15"), 7, "CHANGED");
    const DiffOutcome out = diff_of(first, second);
    if (out.rows.size() != 1 + 3 + 2 + 3 + 1) return false;
    return out.rows.front().text == "... 12 lines the same ..." && !out.rows.front().left.known && out.rows[1].left == FileBytes{true, 13} && out.rows.back().text == "... 11 lines the same ...";
}
static_assert(far_lines_are_left_out());

// What may not be diffed is refused rather than half done.
static_assert(!LineDiffer::start(std::string{"a\0b", 3}, "a"));
static_assert(!LineDiffer::start("a", std::string{"a\0b", 3}));

// Too many changes ends the search and says so, and lists nothing as though it were all.
[[nodiscard]] constexpr bool the_search_has_a_limit() {
    std::string first;
    std::string second;
    for (std::int32_t i = 0; i <= most_diff_changes / 2; ++i) {
        first += "a" + decimal_text(static_cast<std::uint64_t>(i), false) + "\n";
        second += "b" + decimal_text(static_cast<std::uint64_t>(i), false) + "\n";
    }
    auto differ = LineDiffer::start(first, second);
    if (!differ) return false;
    for (std::int32_t round = 0; round <= most_diff_changes + 2; ++round)
        if (differ->step()) break;
    const DiffOutcome out = differ->outcome();
    return differ->ended() && out.too_different && out.rows.empty();
}

// --- The Diff button, on the made-up disk ---

template <class Tree>
[[nodiscard]] constexpr auto& diff_popup_of(Tree& tree) { return tree.template get<files::DiffPopup>(); }

template <class Tree>
constexpr void until_diffed(Tree& tree) {
    for (int i = 0; i < 40 && transfers_of(tree).diffing(); ++i) turn(tree);
}

[[nodiscard]] constexpr bool the_button_diffs_two_files() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& left = tree.template get<Left_>();
    auto& right = tree.template get<Right_>();
    click_on<decltype(tree), files::DiffButton>(tree);
    if (transfers_of(tree).diffing() || transfers_of(tree).line() != "Select exactly two files to diff") return false;
    left.toggle(0);  // one: inner, f1
    right.toggle(0);
    left.toggle(1);  // inner: f2
    if (!same_names(names_of(left), {"one", "inner", "f2", "f1", "two", "note.txt"})) return false;
    tree.template get<GridOf<Left_>>().select_row(GridRow{3});  // f1
    tree.template get<GridOf<Right_>>().select_row(GridRow{2});  // f1 on that side
    click_on<decltype(tree), files::DiffButton>(tree);
    if (!transfers_of(tree).diffing()) return false;
    until_diffed(tree);
    turn(tree);
    if (!diff_popup_of(tree).is_open() || diff_popup_of(tree).heading() != "No lines differ" || !diff_popup_of(tree).rows().empty()) return false;
    jgui_test::dispatch(tree, KeyEvent{Key::Escape, {}, true});
    if (diff_popup_of(tree).is_open()) return false;
    // Two on one side, the first being the left of the diff.
    tree.template get<GridOf<Right_>>().select_row(std::nullopt);
    tree.template get<GridOf<Left_>>().select_rows(std::vector<std::uint32_t>{2, 3}, GridRow{2});  // f2, f1
    click_on<decltype(tree), files::DiffButton>(tree);
    until_diffed(tree);
    turn(tree);
    if (!diff_popup_of(tree).is_open() || !diff_popup_of(tree).heading().starts_with("2 lines only on the left, 1 line only on the right") || diff_popup_of(tree).rows().empty()) return false;
    if (diff_popup_of(tree).detail_text() != "Left: /top/one/inner/f2" || diff_popup_of(tree).right_text() != "Right: /top/one/f1") return false;
    // The popup's OK closes it, and nothing was changed on the disk.
    static_cast<void>(lay_out(tree));
    click_on<decltype(tree), FileDiffPopupButton<files::DiffPopup>>(tree);
    return !diff_popup_of(tree).is_open() && disk.gone.empty() && disk.arrived.empty();
}
static_assert(the_button_diffs_two_files());

[[nodiscard]] constexpr bool what_cannot_be_diffed_says_why() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    tree.template get<GridOf<Left_>>().select_rows(std::vector<std::uint32_t>{0, 1}, GridRow{0});  // two folders
    click_on<decltype(tree), files::DiffButton>(tree);
    if (transfers_of(tree).diffing() || transfers_of(tree).line() != "Nothing to diff: one is not a file") return false;
    tree.template get<GridOf<Left_>>().select_rows(std::vector<std::uint32_t>{0, 1, 2}, GridRow{0});
    click_on<decltype(tree), files::DiffButton>(tree);
    if (transfers_of(tree).line() != "Select exactly two files to diff") return false;
    // Not text, and stopping one.
    auto& pane = tree.template get<Left_>();
    pane.toggle(0);
    tree.template get<GridOf<Left_>>().select_rows(std::vector<std::uint32_t>{2, 4}, GridRow{2});  // f1, note.txt
    click_on<decltype(tree), files::DiffButton>(tree);
    if (!transfers_of(tree).line().contains("not both text") || transfers_of(tree).diffing()) return false;
    transfers_of(tree).start_diff("/top/one/f1", "/top/one/inner/f2");
    jgui_test::dispatch(tree, KeyEvent{Key::Escape, {}, true});
    turn(tree);
    return !transfers_of(tree).diffing() && !diff_popup_of(tree).is_open() && transfers_of(tree).line() == "The diff was stopped";
}
static_assert(what_cannot_be_diffed_says_why());

// Looking again keeps what was open and what was selected, wherever they went, and what has gone is gone.
[[nodiscard]] constexpr bool looking_again_keeps_the_tree() {
    FakeDisk disk;
    auto tree = ui_on(disk);
    if (!lay_out(tree)) return false;
    auto& pane = tree.template get<Left_>();
    auto& grid = tree.template get<GridOf<Left_>>();
    pane.toggle(1);  // two
    pane.toggle(0);  // one
    pane.toggle(1);  // inner
    if (!same_names(names_of(pane), {"one", "inner", "f2", "f1", "two", "f3", "note.txt"})) return false;
    grid.select_row(GridRow{6});
    disk.gone.push_back("/top/one/f1");
    disk.arrived.push_back("/top/two/new");
    pane.reload();
    if (!same_names(names_of(pane), {"one", "inner", "f2", "two", "f3", "new", "note.txt"})) return false;
    if (pane.rows()[1].name.mark != TreeMark::Open || pane.rows()[2].name.depth != 2) return false;
    if (grid.selected() != GridRow{6} || pane.status() != "7 things") return false;
    // What was selected and has gone selects nothing, and a folder gone closes.
    disk.gone.push_back("/top/note.txt");
    disk.gone.push_back("/top/one");
    pane.reload();
    return same_names(names_of(pane), {"two", "f3", "new"}) && !grid.selected();
}
static_assert(looking_again_keeps_the_tree());

// Windows too small to hold it, with a real press in each.
[[nodiscard]] constexpr bool tiny_windows_hold() {
    for (const Size size : {Size{Width{1}, Height{1}}, Size{Width{3}, Height{3}}, Size{Width{20}, Height{5}}, Size{Width{200}, Height{120}}}) {
        auto tree = fake_ui();
        if (!lay_out(tree, Rect{Point{}, size})) return false;
        jgui_test::dispatch(tree, PointerPress{Point{Left{0}, Top{0}}, MouseButton::Left, jgui_test::serial(1)});
        jgui_test::dispatch(tree, KeyEvent{Key::Return, {}, true});
        if (!lay_out(tree, Rect{Point{}, size})) return false;
    }
    return true;
}
static_assert(tiny_windows_hold());

[[nodiscard]] constexpr bool the_window_paints() {
    auto tree = fake_ui();
    const Size small{Width{300}, Height{100}};
    if (!lay_out(tree, Rect{Point{}, small})) return false;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(300) * 100, 0);
    const auto buffer = PixelBuffer::create(pixels, small, PixelBuffer::tight_stride(small));
    Canvas canvas{*buffer};
    jgui_test::paint(tree, canvas);
    const Rect left_strip = tree.template get<FilePaneUpButton<Left_>>().bounds();
    const Rect right_strip = tree.template get<FilePaneUpButton<Right_>>().bounds();
    return left_strip != right_strip && std::ranges::any_of(pixels, [](std::uint32_t pixel) { return pixel == theme::bar.argb; });
}
static_assert(the_window_paints());

// --- A real folder ---

[[nodiscard]] const FileEntry* find_entry(const DirectoryListing& listing, std::string_view name) {
    const auto found = std::ranges::find(listing.entries, name, &FileEntry::name);
    return found == listing.entries.end() ? nullptr : &*found;
}

jgui_test::Checker check;

void make_file(const std::filesystem::path& path, off_t length) {
    const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    check(file >= 0 && ::ftruncate(file, length) == 0 && ::close(file) == 0, "a test file was made");
}

void reads_a_real_folder() {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top{directory.path()};
    std::filesystem::create_directory(top / "inside");
    make_file(top / "plain.txt", 5);
    std::filesystem::create_symlink(top / "inside", top / "link_to_folder");
    std::filesystem::create_symlink(top / "gone", top / "dangling");
    check(::mkfifo((top / "a_pipe").c_str(), S_IRUSR | S_IWUSR) == 0, "::mkfifo((top / 'a_pipe').c_str(), S_IRUSR | S_IWUSR) == 0");
    make_file(top / "bad\xFFname", 1);
    make_file(top / "line\nbreak", 1);

    const auto listing = read_directory(directory.path());
    check(listing.has_value(), "listing.has_value()");
    check(listing->left_out == 2, "listing->left_out == 2");
    check(listing->entries.size() == 5, "listing->entries.size() == 5");
    check(listing->entries.front().name == "inside" && listing->entries.front().kind == FileKind::Folder, "listing->entries.front().name == 'inside' && listing->entries.front().");
    check(!listing->entries.front().size.known, "!listing->entries.front().size.known");
    const FileEntry* plain = find_entry(*listing, "plain.txt");
    check(plain != nullptr && plain->kind == FileKind::File && plain->size == FileBytes{true, 5} && plain->modified.known, "plain != nullptr && plain->kind == FileKind::File && plain->size == Fi");
    check(find_entry(*listing, "link_to_folder") != nullptr && find_entry(*listing, "link_to_folder")->kind == FileKind::Link, "find_entry(*listing, 'link_to_folder') != nullptr && find_entry(*listi");
    check(find_entry(*listing, "dangling") != nullptr && find_entry(*listing, "dangling")->kind == FileKind::Link, "find_entry(*listing, 'dangling') != nullptr && find_entry(*listing, 'd");
    check(find_entry(*listing, "a_pipe") != nullptr && find_entry(*listing, "a_pipe")->kind == FileKind::Other, "find_entry(*listing, 'a_pipe') != nullptr && find_entry(*listing, 'a_p");

    // Nothing is followed: a link to a folder, a file and a pipe are each refused, and none of these waits.
    check(!read_directory((top / "link_to_folder").string()).has_value(), "!read_directory((top / 'link_to_folder').string()).has_value()");
    check(!read_directory((top / "plain.txt").string()).has_value(), "!read_directory((top / 'plain.txt').string()).has_value()");
    check(!read_directory((top / "a_pipe").string()).has_value(), "!read_directory((top / 'a_pipe').string()).has_value()");
    check(!read_directory((top / "nothing_here").string()).has_value(), "!read_directory((top / 'nothing_here').string()).has_value()");
    check(!read_directory("").has_value(), "!read_directory('').has_value()");
    check(!read_directory(std::string{"a\0b", 3}).has_value(), "!read_directory(std::string{'a0b', 3}).has_value()");

    // A folder this user may not read, unless this user is root, who may.
    std::filesystem::create_directory(top / "shut");
    std::filesystem::permissions(top / "shut", std::filesystem::perms::none);
    if (::geteuid() != 0) check(!read_directory((top / "shut").string()).has_value(), "!read_directory((top / 'shut').string()).has_value()");
    std::filesystem::permissions(top / "shut", std::filesystem::perms::owner_all);

    // Where to start: the one place a link is followed, because the person named it.
    const auto followed = starting_folder((top / "link_to_folder").string());
    check(followed.has_value() && *followed == std::filesystem::canonical(top / "inside").string(), "followed.has_value() && *followed == std::filesystem::canonical(top / 'inside').str");
    const auto here = starting_folder(".");
    check(here.has_value() && file_path_can_show(*here), "here.has_value() && file_path_can_show(*here)");
    check(!starting_folder((top / "plain.txt").string()).has_value(), "!starting_folder((top / 'plain.txt').string()).has_value()");
    check(!starting_folder((top / "nothing_here").string()).has_value(), "!starting_folder((top / 'nothing_here').string()).has_value()");
    std::filesystem::create_directory(top / "bad\xFF");
    check(!starting_folder((top / "bad\xFF").string()).has_value(), "!starting_folder((top / 'badxFF').string()).has_value()");

    // The real thing, through a pane, end to end.
    auto tree = files::make_ui(LocalFiles{}, files::StartFolder{std::filesystem::canonical(top).string()}, files::StartFolder{std::filesystem::canonical(top / "inside").string()});
    auto& pane = tree.template get<files::LeftPane>();
    check(pane.rows().size() == 6, "pane.rows().size() == 6");
    check(pane.status() == "6 things, and 3 with a name that cannot be shown", "pane.status() == '6 things, and 3 with a name that cannot be shown'");
    check(tree.template get<files::RightPane>().rows().empty(), "tree.template get<files::RightPane>().rows().empty()");
    // Folders first, and "bad\xFF", also a folder, is not there because a pane cannot name it.
    check(pane.rows().front().name.text == "inside", "pane.rows().front().name.text == 'inside'");
    pane.toggle(1);
    check(pane.rows().size() == 6, "pane.rows().size() == 6");
}

[[nodiscard]] std::string whole_text_of(const std::filesystem::path& path) {
    std::string text(std::filesystem::file_size(path), '\0');
    const int file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    check(file >= 0 && detail::read_at<char>(file, std::span<char>{text}, 0).has_value(), "a whole file was read back");
    static_cast<void>(::close(file));
    return text;
}

void put_text(const std::filesystem::path& path, std::string_view text) {
    const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    check(file >= 0 && detail::write_at<char>(file, std::span<const char>{text}, 0).has_value() && ::close(file) == 0, "a test file was written");
}

// Runs a copy to its end, however many steps that takes, and how it ended.
[[nodiscard]] std::expected<CopyProgress, Error> run_copy(const std::filesystem::path& from, const std::filesystem::path& into) {
    auto job = start_local_copy(from.string(), into.string());
    if (!job) return std::unexpected(job.error());
    while (true) {
        auto done = job->step();
        if (!done) return std::unexpected(done.error());
        if (*done) return job->progress();
    }
}

void copies_real_things() {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top = std::filesystem::canonical(directory.path());
    const std::filesystem::path from = top / "from";
    const std::filesystem::path to = top / "to";
    std::filesystem::create_directories(from / "deep" / "er");
    std::filesystem::create_directories(to);
    // Larger than a chunk, and every byte different from its neighbours in a way a short copy would show.
    std::string large(std::size_t{3} * 1024 * 1024 + 17, '\0');
    for (std::size_t i = 0; i < large.size(); ++i) large[i] = static_cast<char>((i * 31 + i / 256) % 251);
    put_text(from / "large.bin", large);
    put_text(from / "empty", "");
    put_text(from / "deep" / "er" / "small.txt", "small");
    put_text(from / "bad\xFFname", "still copied");
    std::filesystem::create_symlink("large.bin", from / "link");
    std::filesystem::create_symlink("nothing_there", from / "dangling");
    check(::mkfifo((from / "a_pipe").c_str(), S_IRUSR | S_IWUSR) == 0, "a pipe was made");
    check(::chmod((from / "empty").c_str(), S_IRUSR) == 0, "a file was made read-only");
    check(::chmod((from / "large.bin").c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_ISUID) == 0, "a file was made set-user-ID");
    const timespec long_ago[2]{{1000, 0}, {2000, 0}};
    check(::utimensat(AT_FDCWD, (from / "deep" / "er" / "small.txt").c_str(), long_ago, 0) == 0, "a time was set");
    check(::utimensat(AT_FDCWD, (from / "deep" / "er").c_str(), long_ago, 0) == 0, "a folder's time was set");
    check(::chmod((from / "deep").c_str(), S_IRUSR | S_IXUSR) == 0, "a folder was made read-only");

    const auto copied = run_copy(from, to);
    check(copied.has_value(), "a whole tree copied");
    if (!copied) std::cerr << copied.error().message << '\n';
    const std::filesystem::path made = to / "from";
    check(copied && copied->files == 4 && copied->folders == 3 && copied->links == 2 && copied->not_copied == 1, "what it counted");
    check(whole_text_of(made / "large.bin") == large, "every byte of a large file");
    check(std::filesystem::file_size(made / "empty") == 0, "an empty file");
    check(whole_text_of(made / "deep" / "er" / "small.txt") == "small", "a file deep down");
    check(whole_text_of(made / "bad\xFFname") == "still copied", "a name that cannot be shown is still copied");
    check(std::filesystem::read_symlink(made / "link") == "large.bin", "a link is copied as a link");
    check(std::filesystem::is_symlink(made / "dangling") && std::filesystem::read_symlink(made / "dangling") == "nothing_there", "a dangling link too");
    check(!std::filesystem::exists(std::filesystem::symlink_status(made / "a_pipe")), "a pipe is left");
    struct stat seen {};
    check(::stat((made / "large.bin").c_str(), &seen) == 0 && (seen.st_mode & 07777) == (S_IRUSR | S_IWUSR | S_IXUSR), "permissions, with no set-user-ID");
    check(::stat((made / "empty").c_str(), &seen) == 0 && (seen.st_mode & 07777) == S_IRUSR, "a read-only file");
    check(::stat((made / "deep").c_str(), &seen) == 0 && (seen.st_mode & 07777) == (S_IRUSR | S_IXUSR), "a read-only folder, and it could still be filled");
    check(::stat((made / "deep" / "er" / "small.txt").c_str(), &seen) == 0 && seen.st_mtim.tv_sec == 2000, "a file's time");
    check(::stat((made / "deep" / "er").c_str(), &seen) == 0 && seen.st_mtim.tv_sec == 2000, "a folder's time");
    check(whole_text_of(from / "large.bin") == large, "the original is as it was");

    // Taken names, which nothing is allowed to replace.
    const auto again = run_copy(from, to);
    check(!again.has_value() && again.error().message.find("already in") != std::string::npos, "a folder whose name is taken");
    check(whole_text_of(made / "deep" / "er" / "small.txt") == "small", "and nothing changed");
    put_text(to / "same.txt", "keep me");
    put_text(top / "same.txt", "new");
    check(!run_copy(top / "same.txt", to).has_value() && whole_text_of(to / "same.txt") == "keep me", "a file whose name is taken");
    std::filesystem::create_symlink(top / "same.txt", to / "taken_by_a_link");
    put_text(top / "taken_by_a_link", "new");
    check(!run_copy(top / "taken_by_a_link", to).has_value(), "a name taken by a link");
    check(std::filesystem::is_symlink(to / "taken_by_a_link") && whole_text_of(top / "same.txt") == "new", "and what it points to is untouched");

    // Into itself, by text, and by what the folder really is, through a link the text cannot see.
    check(!start_local_copy(from.string(), from.string()).has_value(), "a folder into itself");
    check(!start_local_copy(from.string(), (from / "deep").string()).has_value(), "a folder into what is in it");
    std::filesystem::create_directories(top / "loop" / "inside");
    std::filesystem::create_directories(top / "other");
    std::filesystem::create_symlink(top / "loop" / "inside", top / "other" / "way_in");
    const auto looped = run_copy(top / "loop", top / "other" / "way_in");
    check(!looped.has_value() && looped.error().message == "A folder cannot go inside itself", "a folder into itself through a link");

    // A link at the top is copied as a link too, and never followed.
    std::filesystem::create_directories(top / "links_to");
    check(run_copy(from / "link", top / "links_to").has_value() && std::filesystem::is_symlink(top / "links_to" / "link"), "a link on its own");
    check(!start_local_copy((top / "nothing_here").string(), to.string()).has_value() || !run_copy(top / "nothing_here", to).has_value(), "something not there");
    check(!start_local_copy(from.string(), (top / "nothing_here").string()).has_value(), "into a folder not there");
    check(!start_local_copy("relative", to.string()).has_value() && !start_local_copy("/", to.string()).has_value(), "paths that cannot be used");

    // Stopped part way, by being destroyed, as closing the window does: no part of a file is left to look like a copy, and the original is whole.
    std::filesystem::create_directories(top / "stopped");
    {
        auto job = start_local_copy((from / "large.bin").string(), (top / "stopped").string());
        check(job.has_value(), "a copy started");
        check(job && job->step().has_value() && job->step().has_value(), "two steps");
        check(std::filesystem::exists(top / "stopped" / "large.bin"), "a part is there while it runs");
    }
    check(!std::filesystem::exists(top / "stopped" / "large.bin"), "and is gone when it stops");

    // A real failed write, under a limit on the size of a file.
    std::filesystem::create_directories(top / "limited");
    rlimit before{};
    check(::getrlimit(RLIMIT_FSIZE, &before) == 0, "the limit was read");
    const sighandler_t old_handler = std::signal(SIGXFSZ, SIG_IGN);
    rlimit small = before;
    small.rlim_cur = 1024 * 1024;
    check(::setrlimit(RLIMIT_FSIZE, &small) == 0, "a limit was set");
    const auto refused = run_copy(from / "large.bin", top / "limited");
    check(::setrlimit(RLIMIT_FSIZE, &before) == 0, "the limit was put back");
    static_cast<void>(std::signal(SIGXFSZ, old_handler));
    check(!refused.has_value(), "a write that fails stops it");
    check(!std::filesystem::exists(top / "limited" / "large.bin"), "and leaves no part");
    check(::chmod((from / "deep").c_str(), S_IRWXU) == 0 && ::chmod((made / "deep").c_str(), S_IRWXU) == 0, "the folders can be cleaned up");
}

// Changes one byte in place, keeping the file and its size.
void change_a_byte(const std::filesystem::path& path, std::int64_t at) {
    const int file = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    const std::array<char, 1> different{'y'};
    check(file >= 0 && detail::write_at<char>(file, std::span<const char>{different}, at).has_value() && ::close(file) == 0, "a byte was changed");
}

// A copy goes on until it has written everything, then what is done between that and the read back must be caught. The two ways in are the two things that can go wrong: the copy is
// not what was written, or the original is not what was read.
template <class Interfere>
void a_copy_that_does_not_match(const char* what, Interfere interfere, const bool copy_is_removed) {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top = std::filesystem::canonical(directory.path());
    std::filesystem::create_directories(top / "into");
    const std::string text(std::size_t{3} * 1024 * 1024 + 5, 'x');
    put_text(top / "original", text);
    auto job = start_local_copy((top / "original").string(), (top / "into").string());
    check(job.has_value(), what);
    if (!job) return;
    for (int i = 0; i < 100 && job->progress().bytes != text.size(); ++i) check(job->step().has_value(), what);
    check(job->progress().bytes == text.size(), what);
    interfere(top);
    bool failed = false;
    std::string message{};
    for (int i = 0; i < 100 && !failed; ++i) {
        auto stepped = job->step();
        if (!stepped) {
            failed = true;
            message = stepped.error().message;
        } else if (*stepped) {
            break;
        }
    }
    check(failed, what);
    check(job->progress().files == 0, what);
    job = std::unexpected(Error{ErrorCode::System, "ended"});  // what closes it and removes what it made
    check(std::filesystem::exists(top / "into" / "original") != copy_is_removed, what);
    check(std::filesystem::exists(top / "original"), what);
}

void checks_what_it_copied() {
    a_copy_that_does_not_match(
        "a byte of the copy changed", [](const std::filesystem::path& top) { change_a_byte(top / "into" / "original", std::int64_t{3} * 1024 * 1024 + 4); }, true);
    a_copy_that_does_not_match(
        "a byte of the original changed", [](const std::filesystem::path& top) { change_a_byte(top / "original", 0); }, true);
    a_copy_that_does_not_match(
        "the copy cut short", [](const std::filesystem::path& top) { std::filesystem::resize_file(top / "into" / "original", 10); }, true);
    a_copy_that_does_not_match(
        "the copy grown", [](const std::filesystem::path& top) { std::filesystem::resize_file(top / "into" / "original", std::size_t{3} * 1024 * 1024 + 6); }, true);
    a_copy_that_does_not_match(
        "the original grown", [](const std::filesystem::path& top) { std::filesystem::resize_file(top / "original", std::size_t{3} * 1024 * 1024 + 6); }, true);
    // What took the name is not this job's, so it stays.
    a_copy_that_does_not_match(
        "something else under the copy's name",
        [](const std::filesystem::path& top) {
            std::filesystem::remove(top / "into" / "original");
            put_text(top / "into" / "original", "someone else's");
        },
        false);

    // And a good one is private until it has matched, and then has what the original has.
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top = std::filesystem::canonical(directory.path());
    std::filesystem::create_directories(top / "into");
    put_text(top / "original", "whole");
    check(::chmod((top / "original").c_str(), S_IRUSR | S_IWUSR | S_IRGRP) == 0, "the original's permissions");
    auto job = start_local_copy((top / "original").string(), (top / "into").string());
    check(job.has_value(), "a good copy starts");
    if (!job) return;
    for (int i = 0; i < 100 && job->progress().bytes != 5; ++i) check(job->step().has_value(), "a good copy steps");
    const auto mode_of = [](const std::filesystem::path& path) { return std::filesystem::status(path).permissions(); };
    check(mode_of(top / "into" / "original") == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write), "private until it has matched");
    bool done = false;
    for (int i = 0; i < 100 && !done; ++i) {
        auto stepped = job->step();
        check(stepped.has_value(), "a good copy steps");
        done = stepped.has_value() && *stepped;
    }
    check(done && job->progress().files == 1 && job->progress().bytes_checked == 5, "a good copy matches");
    check(mode_of(top / "into" / "original") == mode_of(top / "original") && whole_text_of(top / "into" / "original") == "whole", "and then has what the original has");
}

// The whole way, through a real window's tree: a real paste that leaves the original, and says it matched.
template <class Tree>
void files_compare(Tree& tree) {
    static_cast<void>(lay_out(tree, Rect{Point{}, Size{Width{1400}, Height{400}}}));
    const Rect area = tree.template get<files::CompareButton>().bounds();
    const Point at{left_after(area.left() + Width{1}), top_after(area.top() + Height{1})};
    jgui_test::dispatch(tree, PointerPress{at, MouseButton::Left, jgui_test::serial(1)});
    jgui_test::dispatch(tree, PointerRelease{at, MouseButton::Left});
}

void copies_through_a_real_tree() {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top = std::filesystem::canonical(directory.path());
    std::filesystem::create_directories(top / "a");
    std::filesystem::create_directories(top / "b");
    put_text(top / "a" / "file.txt", "other");
    auto tree = files::make_ui(LocalFiles{}, files::StartFolder{(top / "a").string()}, files::StartFolder{(top / "b").string()});
    auto& transfers = tree.template get<files::Transfers>();
    transfers.copy({(top / "a" / "file.txt").string()});
    transfers.paste_into((top / "b").string());
    check(transfers.copying(), "a paste started");
    for (int i = 0; i < 100 && transfers.copying(); ++i) jgui_test::notify(tree, TimersDue{});
    check(!transfers.copying() && whole_text_of(top / "b" / "file.txt") == "other", "a paste through the tree");
    check(whole_text_of(top / "a" / "file.txt") == "other", "and the original stays");
    check(transfers.line().starts_with("Copied file.txt to ") && transfers.line().find("every file matched by SHA-256") != std::string_view::npos, "and the line says it matched");

    // Then the button, with one selected on each side: from the rows through the real disk to the popup.
    auto& popup = tree.template get<files::MatchPopup>();
    tree.template get<files::RightPane>().reload();
    tree.template get<FilePaneGrid<files::LeftPane>>().select_rows({0}, GridRow{0});
    tree.template get<FilePaneGrid<files::RightPane>>().select_rows({0}, GridRow{0});
    files_compare(tree);
    check(transfers.matching(), "a match started");
    for (int i = 0; i < 100 && !popup.is_open(); ++i) jgui_test::notify(tree, TimersDue{});
    check(popup.is_open() && popup.heading() == "They all match" && popup.findings().empty(), "and the popup says a copy matches");
    popup.close();

    // A byte changed underneath, and the same two now say so.
    change_a_byte(top / "b" / "file.txt", 0);
    files_compare(tree);
    for (int i = 0; i < 100 && !popup.is_open(); ++i) jgui_test::notify(tree, TimersDue{});
    check(popup.is_open() && popup.heading() == "1 thing does not match" && popup.findings().size() == 1 && popup.findings()[0].why == "The contents differ by SHA-256", "and that it no longer does");
}

struct RealMatch {
    bool done{};
    MatchProgress progress{};
    std::vector<MatchFinding> findings{};
};

[[nodiscard]] RealMatch match_real(const std::filesystem::path& first, const std::filesystem::path& second) {
    RealMatch out;
    auto job = start_local_match(first.string(), second.string());
    if (!job) return out;
    for (int i = 0; i < 10000; ++i) {
        auto stepped = job->step();
        if (!stepped) return out;
        if (*stepped) {
            out.done = true;
            break;
        }
    }
    out.progress = job->progress();
    out.findings.assign(job->findings().begin(), job->findings().end());
    return out;
}

[[nodiscard]] bool found_only(const RealMatch& result, std::string_view where, std::string_view why) {
    return result.done && result.progress.differences == 1 && result.findings.size() == 1 && result.findings[0].where == where && result.findings[0].why.contains(why);
}

void matches_real_things() {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top = std::filesystem::canonical(directory.path());
    const std::filesystem::path a = top / "a";
    const std::filesystem::path b = top / "b";
    const std::string large(std::size_t{3} * 1024 * 1024 + 5, 'x');
    for (const std::filesystem::path& side : {a, b}) {
        std::filesystem::create_directories(side / "sub" / "deeper");
        put_text(side / "large.bin", large);
        put_text(side / "empty", "");
        put_text(side / "sub" / "deeper" / "note.txt", "note");
        std::filesystem::create_symlink("somewhere/gone", side / "link");
    }
    // The same contents at another time still match: times are not what is asked.
    std::filesystem::last_write_time(b / "sub" / "deeper" / "note.txt", std::filesystem::file_time_type{});

    const RealMatch same = match_real(a, b);
    check(same.done && same.progress.differences == 0 && same.findings.empty(), "two copies of a tree match");
    check(same.progress.files == 3 && same.progress.links == 1 && same.progress.folders == 3 && same.progress.bytes == large.size() + 4, "and each thing was looked at");

    put_text(top / "another_name", "note");
    const RealMatch differently_named = match_real(a / "sub" / "deeper" / "note.txt", top / "another_name");
    check(differently_named.done && differently_named.progress.files == 1 && differently_named.progress.differences == 0, "two files of different names match");

    // A byte, the same size: only the sums can tell.
    change_a_byte(b / "large.bin", std::int64_t{3} * 1024 * 1024 + 4);
    check(found_only(match_real(a, b), "a and b/large.bin", "contents differ by SHA-256"), "a changed byte is found");
    std::filesystem::remove(b / "large.bin");
    put_text(b / "large.bin", large);
    check(match_real(a, b).progress.differences == 0, "and matches again once put right");

    // What is deep down, and on one side only, on either side.
    put_text(a / "sub" / "deeper" / "only_a", "");
    check(found_only(match_real(a, b), "a and b/sub/deeper/only_a", "Only in"), "a file only on the first side is found");
    std::filesystem::remove(a / "sub" / "deeper" / "only_a");
    std::filesystem::create_directories(b / "sub" / "only_b");
    check(found_only(match_real(a, b), "a and b/sub/only_b", "Only in"), "a folder only on the second side is found");
    std::filesystem::remove(b / "sub" / "only_b");

    // Sizes differ, kinds differ, links differ.
    std::filesystem::remove(b / "empty");
    put_text(b / "empty", "not");
    check(found_only(match_real(a, b), "a and b/empty", "sizes differ"), "a different size is found");
    std::filesystem::remove(b / "empty");
    put_text(b / "empty", "");
    std::filesystem::remove(b / "link");
    std::filesystem::create_symlink("somewhere/else", b / "link");
    check(found_only(match_real(a, b), "a and b/link", "different places"), "a link saying something else is found");
    std::filesystem::remove(b / "link");
    put_text(b / "link", "");
    check(found_only(match_real(a, b), "a and b/link", "same kind"), "a file for a link is found");
    std::filesystem::remove(b / "link");
    std::filesystem::create_symlink("somewhere/gone", b / "link");

    // A link is never followed: these say the same, so they match, and the file they point to is not counted.
    put_text(top / "target", "t");
    std::filesystem::create_symlink(top / "target", a / "to_target");
    std::filesystem::create_symlink(top / "target", b / "to_target");
    check(match_real(a, b).progress.differences == 0 && match_real(a, b).progress.files == 3, "what a link points to is not read");
    std::filesystem::remove(a / "to_target");
    std::filesystem::remove(b / "to_target");

    // Something with no contents to check is not a match.
    check(::mkfifo((a / "pipe").c_str(), S_IRUSR | S_IWUSR) == 0 && ::mkfifo((b / "pipe").c_str(), S_IRUSR | S_IWUSR) == 0, "two pipes were made");
    check(found_only(match_real(a, b), "a and b/pipe", "nothing that can be checked"), "two pipes are not called a match, and neither is opened");
    std::filesystem::remove(a / "pipe");
    std::filesystem::remove(b / "pipe");

    // One and the same thing is not two copies, so a person told it matched would lose it by deleting one.
    check(found_only(match_real(a, a), "a", "one and the same"), "a folder against itself is not a match");
    std::filesystem::create_hard_link(a / "empty", top / "hard");
    check(found_only(match_real(a / "empty", top / "hard"), "empty and hard", "one and the same"), "a hard link is not a copy");

    // Something unreadable is a finding, and the rest is still looked at. Root can read anything, so this is only asked when it means something.
    if (::geteuid() != 0) {
        std::filesystem::permissions(b / "sub" / "deeper" / "note.txt", std::filesystem::perms::none);
        const RealMatch shut = match_real(a, b);
        check(found_only(shut, "a and b/sub/deeper/note.txt", "Cannot open") && shut.progress.files == 2, "what cannot be read is found, and the rest is still checked");
        std::filesystem::permissions(b / "sub" / "deeper" / "note.txt", std::filesystem::perms::owner_all);
    }

    // Nothing was changed by any of it: a match only reads.
    check(match_real(a, b).progress.differences == 0 && whole_text_of(a / "sub" / "deeper" / "note.txt") == "note", "it left everything as it was");
    check(!start_local_match("/", (a / "empty").string()), "the top of the disk has no name to match under");
    check(!start_local_match("not/absolute", (a / "empty").string()), "a path a pane could not hold is refused");
}

struct RealCount {
    std::optional<DeleteCount> count{};
    std::string why{};
};

[[nodiscard]] RealCount count_real(const std::filesystem::path& path) {
    auto job = start_local_count(path.string());
    if (!job) return RealCount{std::nullopt, job.error().message};
    for (int i = 0; i < 100000; ++i) {
        auto stepped = job->step();
        if (!stepped) return RealCount{std::nullopt, stepped.error().message};
        if (*stepped) return RealCount{job->counted(), {}};
    }
    return RealCount{std::nullopt, "it never ended"};
}

[[nodiscard]] std::size_t things_in(const std::filesystem::path& folder) {
    std::size_t count = 0;
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator{folder, ignored}) {
        static_cast<void>(entry);
        ++count;
    }
    return count;
}

void counts_real_things() {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top = std::filesystem::canonical(directory.path());
    const std::filesystem::path outside = top / "outside";
    std::filesystem::create_directories(outside / "inside");
    put_text(outside / "keep.txt", "keep");
    const std::filesystem::path counted_here = top / "counted";
    std::filesystem::create_directories(counted_here / "sub" / "deeper");
    put_text(counted_here / "a.txt", "12345");
    put_text(counted_here / "sub" / "b.txt", "123");
    put_text(counted_here / "sub" / "deeper" / "c.txt", "1");
    put_text(counted_here / "sub" / "bad\xFFname", "22");  // no pane could show this name, and it still counts
    std::filesystem::create_symlink(outside, counted_here / "link_to_folder");
    std::filesystem::create_symlink(outside / "keep.txt", counted_here / "link_to_file");
    std::filesystem::create_symlink("/nowhere/at/all", counted_here / "broken_link");
    check(::mkfifo((counted_here / "pipe").c_str(), 0600) == 0, "a pipe was made");
    const RealCount counted = count_real(counted_here);
    check(counted.count.has_value(), "a folder can be counted");
    if (!counted.count) return;
    check(counted.count->files == 8, "four files, three links and a pipe");
    check(counted.count->folders == 3, "the folder and two inside");
    check(counted.count->bytes == 11, "the bytes of regular files");
    check(std::filesystem::exists(counted_here / "a.txt"), "the count left it all there");
    std::filesystem::create_symlink(outside, top / "lone_link");
    const RealCount lone = count_real(top / "lone_link");
    check(lone.count && lone.count->files == 1 && lone.count->folders == 0, "a link to a folder counts as one thing");
    // Something it cannot look into stops the count, so nobody is asked about a number that is short.
    if (::geteuid() != 0) {
        const std::filesystem::path shut = top / "shut";
        std::filesystem::create_directories(shut / "closed");
        put_text(shut / "closed" / "x", "x");
        std::filesystem::permissions(shut / "closed", std::filesystem::perms::none);
        const RealCount refused = count_real(shut);
        check(!refused.count && refused.why.contains("closed"), "a folder that cannot be read refuses the count");
        std::filesystem::permissions(shut / "closed", std::filesystem::perms::owner_all);
    }
    // Only counted here, so this test could never harm it. A mount inside a folder goes the same way, and needs privileges to set up.
    const RealCount proc = count_real("/proc");
    check(!proc.count && proc.why.contains("another mounted disk"), "a mounted disk is not gone into");
    check(!start_local_count("/") && !start_local_count("relative"), "and the top of everything, and what is not a plain path, are refused");
    check(!count_real(top / "missing").count, "and what is not there");
}

void moves_real_things_to_the_trash() {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top = std::filesystem::canonical(directory.path());
    const std::filesystem::path data = top / "made" / "on" / "demand";  // not the person's real trash, whatever this runs on
    const std::filesystem::path trash = data / "Trash";
    const std::filesystem::path outside = top / "outside";
    std::filesystem::create_directories(outside);
    put_text(outside / "keep.txt", "keep");

    // A folder, whole, with a link in it that stays a link, and a name that needs escaping.
    std::filesystem::create_directories(top / "a b" / "sub");
    put_text(top / "a b" / "sub" / "in.txt", "inner");
    std::filesystem::create_symlink(outside, top / "a b" / "link");
    check(move_to_trash_under((top / "a b").string(), data.string()).has_value(), "a folder goes to the trash");
    check(!std::filesystem::exists(std::filesystem::symlink_status(top / "a b")), "and has left where it was");
    check(whole_text_of(trash / "files" / "a b" / "sub" / "in.txt") == "inner", "with what was in it whole");
    check(std::filesystem::is_symlink(trash / "files" / "a b" / "link") && whole_text_of(outside / "keep.txt") == "keep", "and a link is still a link, with what it points to left alone");
    const std::string info = whole_text_of(trash / "info" / "a b.trashinfo");
    check(info.starts_with("[Trash Info]\nPath=" + trash_escaped_path((top / "a b").string()) + "\nDeletionDate=20") && info.ends_with("\n"), "and its info says where it came from and when");
    check(info.contains("%20"), "with the path escaped");

    // The same name again takes the next free one, and the first stays.
    put_text(top / "a b", "second");
    check(move_to_trash_under((top / "a b").string(), data.string()).has_value(), "a second of that name goes too");
    check(whole_text_of(trash / "files" / "a b.1") == "second" && whole_text_of(trash / "files" / "a b" / "sub" / "in.txt") == "inner", "beside the first");
    check(std::filesystem::exists(trash / "info" / "a b.1.trashinfo"), "with its own info");

    // A name left in files with no info is not replaced.
    put_text(trash / "files" / "orphan", "older");
    put_text(top / "orphan", "newer");
    check(move_to_trash_under((top / "orphan").string(), data.string()).has_value(), "a name taken in the trash is stepped over");
    check(whole_text_of(trash / "files" / "orphan") == "older" && whole_text_of(trash / "files" / "orphan.1") == "newer", "and what held it stays");
    check(!std::filesystem::exists(trash / "info" / "orphan.trashinfo") && std::filesystem::exists(trash / "info" / "orphan.1.trashinfo"), "with no info left for a move that did not happen");

    // A link alone goes as a link. A longest name still fits.
    std::filesystem::create_symlink(outside, top / "lone_link");
    check(move_to_trash_under((top / "lone_link").string(), data.string()).has_value() && std::filesystem::is_symlink(trash / "files" / "lone_link") && whole_text_of(outside / "keep.txt") == "keep", "a link goes as a link");
    const std::string longest(255, 'n');
    put_text(top / longest, "long");
    check(move_to_trash_under((top / longest).string(), data.string()).has_value() && things_in(trash / "files") == things_in(trash / "info") + 1, "the longest name fits, and only the orphan has no info");

    // What is refused stays where it is, and leaves nothing behind.
    const std::size_t infos = things_in(trash / "info");
    put_text(trash / "files" / "already", "x");
    check(!move_to_trash_under((trash / "files" / "already").string(), data.string()) && whole_text_of(trash / "files" / "already") == "x", "what is in the trash stays");
    check(!move_to_trash_under(data.string(), data.string()) && !move_to_trash_under((top / "made").string(), data.string()) && std::filesystem::exists(trash), "a folder holding the trash stays");
    check(!move_to_trash_under((top / "missing").string(), data.string()), "what is not there is refused");
    check(!move_to_trash_under("/", data.string()) && !move_to_trash_under("relative", data.string()), "and the top of everything, and what is not a plain path");
    check(things_in(trash / "info") == infos, "and none of those left an info file");

    // A trash someone else could write to, or that is a link, is not used, and the thing stays.
    const std::filesystem::path open_data = top / "open";
    std::filesystem::create_directories(open_data / "Trash");
    std::filesystem::permissions(open_data / "Trash", std::filesystem::perms::others_write, std::filesystem::perm_options::add);
    put_text(top / "stays.txt", "stays");
    check(!move_to_trash_under((top / "stays.txt").string(), open_data.string()) && whole_text_of(top / "stays.txt") == "stays", "a trash others can write to is refused");
    // A group may write to it, as on a disk shared with a group.
    const std::filesystem::path group_data = top / "group";
    std::filesystem::create_directories(group_data / "Trash");
    std::filesystem::permissions(group_data / "Trash", std::filesystem::perms::group_write, std::filesystem::perm_options::add);
    put_text(top / "goes.txt", "goes");
    check(move_to_trash_under((top / "goes.txt").string(), group_data.string()).has_value() && whole_text_of(group_data / "Trash" / "files" / "goes.txt") == "goes", "a trash its group can write to is used");
    const std::filesystem::path linked_data = top / "linked";
    std::filesystem::create_directories(linked_data);
    std::filesystem::create_symlink(outside, linked_data / "Trash");
    check(!move_to_trash_under((top / "stays.txt").string(), linked_data.string()) && whole_text_of(top / "stays.txt") == "stays" && !std::filesystem::exists(outside / "files"), "and so is one that is a link");

    // Another disk than the home trash's: the temporary folder's own, with a home trash in the folder the tests run in, when those differ and the temporary folder is the top of its disk. Its own trash is used, by
    // the specification's two ways, and the path is written from the top of that disk. Only what this made is taken away afterwards, because a trash already there is the person's.
    const std::filesystem::path disk_top = top.parent_path();
    std::string far_made = (std::filesystem::current_path() / "trash-test-XXXXXX").string();
    std::error_code no_shared;
    struct statx top_found {};
    const bool can_try = ::mkdtemp(far_made.data()) != nullptr && !std::filesystem::exists(disk_top / ".Trash", no_shared) && ::statx(AT_FDCWD, disk_top.c_str(), AT_NO_AUTOMOUNT, STATX_BASIC_STATS, &top_found) == 0 && (top_found.stx_attributes & STATX_ATTR_MOUNT_ROOT) != 0;
    struct stat near_disk {};
    struct stat far_disk {};
    if (can_try && ::access(disk_top.c_str(), W_OK) == 0 && ::stat(top.c_str(), &near_disk) == 0 && ::stat(far_made.c_str(), &far_disk) == 0 && near_disk.st_dev != far_disk.st_dev) {
        const std::string far_data = far_made;
        const std::string uid = std::to_string(::geteuid());
        const std::filesystem::path own_trash = disk_top / (".Trash-" + uid);
        const bool own_was_there = std::filesystem::exists(own_trash);
        const std::string stem = "jcppgui-test-" + top.filename().string();
        const auto named = [&](std::string_view ending) { return stem + std::string{ending}; };
        put_text(top / named("own"), "own");
        check(move_to_trash_under((top / named("own")).string(), far_data).has_value(), "the other disk's own trash is used");
        check(whole_text_of(own_trash / "files" / named("own")) == "own", "$top/.Trash-$uid");
        check(whole_text_of(own_trash / "info" / (named("own") + ".trashinfo")).contains("Path=" + top.filename().string() + "/" + named("own") + "\n"), "with the path from the top of that disk");

        // A shared trash that is not sticky is passed over, and a sticky one is used.
        const std::filesystem::path shared = disk_top / ".Trash";
        std::filesystem::create_directories(shared);
        std::filesystem::permissions(shared, std::filesystem::perms::owner_all);
        put_text(top / named("plain"), "plain");
        check(move_to_trash_under((top / named("plain")).string(), far_data).has_value() && std::filesystem::exists(own_trash / "files" / named("plain")), "a shared trash that is not sticky is passed over");
        std::filesystem::permissions(shared, std::filesystem::perms::all | std::filesystem::perms::sticky_bit);
        put_text(top / named("shared"), "shared");
        check(move_to_trash_under((top / named("shared")).string(), far_data).has_value(), "a sticky shared trash is used");
        check(whole_text_of(shared / uid / "files" / named("shared")) == "shared", "$top/.Trash/$uid");
        std::filesystem::remove_all(shared);
        for (const std::string& each : {named("own"), named("plain")}) {
            std::filesystem::remove(own_trash / "files" / each);
            std::filesystem::remove(own_trash / "info" / (each + ".trashinfo"));
        }
        if (!own_was_there) std::filesystem::remove_all(own_trash);
    } else {
        std::cout << "not tried here: a trash on another disk than the home trash's\n";
    }
    std::error_code no_far;
    std::filesystem::remove_all(far_made, no_far);

    // Through the real tree, with the real way of finding the trash: the buttons, the popup, and the real disk.
    check(::setenv("XDG_DATA_HOME", data.c_str(), 1) == 0, "the trash for this test is its own");
    const std::filesystem::path shown = top / "shown";
    std::filesystem::create_directories(shown / "folder");
    put_text(shown / "folder" / "in.txt", "abc");
    put_text(shown / "stays.txt", "stays");
    auto tree = files::make_ui(LocalFiles{}, files::StartFolder{shown.string()}, files::StartFolder{shown.string()});
    static_cast<void>(lay_out(tree, Rect{Point{}, Size{Width{1400}, Height{400}}}));
    auto& popup = tree.template get<files::DeletePopup>();
    auto& transfers = tree.template get<files::Transfers>();
    tree.template get<FilePaneGrid<files::LeftPane>>().select_row(GridRow{0});
    const auto click = [&](Rect area) {
        const Point at{left_after(area.left() + Width{1}), top_after(area.top() + Height{1})};
        jgui_test::dispatch(tree, PointerPress{at, MouseButton::Left, jgui_test::serial(1)});
        jgui_test::dispatch(tree, PointerRelease{at, MouseButton::Left});
    };
    click(tree.template get<FilePaneDeleteButton<files::LeftPane>>().bounds());
    for (int i = 0; i < 100 && !popup.is_open(); ++i) jgui_test::notify(tree, TimersDue{});
    check(popup.is_open() && popup.count_text() == "File count to be deleted: 1", "the popup says how many files");
    check(popup.what_text().starts_with("Also 1 folder, and 3 bytes"), "and what else");
    check(std::filesystem::exists(shown / "folder" / "in.txt"), "and it is all still there");
    static_cast<void>(lay_out(tree, Rect{Point{}, Size{Width{1400}, Height{400}}}));
    click(tree.template get<FileDeletePopupConfirm<files::DeletePopup>>().bounds());
    check(transfers.deleting(), "the button starts it");
    for (int i = 0; i < 100 && transfers.deleting(); ++i) jgui_test::notify(tree, TimersDue{});
    check(!std::filesystem::exists(shown / "folder") && whole_text_of(shown / "stays.txt") == "stays", "and what was selected has left, and nothing else");
    check(whole_text_of(trash / "files" / "folder" / "in.txt") == "abc", "and is in the trash");
    check(transfers.line() == "Moved to the trash: 1 file, 1 folder, 3 bytes", "and the line says so");
    jgui_test::notify(tree, TimersDue{});
    check(tree.template get<files::LeftPane>().rows().size() == 1 && tree.template get<files::RightPane>().rows().size() == 1, "and both sides have looked again");
    check(::unsetenv("XDG_DATA_HOME") == 0, "that is undone");
}

void touches_real_things() {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top = std::filesystem::canonical(directory.path());
    put_text(top / "file.txt", "unchanged");
    std::filesystem::create_symlink(top / "file.txt", top / "link");
    const auto long_ago = std::filesystem::file_time_type::clock::now() - std::chrono::hours{24 * 400};
    std::filesystem::last_write_time(top / "file.txt", long_ago);
    const auto recent = [&](const std::filesystem::path& path) { return std::filesystem::last_write_time(path) > long_ago + std::chrono::hours{24 * 399}; };
    const std::string folder = top.string();
    check(touch_local_file(folder, "link") == TouchOutcome::TimeSet, "a link can be touched");
    check(!recent(top / "file.txt"), "and what it points to keeps its time");
    check(touch_local_file(folder, "file.txt") == TouchOutcome::TimeSet && recent(top / "file.txt"), "a file gets the time of now");
    check(whole_text_of(top / "file.txt") == "unchanged", "and what is in it stays");
    check(touch_local_file(folder, "new file.txt") == TouchOutcome::Made && std::filesystem::is_regular_file(top / "new file.txt") && std::filesystem::file_size(top / "new file.txt") == 0, "a name that is not there is made, empty");
    check(touch_local_file(folder, "new file.txt") == TouchOutcome::TimeSet, "and the second time only has its time set");
    std::filesystem::create_symlink(top / "not_there_yet", top / "dangling");
    check(touch_local_file(folder, "dangling") == TouchOutcome::TimeSet && !std::filesystem::exists(top / "not_there_yet"), "a link pointing nowhere makes nothing where it points");
    check(!touch_local_file(folder, "a/b") && !touch_local_file(folder, "..") && !touch_local_file(folder, "") && !touch_local_file(folder, "a\nb"), "a name that could leave the folder or break a line is refused");
    check(!touch_local_file(folder, "../outside") && !std::filesystem::exists(top.parent_path() / "outside"), "and nothing is made outside it");
    check(!touch_local_file("relative", "x") && !touch_local_file((top / "missing").string(), "x") && !std::filesystem::exists(top / "missing"), "as is what is not a plain path, or a folder that is not there");
    std::filesystem::create_directory(top / "real_folder");
    std::filesystem::create_directory_symlink(top / "real_folder", top / "folder_link");
    check(!touch_local_file((top / "folder_link").string(), "x") && !std::filesystem::exists(top / "real_folder" / "x"), "a link standing where the folder should be is not followed");
}

void saves_real_pictures() {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top = std::filesystem::canonical(directory.path());
    const std::filesystem::path shots = top / "ScreenShots";
    const std::string folder = shots.string();
    const std::array<std::uint8_t, 3> bytes{1, 2, 3};
    const auto mode_of = [](const std::filesystem::path& path) { return std::filesystem::status(path).permissions(); };
    const auto saved = save_local_picture(folder, "a.png", bytes);
    check(saved == (shots / "a.png").string() && whole_text_of(shots / "a.png") == std::string{"\x01\x02\x03"}, "the folder is made, and the file holds the bytes");
    check(mode_of(shots) == std::filesystem::perms::owner_all, "the folder is private, because a picture can show private names");
    check(mode_of(shots / "a.png") == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write), "and so is the file");
    const std::array<std::uint8_t, 1> other{9};
    const auto taken = save_local_picture(folder, "a.png", other);
    check(!taken && taken.error().message.contains("already there") && whole_text_of(shots / "a.png") == std::string{"\x01\x02\x03"}, "a name that is taken is refused and replaces nothing");
    std::filesystem::create_symlink(top / "outside", shots / "link.png");
    check(!save_local_picture(folder, "link.png", bytes) && !std::filesystem::exists(top / "outside"), "a link standing at the name is not followed");
    check(!save_local_picture(folder, "a/b.png", bytes) && !save_local_picture(folder, "../b.png", bytes) && !std::filesystem::exists(top / "b.png"), "a name cannot leave the folder");
    put_text(top / "a_file", "x");
    check(!save_local_picture((top / "a_file").string(), "c.png", bytes), "a file standing where the folder should be is refused");

    const char* home_was = std::getenv("HOME");
    const std::string home_before = home_was == nullptr ? std::string{} : std::string{home_was};
    check(::setenv("HOME", top.c_str(), 1) == 0 && local_picture_folder() == folder, "the folder is ScreenShots under HOME");
    check(::setenv("HOME", "relative", 1) == 0 && !local_picture_folder(), "a HOME that is not absolute is not used");
    check(::unsetenv("HOME") == 0 && !local_picture_folder(), "and neither is none");
    check(::setenv("HOME", home_before.c_str(), 1) == 0, "HOME is put back");
}

void renames_real_things() {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top = std::filesystem::canonical(directory.path());
    put_text(top / "file.txt", "first");
    put_text(top / "taken.txt", "second");
    std::filesystem::create_directories(top / "folder" / "inside");
    std::filesystem::create_symlink("taken.txt", top / "link");
    const auto named = [&](const char* name) { return (top / name).string(); };
    check(rename_local_file(named("file.txt"), "new name.txt").has_value() && whole_text_of(top / "new name.txt") == "first" && !std::filesystem::exists(top / "file.txt"), "a file takes its new name");
    check(rename_local_file(named("folder"), "moved").has_value() && std::filesystem::is_directory(top / "moved" / "inside"), "a folder goes whole");
    check(rename_local_file(named("link"), "link2").has_value() && std::filesystem::read_symlink(top / "link2") == "taken.txt" && whole_text_of(top / "taken.txt") == "second", "a link is renamed itself, and what it points to stays");
    const auto refused = rename_local_file(named("new name.txt"), "taken.txt");
    check(!refused && refused.error().message.contains("already there"), "a name that is taken is refused");
    check(whole_text_of(top / "taken.txt") == "second" && whole_text_of(top / "new name.txt") == "first", "and neither is changed");
    check(!rename_local_file(named("new name.txt"), "moved") && !rename_local_file(named("moved"), "taken.txt"), "a folder's name or a file's, whichever way round");
    check(!rename_local_file(named("new name.txt"), "moved/in") && !std::filesystem::exists(top / "moved" / "in"), "a rename cannot move something into another folder");
    check(!rename_local_file(named("new name.txt"), "../out") && !std::filesystem::exists(top.parent_path() / "out"), "or out of this one");
    check(!rename_local_file(named("new name.txt"), "") && !rename_local_file(named("new name.txt"), ".") && !rename_local_file(named("new name.txt"), "a\nb") && std::filesystem::exists(top / "new name.txt"), "a name that could not show is refused");
    check(!rename_local_file(named("missing"), "x") && !std::filesystem::exists(top / "x"), "what is not there is refused");
    check(!rename_local_file("/", "x") && !rename_local_file("relative", "x"), "as is the top of everything, and what is not a plain path");
}

[[nodiscard]] std::string wait_for_text(const std::filesystem::path& path, std::string_view wanted) {
    std::string text;
    for (int i = 0; i < 1000; ++i) {
        std::error_code missing;
        if (std::filesystem::exists(path, missing)) {
            text = whole_text_of(path);
            if (text.contains(wanted)) return text;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return text;
}

// This program stands in for a terminal, named by TERMINAL: see the start of main. Each argument is what was asked for, in full, whatever a name holds, and no shell has read a name as a command.
void opens_a_real_terminal(const std::filesystem::path& this_program) {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top = std::filesystem::canonical(directory.path());
    const std::filesystem::path log = top / "arguments";
    const std::filesystem::path shown = top / "shown";
    const std::filesystem::path bait = top / "ran";
    const std::string awkward = "$(touch ran); `id` 'x' \"y\" ; -rf";
    put_text(top / awkward, "first contents\n");
    put_text(top / "second", "second contents\n");
    check(::setenv("TERMINAL", this_program.c_str(), 1) == 0 && ::setenv("JGUI_TEST_TERMINAL_LOG", log.c_str(), 1) == 0 && ::setenv("JGUI_TEST_TERMINAL_OUT", shown.c_str(), 1) == 0, "the stand-in is told where to write");

    const std::vector<std::string> cat_names{(top / awkward).string(), (top / "second").string()};
    check(run_in_local_terminal(top.string(), *terminal_tool_command(TerminalTool::Cat, cat_names, "")).has_value(), "a terminal starts");
    const std::string text = wait_for_text(shown, "Press Return");
    check(text.contains("first contents\nsecond contents\n"), "and cat shows the files, whatever they are called");
    check(!std::filesystem::exists(bait), "and nothing in a name ran");
    const std::string arguments = wait_for_text(log, "second");
    check(arguments.contains("-e\nsh\n-c\n") && arguments.contains("\n" + (top / awkward).string() + "\n"), "and the name went as one argument");
    check(arguments.contains("folder=" + top.string() + "\n"), "and it started in the file's folder");

    // The editor is only recorded: the stand-in does not run it.
    std::filesystem::remove(log);
    check(::setenv("JGUI_TEST_TERMINAL_RECORD_ONLY", "1", 1) == 0, "only recorded");
    check(::setenv("EDITOR", "sh -x", 1) == 0 && local_editor() == "sh -x", "EDITOR is read, when its program is installed");
    check(run_in_local_terminal(top.string(), *terminal_tool_command(TerminalTool::Edit, cat_names, local_editor())).has_value(), "a terminal starts for the editor");
    const std::string editor_arguments = wait_for_text(log, "second");
    check(editor_arguments.contains("-e\nsh\n-x\n--\n" + (top / awkward).string() + "\n"), "with the editor told where options end, so a name cannot be one");
    check(::unsetenv("EDITOR") == 0 && local_editor().empty(), "and with none it says so");

    // One that is not installed gives way to Vim, whether named alone or by a path, and whatever else it was told.
    const std::string path_before = std::getenv("PATH") == nullptr ? std::string{} : std::string{std::getenv("PATH")};
    const auto editor_given = [](const std::string& setting) {
        check(::setenv("EDITOR", setting.c_str(), 1) == 0, "an editor is set");
        return local_editor();
    };
    check(editor_given("jcppgui-no-such-editor -x").empty(), "a name not on PATH is as good as none");
    check(*terminal_tool_command(TerminalTool::Edit, cat_names, editor_given("jcppgui-no-such-editor")) == [&] { std::vector<std::string> words{"vim", "--"}; words.insert(words.end(), cat_names.begin(), cat_names.end()); return words; }(), "so Edit opens Vim");
    check(editor_given((top / "not_there").string()).empty(), "and so is a path to nothing");
    check(editor_given(top.string()).empty(), "or to a folder");
    put_text(top / "plain_file", "not a program");
    check(editor_given((top / "plain_file").string()).empty(), "or to a file that cannot run");
    put_text(top / "my-editor", "");
    std::filesystem::permissions(top / "my-editor", std::filesystem::perms::owner_all);
    check(editor_given((top / "my-editor").string() + " -x") == (top / "my-editor").string() + " -x", "a path to one that can run stays");
    check(::setenv("PATH", top.c_str(), 1) == 0 && editor_given("my-editor") == "my-editor", "as does a name in a folder of PATH");
    // A folder on PATH that is not absolute is wherever the program happens to be, so it is not looked in.
    const std::filesystem::path was_in = std::filesystem::current_path();
    std::filesystem::current_path(top);
    check(::setenv("PATH", ".:", 1) == 0 && editor_given("my-editor").empty(), "and a relative or empty folder of PATH is not looked in");
    check(::unsetenv("PATH") == 0 && editor_given("my-editor").empty(), "nor is anywhere, with no PATH at all");
    std::filesystem::current_path(was_in);
    check(::setenv("PATH", path_before.c_str(), 1) == 0, "PATH is put back");
    check(editor_given("a'b") == "a'b", "one that cannot be read stays, for Edit to refuse and say why");
    check(::unsetenv("EDITOR") == 0, "that is undone");

    // No terminal anywhere is said, rather than nothing happening.
    const std::string path_was = std::getenv("PATH") == nullptr ? std::string{} : std::string{std::getenv("PATH")};
    check(::setenv("TERMINAL", "jcppgui-no-such-terminal", 1) == 0 && ::setenv("PATH", top.c_str(), 1) == 0, "nothing to find");
    const auto none = run_in_local_terminal(top.string(), *terminal_tool_command(TerminalTool::Edit, cat_names, ""));
    check(!none && none.error().message.contains("terminal"), "no terminal is an error");
    check(::setenv("PATH", path_was.c_str(), 1) == 0 && ::unsetenv("TERMINAL") == 0 && ::unsetenv("JGUI_TEST_TERMINAL_RECORD_ONLY") == 0, "that is undone");
}

void diffs_real_things() {
    const jgui_test::TemporaryDirectory directory;
    const std::filesystem::path top = std::filesystem::canonical(directory.path());
    std::filesystem::create_directories(top / "folder");
    put_text(top / "a.txt", "one\ntwo\nthree\n");
    put_text(top / "b.txt", "one\n2\nthree\nfour\n");
    put_text(top / "zero.bin", std::string{"a\0b", 3});
    std::filesystem::create_symlink(top / "a.txt", top / "link.txt");
    check(::mkfifo((top / "pipe").c_str(), 0600) == 0, "a pipe was made");
    check(read_text_file((top / "a.txt").string()) == "one\ntwo\nthree\n", "a file is read whole");
    check(!read_text_file((top / "link.txt").string()), "a link is not followed");
    check(!read_text_file((top / "pipe").string()), "and a pipe is refused rather than waited on");
    check(!read_text_file((top / "folder").string()), "and a folder");
    check(!read_text_file((top / "missing").string()), "and what is not there");
    put_text(top / "big", std::string(most_diff_file_bytes + 1, 'x'));
    check(!read_text_file((top / "big").string()), "and what is too big");

    auto tree = files::make_ui(LocalFiles{}, files::StartFolder{(top / "folder").string()}, files::StartFolder{(top / "folder").string()});
    auto& transfers = tree.template get<files::Transfers>();
    auto& popup = tree.template get<files::DiffPopup>();
    transfers.start_diff((top / "a.txt").string(), (top / "b.txt").string());
    for (int i = 0; i < 100 && !popup.is_open(); ++i) jgui_test::notify(tree, TimersDue{});
    check(popup.is_open() && popup.heading() == "1 line only on the left, 2 lines only on the right", "two real files differ");
    popup.close();
    transfers.start_diff((top / "a.txt").string(), (top / "zero.bin").string());
    check(!transfers.diffing() && transfers.line().contains("not both text"), "and one that is not text is refused");
}

// The search against the longest common subsequence worked out the slow way, on many small texts of few different lines, so lines repeat as they do in real files. The rows must say what the texts say.
void diffs_agree_with_the_slow_way() {
    std::uint32_t seed = 12345;
    const auto next = [&](std::uint32_t below) {
        seed = seed * 1103515245U + 12345U;
        return ((seed >> 16U) & 0x7FFFU) % below;
    };
    const auto text_of = [&](std::vector<std::string>& lines) {
        const std::uint32_t count = next(25);
        const std::uint32_t different = next(5) + 1;
        std::string text;
        for (std::uint32_t i = 0; i < count; ++i) {
            lines.push_back(decimal_text(next(different), false));
            text += lines.back() + "\n";
        }
        return text;
    };
    bool all_agree = true;
    for (int round = 0; round < 3000 && all_agree; ++round) {
        std::vector<std::string> a;
        std::vector<std::string> b;
        const std::string first = text_of(a);
        const std::string second = text_of(b);
        std::vector<std::vector<std::size_t>> common(a.size() + 1, std::vector<std::size_t>(b.size() + 1, 0));
        for (std::size_t i = a.size(); i-- > 0;)
            for (std::size_t j = b.size(); j-- > 0;) common[i][j] = a[i] == b[j] ? common[i + 1][j + 1] + 1 : std::max(common[i + 1][j], common[i][j + 1]);
        const DiffOutcome out = diff_of(first, second);
        all_agree = !out.too_different && out.left_only == a.size() - common[0][0] && out.right_only == b.size() - common[0][0];
        std::uint64_t last_left = 0;
        std::uint64_t last_right = 0;
        for (const DiffRow& row : out.rows) {
            if (row.text.starts_with("...")) continue;
            if (row.left.known) {
                all_agree = all_agree && row.left.count > last_left && a[row.left.count - 1] == row.text;
                last_left = row.left.count;
            }
            if (row.right.known) {
                all_agree = all_agree && row.right.count > last_right && b[row.right.count - 1] == row.text;
                last_right = row.right.count;
            }
        }
    }
    check(all_agree, "the search finds the fewest changes, and its rows say what the texts say");
    check(the_search_has_a_limit(), "too many changes ends the search");
}

// Asked to, this program is a terminal for opens_a_real_terminal: it writes down what it was given and where it is, and runs the command with its output in a file and no input, so a read for Return ends at once.
[[nodiscard]] int run_everything(const std::filesystem::path& this_program) {
    reads_a_real_folder();
    copies_real_things();
    checks_what_it_copied();
    copies_through_a_real_tree();
    matches_real_things();
    counts_real_things();
    moves_real_things_to_the_trash();
    touches_real_things();
    renames_real_things();
    saves_real_pictures();
    opens_a_real_terminal(this_program);
    diffs_real_things();
    diffs_agree_with_the_slow_way();
    return check.finish();
}

[[nodiscard]] int stand_in_for_a_terminal(std::vector<std::string> arguments) {
    std::string text;
    std::array<char, 4096> folder{};
    if (::getcwd(folder.data(), folder.size()) != nullptr) text.append("folder=").append(folder.data()).append("\n");
    for (const std::string& argument : arguments) text.append(argument).append("\n");
    const std::filesystem::path log{std::getenv("JGUI_TEST_TERMINAL_LOG")};
    put_text(log, text);
    if (std::getenv("JGUI_TEST_TERMINAL_RECORD_ONLY") != nullptr) return 0;
    const int out = ::open(std::getenv("JGUI_TEST_TERMINAL_OUT"), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
    // Input that ends at once, so a read for Return does.
    std::array<int, 2> no_input{-1, -1};
    if (out < 0 || ::dup2(out, STDOUT_FILENO) < 0 || ::pipe(no_input.data()) != 0 || ::close(no_input[1]) != 0 || ::dup2(no_input[0], STDIN_FILENO) < 0) return 126;
    std::vector<char*> pointers;
    for (std::size_t i = 1; i < arguments.size(); ++i) pointers.push_back(arguments[i].data());
    pointers.push_back(nullptr);
    ::execvp(pointers.front(), pointers.data());
    return 127;
}

}  // namespace

int main(int argc, char** argv) {
    auto given = program_arguments(argc, argv);
    if (!given || given->empty()) return 2;
    if (given->size() > 2 && given->at(1) == "-e") return stand_in_for_a_terminal(std::vector<std::string>(given->begin() + 1, given->end()));
    return run_everything(std::filesystem::absolute(given->front()));
}
