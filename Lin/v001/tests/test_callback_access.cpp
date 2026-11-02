// File: tests/test_callback_access.cpp
// Purpose: What a callback that reaches its own application can still try, at run time. Each is a real program
//          mistake, and each ends in an abort instead of reaching the tree in the middle of a delivery. What a
//          callback must not even compile is in tests/compile_fail/.
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <utility>

#include "app_helpers.hpp"
#include "check.hpp"

namespace {

struct Target;

constexpr jgui::Size window_size{jgui::Width{200}, jgui::Height{100}};

// A callback cannot name the application it is part of directly, because the application's type holds the
// callback's. A named type can, through a pointer to something defined later.
enum class Attempt { RunLoop, ProcessPending, FirstFrame, MoveWindow, MoveApplication };

struct Slot;

struct OnClick {
    Slot* slot;
    Attempt attempt;
    template <class U>
    void operator()(U& ui) const;
};

using App = jgui::Application<jgui::HeadlessBackend, jgui::Window<jgui::WindowFrame<jgui::DefaultFrameMetrics, jgui::Button<Target, OnClick>>>>;

struct Slot {
    App* app;
};

template <class U>
void OnClick::operator()(U& ui) const {
    switch (attempt) {
        case Attempt::RunLoop: static_cast<void>(slot->app->run()); return;
        case Attempt::ProcessPending: static_cast<void>(slot->app->process_pending()); return;
        case Attempt::FirstFrame: static_cast<void>(slot->app->present_first_frame()); return;
        case Attempt::MoveWindow: {
            U stolen{std::move(ui)};
            static_cast<void>(stolen);
            return;
        }
        case Attempt::MoveApplication: {
            App stolen{std::move(*slot->app)};
            static_cast<void>(stolen);
            return;
        }
        default: return;
    }
}

[[nodiscard]] bool aborts_on_click(Attempt attempt) {
    const pid_t child = ::fork();
    if (child == 0) {
        Slot slot{nullptr};
        auto app = jgui::make_application(jgui::HeadlessBackend{window_size},
                                          jgui::window(jgui::window_frame(jgui::button<Target>("Go", OnClick{&slot, attempt}))));
        if (!app || !app->process_pending()) ::_exit(2);
        slot.app = &*app;
        jgui_test::click(*app, jgui_test::centre(app->ui().get<Target>().bounds()));
        ::_exit(0);  // reached only if nothing fired
    }
    int status = 0;
    if (child < 0 || ::waitpid(child, &status, 0) != child) return false;
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

}  // namespace

int main() {
    jgui_test::Checker check;
    check(aborts_on_click(Attempt::RunLoop), "running the loop from a callback aborts");
    check(aborts_on_click(Attempt::ProcessPending), "so does processing what is pending");
    check(aborts_on_click(Attempt::FirstFrame), "and waiting for the first frame");
    check(aborts_on_click(Attempt::MoveWindow), "moving the window out from under its own tree aborts");
    check(aborts_on_click(Attempt::MoveApplication), "and so does moving the application");

    // The same application from outside any callback goes on working.
    Slot slot{nullptr};
    auto app = jgui::make_application(jgui::HeadlessBackend{window_size},
                                      jgui::window(jgui::window_frame(jgui::button<Target>("Go", OnClick{&slot, Attempt::RunLoop}))));
    check(app.has_value() && app->process_pending().has_value() && app->process_pending().has_value(), "running it twice in turn is fine");
    return check.finish();
}
