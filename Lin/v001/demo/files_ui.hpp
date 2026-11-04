// File: demo/files_ui.hpp
// Purpose: The file manager's window: a bar with Compare and Diff, a line saying what is on its way, and two panes side by side, each with a folder of its own. The tests include this too, so they check the real tree.
#pragma once

#include <expected>
#include <string>
#include <utility>

#include "jCppGUI/v001/file_delete_popup.hpp"
#include "jCppGUI/v001/file_diff_popup.hpp"
#include "jCppGUI/v001/file_match_popup.hpp"
#include "jCppGUI/v001/file_pane.hpp"
#include "jCppGUI/v001/file_transfers.hpp"
#include "jCppGUI/v001/jgui.hpp"
#include "jCppGUI/v001/two_panes.hpp"

namespace files {

struct LeftPane;
struct RightPane;
struct Panes;
struct Transfers;
struct CompareButton;
struct DiffButton;
struct Layer;
struct DeleteLayer;
struct DiffLayer;
struct MatchPopup;
struct DeletePopup;
struct DiffPopup;

using StartFolder = std::expected<std::string, jgui::Error>;

template <class Pane>
constexpr void start_in(Pane& pane, const StartFolder& folder) {
    if (folder) {
        pane.open(*folder);
    } else {
        pane.say(folder.error().message);
    }
}

template <jgui::FileSource Source>
[[nodiscard]] constexpr auto make_ui(Source source, const StartFolder& left_folder, const StartFolder& right_folder) {
    auto left = jgui::file_pane<LeftPane, Transfers>(source);
    auto right = jgui::file_pane<RightPane, Transfers>(source);
    start_in(left, left_folder);
    start_in(right, right_folder);
    auto bar = jgui::menu_bar(jgui::button<CompareButton>("Compare", [](auto& u) -> void { jgui::compare_panes<LeftPane, RightPane, Transfers>(u); }),
                              jgui::button<DiffButton>("Diff", [](auto& u) -> void { jgui::diff_panes<LeftPane, RightPane, Transfers>(u); }));
    // The transfers come first in the tree, so what they finish is seen by both panes and each popup in the same turn. Only one thing goes at a time, so only one popup is ever open.
    auto content = jgui::popup_layer<DiffLayer>(jgui::FileDiffPopup<DiffPopup, Transfers>{}, std::move(bar), jgui::file_transfers<Transfers>(std::move(source)), jgui::two_panes<Panes>(std::move(left), std::move(right)));
    return jgui::window(jgui::window_frame(jgui::popup_layer<Layer>(jgui::FileMatchPopup<MatchPopup, Transfers>{}, jgui::popup_layer<DeleteLayer>(jgui::FileDeletePopup<DeletePopup, Transfers>{}, std::move(content)))));
}

}  // namespace files
