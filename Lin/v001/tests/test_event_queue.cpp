// File: tests/test_event_queue.cpp
// Purpose: The typed backend event queue: arrival order across types and its limit at run time, and how motion merges,
//          up to a full queue, in static_assert.
#include <array>
#include <cstddef>
#include <cstdint>

#include "check.hpp"
#include "jCppGUI/v001/backend.hpp"
#include "test_access.hpp"

namespace {

struct Sink {
    std::array<int, 3> seen{};
    std::size_t count{};
    void record(int kind) {
        if (count < seen.size()) seen[count] = kind;
        ++count;
    }
    void on(const jgui::CloseEvent&) { record(1); }
    void on(const jgui::ResizeEvent&) { record(2); }
    void on(const jgui::PointerMove&) { record(3); }
    void on(const jgui::PointerButton&) { record(4); }
    void on(const jgui::PointerLeave&) { record(5); }
    void on(const jgui::KeyEvent&) { record(6); }
    void on(const jgui::PointerScroll&) { record(7); }
};

using namespace jgui;

// Motion merges only with the event pushed just before it, and only when no field would be lost.
struct CoalesceSink {
    std::array<int, 4> seen{};
    std::array<PointerMove, 4> moves{};
    std::size_t count{};
    std::size_t move_count{};
    PointerMove last_move{};
    constexpr void kind(int which) {
        if (count < seen.size()) seen[count] = which;
        ++count;
    }
    constexpr void on(const CloseEvent&) { kind(1); }
    constexpr void on(const ResizeEvent&) { kind(2); }
    constexpr void on(const PointerMove& event) {
        kind(3);
        if (move_count < moves.size()) moves[move_count] = event;
        ++move_count;
        last_move = event;
    }
    constexpr void on(const PointerButton&) { kind(4); }
    constexpr void on(const PointerLeave&) { kind(5); }
    constexpr void on(const KeyEvent&) { kind(6); }
    constexpr void on(const PointerScroll&) { kind(7); }
};

constexpr PointerMove motion_to(std::int32_t x, bool captured = false, bool entered = false) {
    return PointerMove{Point{Left{x}, Top{x}}, captured, entered};
}
constexpr PointerButton a_press{Point{}, MouseButton::Left, ButtonState::Pressed, jgui_test::serial(1)};

consteval bool same_move(const PointerMove& a, const PointerMove& b) {
    return a.position == b.position && a.captured == b.captured && a.entered == b.entered;
}

consteval bool a_run_of_motion_takes_one_slot() {
    BackendEventQueue queue;
    for (std::int32_t x = 1; x <= 100; ++x) {
        if (!queue.push(motion_to(x)) || queue.size() != 1) return false;
    }
    CoalesceSink sink;
    queue.drain(sink);
    return sink.count == 1 && same_move(sink.moves[0], motion_to(100)) && queue.empty();
}
static_assert(a_run_of_motion_takes_one_slot());

consteval bool motion_press_motion_stays_three() {
    BackendEventQueue queue;
    if (!queue.push(motion_to(1)) || !queue.push(a_press) || !queue.push(motion_to(2)) || queue.size() != 3) return false;
    CoalesceSink sink;
    queue.drain(sink);
    return sink.count == 3 && sink.seen == std::array<int, 4>{3, 4, 3, 0} && same_move(sink.moves[0], motion_to(1)) && same_move(sink.moves[1], motion_to(2));
}
static_assert(motion_press_motion_stays_three());

consteval bool motion_after_another_event_does_not_merge() {
    BackendEventQueue queue;
    // The last motion is still the back of its own queue, and something else came after it.
    if (!queue.push(motion_to(1)) || !queue.push(PointerLeave{}) || !queue.push(motion_to(2)) || !queue.push(motion_to(3)) || !queue.push(KeyEvent{Key::Space}) || !queue.push(motion_to(4)))
        return false;
    if (queue.size() != 5) return false;
    CoalesceSink sink;
    queue.drain(sink);
    if (sink.count != 5 || sink.seen != std::array<int, 4>{3, 5, 3, 6} || sink.move_count != 3) return false;
    return same_move(sink.moves[0], motion_to(1)) && same_move(sink.moves[1], motion_to(3)) && same_move(sink.moves[2], motion_to(4));
}
static_assert(motion_after_another_event_does_not_merge());

consteval bool motion_after_a_drain_is_new() {
    BackendEventQueue queue;
    if (!queue.push(motion_to(1))) return false;
    CoalesceSink first;
    queue.drain(first);
    if (!queue.push(motion_to(2)) || queue.size() != 1) return false;
    CoalesceSink second;
    queue.drain(second);
    return first.count == 1 && second.count == 1 && same_move(second.moves[0], motion_to(2));
}
static_assert(motion_after_a_drain_is_new());

// Two motions that differ in a flag each say something the other does not, so both are kept. Equal flags merge.
consteval bool kept_apart_by(const PointerMove& older, const PointerMove& newer) {
    BackendEventQueue queue;
    if (!queue.push(older) || !queue.push(newer) || queue.size() != 2) return false;
    CoalesceSink sink;
    queue.drain(sink);
    return sink.count == 2 && same_move(sink.moves[0], older) && same_move(sink.moves[1], newer);
}
consteval bool merged_keeping_flags(bool captured, bool entered) {
    BackendEventQueue queue;
    if (!queue.push(motion_to(1, captured, entered)) || !queue.push(motion_to(2, captured, entered)) || queue.size() != 1) return false;
    CoalesceSink sink;
    queue.drain(sink);
    return same_move(sink.moves[0], motion_to(2, captured, entered));
}
static_assert(kept_apart_by(motion_to(1, false, true), motion_to(2)), "an enter is not lost to the motion after it");
static_assert(kept_apart_by(motion_to(1), motion_to(2, false, true)), "nor to the motion before it");
static_assert(kept_apart_by(motion_to(1, true), motion_to(2)));
static_assert(kept_apart_by(motion_to(1), motion_to(2, true)));
static_assert(merged_keeping_flags(false, true) && merged_keeping_flags(true, false) && merged_keeping_flags(true, true));

// A full queue with motion at its end: motion that merges needs no room, and anything else is refused.
consteval bool full_queue_ending_in_motion() {
    BackendEventQueue queue;
    for (std::size_t i = 0; i + 1 < BackendEventQueue::limit; ++i) {
        if (!queue.push(PointerLeave{})) return false;
    }
    if (!queue.push(motion_to(1)) || queue.size() != BackendEventQueue::limit) return false;
    if (queue.push(a_press) || queue.push(motion_to(2, false, true)) || queue.size() != BackendEventQueue::limit) return false;
    if (!queue.push(motion_to(3)) || queue.size() != BackendEventQueue::limit) return false;
    CoalesceSink sink;
    queue.drain(sink);
    return sink.count == BackendEventQueue::limit && sink.move_count == 1 && same_move(sink.last_move, motion_to(3));
}
static_assert(full_queue_ending_in_motion());

consteval bool full_queue_refuses_motion_after_another_event() {
    BackendEventQueue queue;
    if (!queue.push(motion_to(1))) return false;
    for (std::size_t i = 1; i < BackendEventQueue::limit; ++i) {
        if (!queue.push(PointerLeave{})) return false;
    }
    return !queue.push(motion_to(2)) && queue.size() == BackendEventQueue::limit;
}
static_assert(full_queue_refuses_motion_after_another_event());

}  // namespace

int main() {
    using namespace jgui;
    jgui_test::Checker check;

    BackendEventQueue queue;
    check(queue.push(KeyEvent{Key::Space}) && queue.push(PointerMove{}) && queue.push(KeyEvent{Key::Space}), "events are kept");
    Sink ordered;
    queue.drain(ordered);
    check(ordered.count == 3 && ordered.seen == std::array{6, 3, 6}, "delivered in arrival order");
    check(queue.empty(), "drained");

    BackendEventQueue full;
    bool all_kept = true;
    for (std::size_t i = 0; i < BackendEventQueue::limit; ++i) all_kept = full.push(PointerLeave{}) && all_kept;
    check(all_kept, "kept up to the limit");
    check(!full.push(PointerLeave{}), "refused above the limit");
    Sink counted;
    full.drain(counted);
    check(counted.count == BackendEventQueue::limit && full.empty(), "all delivered");
    return check.finish();
}
