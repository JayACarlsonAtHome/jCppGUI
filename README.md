# jCppGUI

A native C++ GUI toolkit in which every item is its own type. There is no type erasure, so the
compiler can catch mistakes that other toolkits only find at run time.

**This is a skeleton.** It opens a native Wayland window with a menu bar holding a Help button.
Clicking Help shows a line of text, and clicking again hides it. See `docs/Maturity.md` for what
works and what does not.

## Build

Linux, with a C++23 compiler, CMake, Ninja, and the Wayland, xkbcommon, FreeType, HarfBuzz and
fontconfig development files. `Lin/v001/bsir.sh` checks for all of these, offers what is missing,
builds, tests and opens the demo. It runs a system update on every run and needs sudo.

To build by hand, with the tools already installed:

```
cd Lin/v001
cmake --preset gcc-debug
cmake --build --preset gcc-debug
ctest --preset gcc-debug
./build/gcc-debug/jcppgui-demo
```

The presets are `gcc-debug`, `gcc-release`, `clang-debug`, `clang-release` and `gcc-asan`. Build
one compiler at a time.

## A window

```cpp
struct HelpButton;
struct HelpLine;

auto ui = jgui::window(
    jgui::menu_bar(jgui::button<HelpButton>("Help", [](auto& u) -> void {
        auto& line = u.template get<HelpLine>();
        if (line.shown()) line.hide(); else line.show("Some help");
    })),
    jgui::dynamic<HelpLine, jgui::Label<HelpLine>>());
```

Callbacks are generic lambdas with an explicit `-> void`. `docs/Architecture.md` explains why.

## Documents

- `docs/Goals.md`: purpose and priorities.
- `docs/Architecture.md`: the type rule, the layers and the layout.
- `docs/Maturity.md`: an honest account of the current state.
- `docs/SECURITY.md`: trust boundaries and hardening.
- `PROCESS.md`: how work is done here.

## License

BSD 2-Clause. See `LICENSE`.
