// File: tests/test_timer.cpp
// Purpose: Time in the event loop. In static_assert: what poll is told at the edges, DelayedChoice, and the whole loop with widgets that ask to be woken, in a frame, in a
//          child that is not shown, and in a Dynamic, with a flood of other events going past.
#include <chrono>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#include "jCppGUI/v001/headless_backend.hpp"
#include "jCppGUI/v001/jgui.hpp"
#include "test_access.hpp"

// Outside the anonymous namespace because nothing here shapes text, and a compiler rightly reports a member nobody calls on a type with internal linkage.
struct NoText {
    [[nodiscard]] constexpr std::expected<jgui::TextRun, jgui::Error> shape(std::string_view) const { return jgui::TextRun{}; }
    [[nodiscard]] constexpr jgui::Height line_height() const noexcept { return jgui::Height{4}; }
};

namespace {

using namespace jgui;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;

constexpr std::int64_t most = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t least = std::numeric_limits<std::int64_t>::min();
constexpr int int_most = std::numeric_limits<int>::max();

[[nodiscard]] constexpr MonotonicTime at_ns(std::int64_t ns) { return MonotonicTime{nanoseconds{ns}}; }

// --- What poll is told ---
static_assert(poll_timeout_ms(std::nullopt, at_ns(0)) == -1, "no deadline: sleep until something comes");
static_assert(poll_timeout_ms(std::nullopt, at_ns(most)) == -1);
static_assert(poll_timeout_ms(at_ns(5), at_ns(5)) == 0, "it has come");
static_assert(poll_timeout_ms(at_ns(5), at_ns(6)) == 0, "it is past");
static_assert(poll_timeout_ms(at_ns(least), at_ns(most)) == 0, "as far past as can be, with no overflow");
static_assert(poll_timeout_ms(at_ns(1), at_ns(0)) == 1, "a nanosecond away is a millisecond, or it would spin");
static_assert(poll_timeout_ms(at_ns(999'999), at_ns(0)) == 1);
static_assert(poll_timeout_ms(at_ns(1'000'000), at_ns(0)) == 1);
static_assert(poll_timeout_ms(at_ns(1'000'001), at_ns(0)) == 2);
static_assert(poll_timeout_ms(at_ns(300'000'000), at_ns(0)) == 300);
static_assert(poll_timeout_ms(at_ns(std::int64_t{int_most} * 1'000'000 - 1), at_ns(0)) == int_most, "the longest wait there is, rounded up to it");
static_assert(poll_timeout_ms(at_ns(std::int64_t{int_most} * 1'000'000), at_ns(0)) == int_most);
static_assert(poll_timeout_ms(at_ns(std::int64_t{int_most} * 1'000'000 + 1), at_ns(0)) == int_most, "a longer one stops there, and the loop asks again");
static_assert(poll_timeout_ms(at_ns(most), at_ns(0)) == int_most, "huge");
static_assert(poll_timeout_ms(at_ns(most), at_ns(least)) == int_most, "as far away as can be, with no overflow");
static_assert(poll_timeout_ms(at_ns(0), at_ns(least)) == int_most);

// --- Moving a time stops at the limit ---
static_assert(at_ns(most).after(nanoseconds{1}) == at_ns(most));
static_assert(at_ns(most - 1).after(nanoseconds{1}) == at_ns(most));
static_assert(at_ns(1).after(nanoseconds{most}) == at_ns(most));
static_assert(at_ns(least).after(nanoseconds{-1}) == at_ns(least));
static_assert(at_ns(-1).after(nanoseconds{least}) == at_ns(least));
static_assert(at_ns(0).after(nanoseconds{least}) == at_ns(least));
static_assert(at_ns(5).after(nanoseconds{-7}) == at_ns(-2));
static_assert(at_ns(0).after(milliseconds{300}) == at_ns(300'000'000));

// --- A choice with a wait ---
struct AsksToWake {
    std::optional<MonotonicTime> asked{};
    constexpr void wake_at(MonotonicTime when) noexcept { asked = when; }
};

[[nodiscard]] constexpr bool a_delayed_choice_waits() {
    DelayedChoice<int> choice;
    AsksToWake ui;
    const nanoseconds wait{10};
    if (choice.step(TimersDue{at_ns(1)}, wait, ui) || ui.asked) return false;  // nothing to do, and nothing asked
    choice.choose(4);
    if (choice.chosen() != 4 || choice.applied() != 0) return false;
    if (choice.step(TimersDue{at_ns(100)}, wait, ui) || ui.asked != at_ns(110)) return false;
    if (choice.step(TimersDue{at_ns(109)}, wait, ui) || choice.applied() != 0) return false;
    choice.choose(5);
    if (choice.step(TimersDue{at_ns(109)}, wait, ui) || ui.asked != at_ns(119)) return false;
    ui.asked.reset();
    if (!choice.step(TimersDue{at_ns(119)}, wait, ui) || choice.applied() != 5 || ui.asked) return false;
    // Back to what it was: the wait ends with no change.
    choice.choose(9);
    choice.choose(5);
    if (choice.step(TimersDue{at_ns(200)}, wait, ui)) return false;
    if (choice.step(TimersDue{at_ns(210)}, wait, ui) || choice.applied() != 5) return false;
    // At once, and with the wait gone.
    choice.choose(9);
    choice.set_now(1);
    ui.asked.reset();
    return !choice.step(TimersDue{at_ns(300)}, wait, ui) && !ui.asked && choice.applied() == 1 && choice.chosen() == 1;
}
static_assert(a_delayed_choice_waits());

// --- The loop, with a widget that wants to be woken ---
template <class Tag>
struct Waker {
    using tag = Tag;
    std::optional<MonotonicTime> wanted{};
    int turns{};
    int fired{};
    bool draws{};
    bool damaged{};
    [[nodiscard]] constexpr Rect bounds() const noexcept { return Rect{}; }
    constexpr void paint(const TreeKey&, Canvas&) {}
    template <class Shaper>
    constexpr std::expected<Rect, Error> layout(const TreeKey&, Rect area, const Shaper&) {
        return area;
    }
    template <class U>
    constexpr bool handle(const TreeKey&, U&, const QueryDamage&) {
        return damaged;
    }
    template <class U, class E>
    constexpr bool handle(const TreeKey&, U&, const E&) {
        return false;
    }
    template <class U>
    constexpr void notify(const TreeKey&, U&, const ClearDamage&) {
        damaged = false;
    }
    template <class U>
    constexpr void notify(const TreeKey&, U& ui, const TimersDue& timers) {
        ++turns;
        if (!wanted) return;
        if (timers.now >= *wanted) {
            wanted.reset();
            ++fired;
            damaged = draws;
            return;
        }
        ui.wake_at(*wanted);
    }
    template <class U, class E>
    constexpr void notify(const TreeKey&, U&, const E&) {}
};
struct FirstTag;
struct SecondTag;
struct ThirdTag;
struct SwitchTag;
struct ItemsTag;
struct ItemTag;
static_assert(Widget<Waker<FirstTag>>);

constexpr Size window_size{Width{50}, Height{50}};

[[nodiscard]] constexpr bool the_earliest_deadline_is_the_one() {
    ApplicationCore core{HeadlessBackend{window_size}, window(window_frame(Waker<FirstTag>{}, Waker<SecondTag>{}), one_shown<SwitchTag>(Waker<ThirdTag>{}))};
    auto& first = core.ui().get<FirstTag>();
    auto& second = core.ui().get<SecondTag>();
    auto& hidden = core.ui().get<ThirdTag>();  // never shown, and it still counts
    if (!core.process_pending(NoText{}) || core.next_wake()) return false;
    first.wanted = at_ns(500);
    second.wanted = at_ns(300);
    hidden.wanted = at_ns(400);
    if (!core.process_pending(NoText{}) || core.next_wake() != at_ns(300)) return false;
    jgui_test::backend(core).advance_time(nanoseconds{300});
    if (!core.process_pending(NoText{}) || second.fired != 1 || first.fired != 0 || core.next_wake() != at_ns(400)) return false;
    jgui_test::backend(core).advance_time(nanoseconds{100});
    if (!core.process_pending(NoText{}) || hidden.fired != 1 || core.next_wake() != at_ns(500)) return false;
    jgui_test::backend(core).advance_time(nanoseconds{1000});
    return core.process_pending(NoText{}).has_value() && first.fired == 1 && !core.next_wake();
}
static_assert(the_earliest_deadline_is_the_one());

// What a timer damages is drawn in that same turn, and nothing else draws: a turn with nothing due draws nothing.
[[nodiscard]] constexpr bool a_timer_draws_only_when_it_says_so() {
    ApplicationCore core{HeadlessBackend{window_size}, window(Waker<FirstTag>{})};
    auto& waker = core.ui().get<FirstTag>();
    if (!core.process_pending(NoText{})) return false;
    const std::uint64_t frames = core.frames();
    waker.wanted = at_ns(10);
    waker.draws = true;
    if (!core.process_pending(NoText{}) || core.frames() != frames) return false;
    jgui_test::backend(core).advance_time(nanoseconds{10});
    if (!core.process_pending(NoText{}) || core.frames() != frames + 1) return false;
    // After that frame the timers run once more, so a turn with a frame has two, and one with none has one.
    const int turns = waker.turns;
    if (!core.process_pending(NoText{}) || waker.turns != turns + 1 || core.frames() != frames + 1) return false;
    return !core.next_wake();
}
static_assert(a_timer_draws_only_when_it_says_so());

// A flood of other events goes past, turn after turn, and the deadline is not put off: only the clock decides.
[[nodiscard]] constexpr bool a_flood_does_not_hold_a_deadline_back() {
    ApplicationCore core{HeadlessBackend{window_size}, window(Waker<FirstTag>{})};
    auto& waker = core.ui().get<FirstTag>();
    if (!core.process_pending(NoText{})) return false;
    waker.wanted = at_ns(100);
    HeadlessBackend& backend = jgui_test::backend(core);
    for (std::int32_t i = 0; i < 99; ++i) {
        backend.inject(PointerMove{Point{Left{i}, Top{i}}});
        backend.inject(KeyEvent{Key::Down, ModifierMask{}, true});
        backend.advance_time(nanoseconds{1});
        if (!core.process_pending(NoText{})) return false;
        if (waker.fired != 0) return false;
    }
    backend.inject(PointerMove{Point{Left{1}, Top{1}}});
    backend.advance_time(nanoseconds{1});
    return core.process_pending(NoText{}).has_value() && waker.fired == 1;
}
static_assert(a_flood_does_not_hold_a_deadline_back());

// A wait with events queued delivers them and leaves the clock alone, and one with none runs the clock to the deadline.
[[nodiscard]] constexpr bool a_headless_wait_runs_the_clock_only_when_idle() {
    ApplicationCore core{HeadlessBackend{window_size}, window(Waker<FirstTag>{})};
    auto& waker = core.ui().get<FirstTag>();
    HeadlessBackend& backend = jgui_test::backend(core);
    waker.wanted = at_ns(1000);
    backend.inject(CloseEvent{});  // delivered at the first wait, with the clock where it was
    const auto closed = core.run(NoText{});
    if (!closed || backend.now() != MonotonicTime{} || waker.fired != 0 || core.next_wake()) return false;
    ApplicationCore other{HeadlessBackend{window_size}, window(Waker<FirstTag>{})};
    other.ui().get<FirstTag>().wanted = at_ns(1000);
    const auto ended = other.run(NoText{});
    return !ended && ended.error().code == ErrorCode::WouldBlock && jgui_test::backend(other).now() == at_ns(1000) && other.ui().get<FirstTag>().fired == 1;
}
static_assert(a_headless_wait_runs_the_clock_only_when_idle());

// An item of a Dynamic is told too, and one that dies takes its deadline with it, harmlessly.
[[nodiscard]] consteval bool an_item_can_be_woken() {
    ApplicationCore core{HeadlessBackend{window_size}, window(Dynamic<ItemsTag, Waker<ItemTag>, std::vector>{})};
    auto& items = core.ui().get<ItemsTag>();
    auto handle = items.create();
    if (!handle) return false;
    items.find(*handle)->wanted = at_ns(50);
    if (!core.process_pending(NoText{}) || core.next_wake() != at_ns(50)) return false;
    items.destroy(*handle);
    return core.process_pending(NoText{}).has_value() && !core.next_wake();
}
static_assert(an_item_can_be_woken());

}  // namespace

int main() {
    std::cout << "time in the loop holds at compile time\n";
    return 0;
}
