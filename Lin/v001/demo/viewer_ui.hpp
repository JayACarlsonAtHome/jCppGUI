// File: demo/viewer_ui.hpp
// Purpose: The viewer's window: a menu bar with one button per section of the redraw monitor's file and of the settings record, a line that says what is showing
//          or what went wrong, and the chosen section's grid in all the room left. Each section's columns are declared here, in C++,
//          with the names the file uses. The tests include this too, so they check the real tree.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "jCppGUI/v001/jgui.hpp"
#include "jCppGUI/v001/jtext.hpp"

namespace viewer {

using jgui::Column;
using jgui::DataGrid;
using jgui::NoGridMessage;

struct RunRow {
    std::uint64_t run_id{};
    std::string label{};
    std::int64_t slow_budget_ns{};
    std::uint64_t row_limit{};
    std::uint64_t frames_seen{};
    std::uint64_t frames_dropped{};
    std::uint64_t damage_seen{};
    std::uint64_t damage_dropped{};
    std::uint64_t deferred_seen{};
    std::uint64_t deferred_dropped{};
    std::uint64_t rows_refused{};
    std::uint64_t writes_before_this{};
    std::int64_t previous_write_ns{};
    bool ended{};
};

struct FramesRow {
    std::uint64_t frame_id{};
    std::int64_t start_ns{};
    std::string reason{};
    jgui::Left left{};
    jgui::Top top{};
    jgui::Width width{};
    jgui::Height height{};
    std::int64_t layout_ns{};
    std::int64_t acquire_ns{};
    std::int64_t paint_ns{};
    std::int64_t hash_ns{};
    std::int64_t present_ns{};
    std::int64_t total_ns{};
    std::int64_t last_write_ns{};
    bool slow{};
    bool same_pixels{};
};

struct DamageRow {
    std::uint64_t record_id{};
    std::uint64_t frame_id{};
    std::string class_name{};
    std::string tag{};
    std::uint64_t tree_position{};
};

struct DeferredRow {
    std::uint64_t record_id{};
    std::uint64_t next_frame_id{};
    std::int64_t time_ns{};
    std::string reason{};
    jgui::Width width{};
    jgui::Height height{};
};

// One item as the settings record wrote it down, in the order the file has its columns.
struct ItemsRow {
    std::uint64_t id{};
    std::uint64_t parent_id{};
    std::string class_name{};
    std::string name{};
    jgui::Left left{};
    jgui::Top top{};
    jgui::Bottom bottom{};
    jgui::Right right{};
    jgui::Height height{};
    jgui::Width width{};
};

struct ProblemsRow {
    std::uint64_t record_id{};
    std::uint64_t item_id{};
    std::string what_failed{};
};

struct RunGridTag;
struct FramesGridTag;
struct DamageGridTag;
struct DeferredGridTag;
struct ItemsGridTag;
struct ProblemsGridTag;

using RunGrid = DataGrid<RunGridTag, RunRow, NoGridMessage, Column<"Run_ID", &RunRow::run_id, 70>, Column<"Label", &RunRow::label, 160>, Column<"Slow_Budget_ns", &RunRow::slow_budget_ns, 120>,
                         Column<"Row_Limit", &RunRow::row_limit, 90>, Column<"Frames_Seen", &RunRow::frames_seen, 100>, Column<"Frames_Dropped", &RunRow::frames_dropped, 120>,
                         Column<"Damage_Seen", &RunRow::damage_seen, 110>, Column<"Damage_Dropped", &RunRow::damage_dropped, 130>, Column<"Deferred_Seen", &RunRow::deferred_seen, 120>,
                         Column<"Deferred_Dropped", &RunRow::deferred_dropped, 140>, Column<"Rows_Refused", &RunRow::rows_refused, 110>, Column<"Writes_Before_This", &RunRow::writes_before_this, 150>,
                         Column<"Previous_Write_ns", &RunRow::previous_write_ns, 150>, Column<"Ended", &RunRow::ended, 70>>;

using FramesGrid = DataGrid<FramesGridTag, FramesRow, NoGridMessage, Column<"Frame_ID", &FramesRow::frame_id, 90>, Column<"Start_ns", &FramesRow::start_ns, 120>, Column<"Reason", &FramesRow::reason, 120>,
                            Column<"Left", &FramesRow::left, 60>, Column<"Top", &FramesRow::top, 60>, Column<"Width", &FramesRow::width, 60>, Column<"Height", &FramesRow::height, 60>,
                            Column<"Layout_ns", &FramesRow::layout_ns, 100>, Column<"Acquire_ns", &FramesRow::acquire_ns, 100>, Column<"Paint_ns", &FramesRow::paint_ns, 100>,
                            Column<"Hash_ns", &FramesRow::hash_ns, 100>, Column<"Present_ns", &FramesRow::present_ns, 100>, Column<"Total_ns", &FramesRow::total_ns, 100>,
                            Column<"Last_Write_ns", &FramesRow::last_write_ns, 120>, Column<"Slow", &FramesRow::slow, 60>, Column<"SamePixels", &FramesRow::same_pixels, 90>>;

using DamageGrid = DataGrid<DamageGridTag, DamageRow, NoGridMessage, Column<"Record_ID", &DamageRow::record_id, 90>, Column<"Frame_ID", &DamageRow::frame_id, 90>, Column<"Class_Name", &DamageRow::class_name, 200>,
                            Column<"Tag", &DamageRow::tag, 240>, Column<"Tree_Position", &DamageRow::tree_position, 110>>;

using DeferredGrid = DataGrid<DeferredGridTag, DeferredRow, NoGridMessage, Column<"Record_ID", &DeferredRow::record_id, 90>, Column<"Next_Frame_ID", &DeferredRow::next_frame_id, 120>,
                              Column<"Time_ns", &DeferredRow::time_ns, 120>, Column<"Reason", &DeferredRow::reason, 120>, Column<"Width", &DeferredRow::width, 60>, Column<"Height", &DeferredRow::height, 60>>;

using ItemsGrid = DataGrid<ItemsGridTag, ItemsRow, NoGridMessage, Column<"ID", &ItemsRow::id, 60>, Column<"Parent_ID", &ItemsRow::parent_id, 80>, Column<"Class_Name", &ItemsRow::class_name, 200>,
                           Column<"Name", &ItemsRow::name, 200>, Column<"Left", &ItemsRow::left, 60>, Column<"Top", &ItemsRow::top, 60>, Column<"Bottom", &ItemsRow::bottom, 60>,
                           Column<"Right", &ItemsRow::right, 60>, Column<"Height", &ItemsRow::height, 60>, Column<"Width", &ItemsRow::width, 60>>;

using ProblemsGrid = DataGrid<ProblemsGridTag, ProblemsRow, NoGridMessage, Column<"Record_ID", &ProblemsRow::record_id, 90>, Column<"Item_ID", &ProblemsRow::item_id, 80>,
                              Column<"What_Failed", &ProblemsRow::what_failed, 360>>;

struct RunButton;
struct FramesButton;
struct DamageButton;
struct DeferredButton;
struct ItemsButton;
struct ProblemsButton;
struct StatusLine;
struct SectionGrids;

inline constexpr std::string_view run_section = "Run";
inline constexpr std::string_view frames_section = "Frames";
inline constexpr std::string_view damage_section = "Damage";
inline constexpr std::string_view deferred_section = "Deferred";
inline constexpr std::string_view items_section = "Items";
inline constexpr std::string_view problems_section = "Problems";
inline constexpr std::string_view status_when_refused = "This text cannot be shown.";
inline constexpr std::size_t most_path_bytes = 4096;
inline constexpr std::size_t most_title_name_bytes = 255;  // the longest name a file has on Linux, and well under a title's limit

static_assert(jgui::JTextLimits{}.rows_in_a_section <= jgui::max_grid_rows, "every row a file can hold fits a grid, so no grid refuses them");

// A path this program will try: not empty, not over the length of a path on this system, and text it can show. So an error
// naming it can always be drawn, and the window's title can be made from it.
[[nodiscard]] constexpr std::expected<std::string, jgui::Error> checked_path(std::string path) {
    if (path.empty()) return jgui::make_error(jgui::ErrorCode::System, "the path is empty");
    if (path.size() > most_path_bytes) return jgui::make_error(jgui::ErrorCode::SizeLimit, "the path is over the limit of 4096 bytes");
    if (!jgui::valid_utf8(path) || jgui::detail::holds_jtext_control(path)) return jgui::make_error(jgui::ErrorCode::InvalidUtf8, "the path is not text that can be shown");
    return path;
}

// What follows the last slash. A path ending in a slash has no name, and so comes back empty.
[[nodiscard]] constexpr std::string_view file_name_in(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

[[nodiscard]] constexpr std::string window_title_for(const std::expected<std::string, jgui::Error>& path) {
    std::string title{"jCppGUI viewer"};
    if (path && !file_name_in(*path).empty()) title.append(": ").append(jgui::utf8_prefix(file_name_in(*path), most_title_name_bytes));
    return title;
}

// A plural that reads right, for what a label says.
[[nodiscard]] constexpr std::string count_of(std::size_t count, std::string_view one) {
    std::string text = jgui::decimal_text(count, false);
    text.append(" ").append(one);
    if (count != 1) text.push_back('s');
    return text;
}

// What the status line says for a section that is showing.
[[nodiscard]] constexpr std::string section_status(std::string_view name, std::size_t rows, std::size_t left_out) {
    std::string text{name};
    text.append(": ").append(count_of(rows, "row"));
    if (left_out != 0) text.append(", ").append(count_of(left_out, "row")).append(" left out: a value did not convert");
    return text;
}

// One section as loaded. With no grid to show, its status says why, and that takes the grid's place.
template <class Row>
struct LoadedSection {
    std::vector<Row> rows{};
    std::string status{};
    bool usable{};
};

template <class Grid>
[[nodiscard]] constexpr LoadedSection<typename Grid::row_type> load_section(const std::expected<jgui::JTextFile, jgui::Error>& file, std::string_view name) {
    if (!file) return {.status = file.error().message};
    const auto section = jgui::get_section(*file, name);
    if (!section) return {.status = section.error().message};
    auto typed = jgui::get_rows_for_grid<Grid>(*section);
    if (!typed) return {.status = typed.error().message};
    const std::string status = section_status(name, typed->rows.size(), typed->left_out);
    return {.rows = std::move(typed->rows), .status = status, .usable = true};
}

struct LoadedFile {
    LoadedSection<RunRow> run{};
    LoadedSection<FramesRow> frames{};
    LoadedSection<DamageRow> damage{};
    LoadedSection<DeferredRow> deferred{};
};

[[nodiscard]] constexpr LoadedFile load_file(const std::expected<jgui::JTextFile, jgui::Error>& file) {
    return LoadedFile{load_section<RunGrid>(file, run_section), load_section<FramesGrid>(file, frames_section), load_section<DamageGrid>(file, damage_section),
                      load_section<DeferredGrid>(file, deferred_section)};
}

// The settings record is a file of its own, so it can be missing or refused while the monitor's file is fine, or the other way about.
struct LoadedSettings {
    LoadedSection<ItemsRow> items{};
    LoadedSection<ProblemsRow> problems{};
};

[[nodiscard]] constexpr LoadedSettings load_settings_file(const std::expected<jgui::JTextFile, jgui::Error>& file) {
    return LoadedSettings{load_section<ItemsGrid>(file, items_section), load_section<ProblemsGrid>(file, problems_section)};
}

inline constexpr std::size_t run_position = 0;
inline constexpr std::size_t frames_position = 1;
inline constexpr std::size_t damage_position = 2;
inline constexpr std::size_t deferred_position = 3;
inline constexpr std::size_t items_position = 4;
inline constexpr std::size_t problems_position = 5;

// What a section's button does. It says its section's status, and shows its grid, or no grid when there is none to show. Messages
// from the reader are text, and fit a line, and the path was checked. So a refusal here, which keeps the old text, does not happen.
struct ShowSection {
    std::string status;
    std::optional<std::size_t> grid;

    template <class U>
    constexpr void operator()(U& u) const {
        u.template get<SectionGrids>().show(grid);
        static_cast<void>(u.template get<StatusLine>().set_text(status));
    }
};

// A message naming a long path can be longer than a line can hold, and the start of it is what matters.
[[nodiscard]] constexpr std::string_view status_fitting_a_line(std::string_view status) noexcept { return jgui::utf8_prefix(status, jgui::max_caption_bytes); }

template <class Row>
[[nodiscard]] constexpr ShowSection show_of(const LoadedSection<Row>& section, std::size_t position) {
    return ShowSection{std::string{status_fitting_a_line(section.status)}, section.usable ? std::optional<std::size_t>{position} : std::nullopt};
}

[[nodiscard]] constexpr jgui::CheckedText status_text(std::string_view status) {
    auto checked = jgui::CheckedText::check(status_fitting_a_line(status));
    return checked ? *checked : jgui::CheckedText{status_when_refused};
}

// The whole window. It opens on the Frames section, the one with most to look at.
[[nodiscard]] constexpr auto make_ui(LoadedFile loaded, LoadedSettings settings = LoadedSettings{}) {
    auto ui = jgui::window(jgui::window_frame(
        jgui::menu_bar(jgui::button<RunButton>("Run", show_of(loaded.run, run_position)), jgui::button<FramesButton>("Frames", show_of(loaded.frames, frames_position)),
                       jgui::button<DamageButton>("Damage", show_of(loaded.damage, damage_position)), jgui::button<DeferredButton>("Deferred", show_of(loaded.deferred, deferred_position)),
                       jgui::button<ItemsButton>("Items", show_of(settings.items, items_position)), jgui::button<ProblemsButton>("Problems", show_of(settings.problems, problems_position))),
        jgui::label<StatusLine>(status_text(loaded.frames.status)), jgui::one_shown<SectionGrids>(RunGrid{}, FramesGrid{}, DamageGrid{}, DeferredGrid{}, ItemsGrid{}, ProblemsGrid{})));
    ui.template get<SectionGrids>().show(loaded.frames.usable ? std::optional<std::size_t>{frames_position} : std::nullopt);
    // The reader holds no more rows than a grid does, as the static_assert above shows, so none of these refuses.
    static_cast<void>(ui.template get<RunGridTag>().set_rows(std::move(loaded.run.rows)));
    static_cast<void>(ui.template get<FramesGridTag>().set_rows(std::move(loaded.frames.rows)));
    static_cast<void>(ui.template get<DamageGridTag>().set_rows(std::move(loaded.damage.rows)));
    static_cast<void>(ui.template get<DeferredGridTag>().set_rows(std::move(loaded.deferred.rows)));
    static_cast<void>(ui.template get<ItemsGridTag>().set_rows(std::move(settings.items.rows)));
    static_cast<void>(ui.template get<ProblemsGridTag>().set_rows(std::move(settings.problems.rows)));
    return ui;
}

}  // namespace viewer
