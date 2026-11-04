// File: tests/test_files.cpp
// Purpose: The file manager. Names, paths, dates and sizes, then the panes in static_assert with a made-up disk, from events to state and pixels: above all that
//          what is done in one pane does nothing to the other, and what a copy or a drag asks of the disk. At run time, reading and copying against real folders, and copies that do not match.
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
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
#include <vector>

#include "check.hpp"
#include "file_io.hpp"
#include "files_ui.hpp"
#include "jCppGUI/v001/local_files.hpp"
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
    int steps_to_copy{3};
    int steps_taken{};
    int jobs_ended{};
    bool step_fails{};
    bool start_fails{};
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

struct FakeFiles {
    using CopyJob = FakeJob;
    FakeDisk* disk{};

    [[nodiscard]] static constexpr FileEntry folder(std::string_view name) { return FileEntry{std::string{name}, FileKind::Folder, {}, FileTime{true, 0}}; }
    [[nodiscard]] static constexpr FileEntry file(std::string_view name, std::uint64_t size) { return FileEntry{std::string{name}, FileKind::File, FileBytes{true, size}, FileTime{true, 0}}; }

    [[nodiscard]] static constexpr std::expected<DirectoryListing, Error> on_the_disk(const std::string& path) {
        if (path == "/") return DirectoryListing{{folder("locked"), folder("other"), folder("top")}, 0};
        if (path == "/top") return DirectoryListing{{folder("one"), folder("two"), file("note.txt", 5)}, 1};
        if (path == "/top/one") return DirectoryListing{{folder("inner"), file("f1", 1000)}, 0};
        if (path == "/top/one/inner") return DirectoryListing{{file("f2", 2)}, 0};
        if (path == "/top/two") return DirectoryListing{{file("f3", 3)}, 0};
        if (path == "/other") return DirectoryListing{{file("g1", 7)}, 0};
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
    if (transfers_of(tree).line() != "Compared what shows: 3 only on the left, 1 only on the right, 0 different, 0 same size and time. Contents were not read, and a closed folder was not looked inside.") return false;
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
}

}  // namespace

int main() {
    reads_a_real_folder();
    copies_real_things();
    checks_what_it_copied();
    copies_through_a_real_tree();
    return check.finish();
}
