// File: include/jCppGUI/v001/file_transfers.hpp
// Purpose: FileTransfers<Tag, Source>: what goes between two panes. A line of its own, what was last copied, one thing or several, a copy under way, and a drag. A copy goes a step on each turn of the
//          loop, and asks for the next turn at once, so the window stays alive and Escape can stop it, with no thread. Nothing here replaces or deletes anything: the source refuses
//          a name that is taken. jgui.hpp does not include it. It must come before the panes in the tree: it is what turns a press into a drag, and each pane must then see that on the
//          same motion, because a flood of motion can arrive as one event.
#pragma once

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
#include "label.hpp"
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

template <class Tag, FileSource Source>
class FileTransfers {
    using Line = Label<FileTransfersLine<Tag>>;

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

    // Says something from elsewhere on this line.
    constexpr void tell(std::string_view text) { say(text); }

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
    // What can be refused from the paths alone is refused before anything is made. A name taken in the folder is found by the source when that one's turn comes, and it stops there,
    // with what went before still there.
    constexpr void start_copying(std::vector<std::string> from, const std::string& into) {
        if (job_) {
            say("A copy is still going. Escape stops it.");
            return;
        }
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
};

template <class Tag, class Source>
[[nodiscard]] constexpr FileTransfers<Tag, Source> file_transfers(Source source) {
    return FileTransfers<Tag, Source>{std::move(source)};
}

}  // namespace jgui::inline v001
