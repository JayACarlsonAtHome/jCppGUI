# jCppGUI

A native C++ GUI toolkit in which every item is its own type. There is no type erasure, so the
compiler can catch mistakes that other toolkits only find at run time.

**This is still experimental and changing constantly. It is an early prototype, built security-first and not yet independently reviewed. Do not rely on it to protect anything yet.**
Anything here, including its API, can change without notice. It runs only on Linux under Wayland, and it has no text entry and no drop-down menus.

It opens a native Wayland window with a menu bar holding a Help button. Clicking Help shows a line of text, and clicking again hides it.
A second program, `jcppgui-viewer`, shows a redraw monitor's file and a settings record in read-only data grids. A third, `jcppgui-files`, shows two folders side by side, and copies things between them, checking each file by SHA-256.
See `docs/Maturity.md` for what works and what does not, and `docs/SECURITY.md` to report a vulnerability.

## Build

Linux, with a C++23 compiler, CMake, Ninja, and the Wayland, xkbcommon, FreeType, HarfBuzz and
fontconfig development files. On Fedora, from `Lin/v001`, `bash bsir.sh` checks for all of these, offers
what is missing, builds, tests and opens the demo. It runs a system update on every run and needs sudo,
and it stops on a system with no `dnf`. It has been run end to end only on Fedora 44. The
scripts have no shebang, because a shebang cannot be a relative path. `Lin/v001/bsir.sh` alone has the executable bit,
so from a bash shell `./bsir.sh` works as well as `bash bsir.sh`.

To build by hand, with the tools already installed:

```
cd Lin/v001
cmake --preset gcc-debug
cmake --build --preset gcc-debug
ctest --preset gcc-debug
./build/gcc-debug/jcppgui-demo
```

CTest opens no window: every test that connects reaches only the test compositor's private socket. Coverage against a real
compositor comes from a check by hand, or from configuring with `-DJCPPGUI_LIVE_TESTS=ON`, which no preset does, and which adds
`demo_smoke` and `demo_smoke_redraw_monitor`. Those two open real windows on the desktop.

The presets are `gcc-debug`, `gcc-release`, `clang-debug`, `clang-release` and `gcc-asan`, plus `gcc-warn`, which
is build only and never shipped, and `clang-fuzz`: see `docs/SECURITY.md`. Build one compiler at a time. Without a
preset, name a build type: an unnamed one stops the configure.

## A window

```cpp
struct HelpButton;
struct HelpLine;

auto ui = jgui::window(jgui::window_frame(
    jgui::menu_bar(jgui::button<HelpButton>("Help", [](auto& u) -> void {
        auto& line = u.template get<HelpLine>();
        if (line.shown()) line.hide(); else line.show(jgui::CheckedText{"Some help"});
    })),
    jgui::dynamic<HelpLine, jgui::Label<HelpLine>>()));
```

Callbacks are generic lambdas with an explicit `-> void`. `docs/Architecture.md` explains why.

## Watching redraws

`./build/gcc-debug/jcppgui-demo --redraw-monitor` runs the demo with a development tool that records each frame to `redraw_monitor.jtext`
in the application's base directory, the `vNNN` folder above the executable, so here `Lin/v001/redraw_monitor.jtext`: why it was drawn, how long each part took, which widgets asked for it, and whether it changed any pixel.
A program opts in by passing a `jgui::FrameMonitor<>` to `make_application`, from `jCppGUI/v001/redraw_monitor.hpp`, and linking `jgui_monitor`.
Without one, the toolkit has no monitor in it. See `docs/Architecture.md`.

## Recording what a window holds

`./build/gcc-debug/jcppgui-demo --record-settings` writes one row for each item in the window to `settings.jtext` in the same base directory: its ID, its parent's ID, its class name, its name and where it is. It also checks each row, for example that a child lies inside its parent, and writes anything wrong in a `Problems` section, which is normally empty. It can be used with `--redraw-monitor`.
**It is a record, for looking things up and checking them. Nothing is built from this file.** A program opts in by passing `jgui::make_settings_recorder()` to `make_application`, from `jCppGUI/v001/settings_recorder.hpp`, and linking `jgui_monitor`. `jgui::BothMonitors` holds it and a `FrameMonitor` together. See `docs/Architecture.md`.

## The viewer

The viewer shows both files, one section at a time. Close the demo first, then:

```
./build/gcc-debug/jcppgui-viewer
./build/gcc-debug/jcppgui-viewer some/other.jtext
./build/gcc-debug/jcppgui-viewer some/other.jtext some/settings.jtext
```

With no path for a file, it reads `redraw_monitor.jtext` or `settings.jtext` from the same base directory. A file it cannot read does not end it, and does not affect the other file: the window says why, at that file's buttons. It reads the full profile of jText only, and reads each file once, when it starts.

## The file manager

`jcppgui-files` shows two folders side by side, as trees, in the manner of TreeSync. Each pane is on its own: its folder, what is open, its selection, its sort and its scrolling are its own. **It changes real files, and it is as unproven as the rest of this: try it on copies first.** It can copy several things at a time, and it never moves, replaces or deletes anything: a name already taken is refused. To move something, copy it, check the line says it matched, and delete the original yourself. It has no rename and no remote side yet.

```
./build/gcc-debug/jcppgui-files
./build/gcc-debug/jcppgui-files some/folder other/folder
```

With no folder given, a pane starts where the program was started. Click a `[+]` or `[-]`, or select a row and press `+`, `-` or Return, to open and close a folder. **Up** or Backspace goes to the folder above, and **Show this folder** puts the selected folder at the top of that pane.

- **To copy,** select a row, press **Copy** or Ctrl+C, then **Paste** or Ctrl+V on either side. It goes into the folder selected there, or the folder of the row selected, or with nothing selected the folder at the top. The window stays alive during a copy, and Escape stops it.
- **Or drag** a row onto a folder, or onto a row in the folder you want, or below the last row for the folder at the top. That is also a copy, and what you dragged stays where it is. Escape stops a drag.
- **To select several,** Ctrl-click adds or takes away a row, Shift-click or Shift and an arrow reaches from the last one picked, and Ctrl+A takes everything on that side. Copy and a drag then take all of them. Something inside a selected folder goes with that folder. If one cannot go, the rest stop there, what went stays, and the line says how many were done.
- **To compare,** press **Compare**. Each side's rows are marked against the other's by name, size and time: only here, newer, older, size differs, or the same size and time. It reads no file's contents, so the same size and time is all that says, and it looks only at what shows, so a closed folder is not looked inside. Reading a side again takes its marks away.
- **When a copy ends,** each file has been flushed to the disk and read back from both sides, and only a file whose SHA-256 sums match is kept and counted. Links and folders have no sum, and the line counts anything not copied, such as a pipe. Read it before you delete an original.

A program can have the same panes with `jgui::file_pane`, `jgui::file_transfers` and `jgui::two_panes`, and bring them up when it wants by holding them in a `jgui::one_shown`. See `docs/Architecture.md`.

## Documents

- `docs/Goals.md`: purpose and priorities.
- `docs/Architecture.md`: the type rule, the layers and the layout.
- `docs/Maturity.md`: an honest account of the current state.
- `docs/SECURITY.md`: trust boundaries and hardening.
- `PROCESS.md`: how work is done here.

## License

BSD 2-Clause. See `LICENSE`.
