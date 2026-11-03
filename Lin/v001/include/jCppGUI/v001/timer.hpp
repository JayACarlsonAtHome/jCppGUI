// File: include/jCppGUI/v001/timer.hpp
// Purpose: Time in the event loop. A widget that wants to act later says when, each time the loop asks, and the loop sleeps until the earliest such
//          time and no longer. The backend owns the clock, so the core reads none itself and a test moves time by hand. All but monotonic_now is constexpr.
#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <limits>
#include <optional>

namespace jgui::inline v001 {

// A point on the monotonic clock. Moving it past what it can hold stops at the limit.
class MonotonicTime {
public:
    constexpr MonotonicTime() noexcept = default;
    constexpr explicit MonotonicTime(std::chrono::nanoseconds since_start) noexcept : ns_{since_start.count()} {}

    [[nodiscard]] constexpr MonotonicTime after(std::chrono::nanoseconds delay) const noexcept {
        constexpr std::int64_t most = std::numeric_limits<std::int64_t>::max();
        constexpr std::int64_t least = std::numeric_limits<std::int64_t>::min();
        const std::int64_t by = delay.count();
        if (by > 0 && ns_ > most - by) return MonotonicTime{std::chrono::nanoseconds{most}};
        if (by < 0 && ns_ < least - by) return MonotonicTime{std::chrono::nanoseconds{least}};
        return MonotonicTime{std::chrono::nanoseconds{ns_ + by}};
    }

    friend constexpr auto operator<=>(const MonotonicTime&, const MonotonicTime&) = default;

private:
    friend constexpr int poll_timeout_ms(std::optional<MonotonicTime> deadline, MonotonicTime now) noexcept;

    std::int64_t ns_{};
};

// What poll is told to wait, from a deadline and the time now: -1 with no deadline, 0 once it has come, and otherwise the wait in whole milliseconds,
// rounded up, because rounding down would spin through the last millisecond. A wait too long for an int stops at the longest one, and the loop just asks again.
[[nodiscard]] constexpr int poll_timeout_ms(std::optional<MonotonicTime> deadline, MonotonicTime now) noexcept {
    if (!deadline) return -1;
    if (*deadline <= now) return 0;
    // The later minus the earlier, as unsigned numbers, is right for any two values, however far apart.
    const std::uint64_t ns = static_cast<std::uint64_t>(deadline->ns_) - static_cast<std::uint64_t>(now.ns_);
    constexpr std::uint64_t ns_per_ms = 1'000'000;
    const std::uint64_t ms = ns / ns_per_ms + (ns % ns_per_ms != 0 ? 1U : 0U);
    constexpr std::uint64_t longest = std::numeric_limits<int>::max();
    return ms >= longest ? std::numeric_limits<int>::max() : static_cast<int>(ms);
}

// The real clock. Only a real backend reads it.
[[nodiscard]] inline MonotonicTime monotonic_now() noexcept {
    const auto since = std::chrono::steady_clock::now().time_since_epoch();
    return MonotonicTime{std::chrono::duration_cast<std::chrono::nanoseconds>(since)};
}

// The notice the loop sends each widget on each turn, however it woke, so a flood of other events cannot hold a deadline back. A widget acts on what has
// come due, and says when next it wants to act with ui.wake_at. Asking is only for this notice: the answer is thrown away at the next one.
struct TimersDue {
    MonotonicTime now{};
};

// A choice a control shows at once, and whatever it controls takes up only after a wait since the last time it was chosen. A press has no time to
// hand, so the wait starts at the next TimersDue, which follows that same delivery.
template <class T>
class DelayedChoice {
public:
    [[nodiscard]] constexpr T chosen() const noexcept { return chosen_; }
    [[nodiscard]] constexpr T applied() const noexcept { return applied_; }

    constexpr void choose(T value) noexcept {
        chosen_ = value;
        restart_ = true;
    }

    // At once, with no wait, and ends any wait there was.
    constexpr void set_now(T value) noexcept {
        chosen_ = value;
        applied_ = value;
        restart_ = false;
        due_.reset();
    }

    // True when what is applied has just changed. Coming back to what was applied before the wait ended changes nothing.
    template <class U>
    [[nodiscard]] constexpr bool step(const TimersDue& timers, std::chrono::nanoseconds wait, U& ui) noexcept {
        if (restart_) {
            restart_ = false;
            due_ = timers.now.after(wait);
        }
        if (!due_) return false;
        if (timers.now < *due_) {
            ui.wake_at(*due_);
            return false;
        }
        due_.reset();
        const bool changed = applied_ != chosen_;
        applied_ = chosen_;
        return changed;
    }

private:
    T chosen_{};
    T applied_{};
    std::optional<MonotonicTime> due_{};
    bool restart_{};
};

}  // namespace jgui::inline v001
