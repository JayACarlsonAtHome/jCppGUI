// File: tests/test_event_queue.cpp
// Purpose: The typed backend event queue at run time: arrival order across types, and its limit.
#include <array>
#include <cstddef>

#include "check.hpp"
#include "jCppGUI/v001/backend.hpp"

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
};

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
