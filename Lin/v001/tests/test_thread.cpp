// File: tests/test_thread.cpp
// Purpose: The UI-thread rule: using an Application from another thread aborts, in every build.
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <thread>

#include "app_helpers.hpp"
#include "check.hpp"

namespace {

struct Unused;

}  // namespace

int main() {
    jgui_test::Checker check;
    auto app = jgui::make_application(jgui::HeadlessBackend{jgui::Size{100, 100}}, jgui::window(jgui::label<Unused>("x")));
    check(app.has_value() && app->process_pending().has_value(), "the owning thread may use it");

    const pid_t child = ::fork();
    check(child >= 0, "fork");
    if (child == 0) {
        std::thread other{[&] { static_cast<void>(app->process_pending()); }};
        other.join();
        ::_exit(0);  // reached only if the check did not fire
    }
    int status = 0;
    check(::waitpid(child, &status, 0) == child, "waitpid");
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT, "another thread aborts with SIGABRT");
    return check.finish();
}
