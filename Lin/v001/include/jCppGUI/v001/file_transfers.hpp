// File: include/jCppGUI/v001/file_transfers.hpp
// Purpose: FileTransfers<Tag, Source>: what goes between two panes. A line of its own, what was last copied, one thing or several, a copy under way, and a drag. A copy goes a step on each turn of the
//          loop, and asks for the next turn at once, so the window stays alive and Escape can stop it, with no thread. A copy replaces nothing: the source refuses a name that is taken. Deleting
//          moves to the trash, is counted first, and goes ahead only on confirm_delete. Only one of these goes at a time. jgui.hpp does not include it. It must come before the panes in the tree: it is what turns a press into a drag, and each pane must then see that on the
//          same motion, because a flood of motion can arrive as one event.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "file_listing.hpp"
#include "copy_line.hpp"
#include "line_diff.hpp"
#include "png.hpp"
#include "terminal_command.hpp"
#include "timer.hpp"
#include "widget.hpp"

namespace jgui::inline v001 {

// The line's tag is made from the widget's own.
template <class Transfers>
struct FileTransfersLine;

inline constexpr std::chrono::milliseconds file_progress_interval{100};

namespace detail {

[[nodiscard]] constexpr std::string count_with_plural(std::uint64_t count, std::string_view one) {
    std::string text = decimal_text(count, false);
    text.append(" ").append(one);
    if (count != 1) text.push_back('s');
    return text;
}

[[nodiscard]] constexpr CopyProgress added_up(const CopyProgress& a, const CopyProgress& b) noexcept {
    return CopyProgress{a.files + b.files, a.folders + b.folders, a.links + b.links, a.bytes + b.bytes, a.bytes_checked + b.bytes_checked, a.not_copied + b.not_copied};
}

[[nodiscard]] constexpr MatchProgress added_up(const MatchProgress& a, const MatchProgress& b) noexcept {
    return MatchProgress{a.files + b.files, a.folders + b.folders, a.links + b.links, a.bytes + b.bytes, a.differences + b.differences};
}

[[nodiscard]] constexpr DeleteCount added_up(const DeleteCount& a, const DeleteCount& b) noexcept { return DeleteCount{a.files + b.files, a.folders + b.folders, a.bytes + b.bytes}; }

[[nodiscard]] constexpr std::string delete_progress_text(const DeleteCount& done) {
    return count_with_plural(done.files, "file") + ", " + count_with_plural(done.folders, "folder") + ", " + CellFormat<FileBytes>::text(FileBytes{true, done.bytes}) + " bytes";
}

[[nodiscard]] constexpr std::string match_progress_text(const MatchProgress& done) {
    std::string text = count_with_plural(done.files, "file");
    text.append(" matched by SHA-256");
    if (done.links != 0) text.append(", ").append(count_with_plural(done.links, "link"));
    if (done.folders != 0) text.append(", ").append(count_with_plural(done.folders, "folder")).append(" looked through");
    text.append(", ").append(CellFormat<FileBytes>::text(FileBytes{true, done.bytes})).append(" bytes read from each side");
    return text;
}

[[nodiscard]] constexpr std::string progress_text(const CopyProgress& done) {
    std::string text = count_with_plural(done.files, "file");
    if (done.folders != 0) text.append(", ").append(count_with_plural(done.folders, "folder"));
    if (done.links != 0) text.append(", ").append(count_with_plural(done.links, "link"));
    text.append(", ").append(CellFormat<FileBytes>::text(FileBytes{true, done.bytes})).append(" bytes");
    if (done.bytes_checked != done.bytes) text.append(", ").append(CellFormat<FileBytes>::text(FileBytes{true, done.bytes_checked})).append(" checked");
    if (done.not_copied != 0) text.append(", and ").append(decimal_text(done.not_copied, false)).append(" not copied, being no file, folder or link");
    return text;
}

}  // namespace detail

// How matching a selection came out, for whatever shows it.
struct MatchReport {
    std::vector<MatchFinding> findings{};  // at most most_match_findings
    MatchProgress checked{};
    std::size_t pairs_done{};
    std::size_t pairs_total{};
    bool stopped{};
};

// What a person must say yes to before anything is deleted.
struct DeleteQuestion {
    std::vector<std::string> paths{};
    DeleteCount count{};
};

enum class NameAsk : std::uint8_t { Touch, Rename, ScreenShot };

// A name a person must type. current is what the box starts with: the name a rename starts from, and nothing for a touch.
struct NameQuestion {
    NameAsk ask{NameAsk::Touch};
    std::string folder{};
    std::string current{};
};

// Where the next picture of the window goes.
struct PictureTarget {
    std::string folder{};
    std::string name{};
};

// How a diff came out, for whatever shows it.
struct DiffReport {
    std::string first{};
    std::string second{};
    DiffOutcome outcome{};
};

template <class Tag, FileSource Source>
class FileTransfers {
    using Line = CopyLine<FileTransfersLine<Tag>>;

public:
    using tag = Tag;
    using children_type = std::tuple<Line>;

    constexpr explicit FileTransfers(Source source) : source_{std::move(source)}, children_{Line{"Select something and press Copy, then Paste on either side. Or drag it to copy it."}} {}
    FileTransfers(const FileTransfers&) = delete;
    FileTransfers& operator=(const FileTransfers&) = delete;
    constexpr FileTransfers(FileTransfers&&) = default;
    FileTransfers& operator=(FileTransfers&&) = delete;
    constexpr ~FileTransfers() = default;

    [[nodiscard]] constexpr Rect bounds() const noexcept { return std::get<Line>(children_).bounds(); }
    [[nodiscard]] constexpr children_type& children() noexcept { return children_; }
    [[nodiscard]] constexpr const children_type& children() const noexcept { return children_; }
    [[nodiscard]] constexpr std::string_view line() const noexcept { return std::get<Line>(children_).text(); }
    [[nodiscard]] constexpr bool copying() const noexcept { return job_.has_value(); }
    [[nodiscard]] constexpr bool dragging() const noexcept { return drag_from_.has_value(); }
    [[nodiscard]] constexpr std::span<const std::string> copied() const noexcept { return copied_; }
    // Counts up each time something on a disk may have changed. A pane looks again when this moves.
    [[nodiscard]] constexpr std::uint64_t changes() const noexcept { return changes_; }

    [[nodiscard]] constexpr bool matching() const noexcept { return matching_; }

    // Says something from elsewhere on this line.
    constexpr void tell(std::string_view text) { say(text); }

    [[nodiscard]] constexpr bool counting() const noexcept { return count_job_.has_value(); }
    [[nodiscard]] constexpr bool awaiting_delete_answer() const noexcept { return awaiting_answer_; }
    [[nodiscard]] constexpr bool deleting() const noexcept { return trashing_; }
    [[nodiscard]] constexpr bool diffing() const noexcept { return differ_.has_value(); }

    // Counts what deleting these would remove. Nothing is removed until confirm_delete, and nothing can be if the count does not finish.
    constexpr void start_deleting(std::vector<std::string> paths) {
        if (say_if_busy()) return;
        for (const std::string& path : paths) {
            if (!file_path_can_show(path) || path == "/") {
                say("Nothing was deleted: that path cannot be deleted");
                return;
            }
        }
        if (paths.empty()) {
            say("Select something to delete first");
            return;
        }
        delete_paths_ = std::move(paths);
        delete_next_ = 0;
        delete_counted_ = DeleteCount{};
        due_line_.reset();
        say("Counting what would be deleted. Escape stops it, and nothing has been deleted.");
        start_next_count();
    }

    // Once, when the count has finished, for whatever asks the person.
    [[nodiscard]] constexpr std::optional<DeleteQuestion> take_delete_question() { return std::exchange(delete_question_, std::nullopt); }

    constexpr void cancel_delete() {
        if (!awaiting_answer_) return;
        awaiting_answer_ = false;
        delete_paths_.clear();
        say("Nothing was deleted");
    }

    // The one way anything already there comes to leave where it is. It goes to the trash, a thing a turn.
    constexpr void confirm_delete() {
        if (!awaiting_answer_) return;
        awaiting_answer_ = false;
        delete_next_ = 0;
        trashing_ = true;
        say("Moving to the trash. Escape stops it.");
    }

    // Reads two files and starts finding what differs. They are read at once, each at most most_diff_file_bytes, and the search goes a step on each turn.
    constexpr void start_diff(const std::string& first, const std::string& second) {
        if (say_if_busy()) return;
        auto first_text = source_.read_text(first);
        if (!first_text) {
            say("Nothing to diff: " + first_text.error().message);
            return;
        }
        auto second_text = source_.read_text(second);
        if (!second_text) {
            say("Nothing to diff: " + second_text.error().message);
            return;
        }
        auto started = LineDiffer::start(std::move(*first_text), std::move(*second_text));
        if (!started) {
            say("Nothing to diff: " + started.error().message);
            return;
        }
        differ_.emplace(std::move(*started));
        diff_first_ = first;
        diff_second_ = second;
        say("Finding what differs. Escape stops it.");
    }

    // Asks for the name to touch in this folder. Nothing happens until it is given.
    constexpr void ask_touch(std::string folder) {
        if (say_if_busy()) return;
        name_question_ = NameQuestion{NameAsk::Touch, std::move(folder), {}};
    }

    // Asks for a new name for this one thing, starting from the name it has.
    constexpr void ask_rename(const std::string& path) {
        if (say_if_busy()) return;
        if (!file_path_can_show(path) || path == "/") {
            say("That path cannot be renamed");
            return;
        }
        name_question_ = NameQuestion{NameAsk::Rename, parent_of_path(path), std::string{last_name_of(path)}};
    }

    // Asks what to call a screenshot. It may be asked with something else going on, because all it does is add a file of its own.
    constexpr void ask_picture() {
        auto folder = source_.picture_folder();
        if (!folder) {
            say("No screenshot was saved: " + folder.error().message);
            return;
        }
        name_question_ = NameQuestion{NameAsk::ScreenShot, std::move(*folder), {}};
    }

    // Once, for whatever asks the person.
    [[nodiscard]] constexpr std::optional<NameQuestion> take_name_question() { return std::exchange(name_question_, std::nullopt); }

    // Does what was asked with the name typed. A refusal changes nothing, and is for whoever asked, to say beside the name so it can be corrected. Both panes look again after a change.
    [[nodiscard]] constexpr std::expected<void, Error> give_name(const NameQuestion& question, std::string_view name) {
        if (auto checked = check_given_name(question.folder, name); !checked) return std::unexpected(std::move(checked.error()));
        const std::string typed{name};
        if (question.ask == NameAsk::ScreenShot) return name_the_picture(question.folder, picture_file_name(typed));
        if (question.ask == NameAsk::Touch) {
            auto touched = source_.touch(question.folder, typed);
            if (!touched) return std::unexpected(std::move(touched.error()));
            ++changes_;
            say((*touched == TouchOutcome::Made ? "Made " : "Set the time of ") + typed);
            return {};
        }
        if (typed == question.current) {
            say("Nothing was changed");
            return {};
        }
        if (auto renamed = source_.rename(child_of_path(question.folder, question.current), typed); !renamed) return std::unexpected(std::move(renamed.error()));
        ++changes_;
        say("Renamed " + question.current + " to " + typed);
        return {};
    }

    // Opens a terminal showing or editing these. It goes on by itself, so whatever else is going on here may go on too. A pane does not see what it changes until it looks again.
    constexpr void open_in_terminal(TerminalTool tool, std::span<const std::string> paths) {
        const auto command = terminal_tool_command(tool, paths, source_.editor());
        if (!command) {
            say(command.error().message);
            return;
        }
        if (auto opened = source_.run_in_terminal(parent_of_path(paths.front()), *command); !opened) {
            say(opened.error().message);
            return;
        }
        const std::string names = paths.size() == 1 ? std::string{last_name_of(paths.front())} : detail::count_with_plural(paths.size(), "file");
        say((tool == TerminalTool::Cat ? "Showing " : "Editing ") + names + " in a terminal");
    }

    // How the last diff came out, once. Empty until it has ended.
    [[nodiscard]] constexpr std::optional<DiffReport> take_diff_report() { return std::exchange(diff_done_, std::nullopt); }

    // Matches these one after another. It only reads, but a copy going on would change what it reads, so the two do not run together.
    constexpr void start_matching(MatchPlan plan) {
        if (say_if_busy()) return;
        if (!plan.refused.empty()) {
            say(plan.refused);
            return;
        }
        match_pairs_ = std::move(plan.pairs);
        match_next_ = 0;
        match_progress_line_.reset();
        matching_ = true;
        match_report_ = MatchReport{};
        match_report_.pairs_total = match_pairs_.size();
        match_report_.checked.differences = plan.unpaired.size();
        match_report_.findings = std::move(plan.unpaired);
        say("Matching " + detail::count_with_plural(match_pairs_.size(), "pair") + ". Escape stops it.");
        start_next_match();
    }

    // How the last match came out, once. Empty until it has ended.
    [[nodiscard]] constexpr std::optional<MatchReport> take_match_report() { return std::exchange(match_done_, std::nullopt); }

    constexpr void copy(std::vector<std::string> paths) {
        copied_ = std::move(paths);
        say("Ready to paste " + (copied_.size() == 1 ? copied_.front() : detail::count_with_plural(copied_.size(), "thing")));
    }

    constexpr void paste_into(const std::string& into) {
        if (copied_.empty()) {
            say("Nothing has been copied yet");
            return;
        }
        start_copying(copied_, into);
    }

    // A press on a row may be the start of a drag. Motion will say.
    constexpr void arm_drag(std::vector<std::string> from, Point at) {
        armed_from_ = std::move(from);
        armed_at_ = at;
    }

    // The pane under the pointer says where a drop would go. The rectangle says which pane, so one the pointer has left can only clear what it said itself.
    constexpr void drag_over(Rect pane, std::string into) {
        if (!drag_from_) return;
        if (drag_into_ == into && drag_pane_ == pane) return;
        drag_into_ = std::move(into);
        drag_pane_ = pane;
        say_drag();
    }

    constexpr void drag_left(Rect pane) {
        if (!drag_from_ || drag_pane_ != pane) return;
        drag_into_.reset();
        drag_pane_ = Rect{};
        say_drag();
    }

    constexpr void stop_drag() {
        armed_from_.reset();
        if (!drag_from_) return;
        drag_from_.reset();
        drag_into_.reset();
        say("The drag was stopped, and nothing was copied");
    }

    // A drop is a copy like any other, and what was dragged stays where it is. It leaves alone what was last copied for a paste.
    constexpr void finish_drag() {
        armed_from_.reset();
        if (!drag_from_) return;
        const std::vector<std::string> from = *std::exchange(drag_from_, std::nullopt);
        const std::optional<std::string> into = std::exchange(drag_into_, std::nullopt);
        if (!into) {
            say("Nothing was copied: let go over a list to copy something");
            return;
        }
        for (const std::string& path : from) {
            if (parent_of_path(path) == *into) {
                say(from.size() == 1 ? std::string{"Nothing was copied: it is already there"} : "Nothing was copied: " + display_name(path) + " is already there");
                return;
            }
        }
        start_copying(from, *into);
    }

    template <TextShaper Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey& key, Rect area, const Shaper& text) {
        return std::get<Line>(children_).layout(key, area, text);
    }

    constexpr void paint(const TreeKey& key, Canvas& canvas) { std::get<Line>(children_).paint(key, canvas); }

    // Escape stops a drag or a copy, wherever the focus is.
    template <class U>
    constexpr bool handle(const TreeKey&, U&, const KeyEvent& event) {
        if (!event.pressed || event.keysym != Key::Escape) return false;
        if (drag_from_) {
            stop_drag();
            return true;
        }
        armed_from_.reset();
        if (matching_) {
            end_match(true);
            return true;
        }
        if (count_job_) {
            count_job_.reset();
            delete_paths_.clear();
            say("Counting was stopped, and nothing was deleted");
            return true;
        }
        if (trashing_) {
            end_delete("Stopped: ");
            return true;
        }
        if (differ_) {
            differ_.reset();
            say("The diff was stopped");
            return true;
        }
        if (job_) {
            end_job(pending_.size() < 2 ? "Stopped: what was copied so far is in " + job_into_ + ". " : "Stopped: " + how_many_done() + "What was copied so far is in " + job_into_ + ". ");
            return true;
        }
        return false;
    }

    template <class U>
    constexpr bool handle(const TreeKey& key, U& ui, const PointerMove& event) {
        if (armed_from_ && has_moved_from(armed_at_, event.position)) {
            drag_from_ = *std::exchange(armed_from_, std::nullopt);
            drag_into_.reset();
            drag_pane_ = Rect{};
            say_drag();
        }
        return detail::deliver(children_, key, ui, event);
    }

    template <class U, class E>
    constexpr bool handle(const TreeKey& key, U& ui, const E& event) {
        return detail::deliver(children_, key, ui, event);
    }

    // One step of a copy on each turn, and straight on to the next turn. The line is not drawn for every step, because that would cost more than the copy.
    template <class U>
    constexpr void notify(const TreeKey& key, U& ui, const TimersDue& timers) {
        detail::notify_all(children_, key, ui, timers);
        if (auto picture = ui.take_window_picture(); picture && picture_target_) save_picture(*picture);
        if (matching_) {
            step_match(ui, timers);
            return;
        }
        if (count_job_) {
            step_count(ui, timers);
            return;
        }
        if (trashing_) {
            step_trash(ui, timers);
            return;
        }
        if (differ_) {
            step_diff(ui, timers);
            return;
        }
        if (!job_) return;
        auto stepped = job_->step();
        if (!stepped) {
            end_job("Stopped: " + stepped.error().message + ". " + how_many_done() + "What was copied so far is in " + job_into_ + ". ");
            return;
        }
        if (*stepped) {
            next_or_end();
            if (job_) ui.wake_at(timers.now);
            return;
        }
        if (!due_line_ || timers.now >= *due_line_) {
            due_line_ = timers.now.after(file_progress_interval);
            say("Copying " + job_name_ + ": " + detail::progress_text(progress_so_far()) + ". Escape stops it.");
        }
        ui.wake_at(timers.now);
    }

    template <class U, class E>
    constexpr void notify(const TreeKey& key, U& ui, const E& event) {
        detail::notify_all(children_, key, ui, event);
    }

private:
    // Refused while the person can still correct it, if the name is already taken. Saving makes the file with O_EXCL anyway, so this only says so sooner, and a folder not there yet holds nothing.
    [[nodiscard]] constexpr std::expected<void, Error> name_the_picture(const std::string& folder, std::string file) {
        if (auto checked = check_given_name(folder, file); !checked) return std::unexpected(std::move(checked.error()));
        const auto there = source_.read(folder);
        if (there && std::ranges::contains(there->entries, file, &FileEntry::name)) return make_error(ErrorCode::System, file + " is already there, and nothing was replaced");
        picture_target_ = PictureTarget{folder, std::move(file)};
        return {};
    }

    // The panes are not told to look again, because that would take away the marks a comparison left, which may be what the picture is of.
    constexpr void save_picture(const WindowPicture& picture) {
        const PictureTarget target = *std::exchange(picture_target_, std::nullopt);
        const auto png = png_of(picture);
        if (!png) {
            say("No screenshot was saved: " + png.error().message);
            return;
        }
        const auto saved = source_.save_picture(target.folder, target.name, *png);
        say(saved ? "Saved a screenshot of this window to " + *saved : "No screenshot was saved: " + saved.error().message);
    }

    // What can be refused from the paths alone is refused before anything is made. A name taken in the folder is found by the source when that one's turn comes, and it stops there,
    // with what went before still there.
    constexpr void start_copying(std::vector<std::string> from, const std::string& into) {
        if (say_if_busy()) return;
        for (const std::string& path : from) {
            if (auto checked = check_transfer(path, into); !checked) {
                say(checked.error().message);
                return;
            }
        }
        for (std::size_t i = 0; i < from.size(); ++i) {
            for (std::size_t j = i + 1; j < from.size(); ++j) {
                if (last_name_of(from[i]) == last_name_of(from[j])) {
                    say("Nothing was copied: two of those have the same name, " + display_name(from[i]));
                    return;
                }
            }
        }
        pending_ = std::move(from);
        next_ = 0;
        job_into_ = into;
        job_name_ = pending_.size() == 1 ? display_name(pending_.front()) : detail::count_with_plural(pending_.size(), "thing");
        finished_ = CopyProgress{};
        due_line_.reset();
        auto started = source_.start_copy(pending_.front(), into);
        if (!started) {
            say(started.error().message);
            return;
        }
        job_.emplace(std::move(*started));
        say("Copying " + job_name_ + ". Escape stops it.");
    }

    // Only one thing at a time: each would change or be misled by what another reads.
    [[nodiscard]] constexpr bool say_if_busy() {
        if (job_) {
            say("A copy is still going. Escape stops it.");
        } else if (matching_) {
            say("A match is still going. Escape stops it.");
        } else if (count_job_ || trashing_) {
            say("A delete is still going. Escape stops it.");
        } else if (differ_) {
            say("A diff is still going. Escape stops it.");
        } else {
            return false;
        }
        return true;
    }

    constexpr void start_next_count() {
        auto started = source_.start_count(delete_paths_[delete_next_]);
        if (!started) {
            delete_paths_.clear();
            say("Nothing was deleted: " + started.error().message);
            return;
        }
        count_job_.emplace(std::move(*started));
    }

    template <class U>
    constexpr void step_count(U& ui, const TimersDue& timers) {
        auto stepped = count_job_->step();
        if (!stepped) {
            count_job_.reset();
            delete_paths_.clear();
            say("Nothing was deleted: " + stepped.error().message);
            return;
        }
        if (!*stepped) {
            if (!due_line_ || timers.now >= *due_line_) {
                due_line_ = timers.now.after(file_progress_interval);
                say("Counting what would be deleted: " + detail::count_with_plural(delete_counted_.files + count_job_->counted().files, "file") + " so far. Escape stops it, and nothing has been deleted.");
            }
            ui.wake_at(timers.now);
            return;
        }
        delete_counted_ = detail::added_up(delete_counted_, count_job_->counted());
        count_job_.reset();
        ++delete_next_;
        if (delete_next_ < delete_paths_.size()) {
            start_next_count();
            if (count_job_) ui.wake_at(timers.now);
            return;
        }
        awaiting_answer_ = true;
        delete_question_ = DeleteQuestion{delete_paths_, delete_counted_};
        say("Counted. Nothing has been deleted yet.");
    }

    // One thing a turn, so Escape can stop it between them.
    template <class U>
    constexpr void step_trash(U& ui, const TimersDue& timers) {
        auto moved = source_.move_to_trash(delete_paths_[delete_next_]);
        if (!moved) {
            end_delete("Stopped: " + moved.error().message + ". ");
            return;
        }
        ++delete_next_;
        if (delete_next_ == delete_paths_.size()) {
            end_delete("");
            return;
        }
        ui.wake_at(timers.now);
    }

    // first is why it stopped, or empty when all of it went. Each thing goes whole or not at all, so what is left is plain.
    constexpr void end_delete(std::string first) {
        const std::size_t total = delete_paths_.size();
        const std::size_t moved = delete_next_;
        trashing_ = false;
        delete_paths_.clear();
        ++changes_;
        say(first.empty() ? "Moved to the trash: " + detail::delete_progress_text(delete_counted_) : first + "Moved " + decimal_text(moved, false) + " of " + decimal_text(total, false) + " selected to the trash");
    }

    template <class U>
    constexpr void step_diff(U& ui, const TimersDue& timers) {
        if (!differ_->step()) {
            ui.wake_at(timers.now);
            return;
        }
        diff_done_ = DiffReport{std::move(diff_first_), std::move(diff_second_), differ_->outcome()};
        differ_.reset();
        say("The diff is done");
    }

    constexpr void note_match(std::string where, std::string why) {
        ++match_report_.checked.differences;
        if (match_report_.findings.size() < most_match_findings) match_report_.findings.push_back(MatchFinding{std::move(where), std::move(why)});
    }

    // What the job going now has found goes into the report, and it is done with.
    constexpr void close_match_job() {
        if (!match_job_) return;
        const MatchProgress done = match_job_->progress();
        match_report_.checked = detail::added_up(match_report_.checked, done);
        for (const MatchFinding& finding : match_job_->findings()) {
            if (match_report_.findings.size() == most_match_findings) break;
            match_report_.findings.push_back(finding);
        }
        match_job_.reset();
    }

    // On to the next that can be started. One that cannot is a finding, and the rest still go.
    constexpr void start_next_match() {
        while (match_next_ < match_pairs_.size()) {
            auto started = source_.start_match(match_pairs_[match_next_].first, match_pairs_[match_next_].second);
            if (started) {
                match_job_.emplace(std::move(*started));
                return;
            }
            note_match(match_pairs_[match_next_].first, started.error().message);
            ++match_next_;
        }
        end_match(false);
    }

    template <class U>
    constexpr void step_match(U& ui, const TimersDue& timers) {
        if (!match_job_) return;
        auto stepped = match_job_->step();
        if (!stepped || *stepped) {
            if (!stepped) note_match(match_pairs_[match_next_].first, stepped.error().message);
            close_match_job();
            ++match_next_;
            start_next_match();
            if (matching_) ui.wake_at(timers.now);
            return;
        }
        if (!match_progress_line_ || timers.now >= *match_progress_line_) {
            match_progress_line_ = timers.now.after(file_progress_interval);
            say("Matching " + decimal_text(match_next_ + 1, false) + " of " + decimal_text(match_pairs_.size(), false) + ": " + detail::match_progress_text(detail::added_up(match_report_.checked, match_job_->progress())) + ". Escape stops it.");
        }
        ui.wake_at(timers.now);
    }

    constexpr void end_match(bool stopped) {
        close_match_job();
        match_report_.pairs_done = match_next_;
        match_report_.stopped = stopped;
        matching_ = false;
        match_pairs_.clear();
        say(stopped ? "Matching was stopped" : "Matching is done");
        match_done_ = std::move(match_report_);
        match_report_ = MatchReport{};
    }

    // A last name here was shown in a row, or came from a path that could be, so it can show.
    [[nodiscard]] static constexpr std::string display_name(std::string_view path) { return std::string{last_name_of(path)}; }

    // How far the whole of it is, with what a job now going has done.
    [[nodiscard]] constexpr CopyProgress progress_so_far() const noexcept { return job_ ? detail::added_up(finished_, job_->progress()) : finished_; }

    // How many of several were done, when something stops it. With one, nothing to say.
    [[nodiscard]] constexpr std::string how_many_done() const {
        if (pending_.size() < 2) return {};
        return decimal_text(next_, false) + " of " + decimal_text(pending_.size(), false) + " done. ";
    }

    // This one is done: on to the next, or the end of all of them.
    constexpr void next_or_end() {
        finished_ = progress_so_far();
        job_.reset();
        ++next_;
        if (next_ == pending_.size()) {
            // A file is only counted once it has matched, so this can say so. A folder holding no files has nothing to say it of.
            end_job("Copied " + job_name_ + " to " + job_into_ + (finished_.files != 0 ? ", and every file matched by SHA-256: " : ": "));
            return;
        }
        auto started = source_.start_copy(pending_[next_], job_into_);
        if (!started) {
            end_job("Stopped: " + started.error().message + ". " + how_many_done() + "What was copied so far is in " + job_into_ + ". ");
            return;
        }
        job_.emplace(std::move(*started));
    }

    // Past this far, a press has become a drag, so a hand that shakes a little on a click copies nothing.
    [[nodiscard]] static constexpr bool has_moved_from(Point start, Point now) noexcept {
        constexpr Width across{6};
        constexpr Height down{6};
        const Point corner{(start.left() + Width{}) - across, (start.top() + Height{}) - down};
        const Rect still{corner, Size{across + across + Width{1}, down + down + Height{1}}};
        return !still.contains(now);
    }

    constexpr void end_job(std::string first) {
        const CopyProgress done = progress_so_far();
        job_.reset();
        pending_.clear();
        ++changes_;
        say(first + detail::progress_text(done));
    }

    constexpr void say_drag() {
        const std::string name = drag_from_->size() == 1 ? display_name(drag_from_->front()) : detail::count_with_plural(drag_from_->size(), "thing");
        say(drag_into_ ? "To copy " + name + " to " + *drag_into_ + ", let go there, or press Escape to stop" : "To copy " + name + ", let go over a list, or press Escape to stop");
    }

    // Cut to fit. A path here could be shown, and what a source says of a name it cannot show must not hold one, so a refusal only means that rule was broken, and the line says so.
    constexpr void say(std::string_view text) {
        Line& shown = std::get<Line>(children_);
        if (!shown.set_text(utf8_prefix(text, max_caption_bytes))) static_cast<void>(shown.set_text("This message cannot be shown"));
    }

    Source source_;
    children_type children_;
    std::vector<std::string> copied_{};
    std::vector<std::string> pending_{};  // what this paste or drop is copying, one job after another
    std::size_t next_{};
    CopyProgress finished_{};
    std::optional<typename Source::CopyJob> job_{};
    std::string job_into_{};
    std::string job_name_{};
    std::optional<MonotonicTime> due_line_{};
    std::optional<std::vector<std::string>> armed_from_{};
    Point armed_at_{};
    std::optional<std::vector<std::string>> drag_from_{};
    std::optional<std::string> drag_into_{};
    Rect drag_pane_{};
    std::uint64_t changes_{};
    std::vector<MatchPair> match_pairs_{};
    std::size_t match_next_{};
    std::optional<typename Source::MatchJob> match_job_{};
    std::optional<MonotonicTime> match_progress_line_{};
    MatchReport match_report_{};
    std::optional<MatchReport> match_done_{};
    bool matching_{};
    std::vector<std::string> delete_paths_{};
    std::size_t delete_next_{};
    DeleteCount delete_counted_{};
    std::optional<typename Source::CountJob> count_job_{};
    bool trashing_{};
    std::optional<DeleteQuestion> delete_question_{};
    bool awaiting_answer_{};
    std::optional<LineDiffer> differ_{};
    std::string diff_first_{};
    std::string diff_second_{};
    std::optional<DiffReport> diff_done_{};
    std::optional<NameQuestion> name_question_{};
    std::optional<PictureTarget> picture_target_{};
};

template <class Tag, class Source>
[[nodiscard]] constexpr FileTransfers<Tag, Source> file_transfers(Source source) {
    return FileTransfers<Tag, Source>{std::move(source)};
}

}  // namespace jgui::inline v001
