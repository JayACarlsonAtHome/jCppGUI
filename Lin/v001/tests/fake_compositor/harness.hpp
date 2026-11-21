// File: tests/fake_compositor/harness.hpp
// Purpose: Runs one scenario: a private XDG_RUNTIME_DIR with a socket in it, the client in a forked child, and the
//          compositor in this process. The child is the real WaylandBackend under the real demo tree, or what the
//          script chooses, so that a crash, an abort or a sanitizer report in it fails the scenario.
#pragma once

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include "compositor.hpp"
#include "jCppGUI/v001/error.hpp"

namespace jgui_test::fake {

// How a client is expected to end: cleanly, or with an error of this code whose message holds this text.
struct ClientEnd {
    bool clean{true};
    jgui::ErrorCode code{jgui::ErrorCode::System};
    std::string_view text{};
    bool any_error{};
};
inline constexpr ClientEnd ends_clean{};
[[nodiscard]] consteval ClientEnd ends_with(jgui::ErrorCode code, std::string_view text = {}) { return ClientEnd{false, code, text, false}; }
inline constexpr ClientEnd ends_with_some_error{false, jgui::ErrorCode::System, {}, true};

// Whether what a client did is what the scenario expected. Prints why when it is not.
[[nodiscard]] inline bool ended_as_expected(const ClientEnd& expected, const std::expected<void, jgui::Error>& result) {
    if (result) {
        if (expected.clean) return true;
        std::cerr << "the client ended cleanly, and an error was expected\n";
        return false;
    }
    const jgui::Error& error = result.error();
    std::cout << "client ended with: " << error.message << '\n';
    if (expected.clean) {
        std::cerr << "the client failed: " << error.message << '\n';
        return false;
    }
    if (expected.any_error) return true;
    if (error.code != expected.code || !error.message.units().contains(expected.text)) {
        std::cerr << "the client failed in another way than expected: " << error.message << '\n';
        return false;
    }
    return true;
}

inline constexpr std::chrono::milliseconds scenario_deadline{240000};
inline constexpr std::chrono::milliseconds quiet_spell{300};
inline constexpr std::chrono::milliseconds exit_deadline{60000};
inline constexpr int client_killed = 97;

// Prepares the child's environment. libwayland reads these two, and only an empty WAYLAND_SOCKET would get in the way.
inline void point_client_at(const std::string& directory) {
    ::setenv("XDG_RUNTIME_DIR", directory.c_str(), 1);
    ::setenv("WAYLAND_DISPLAY", "wl-test", 1);
    ::unsetenv("WAYLAND_SOCKET");
}

// Waits for a child to end, and kills it if it has not by the deadline. Answers its exit status or client_killed.
[[nodiscard]] inline int wait_for_exit(pid_t child, std::chrono::milliseconds deadline) {
    const auto end = std::chrono::steady_clock::now() + deadline;
    while (true) {
        int status = 0;
        const pid_t done = ::waitpid(child, &status, WNOHANG);
        if (done == child) {
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            std::cerr << "the client died from signal " << (WIFSIGNALED(status) ? WTERMSIG(status) : 0) << '\n';
            return client_killed;
        }
        if (done < 0 || std::chrono::steady_clock::now() >= end) {
            ::kill(child, SIGKILL);
            ::waitpid(child, &status, 0);
            std::cerr << "the client had to be killed\n";
            return client_killed;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
}

// Runs a client function in a child against a script in this process. True when all went as expected.
template <class Script, class Client>
[[nodiscard]] bool run_scenario(Script script, GlobalSet globals, Client client) {
    std::string directory = (std::filesystem::temp_directory_path() / "jcppgui-fake-XXXXXX").string();
    if (::mkdtemp(directory.data()) == nullptr) {
        std::cerr << "mkdtemp failed\n";
        return false;
    }
    bool passed = true;
    {
        auto listener = wire::listen_at(directory + "/wl-test");
        if (!listener) {
            std::cerr << "cannot listen: " << listener.error().message << '\n';
            std::error_code ignored;
            std::filesystem::remove_all(directory, ignored);
            return false;
        }
        std::cout.flush();
        std::cerr.flush();
        const pid_t child = ::fork();
        if (child < 0) {
            std::cerr << "fork failed\n";
            return false;
        }
        if (child == 0) {
            *listener = UniqueFd{};
            point_client_at(directory);
            std::exit(client());
        }
        // The client must connect, unless it died first.
        pollfd watch{listener->get(), POLLIN, 0};
        int ready = 0;
        const auto connect_by = std::chrono::steady_clock::now() + exit_deadline;
        while (ready == 0 && std::chrono::steady_clock::now() < connect_by) {
            ready = ::poll(&watch, 1, 100);
            if (ready == 0) {
                int status = 0;
                if (::waitpid(child, &status, WNOHANG) == child) {
                    std::cerr << "the client ended before it connected\n";
                    std::error_code ignored;
                    std::filesystem::remove_all(directory, ignored);
                    return false;
                }
            }
        }
        auto accepted = wire::accept_one(listener->get());
        if (!accepted) {
            std::cerr << "accept failed\n";
            ::kill(child, SIGKILL);
            passed = false;
        } else {
            Session session{std::move(*accepted), globals};
            Compositor<Script> compositor{session, script};
            if (!compositor.run(scenario_deadline, quiet_spell)) {
                std::cerr << "the scenario ran out of time\n";
                ::kill(child, SIGKILL);
                passed = false;
            }
            session.disconnect();
            if constexpr (requires { script.finish(session); }) script.finish(session);
            if (Script::end.clean) session.check_teardown();
            const int exit_code = wait_for_exit(child, exit_deadline);
            if (exit_code != 0) {
                std::cerr << "the client ended with status " << exit_code << '\n';
                passed = false;
            }
            for (const std::string& broken : session.violations()) {
                std::cerr << "VIOLATION: " << broken << '\n';
                passed = false;
            }
            std::cout << "commits with a buffer: " << session.commits_with_buffer() << ", acks: " << session.acks() << '\n';
        }
    }
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    return passed;
}

}  // namespace jgui_test::fake
