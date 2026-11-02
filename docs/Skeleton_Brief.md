# jCppGUI: Lin/v001 skeleton brief

Status: approved by the owner 2026-11-02. Delete this file when the skeleton is accepted; its
lasting content moves into `docs/Architecture.md` and `docs/SECURITY.md`.

## Goal

The owner's first goal: **an empty window with a menu bar holding a Help button.** It is a native
Wayland window, and the same behaviour is tested with no screen. It exists to prove the foundations
that are expensive to change later. Widget count does not matter yet.

Clicking Help shows a short line of text in the window's empty area, and clicking it again hides
the line. A drop-down menu needs Wayland popups and comes in a later slice.

Priorities, in order: **secure first, efficient second.** A check is removed only when a
measurement shows that it matters.

## Clean-room rule

jCppGUI takes concepts from U++ and no code. Do not open, read, search or copy from `uppsrc`,
`upp_basefix`, `jText_GUI/gui`, or any other U++ source. Do not reuse U++ class names. The concepts
are listed below, and that list is all the implementer needs.

Concepts to carry over:

- A plain widget tree in which parents contain children.
- Clear ownership: everything belongs somewhere.
- Few dependencies and fast build cycles.
- Later, not in this slice: display objects separate from data conversion, and layouts as data.
  Layouts must become types at build time (generated or `constexpr`), never interpreted into
  erased objects at run time.

## Type rule: the owner's absolute rule

**Objects come from templates, each item is its own type, and there is NO type erasure.** The
compiler must be able to catch mistakes.

Banned in all our code, including tests and the demo:

- `std::function`, `std::move_only_function`, `std::copy_only_function`, `std::function_ref`
- `std::any`, `std::shared_ptr` (it erases its deleter), `dynamic_cast`, `typeid`
- `virtual`, abstract base classes, and any base-class pointer or container of widgets
- `void*`, and function pointers used to hide a type
- Hand-written erasure of any kind, under any name

Allowed:

- Class templates, concepts, deducing `this`, `std::tuple`, fold expressions.
- `std::variant` **only as a last resort**, over a closed list of types, when overloads and templates
  truly cannot do the job. Any use needs a one-line comment saying why, and the report must list it.
- `std::unique_ptr<T>` of a concrete `T` with a stateless deleter type.

**The one exception is the C boundary, and it is temporary.** libwayland hands listeners a
`void* data` and requires C function pointers. This is allowed only in one templated trampoline
helper in `src/wayland/`, which casts straight back to the exact type it was registered with. The
pointer never leaves that helper. The exception ends when our own Wayland client replaces libwayland,
which is the step after this prototype.

This design is proven by a prototype compiled with g++ 16 and clang 22 under ASan and UBSan:
tag lookup, generic callbacks, self-destruction during a callback, and compile-time rejection of a
wrong tag and a wrong handle type.

## Verified facts (2026-11-02)

| Item | State |
|---|---|
| Compilers | g++ 16.2.1, clang 22.1.8, both using libstdc++ |
| Build tools | CMake 4.3.0, Ninja 1.13.2, `wayland-scanner` present |
| Libraries present | wayland-client 1.26.0, wayland-protocols 1.49, xkbcommon 1.13.1, FreeType, HarfBuzz, fontconfig 2.17.0 |
| Session | GNOME on Wayland |
| Not present | Weston, so there is no nested or headless compositor for tests |

**GNOME does not draw title bars for Wayland clients.** It does not offer the server-side
decoration protocol, so a bare `xdg_toplevel` has no title bar, close button or resize border. This
slice therefore draws its own minimal window frame. We do not use libdecor, because the frame is a
toolkit concern we want to control.

## Repository layout

```
jCppGUI/
  README.md  PROCESS.md  LICENSE (BSD 2-Clause, the same license family as U++; already in place)
  docs/      Goals.md  Architecture.md  Maturity.md  SECURITY.md
  Lin/
    paths.conf          only if a location outside the repository is ever needed
    v001/               C++23, self-contained
      bsir.sh  CMakeLists.txt  CMakePresets.json  Setup/recipes.conf
      include/jCppGUI/v001/...
      src/...
      tests/...
      demo/...
```

- `Win/` and `Mac/` sit beside `Lin/` when they start. Do not create them now, and do not create
  `Lin/v002` or any C++26 code.
- Each `<OS>/<version>` is a self-contained world. Do not add a shared source directory.
- Namespace: `namespace jgui::inline v001`, so `jgui::Button` works for consumers.
- No absolute paths anywhere. Everything is found relative to the script or `CMAKE_SOURCE_DIR`.
- Textual headers only in this slice. A modules front-end comes later.
- The repository exists, with branches `main`, `test-work` and `dev-work` and a remote named
  `gitlab`. Work only on `dev-work`. Commit there when done. Do not push, and do not touch `main` or
  `test-work`.

## Build rules

Both g++ and clang++ must build and pass. Build one compiler at a time; `-j` within a build is fine.

- `-std=c++23`, `CMAKE_CXX_EXTENSIONS OFF`.
- Always: `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`.
- Always, in every configuration: `-D_GLIBCXX_ASSERTIONS`, `-fstack-protector-strong`,
  `-fstack-clash-protection`, `-fcf-protection`, `-fPIE -pie`, `-Wl,-z,relro,-z,now`.
- Release adds `-O2 -D_FORTIFY_SOURCE=3`.
- Presets: `gcc-debug`, `gcc-release`, `clang-debug`, `clang-release`, `gcc-asan` (ASan + UBSan,
  `-fno-sanitize-recover=all`).
- Generate the xdg-shell client header and glue with `wayland-scanner` at build time, from the path
  given by `pkg-config --variable=pkgdatadir wayland-protocols`. Generated files go in the build
  directory and are never committed. Generated C glue is compiled without our `-Werror` flags.
- No third-party test framework. A small header in `tests/` is enough, run through CTest.

## Code rules

- No raw `new` or `delete`, and no owning raw pointers. Wrap every Wayland and xkb object in an
  RAII type.
- No C-style casts. `reinterpret_cast` only inside `src/wayland/`, each with a one-line reason.
- No mutable global or static state. Two independent `Application` objects must be possible.
- Fallible operations return `std::expected<T, Error>`. Exceptions must never cross a C callback:
  every Wayland listener trampoline is `noexcept`, and it records any failure for the event loop to
  report after dispatch returns.
- All sizes and coordinates use checked or saturating arithmetic. No unchecked narrowing.
- Single UI thread. `Application` records its thread, and public entry points check it with a
  contract-style check that stays on in release builds and aborts on violation.
- Every source file starts with a short `File:` and `Purpose:` comment, as in the sibling repos.
- Remove anything superseded in the same change. No commented-out code or compatibility stubs.

## Components

### 1. Geometry (`geometry.hpp`)

`Point`, `Size`, `Rect` on `int32_t`. `Size` is never negative. Arithmetic saturates instead of
overflowing. `Rect::intersect` and `Rect::contains` have no undefined behaviour at the limits.

### 2. Typed tree and typed pools (`tree.hpp`, `pool.hpp`): the core of the slice

**Static part.** The window's tree is one composite type built from `std::tuple`s, so its whole
shape is known to the compiler. Each widget has a `Tag` type naming it:

```cpp
struct HelpButton; struct HelpLine;

auto ui = jgui::window(
    jgui::menu_bar(
        jgui::button<HelpButton>("Help", [](auto& u) -> void {
            auto& line = u.template get<HelpLine>();
            if (line.shown()) line.hide(); else line.show("...");
        })),
    jgui::dynamic<HelpLine, jgui::Label<HelpLine>>());
```

- `u.get<Tag>()` is resolved at compile time. An unknown tag is a compile error, through a
  `static_assert` with a readable message. A duplicate tag is also a compile error.
- Traversal (paint, hit test, event delivery) uses fold expressions over the tuple. There is no
  run-time widget list and no dynamic dispatch.
- **Callbacks are generic lambdas with an explicit `-> void`.** This is required, not style: the
  tree's type contains the callback's type, so the body may only be instantiated after the tree is
  complete. A deduced return type would force it too early. Do not constrain callbacks with
  `std::invocable` on the tree type, for the same reason.

**Dynamic part.** Items that come and go live in a typed container, never behind a base pointer.

- `Pool<T>` stores `T` in `std::deque` slots, so references survive growth, with a generation per
  slot. It is one pool per concrete type.
- `Handle<T>` is `{uint32_t index, uint32_t generation}`, can only be made by `Pool<T>`, and is
  trivially copyable. A `Handle<Label<A>>` cannot be passed to a `Pool<Button<...>>`: compile error.
- Lookup with a stale or out-of-range handle returns an empty result, never a dangling reference.
- A slot whose generation reaches the maximum is retired and never reused.
- **Destruction during event delivery is deferred.** A callback may destroy its own widget. The slot
  is marked at once, so lookups fail immediately, and it is freed when the outermost dispatch ends.
  A running callback's own storage therefore stays valid.
- `Dynamic<Tag, T>` is the tree node that holds such items, one concrete `T` per node. Several kinds
  of item use several nodes. Pointer capture and focus use typed handles or tags, so they go inert
  when the item dies.

The Help line uses `Dynamic`: clicking creates it, clicking again destroys it. So this first slice
exercises the pool for real, and it is not unused code.

### 2a. Messages (`message.hpp`)

A message has no class hierarchy of its own. It is a name, the list of types that accept it, and any
number of ordinary typed values such as a `std::string` or a `std::vector`. Routing happens at
compile time:

```cpp
using ToggleHelp = jgui::Message<"toggle_help", jgui::To<HelpLine, StatusBar>>;
using SetHelp    = jgui::Message<"set_help",    jgui::To<HelpLine>, std::string>;
using Rows       = jgui::Message<"rows",        jgui::To<Grid, StatusBar>,
                                 std::string, std::vector<int>>;

auto [shown, count] = ui.send<ToggleHelp>();              // one typed response per receiver
auto [length]       = ui.send<SetHelp>(std::string{"Help text"});
```

- Each receiver has an ordinary overload per message:
  `int on(SetHelp, const std::string& text)`.
- These are compile errors: a receiver not in the tree, a receiver with no handler for that message,
  a wrong value type, and a missing value. An empty `To<>` is also a compile error.
- Values are passed by `const&` and are valid only during the handler. A handler that keeps a value
  must copy it. This rule goes in `Architecture.md`.
- A handler may return `void`. Its slot in the response tuple is then the empty type `jgui::NoReply`.
  The prototype did not cover this case, so the implementer must build and test it.
- Delivery is immediate. There is no run-time queue, subscription list or message ID in this slice.
- The Help button sends a message to the help line, so this slice uses the system
  for real.

The name is needed because two messages with the same receivers and values would otherwise be the
same thing, and a receiver could not tell them apart. All of this except `NoReply` is proven by a
prototype on g++ 16 and clang 22.

### 3. Widgets (`widget.hpp`, `button.hpp`, `menu_bar.hpp`, `label.hpp`)

Every widget is a class template and its own type. The `Widget` **concept**, not a base class,
requires `tag`, bounds, `paint(Canvas&)` and event handlers. Shared behaviour uses deducing `this`
or composition.

`Button<Tag, OnClick>` keeps its lambda's real type as a template parameter. It has a text caption
and normal, hover and pressed states. A click fires only if press and release both land on the
button. Pressing, dragging off and releasing does not fire.

`MenuBar<Children...>` is a full-width strip directly below the window frame. It lays out its child
buttons left to right, sized from their measured text, and keeps its width when the window resizes.
`Label<Tag>` draws one line of text.

### 3a. Text (`text.hpp`, `src/text/`)

Just enough to draw a caption: one font, one size, one line, no editing.

- Find the default sans-serif font through fontconfig. Never hard-code a font path. A missing font
  is a clean error.
- Shape with HarfBuzz and rasterize with FreeType, in RAII wrappers. Blend grayscale coverage
  through `Canvas`, clipped like every other drawing call.
- Input is a validated UTF-8 `std::string_view`. Invalid UTF-8 is refused with an `Error`.
- Limit caption length (4096 bytes) and refuse glyph bitmaps above a size limit, before any
  allocation or copy.
- Font files are untrusted input. Record this in `SECURITY.md`.

### 4. Pixel surface and canvas (`canvas.hpp`)

- `PixelBuffer` is a view of ARGB8888 pixels: `std::span<uint32_t>`, size and stride. Its creation
  validates that width × height × 4 fits a limit (16384 per side, 1 GiB total) and fails cleanly
  otherwise.
- `Canvas` has `fill_rect` and `stroke_rect`, clipped to the buffer and to a clip rect. No
  out-of-bounds write is possible for any input.
- Physical pixels only. Scale is fixed at 1 in this slice.

### 5. Backend boundary (`backend.hpp`)

A `Backend` **concept**, not a base class, with exactly two models: `WaylandBackend` and
`HeadlessBackend`. The backend is a template parameter chosen at compile time. It delivers each event
as its own struct, by calling an overload on a templated sink such as `sink.on(PointerMove{...})`,
with no `std::variant`. Events: close, resize, pointer move, pointer button, pointer leave, key. Key
events carry the keysym and modifiers. Text input is deferred.

The toolkit above this line includes no Wayland header. `HeadlessBackend` lets tests inject events
and read back the pixel buffer. Add `static_assert`s that both types satisfy the concept.

### 6. Application and event loop (`application.hpp`)

`Application<Backend, Root>` owns the backend and tree by value. `run()` until closed, plus
`process_pending()` for tests. Hit-testing, pointer capture, redraw only when damaged. Sleep in
`poll`; no busy loop or fixed-rate timer. Toolkit code is header templates; the non-template parts,
such as the Wayland and text wrappers, stay in `src/` to limit compile time.

### 7. Wayland backend (`src/wayland/`)

- Bind `wl_compositor`, `wl_shm`, `xdg_wm_base`, `wl_seat`, with explicit version limits. A missing
  required global is a clean error.
- Answer `xdg_wm_base.ping`. Ack every configure before committing the matching frame. A 0×0
  configure means "choose": use a default size.
- Treat every compositor value as untrusted. Oversized or negative sizes are clamped or refused
  through `PixelBuffer` validation, never used in arithmetic first.
- Shared memory: `memfd_create` with `MFD_CLOEXEC`, unmapped and closed by RAII. Never resize a
  buffer the compositor still holds: track `wl_buffer.release`.
- Keymap: accept only the xkb v1 format, check the size against a limit (1 MiB) before mapping,
  map `MAP_PRIVATE`, and always close the received file descriptor.
- Loop: `wl_display_prepare_read` / `poll` / `wl_display_read_events` / `dispatch_pending`, with
  correct flush, `EINTR` and `EAGAIN` handling. A protocol error ends `run()` with an `Error` that
  includes `wl_display_get_error`.
- Handle compositor seat capability changes: pointer and keyboard objects may appear or disappear.

### 8. Minimal window frame

Drawn by the toolkit using ordinary hit regions: a title strip with no text, a close box, and resize
edges and corners. Strip drag calls `xdg_toplevel.move` and edges call `xdg_toplevel.resize`, using
the serial of the triggering button press. The close box delivers the normal close event. Frame
behaviour that does not need a compositor, such as hit regions and the close box, is tested
headless.

### 9. Demo (`demo/`)

Opens a 640×480 window with a menu bar holding a Help button, and an otherwise empty client area.
Clicking Help toggles the help line described under Goal. Flags: `--smoke` waits for the first
configure, presents one frame and exits 0.

## Tests (headless unless marked)

1. Geometry at `INT32_MIN` / `INT32_MAX`: saturation, and intersect and contains correct.
2. Canvas: rectangles wholly or partly outside the buffer write nothing out of bounds, checked by a
   guard region around the pixels.
3. `PixelBuffer` refuses negative-free oversize and multiplication-overflow sizes.
4. Stale handle: destroy an item, and the old handle finds nothing, even after its slot is reused.
5. Generation exhaustion: a slot at the maximum generation is retired.
6. Click: press and release on the button fires once.
7. Press on, release off fires nothing. Press off, release on fires nothing.
8. A dynamic button whose callback destroys itself: no crash, and the ASan build is clean.
9. A late injected event for a destroyed item does nothing.
10. Capture and focus go inert when their item dies.
11. Frame: a click on the close box produces close, and resize to an absurd size fails cleanly.
12. Rendered pixels: button colour differs across normal, hover and pressed at a sampled point.
13. Menu bar: stays full width after a resize, and the Help button stays inside it.
14. Clicking Help shows the help line, and clicking again hides it, checked by rendered pixels.
15. Text: "Help" draws non-background pixels inside its measured bounds and none outside the clip.
16. Text: invalid UTF-8 and over-long captions return an `Error` and draw nothing.
16a. Messages: no values, a `std::string`, and a `std::string` plus `std::vector` all reach every
     listed receiver, responses arrive in receiver order, and `void` handlers give `NoReply`.
17. *Live:* `demo --smoke` under CTest. Return the CTest skip code when `WAYLAND_DISPLAY` is unset.

**Compile-fail tests.** These prove the compiler is doing the job, and each runs under CTest as a
compile that must fail, with both compilers. Pair each with a nearly identical source that must
compile, so a failure for an unrelated reason is caught:

- `get<Tag>()` with a tag not in the tree.
- Two widgets with the same tag in one tree.
- A `Handle<A>` passed to a `Pool<B>`.
- A `Handle<T>` constructed by anything other than its pool.
- A type that lacks `paint` used as a widget, which must fail on the concept.
- A message sent to a receiver not in the tree, or to one with no handler for it.
- A message sent with a wrong value type, or with a value missing.

Tiny runs only: no soak or benchmark runs in this slice.

## bsir.sh

Follows the pattern in `jText_GUI/v001/bsir.sh`, reading that script only for its flow: detect OS,
check components with `Setup/recipes.conf`, run the system upgrade on every run without asking,
offer anything missing, build, test, open the demo. Fedora and apt recipes for
wayland-protocols, wayland-client development files, xkbcommon development files, FreeType,
HarfBuzz and fontconfig development files, CMake, Ninja and a C++23 compiler.

The implementer does not run `bsir.sh`, since it needs sudo. Use the CMake presets directly.

## Docs

Short and candid, in the style of `jText_GUI/docs/`:

- `Goals.md`: purpose and priorities.
- `Architecture.md`: the type rule, the layers, typed handles and deferred destruction, and the OS
  and version layout.
- `Maturity.md`: what works and what does not. State plainly that this is a skeleton.
- `SECURITY.md`: trust boundaries (compositor values, keymap fd, later clipboard, files, fonts and
  images), the hardening flags, and what is not yet fuzzed.
- `PROCESS.md`: `Lin/v001` is the only active tree. Build order and the clean-room rule.
- `README.md`: what it is and how to build.

## Out of scope for this slice

Text editing, multi-line text, font selection, input methods, clipboard, drag and drop, HiDPI
scaling, general layout managers, themes, accessibility, key repeat, popups and drop-down menus,
X11, Windows, macOS, C++26, modules, fuzzing harnesses (there is no parser of ours yet).

After acceptance, the owner's planned next step is **replacing libwayland with our own typed C++
Wayland client**: a generator from the protocol XML plus a fuzzed wire parser. That removes the
`void*` exception. So keep every libwayland call inside `src/wayland/`, behind the `Backend` concept,
so that only the backend changes. GPU drawing is a known cost of that swap, because standard EGL and
Vulkan Wayland support expects libwayland's `wl_display`.

After that, the likely slices are a real drop-down Help menu using `xdg_popup`, then an editable
text field.

## Done means

- All four non-sanitizer presets and `gcc-asan` build with no warnings and pass CTest.
- `demo --smoke` passes in the live GNOME session.
- All compile-fail tests pass with both compilers.
- A CTest script fails the build on any banned type-erasure token from the Type rule, in all our
  source. `void*` is allowed only in the trampoline helper.
- `grep` finds no `new `, `delete `, C-style casts in `src/` outside the documented exceptions, and
  no absolute paths in the repository.
- The report lists each done item above with its evidence, files created, and anything that could
  not be verified. Interactive behaviour (dragging, resizing, clicking) is listed for the owner to
  check by hand.
