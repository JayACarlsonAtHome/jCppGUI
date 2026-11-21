// File: include/jCppGUI/v001/file_pane.hpp
// Purpose: FilePane<Tag, Source, Transfers>: one folder as a tree that can be opened and closed, with its own buttons, path, status and grid. Every part's tag is made from
//          the pane's own, so two panes in a window hold nothing in common, and neither knows the other is there. What goes between them goes through the FileTransfers it
//          is told the tag of, and when that says something changed, each pane looks again by itself. jgui.hpp does not include it.
#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "button.hpp"
#include "copy_line.hpp"
#include "datagrid.hpp"
#include "file_listing.hpp"
#include "file_transfers.hpp"
#include "label.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

inline constexpr std::uint32_t most_tree_depth = 128;

enum class TreeMark : std::uint8_t { None, Closed, Open };

inline constexpr std::string_view tree_indent_text = "    ";
inline constexpr std::string_view tree_closed_text = "[+] ";
inline constexpr std::string_view tree_open_text = "[-] ";

// A name in a tree: how deep it is and whether it can be opened. Names sort as names, wherever in the tree they are.
struct TreeName {
    std::string text{};
    std::uint32_t depth{};
    TreeMark mark{TreeMark::None};

    friend constexpr bool operator==(const TreeName& a, const TreeName& b) noexcept { return a.text == b.text; }
    friend constexpr std::strong_ordering operator<=>(const TreeName& a, const TreeName& b) noexcept { return compare_file_names(a.text, b.text) <=> 0; }
};

template <>
struct CellFormat<TreeName> {
    // The same room before every name at one depth, marked or not, so the names line up.
    [[nodiscard]] static constexpr std::string text(const TreeName& name) {
        std::string out;
        out.reserve(tree_indent_text.size() * (name.depth + 1) + name.text.size());
        for (std::uint32_t i = 0; i < name.depth; ++i) out.append(tree_indent_text);
        switch (name.mark) {
            case TreeMark::Closed: out.append(tree_closed_text); break;
            case TreeMark::Open: out.append(tree_open_text); break;
            case TreeMark::None:
            default: out.append(tree_indent_text); break;
        }
        out.append(name.text);
        return out;
    }
};

struct FileRow {
    TreeName name{};
    FileBytes size{};
    FileTime modified{};
    FileKind kind{FileKind::Unknown};
    FileMatch match{FileMatch::NotCompared};
};

// Each part of a pane is named by the pane's own tag.
template <class Pane>
struct FilePaneGrid;
template <class Pane>
struct FilePanePath;
template <class Pane>
struct FilePaneStatus;
template <class Pane>
struct FilePaneUpButton;
template <class Pane>
struct FilePaneOpenButton;
template <class Pane>
struct FilePaneReloadButton;
template <class Pane>
struct FilePaneCopyButton;
template <class Pane>
struct FilePanePasteButton;
template <class Pane>
struct FilePaneDeleteButton;
template <class Pane>
struct FilePaneCatButton;
template <class Pane>
struct FilePaneTouchButton;
template <class Pane>
struct FilePaneEditButton;
template <class Pane>
struct FilePaneRenameButton;

inline constexpr std::size_t file_name_column = 0;
inline constexpr std::size_t file_size_column = 1;

template <class Pane>
using FileGrid = DataGrid<FilePaneGrid<Pane>, FileRow, NoGridMessage, Column<"Name", &FileRow::name, 180>, Column<"Size", &FileRow::size, 100>, Column<"Modified UTC", &FileRow::modified, 130>, Column<"Kind", &FileRow::kind, 50>, Column<"Compared", &FileRow::match, 120>>;

namespace detail {
template <class Pane>
struct FilePaneGoUp {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Pane>().go_up();
    }
};
template <class Pane>
struct FilePaneOpenSelected {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Pane>().open_selected();
    }
};
template <class Pane>
struct FilePaneReload {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Pane>().reload();
    }
};
template <class Pane>
struct FilePaneCopy {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Pane>().copy_selected(u);
    }
};
template <class Pane>
struct FilePanePaste {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Pane>().paste_here(u);
    }
};

template <class Pane>
struct FilePaneDelete {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Pane>().delete_selected(u);
    }
};

template <class Pane>
struct FilePaneCat {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Pane>().open_selected_in_terminal(u, TerminalTool::Cat);
    }
};
template <class Pane>
struct FilePaneEdit {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Pane>().open_selected_in_terminal(u, TerminalTool::Edit);
    }
};
template <class Pane>
struct FilePaneTouch {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Pane>().touch_here(u);
    }
};
template <class Pane>
struct FilePaneRename {
    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<Pane>().rename_selected(u);
    }
};

[[nodiscard]] constexpr std::string count_of_things(std::size_t count) {
    std::string text = decimal_text(count, false);
    text.append(count == 1 ? " thing" : " things");
    return text;
}
}  // namespace detail

template <class Tag, FileSource Source, class Transfers>
class FilePane {
    using UpButton = Button<FilePaneUpButton<Tag>, detail::FilePaneGoUp<Tag>>;
    using OpenButton = Button<FilePaneOpenButton<Tag>, detail::FilePaneOpenSelected<Tag>>;
    using ReloadButton = Button<FilePaneReloadButton<Tag>, detail::FilePaneReload<Tag>>;
    using CopyButton = Button<FilePaneCopyButton<Tag>, detail::FilePaneCopy<Tag>>;
    using PasteButton = Button<FilePanePasteButton<Tag>, detail::FilePanePaste<Tag>>;
    using DeleteButton = Button<FilePaneDeleteButton<Tag>, detail::FilePaneDelete<Tag>>;
    using CatButton = Button<FilePaneCatButton<Tag>, detail::FilePaneCat<Tag>>;
    using TouchButton = Button<FilePaneTouchButton<Tag>, detail::FilePaneTouch<Tag>>;
    using EditButton = Button<FilePaneEditButton<Tag>, detail::FilePaneEdit<Tag>>;
    using RenameButton = Button<FilePaneRenameButton<Tag>, detail::FilePaneRename<Tag>>;
    using PathLabel = CopyLine<FilePanePath<Tag>>;
    using StatusLabel = CopyLine<FilePaneStatus<Tag>>;
    using Grid = FileGrid<Tag>;

public:
    using tag = Tag;
    using children_type = std::tuple<UpButton, OpenButton, ReloadButton, CopyButton, PasteButton, DeleteButton, RenameButton, CatButton, TouchButton, EditButton, PathLabel, StatusLabel, Grid>;

    constexpr explicit FilePane(Source source)
        : source_{std::move(source)},
          children_{UpButton{"Up", detail::FilePaneGoUp<Tag>{}}, OpenButton{"Show this folder", detail::FilePaneOpenSelected<Tag>{}}, ReloadButton{"Reload", detail::FilePaneReload<Tag>{}},
                    CopyButton{"Copy", detail::FilePaneCopy<Tag>{}}, PasteButton{"Paste", detail::FilePanePaste<Tag>{}}, DeleteButton{"Delete", detail::FilePaneDelete<Tag>{}}, RenameButton{"Rename", detail::FilePaneRename<Tag>{}},
                    CatButton{"Cat", detail::FilePaneCat<Tag>{}}, TouchButton{"Touch", detail::FilePaneTouch<Tag>{}}, EditButton{"Edit", detail::FilePaneEdit<Tag>{}},
                    PathLabel{"No folder"}, StatusLabel{"Nothing read yet"}, Grid{}} {
        // The column is there, so this is not refused.
        static_cast<void>(grid().set_column_align(file_size_column, ColumnAlign::AtRight));
    }
    FilePane(const FilePane&) = delete;
    FilePane& operator=(const FilePane&) = delete;
    constexpr FilePane(FilePane&&) = default;
    FilePane& operator=(FilePane&&) = delete;
    constexpr ~FilePane() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }
    [[nodiscard]] constexpr std::string_view folder() const noexcept { return folder_; }
    [[nodiscard]] constexpr std::span<const FileRow> rows() const noexcept { return grid().rows(); }
    [[nodiscard]] constexpr std::string_view status() const noexcept { return std::get<StatusLabel>(children_).text(); }

    // Shows this folder at the top of the tree. One that cannot be read changes nothing but the status line.
    constexpr void open(std::string path) {
        if (!file_path_can_show(path)) {
            say("That path cannot be used");
            return;
        }
        auto listing = source_.read(path);
        if (!listing) {
            say(listing.error().message.units());
            return;
        }
        folder_ = std::move(path);
        std::vector<FileRow> rows;
        rows.reserve(listing->entries.size());
        for (FileEntry& entry : listing->entries) rows.push_back(row_of(std::move(entry), 0));
        const std::size_t count = rows.size();
        // A source gives no more than this, but one is not trusted to, and an old tree stays rather than half a new one.
        if (count > max_grid_rows) {
            say("That folder holds more than a pane can");
            return;
        }
        static_cast<void>(grid().set_rows(std::move(rows)));  // within the limit, as checked
        static_cast<void>(std::get<PathLabel>(children_).set_text(utf8_prefix(folder_, max_caption_bytes)));  // a path that can be shown, cut to fit
        say_counts(count, listing->left_out);
    }

    // A reason from outside this pane, such as why there is no folder to start in.
    constexpr void say(std::string_view text) {
        // Cut to fit, and an error's text is our own and a path that was given, so this is refused only for a path that cannot be shown, and the line then says so.
        if (!std::get<StatusLabel>(children_).set_text(utf8_prefix(text, max_caption_bytes))) static_cast<void>(std::get<StatusLabel>(children_).set_text("This reason cannot be shown"));
    }

    constexpr void go_up() {
        if (folder_.empty() || folder_ == "/") return;
        open(parent_of_path(folder_));
    }

    // The folder selected becomes the top of the tree.
    constexpr void open_selected() {
        const std::optional<GridRow> row = grid().selected();
        if (!row || rows()[row->index].kind != FileKind::Folder) {
            say("Select a folder first");
            return;
        }
        open(path_of(row->index));
    }

    // What shows on this side, sorted by where it is below the top. Empty with no folder.
    [[nodiscard]] constexpr std::vector<ComparedItem> items_to_compare() const {
        std::vector<ComparedItem> items;
        items.reserve(rows().size());
        const std::size_t skip = folder_ == "/" ? folder_.size() : folder_.size() + 1;
        for (std::size_t i = 0; i < rows().size(); ++i) {
            const FileRow& row = rows()[i];
            items.push_back(ComparedItem{path_of(i).substr(skip), row.kind, row.size, row.modified, row.name.mark == TreeMark::Closed});
        }
        std::ranges::sort(items, {}, &ComparedItem::relative);
        return items;
    }

    // Marks each row with what it is against the other side. Reading again takes the marks away, because they may no longer be true.
    constexpr FileMatchCounts compare_with(std::span<const ComparedItem> there) {
        FileMatchCounts counts;
        std::vector<FileRow> changed{rows().begin(), rows().end()};
        const std::size_t skip = folder_ == "/" ? folder_.size() : folder_.size() + 1;
        for (std::size_t i = 0; i < changed.size(); ++i) {
            FileRow& row = changed[i];
            row.match = match_against(ComparedItem{path_of(i).substr(skip), row.kind, row.size, row.modified, row.name.mark == TreeMark::Closed}, there);
            switch (row.match) {
                case FileMatch::OnlyHere: ++counts.only_here; break;
                case FileMatch::NewerHere:
                case FileMatch::OlderHere:
                case FileMatch::SizeDiffers:
                case FileMatch::KindDiffers: ++counts.differ; break;
                case FileMatch::SameSizeAndTime: ++counts.same; break;
                case FileMatch::InBoth: ++counts.in_both; break;
                case FileMatch::NotCompared:
                default: ++counts.not_looked_at; break;
            }
        }
        replace_rows(std::move(changed), selection_mapped([](std::size_t row) { return row; }));
        return counts;
    }

    // Reads everything again, with what was open still open where it is still there, and says how much there is.
    constexpr void reload() {
        if (look_again()) say_counts(rows().size(), 0);
    }

    // Copy: remembers what is selected, for a Paste on either side.
    template <class U>
    constexpr void copy_selected(U& ui) {
        std::vector<std::string> paths = selected_paths();
        if (paths.empty()) {
            say("Select something to copy first");
            return;
        }
        ui.template get<Transfers>().copy(std::move(paths));
    }

    // Delete: only what is selected on this side. The transfers count it, and a person is asked before it goes to the trash.
    template <class U>
    constexpr void delete_selected(U& ui) {
        std::vector<std::string> paths = selected_paths();
        if (paths.empty()) {
            say("Select something to delete first");
            return;
        }
        ui.template get<Transfers>().start_deleting(std::move(paths));
    }

    // Cat and Edit: what is selected on this side, all of it files or links to them. A folder or a pipe is refused, because a terminal would only sit and wait on one.
    template <class U>
    constexpr void open_selected_in_terminal(U& ui, TerminalTool tool) {
        std::vector<std::string> paths;
        for (const std::uint32_t row : grid().selected_rows()) {
            if (rows()[row].kind != FileKind::File && rows()[row].kind != FileKind::Link) {
                say("Only files can be shown or edited, and " + rows()[row].name.text + " is not one");
                return;
            }
            paths.push_back(path_of(row));
        }
        if (paths.empty()) {
            say("Select a file first");
            return;
        }
        ui.template get<Transfers>().open_in_terminal(tool, paths);
    }

    // Touch: asks for a name, for the same folder a paste would go into.
    template <class U>
    constexpr void touch_here(U& ui) {
        const std::optional<GridRow> row = grid().selected();
        std::string into = row ? folder_for_row(row->index) : folder_;
        if (into.empty()) {
            say("This side has no folder to touch in");
            return;
        }
        ui.template get<Transfers>().ask_touch(std::move(into));
    }

    // Rename: exactly one thing selected on this side, because several cannot take one name.
    template <class U>
    constexpr void rename_selected(U& ui) {
        const std::span<const std::uint32_t> chosen = grid().selected_rows();
        if (chosen.size() != 1) {
            say("Select one thing to rename");
            return;
        }
        ui.template get<Transfers>().ask_rename(path_of(chosen.front()));
    }

    // What is selected, in tree order. Something inside a selected folder is left out, because it goes with that folder.
    [[nodiscard]] constexpr std::vector<SelectedThing> selected_things() const {
        std::vector<SelectedThing> things;
        std::size_t inside_until = 0;
        for (const std::uint32_t row : grid().selected_rows()) {
            if (row < inside_until) continue;
            things.push_back(SelectedThing{path_of(row), rows()[row].kind});
            const std::uint32_t depth = rows()[row].name.depth;
            inside_until = row + std::size_t{1};
            while (inside_until < rows().size() && rows()[inside_until].name.depth > depth) ++inside_until;
        }
        return things;
    }

    [[nodiscard]] constexpr std::vector<std::string> selected_paths() const {
        std::vector<std::string> paths;
        for (SelectedThing& thing : selected_things()) paths.push_back(std::move(thing.path));
        return paths;
    }

    // True when nothing selected on this side is anything but a file or a link.
    [[nodiscard]] constexpr bool selected_are_files() const noexcept {
        return std::ranges::all_of(grid().selected_rows(), [&](std::uint32_t row) noexcept { return rows()[row].kind == FileKind::File || rows()[row].kind == FileKind::Link; });
    }

    // Paste: into the selected folder, or the folder the selected row is in, or with nothing selected the folder at the top.
    template <class U>
    constexpr void paste_here(U& ui) {
        const std::optional<GridRow> row = grid().selected();
        const std::string into = row ? folder_for_row(row->index) : folder_;
        if (into.empty()) {
            say("This side has no folder to paste into");
            return;
        }
        ui.template get<Transfers>().paste_into(into);
    }

    // Opens a closed folder or closes an open one. A row that is neither is left alone.
    constexpr void toggle(std::size_t index) {
        if (index >= rows().size()) return;
        const TreeMark mark = rows()[index].name.mark;
        if (mark == TreeMark::Closed) open_in_tree(index);
        if (mark == TreeMark::Open) close_in_tree(index);
    }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& text) {
        bounds_ = area;
        const Height strip_height = text.line_height() + theme::button_pad_y + theme::button_pad_y + theme::bar_pad_y + theme::bar_pad_y;
        strip_ = area.intersect(Rect{area.origin(), Size{area.width(), strip_height}});
        const Width end_x = padding_of(text).bar_x;
        std::expected<Rect, Error> room{strip_.inset(end_x, theme::bar_pad_y)};
        const auto place_button = [&](auto& button) {
            if (room) room = button.layout(key, *room, text);
        };
        place_button(std::get<UpButton>(children_));
        place_button(std::get<OpenButton>(children_));
        place_button(std::get<ReloadButton>(children_));
        place_button(std::get<CopyButton>(children_));
        place_button(std::get<PasteButton>(children_));
        place_button(std::get<DeleteButton>(children_));
        place_button(std::get<RenameButton>(children_));
        place_button(std::get<CatButton>(children_));
        place_button(std::get<TouchButton>(children_));
        place_button(std::get<EditButton>(children_));
        if (!room) return std::unexpected(std::move(room.error()));
        report_row(text, strip_.width() - end_x - end_x, std::get<UpButton>(children_), std::get<OpenButton>(children_), std::get<ReloadButton>(children_),
                   std::get<CopyButton>(children_), std::get<PasteButton>(children_), std::get<DeleteButton>(children_), std::get<RenameButton>(children_),
                   std::get<CatButton>(children_), std::get<TouchButton>(children_), std::get<EditButton>(children_));
        std::expected<Rect, Error> below{Rect{Point{area.left(), top_after(strip_.bottom())}, Size{area.width(), area.height() - strip_.height()}}};
        const auto place_below = [&](auto& part) {
            if (below) below = part.layout(key, *below, text);
        };
        place_below(std::get<PathLabel>(children_));
        place_below(std::get<StatusLabel>(children_));
        place_below(grid());
        if (!below) return std::unexpected(std::move(below.error()));
        measure_marks(text);
        return Rect{Point{area.left(), top_after(area.bottom())}, Size{area.width(), Height{}}};
    }

    constexpr void paint(const TreeKey& key, Canvas& canvas) {
        constexpr Height one_down{1};
        Canvas local = canvas.clipped(bounds_);
        local.fill_rect(strip_, theme::bar);
        local.fill_rect(Rect{Point{strip_.left(), strip_.bottom() - one_down}, Size{strip_.width(), one_down}}, theme::bar_edge);
        detail::paint_in_order(children_, key, local);
    }

    // The grid selects the row and takes the focus as it always does, and a press on a row's mark then opens or closes it.
    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerPress& event) {
        const bool taken = detail::deliver(children_, key, ui, event);
        if (!taken || event.button != MouseButton::Left) return taken;
        const std::optional<GridRow> row = grid().row_at(event.position);
        if (!row) return taken;
        // With Ctrl or Shift it is only a change to what is selected.
        if (event.modifiers.holds(Modifier::Ctrl) || event.modifiers.holds(Modifier::Shift)) return taken;
        if (on_the_mark(rows()[row->index], event.position)) {
            toggle(row->index);
        } else {
            // A press on a row may turn out to be the start of a drag, of everything selected, so this holds the pointer until it is let go, wherever that is.
            pressed_on_row_ = true;
            drag_seen_ = false;
            ui.template get<Transfers>().arm_drag(selected_paths(), event.position);
        }
        return taken;
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerMove& event) {
        auto& transfers = ui.template get<Transfers>();
        if (transfers.dragging()) {
            drag_seen_ = true;
            if (grid().bounds().contains(event.position)) {
                transfers.drag_over(bounds_, folder_at(event.position));
            } else {
                transfers.drag_left(bounds_);
            }
        }
        return detail::deliver(children_, key, ui, event);
    }

    // The pane the drag started in ends it. The pane under the pointer has already said where it would go.
    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerRelease& event) {
        if (event.button == MouseButton::Left && pressed_on_row_ && drag_seen_) grid().keep_selection_on_release();
        const bool taken = detail::deliver(children_, key, ui, event);
        if (event.button != MouseButton::Left || !pressed_on_row_) return taken;
        pressed_on_row_ = false;
        ui.template get<Transfers>().finish_drag();
        return true;
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const HoldsCapture& event) {
        const bool children_hold = detail::deliver(children_, key, ui, event);
        return children_hold || pressed_on_row_;
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const KeyEvent& event) {
        if (grid().focused() && event.pressed && act_on_key(ui, event)) return true;
        return detail::deliver(children_, key, ui, event);
    }

    template <class U, class E>
    constexpr bool handle(const TreeKey& key, U& ui, const E& event) {
        return detail::deliver(children_, key, ui, event);
    }

    // The pointer went while a drag was on, so nothing is copied.
    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const PointerLeave& event) {
        if (pressed_on_row_) ui.template get<Transfers>().stop_drag();
        pressed_on_row_ = false;
        detail::notify_all(children_, key, ui, event);
    }

    // Something on a disk changed through the transfers, on this side or the other, so this side looks again.
    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const TimersDue& event) {
        const std::uint64_t changes = ui.template get<Transfers>().changes();
        if (changes != changes_seen_) {
            changes_seen_ = changes;
            if (!folder_.empty()) static_cast<void>(look_again());
        }
        detail::notify_all(children_, key, ui, event);
    }

    template <class U, class E>
    constexpr void notify(const TreeKey& key, U& ui, const E& event) {
        detail::notify_all(children_, key, ui, event);
    }

private:
    [[nodiscard]] constexpr Grid& grid() noexcept { return std::get<Grid>(children_); }
    [[nodiscard]] constexpr const Grid& grid() const noexcept { return std::get<Grid>(children_); }

    [[nodiscard]] static constexpr FileRow row_of(FileEntry entry, std::uint32_t depth) {
        const TreeMark mark = entry.kind == FileKind::Folder ? TreeMark::Closed : TreeMark::None;
        return FileRow{TreeName{std::move(entry.name), depth, mark}, entry.size, entry.modified, entry.kind};
    }

    // The rows stay in tree order, whatever order they show in, so what a row is inside is the nearest row before it that is less deep.
    [[nodiscard]] constexpr std::string path_of(std::size_t index) const {
        std::vector<std::string_view> names{rows()[index].name.text};
        std::uint32_t depth = rows()[index].name.depth;
        for (std::size_t at = index; at-- > 0 && depth > 0;) {
            if (rows()[at].name.depth < depth) {
                depth = rows()[at].name.depth;
                names.emplace_back(rows()[at].name.text);
            }
        }
        std::string path = folder_;
        for (std::size_t at = names.size(); at-- > 0;) path = child_of_path(path, names[at]);
        return path;
    }

    constexpr void open_in_tree(std::size_t index) {
        const std::uint32_t depth = rows()[index].name.depth;
        if (depth + 1 >= most_tree_depth) {
            say("That folder is too deep for a pane");
            return;
        }
        const std::string path = path_of(index);
        if (!file_path_can_show(path)) {
            say("That folder's path is too long for a pane");
            return;
        }
        auto listing = source_.read(path);
        if (!listing) {
            say(listing.error().message.units());
            return;
        }
        if (listing->entries.size() > max_grid_rows - rows().size()) {
            say("Opening that folder would be more than a pane can hold");
            return;
        }
        std::vector<FileRow> changed;
        changed.reserve(rows().size() + listing->entries.size());
        changed.insert(changed.end(), rows().begin(), rows().begin() + static_cast<std::ptrdiff_t>(index) + 1);
        changed.back().name.mark = TreeMark::Open;
        const std::size_t added = listing->entries.size();
        for (FileEntry& entry : listing->entries) changed.push_back(row_of(std::move(entry), depth + 1));
        changed.insert(changed.end(), rows().begin() + static_cast<std::ptrdiff_t>(index) + 1, rows().end());
        // Rows after this one move down by what came in, and the selection stays on its own row.
        replace_rows(std::move(changed), selection_mapped([&](std::size_t row) { return row > index ? row + added : row; }));
        say_counts(added, listing->left_out);
    }

    constexpr void close_in_tree(std::size_t index) {
        const std::uint32_t depth = rows()[index].name.depth;
        std::size_t after = index + 1;
        while (after < rows().size() && rows()[after].name.depth > depth) ++after;
        std::vector<FileRow> changed;
        changed.reserve(rows().size() - (after - index - 1));
        changed.insert(changed.end(), rows().begin(), rows().begin() + static_cast<std::ptrdiff_t>(index) + 1);
        changed.back().name.mark = TreeMark::Closed;
        changed.insert(changed.end(), rows().begin() + static_cast<std::ptrdiff_t>(after), rows().end());
        // Rows after what went move up. A selected row that went with the folder selects the folder.
        replace_rows(std::move(changed), selection_mapped([&](std::size_t row) {
            if (row <= index) return row;
            return row < after ? index : row - (after - index - 1);
        }));
    }

    // New rows clear a grid's sort and selection, and here the person has only opened or closed a folder, so both come back.
    struct RowSelection {
        std::vector<std::uint32_t> rows{};
        std::optional<GridRow> current{};
    };

    // What is selected, each row where it will be.
    template <class Map>
    [[nodiscard]] constexpr RowSelection selection_mapped(Map map) const {
        RowSelection selection;
        for (const std::uint32_t row : grid().selected_rows()) selection.rows.push_back(static_cast<std::uint32_t>(map(std::size_t{row})));
        if (const std::optional<GridRow> current = grid().selected(); current) selection.current = GridRow{map(current->index)};
        return selection;
    }

    constexpr void replace_rows(std::vector<FileRow> changed, RowSelection selected) {
        const std::span<const GridSortKey> current = grid().sort_keys();
        std::array<GridSortKey, grid_most_sort_keys> keys{};
        std::ranges::copy(current, keys.begin());
        const std::size_t key_count = current.size();
        static_cast<void>(grid().set_rows(std::move(changed)));  // within the limit, as each caller checked
        static_cast<void>(grid().set_sort_keys(std::span<const GridSortKey>{keys}.first(key_count)));  // the keys it just had
        grid().select_rows(std::move(selected.rows), selected.current);
    }

    // What was selected and open before a look, by path, because the rows are about to be new.
    struct KeptState {
        std::vector<std::string> open{};
        std::vector<std::string> selected{};  // sorted
        std::string current{};
        RowSelection found{};
    };

    constexpr void append_folder(std::vector<FileRow>& out, const std::string& path, std::uint32_t depth, KeptState& kept, DirectoryListing listing) const {
        for (FileEntry& entry : listing.entries) {
            if (out.size() == max_grid_rows) return;
            const std::string child = child_of_path(path, entry.name);
            if (std::ranges::binary_search(kept.selected, child)) kept.found.rows.push_back(static_cast<std::uint32_t>(out.size()));
            if (child == kept.current) kept.found.current = GridRow{out.size()};
            out.push_back(row_of(std::move(entry), depth));
            if (out.back().name.mark != TreeMark::Closed || depth + 1 >= most_tree_depth || !std::ranges::contains(kept.open, child)) continue;
            auto inner = source_.read(child);
            if (!inner) continue;  // gone since, or shut: it shows closed
            out.back().name.mark = TreeMark::Open;
            append_folder(out, child, depth + 1, kept, std::move(*inner));
        }
    }

    // Reads again what shows. False when the top folder cannot be read, which the status line then says, and the rows stay as they were.
    [[nodiscard]] constexpr bool look_again() {
        if (folder_.empty()) return false;
        auto top = source_.read(folder_);
        if (!top) {
            say(top.error().message.units());
            return false;
        }
        KeptState kept;
        for (std::size_t i = 0; i < rows().size(); ++i)
            if (rows()[i].name.mark == TreeMark::Open) kept.open.push_back(path_of(i));
        for (const std::uint32_t row : grid().selected_rows()) kept.selected.push_back(path_of(row));
        std::ranges::sort(kept.selected);
        if (const std::optional<GridRow> row = grid().selected(); row) kept.current = path_of(row->index);
        std::vector<FileRow> changed;
        append_folder(changed, folder_, 0, kept, std::move(*top));
        replace_rows(std::move(changed), std::move(kept.found));
        return true;
    }

    // A folder row is itself. Anything else is the folder it is in.
    [[nodiscard]] constexpr std::string folder_for_row(std::size_t index) const {
        const std::string path = path_of(index);
        return rows()[index].kind == FileKind::Folder ? path : parent_of_path(path);
    }

    // Where a drop here would go: the folder under the pointer, or that of the row under it, or past the last row the folder at the top.
    [[nodiscard]] constexpr std::string folder_at(Point at) const {
        const std::optional<GridRow> row = grid().row_at(at);
        return row ? folder_for_row(row->index) : folder_;
    }

    constexpr void say_counts(std::size_t count, std::size_t left_out) {
        std::string text = detail::count_of_things(count);
        if (left_out != 0) text.append(", and ").append(decimal_text(left_out, false)).append(" with a name that cannot be shown");
        say(text);
    }

    template <class U>
    [[nodiscard]] constexpr bool act_on_key(U& ui, const KeyEvent& event) {
        if (event.modifiers.holds(Modifier::Ctrl)) {
            if (event.keysym == Key::LowerC || event.keysym == Key::UpperC) {
                copy_selected(ui);
                return true;
            }
            if (event.keysym == Key::LowerV || event.keysym == Key::UpperV) {
                paste_here(ui);
                return true;
            }
            if (event.keysym == Key::LowerA || event.keysym == Key::UpperA) {
                grid().select_all_rows();
                return true;
            }
            return false;
        }
        if (event.keysym == Key::BackSpace) {
            go_up();
            return true;
        }
        const std::optional<GridRow> row = grid().selected();
        if (!row) return false;
        const TreeMark mark = rows()[row->index].name.mark;
        if (event.keysym == Key::Return) {
            toggle(row->index);
            return true;
        }
        if (event.keysym == Key::Plus || event.keysym == Key::KeypadPlus) {
            if (mark == TreeMark::Closed) toggle(row->index);
            return true;
        }
        if (event.keysym == Key::Minus || event.keysym == Key::KeypadMinus) {
            if (mark == TreeMark::Open) toggle(row->index);
            return true;
        }
        return false;
    }

    // How wide a mark and a level of indent are, in the font the name column shows now.
    template <TextShaper Shaper>
    constexpr void measure_marks(const Shaper& text) {
        if constexpr (HasMonospaceFace<Shaper>) {
            if (grid().column_font_applied(file_name_column) == ColumnFont::Monospace) {
                if (auto face = text.monospace_face(); face) {
                    measure_marks_with(face->get());
                    return;
                }
            }
        }
        measure_marks_with(text);
    }

    template <TextShaper Shaper>
    constexpr void measure_marks_with(const Shaper& text) {
        const auto width_of = [&](std::string_view part) {
            const auto shaped = text.shape(part);
            return shaped ? shaped->size().width() : Width{};
        };
        mark_width_ = width_of(tree_closed_text);
        indent_width_ = width_of(tree_indent_text);
    }

    // Only where a mark is drawn counts, which needs the name at its usual place, at the left of its column.
    [[nodiscard]] constexpr bool on_the_mark(const FileRow& row, Point at) const {
        if (row.name.mark == TreeMark::None || grid().column_align_applied(file_name_column) != ColumnAlign::AtLeft) return false;
        const Left text_left = left_after(grid().column_left(file_name_column) + theme::cell_pad_x);
        const Left mark_left = left_after(text_left + repeated_extent(indent_width_, row.name.depth));
        return at.left() >= mark_left && at.left() < left_after(mark_left + mark_width_);
    }

    Source source_;
    children_type children_;
    std::string folder_{};
    Rect bounds_{};
    Rect strip_{};
    Width mark_width_{};
    Width indent_width_{};
    std::uint64_t changes_seen_{};
    bool pressed_on_row_{};
    bool drag_seen_{};
};

// With two or more selected, across both sides, those are matched by their contents, one pair at a time. With fewer, this compares what shows on each side and says on the transfers' line how it
// came out: neither side is read again and nothing is changed on a disk, and differ counts each thing once, from the left.
template <class LeftTag, class RightTag, class TransfersTag, class U>
constexpr void compare_panes(U& ui) {
    auto& left = ui.template get<LeftTag>();
    auto& right = ui.template get<RightTag>();
    auto& transfers = ui.template get<TransfersTag>();
    const std::vector<std::string> left_selected = left.selected_paths();
    const std::vector<std::string> right_selected = right.selected_paths();
    if (left_selected.size() + right_selected.size() >= 2) {
        transfers.start_matching(plan_matches(left_selected, right_selected));
        return;
    }
    if (left.folder().empty() || right.folder().empty()) {
        transfers.tell("Comparing needs a folder on each side");
        return;
    }
    const std::vector<ComparedItem> left_items = left.items_to_compare();
    const std::vector<ComparedItem> right_items = right.items_to_compare();
    const FileMatchCounts on_left = left.compare_with(right_items);
    const FileMatchCounts on_right = right.compare_with(left_items);
    std::string text = "Compared what shows: ";
    text.append(decimal_text(on_left.only_here, false)).append(" only on the left, ");
    text.append(decimal_text(on_right.only_here, false)).append(" only on the right, ");
    text.append(decimal_text(on_left.differ, false)).append(" different, ");
    text.append(decimal_text(on_left.same, false)).append(" same size and time. Contents were not read. To match by contents, select two or more first.");
    transfers.tell(text);
}

// Exactly one selected, on either side, and that one a file or a link to one.
template <class LeftTag, class RightTag, class TransfersTag, class U>
constexpr void open_from_panes(U& ui) {
    auto& left = ui.template get<LeftTag>();
    auto& right = ui.template get<RightTag>();
    auto& transfers = ui.template get<TransfersTag>();
    std::vector<std::string> selected = left.selected_paths();
    const std::vector<std::string> on_right = right.selected_paths();
    selected.insert(selected.end(), on_right.begin(), on_right.end());
    if (selected.size() != 1) {
        transfers.tell("Select one file to open");
        return;
    }
    if (!left.selected_are_files() || !right.selected_are_files()) {
        transfers.tell("Only a file can be opened, and " + std::string{last_name_of(selected.front())} + " is not one");
        return;
    }
    transfers.open_file(selected.front());
}

// One selected on each side. The transfers say what becomes of them.
template <class LeftTag, class RightTag, class TransfersTag, class U>
constexpr void diff_panes(U& ui) {
    ui.template get<TransfersTag>().diff_selected(ui.template get<LeftTag>().selected_things(), ui.template get<RightTag>().selected_things());
}

template <class Tag, class Transfers, class Source>
[[nodiscard]] constexpr FilePane<Tag, Source, Transfers> file_pane(Source source) {
    return FilePane<Tag, Source, Transfers>{std::move(source)};
}

}  // namespace jgui::inline v001
