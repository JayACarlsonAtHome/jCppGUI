# Architecture

## The type rule

Objects come from templates, each item is its own type, and there is no type erasure. These are
banned in all our code, including tests and the demo: `std::function` and its relatives, `std::any`,
`std::shared_ptr`, `dynamic_cast`, `typeid`, `virtual`, abstract base classes, base-class pointers or
containers of widgets, `void*`, function pointers used to hide a type, and hand-written erasure under
any name. `std::variant` is a last resort and needs a comment saying why. There are none today.

**The one exception** is `src/wayland/trampoline.hpp`. libwayland hands listeners an untyped pointer and requires
C function pointers, and returns a new global untyped. The helper casts each straight back, and the pointer goes no
further. Each cast is tied by type: a listener table carries its `Owner`, every entry must be that owner's method, and
`listen` takes only that owner, while a bound global's interface comes from the type asked for. A mismatch does not
compile. The exception ends when our own Wayland client replaces libwayland.

**Anything that is part of the window is a class template.** System-level classes with nothing to
vary, such as the backends, may stay plain, as long as nothing is erased. `WaylandBackend` and
`HeadlessBackend` are plain classes for this reason. `WaylandBackend` gets revisited when our own
Wayland client replaces libwayland.

## The constexpr rule

`constexpr` wherever possible, and `consteval` where a value must be known at compile time. Constant
evaluation rejects undefined behaviour, but only on paths it actually evaluates, so
`tests/test_constexpr.cpp` runs edge cases through the real code in `static_assert`. That includes
the whole demo tree, from injected events to rendered pixels.

Three design choices exist to make this possible:

- `ApplicationCore` holds all the event and drawing logic and is `constexpr`. `Application` adds only
  the real font engine and the UI-thread check.
- Layout takes any `TextShaper`. `TextEngine` needs C libraries, so the compile-time tests use a small
  `constexpr` model that makes the same refusals.
- `Pool` and `Dynamic` take a `Slots` template parameter. It is always `std::deque` in real use, for
  stable references, but `std::deque` cannot be used in constant evaluation. The tests pass
  `std::vector` to run the same logic. A vector-backed pool's `emplace` is `consteval`, so using one at run
  time does not compile, directly or through anything above it: under C++23's rules, each function template
  that calls it becomes immediate too. So a test that runs such a tree must itself be `consteval`. One limit:
  clang 22 refuses this for a generic lambda with a stated return type, so a callback in a vector-backed tree
  that reaches `emplace` must be a named type with a template `operator()`. Real trees are unaffected.

Non-`constexpr` code is limited to what needs the OS or a C library:

| Code | Why |
|---|---|
| `src/wayland/wayland_impl.hpp`, `wayland_backend.cpp` | libwayland, sockets, `poll`. Every C call that can fail is behind a template parameter, `Api`, so a test can make it fail, with no test branch here |
| `load_keymap`, `create_buffer_file`, `UniqueFd` in `src/wayland/untrusted` | `close`, `fstat`, `memfd_create`, `posix_fallocate`, xkbcommon. The checks on values are `constexpr` |
| `read_at`, `write_at` in `src/file_io.hpp` | `pread`, `pwrite`. Their loop and arithmetic are `constexpr` and tested with made-up system calls |
| `FileSink` in `src/monitor/file_sink.cpp`, `SteadyClock` | `openat`, `fstat`, `fstatat`, `unlinkat`, `renameat`, the clocks. `FrameMonitor` is `constexpr` over a clock and a sink given as template parameters |
| `TextEngine` in `src/text/text.cpp` | fontconfig, HarfBuzz, FreeType. Validation, rounding and glyph copying are `constexpr` |
| `Application` | `std::this_thread::get_id`, and it owns a `TextEngine` |

## Layers

```
demo, or your program
  Application                  the real font engine and the thread check
  ApplicationCore              sends events into the tree, carries out window requests, damage, drawing
  Window, WindowFrame, MenuBar,  the typed tree, messages and pools
  Button, Label, Dynamic, Pool
  Canvas, PixelBuffer, Geometry
---------------- Backend concept: nothing above this line includes a display header ----------------
  HeadlessBackend              tests inject events and read pixels back
  WaylandBackend               src/wayland/ only: libwayland and xkbcommon
```

The backend is a template parameter chosen at compile time. There are exactly two models, and a
`static_assert` checks each against the concept. `src/text/` is the only place that uses FreeType,
HarfBuzz and fontconfig.

## Wire values

A serial, a pointer button, a keysym and a modifier mask are each their own type: `Serial`,
`PointerButtonCode`, `Keysym` and `ModifierMask`. There is no implicit conversion in or out, so one cannot be
passed as another. Above the backend line they can be compared, and a button, a key or a mask can be named at compile
time, because those carry no authority. A `Serial` proves that a press happened, so it cannot be made there at all,
not even by default. Only `src/wayland/` can make one from a number at run time or read a number back, through
`detail::WireAccess`, which is defined only there. Tests use `detail::TestAccess`.

## Geometry

`Left`, `Right`, `Top`, `Bottom`, `Width` and `Height` are six types, with no implicit conversion to or from an integer or
each other. Each is made explicitly from exactly an `int32_t`. A length is never negative, and all arithmetic saturates.
`Point{Left, Top}`, `Size{Width, Height}` and `Rect{Point, Size}` take nothing else, and their accessors give the typed
values back.

The arithmetic is only what means something: `Left + Width` is `Right`, `Right - Left` is `Width`, `Right - Width` is
`Left`, the same for `Top`, `Bottom` and `Height`, and `Width` plus or minus `Width`, likewise `Height`. A far edge is one
past the last pixel, so `left_after(Right)` and `top_after(Bottom)` name where the next thing starts, which is also how
an edge moves by a length: `left_after(left + padding)`. Edges of different kinds cannot be compared or added.

The number can be read only by `PixelBuffer`, `Canvas`, `TextRun` and `Rect`, which index memory or do the sums in 64 bits,
in `src/` through `detail::GeometryAccess`, for libwayland and the font libraries, and by `detail::RedrawNumbers`, only to write it in the
redraw monitor's file. These stay plain `int32_t`, because each
is a bare length with no axis, or is not a window position at all: the fields of a `FrameMetrics` type, which `WindowFrame`
turns into typed values in one place, `max_buffer_side`, a buffer's stride in bytes,.

## The typed tree

A window is one composite type built from `std::tuple`s, so the compiler knows its whole shape.

- `u.get<Tag>()` is resolved at compile time. An unknown tag and a duplicate tag are compile errors.
- Paint, hit testing and event delivery are fold expressions over the tuple. There is no run-time
  widget list and no dynamic dispatch.
- `Widget` is a concept and not a base class. `handle` takes each event that has an answer, as its own
  type, and `notify` takes each notice, which has no answer and so nothing to discard. A widget with no
  interest in some of them gives one catch-all template of each. `layout`, `paint`, `handle` and `notify` each take a
  `const TreeKey&` first, which only the `Window` and tests can make. A widget passes the one it was given to its children.

**Callbacks are generic lambdas with an explicit `-> void`.** This is required, not style. The tree's
type contains the callback's type, so the body may only be instantiated after the tree is complete. A
deduced return type would force it too early. For the same reason, do not constrain callbacks with
`std::invocable` on the tree type. The `Widget` concept is checked against an empty probe type, and
declared return types keep that from instantiating any handler body.

## Text a widget holds

A widget takes its text as `CheckedText`. A string literal or other text known at compile time converts by itself and is
checked by a `consteval` constructor, so a bad one does not compile. Text known only at run time goes through
`CheckedText::check`, which answers with a `std::expected`, so a constructor has nothing left to refuse. `Dynamic::create` and
`show` forward their arguments, which ends compile-time knowledge, so a literal is written `CheckedText{"text"}` there.
`Label::set_text` checks when it is called and keeps the old text on refusal. If a shaper later refuses one widget's text, the
widget keeps the reason in `text_error()`, draws no text and keeps its row. Only failures not about that text end `run()`.

## Typed handles and deferred destruction

Items that come and go live in `Pool<T>`, one pool per concrete type, inside a `Dynamic<Tag, T>` tree
node. Several kinds of item use several nodes.

- `Handle<T>` can only be made by its pool, has no default value, and cannot be passed to another
  type's pool.
- A stale or out-of-range handle finds nothing. Each slot has a generation, and a slot at the maximum
  generation is retired and never reused.
- **Destruction is deferred.** A callback may destroy its own widget. The slot is marked at once, so
  lookups fail immediately, and it is freed when the outermost dispatch ends. A running callback's
  storage therefore stays valid.
- Pointer capture and focus in a `Dynamic` are typed handles, so they go inert when the item dies.

## Messages

A message is a name, the tags of the widgets that accept it, and any number of ordinary typed values:

```cpp
using SetHelp = jgui::Message<"set_help", jgui::To<HelpLine>, std::string>;
auto [length] = ui.send<SetHelp>(std::string{"Help text"});
```

- Each receiver has an ordinary overload per message: `int on(SetHelp, const std::string& text)`.
- There is one typed response per receiver, in receiver order. A `void` handler gives `jgui::NoReply`.
- A receiver not in the tree, a receiver with no handler, a wrong value type, a missing value and an
  empty `To<>` are all compile errors. Value types must match exactly, with no implicit conversion.
- **Values are passed by `const&` and are valid only during the handler. A handler that keeps a value
  must copy it.**
- Delivery is immediate. There is no run-time queue, subscription list or message ID.

## Backend events

A Wayland event arrives inside a C callback. The toolkit's templates cannot be reached from there
without Wayland headers above the line, and user code must not run under libwayland. So handlers
only record the event in `EventQueue`, a tuple of per-type queues with arrival stamps, and it is
delivered to `sink.on(...)` in order after dispatch returns. This is input buffering at the system
boundary and has nothing to do with messages, which never pass through it.

**The owner accepted `EventQueue` as input buffering at the system boundary only. It is not a message
queue, and it should go away if our own Wayland client delivers events directly.**

## No shared mapping

Our code maps nothing, and no mapping call may appear in our source. A frame is drawn in a
`std::vector<std::uint32_t>` we own and written to the `memfd_create` buffer file with `write_at`. The
file is sized with `posix_fallocate`, and a file the compositor still holds is never written. The
keymap is read with `read_at` into a private copy. The C libraries we link may map things of their own:
see `SECURITY.md`.

## What a callback can reach

A callback receives the `Window`, and can use only `get<Tag>()`, `send<Message>()`, `size()` and the three requests.

- **The tree's machinery is private.** `dispatch`, `notify`, `clear_damage`, `layout`, `paint`, `damaged`, `capture_active`,
  `set_size` and `take_requests` are private, with `ApplicationCore` as a friend, so an attempt does not compile. Neither
  `Application` nor `ApplicationCore` gives out its backend, and `ApplicationCore::on` is reachable only from a backend's
  `EventQueue`.
- **A widget from `get<Tag>()` does not reach it either.** A widget's `layout`, `paint`, `handle` and `notify` need a
  `TreeKey`, which a callback is never given and cannot make.
- **Nothing is replaced underneath.** A `Window` and the toolkit's widgets cannot be copied or assigned, because assigning
  over a `Dynamic` could free the item whose callback is running.
- **What compiles but is a mistake aborts.** A callback that reaches its own `Application` through a pointer and runs
  it would redraw in the middle of a delivery, and moving the `Window` or the `Application` would leave the tree empty
  under the callback. Each aborts through `require`, in every build. `tests/test_callback_access.cpp` shows each.

Tests get past this through `detail::TestAccess`, which the toolkit only declares. It is defined in
`tests/test_access.hpp`, and `tests/check_source_rules.sh` keeps it there. `TestAccess`, `WireAccess` and `GeometryAccess` are
friends declared by name, so **a program could define one of those names itself and get past all this.** C++ has no
way to stop that. Only the rule script stops it in this repository.

**What remains reachable from a callback.** The public interface of each widget it gets by tag, which is what the widget
offers: `Dynamic::create`, `destroy`, `find`, `show`, `hide`, `captured` and `focused`, `Label::set_text`, the state getters, and
`children()` on a `MenuBar` or `WindowFrame`, which gives only the same widgets. A pointer from `Dynamic::find` can be kept past
the item's life, as any C++ pointer can. A callback can loop for ever, recurse through `send` until the stack runs out, or
call `std::exit`. Through a pointer to its `Application`: `ui()`, `closed()`, `frames()` and `size()`, which are harmless.

## The redraw monitor

A development tool, chosen by type: `ApplicationCore<B, Root, Monitor = NoMonitor>`, and likewise `Application` and `make_application`.
`NoMonitor` is empty and every point on it does nothing, and it is held with `[[no_unique_address]]`, so a core without a monitor is the
size it would be with no such member, as `tests/test_redraw_monitor.cpp` shows against a struct of the same members. `FrameMonitor` is in
`redraw_monitor.hpp`, which `jgui.hpp` does not include, and a program using its default file links `jgui_monitor`.

**The core never reads a clock or a file.** It tells the monitor what happens at named points, listed with the `RedrawMonitor` concept. It
asks the tree nothing extra for this except, with a monitor that watches, the damage walk below. `FrameMonitor<Clock, Sink>` owns the time and
the output, so the same logic and the same file text run in `static_assert` with a test clock and a sink in memory.

**One file, four sections.** `redraw_monitor.jtext` is a whole jText file in the full profile, written by a small writer of our own. It does not link the jText library.

| Section | One row per | Holds |
|---|---|---|
| `Run` | run | the label, the budget, the row limit, how many rows of each kind there were and were dropped, rows refused, writes, the last write's time, whether the run ended |
| `Frames` | frame presented | id, start, reason, drawn area, nanoseconds for layout, acquire, paint, hash, present and total, the previous file write's time, `Slow`, `SamePixels` |
| `Damage` | damaged widget in a frame | record id, frame id, class name, tag, position in the tree |
| `Deferred` | time there was damage and `can_present` said no | record id, the frame waiting, time, reason, size |

- `total` is the sum of the four phases, so what the monitor itself adds is not in it: not the hash, and no file write. `Slow` compares that to the budget.
- `SamePixels` is a 64-bit FNV-1a of the pixels equal to the previous frame's, at the same size. The monitor hashes inside its call, is given the pixels only for that call, and keeps only the number.
- The reason is the first that holds: `FirstFrame`, `Resize`, `WidgetDamage`. It comes from what the core already knows.
- **Damage** is found by a compile-time walk of the tree before damage is cleared, and before the timing starts. Names come from `std::source_location` in a `consteval` function, checked on g++ and clang++. Only the class's name without its template arguments is kept, because those can be very long and on clang++ can hold a source path, and the tag's name beside it. The two compilers print inline namespaces and anonymous namespaces differently.
- A `Dynamic`, or a widget holding one, is one row, however many items are in it: its items have no place in the compile-time tree. A composite that does not offer `own_damage()` is named only when none of its children is damaged, because their damage hides its own. `WindowFrame` offers it.
- A frame that fails part way is not recorded.

**Writing** happens only between frames, on the UI thread, at most once per interval, default one second, and once at the end of `run` or `present_first_frame`. A program with its own `process_pending` loop calls `finish_monitor`. Each write is a whole new file, renamed over the old, so a reader always sees a whole valid file. Rows are bounded, 65536 of each kind by default, keeping the newest, and each section's drop count is in `Run`. So a full monitor writes some megabytes each second. A failed write stops all further writing and nothing else. `monitor_error()` gives the reason for the program to report at exit, and the monitor itself is never given out.

**The writer's limits.** The format has no escapes. A line uses `▮▌`, and falls back as the spec says on a character the data holds. A value with a control character, invalid UTF-8 or space around it cannot read back as itself, so its row is left out and counted, and a run label like that is dropped and counted. No value can break the file's structure.

**The limits of "a crash loses about a second".** That holds while frames keep coming. The loop sleeps with no timer, so rows recorded just before a quiet time wait for the next event, and a crash in the quiet time loses them. The file is not flushed to the disk with `fsync`, so a crash of the whole system can lose more.

The file's place is the working directory when the monitor is made, as the application's base directory, or a directory given to `FileSink`. See `docs/SECURITY.md`.

## Frames

`acquire_frame` returns a move-only `Frame<Backend>`, and `present` takes it by value, so a frame cannot be
presented twice. A frame holds no pointer: only its generation, its buffer's index and its size. A new
acquire invalidates every older frame even if it then fails, and `frame_pixels` and `present` refuse anything
but the current one with `ErrorCode::StaleFrame`. A frame moved from has generation 0, which is never current.
Both backends use the same `detail::FrameLedger`, so the headless tests run the Wayland backend's rule.

A `Canvas` and a `PixelBuffer` cannot be copied or moved, so a widget cannot store what `paint` gives it. `clipped` still
returns a new object, which can initialize a member, so `ApplicationCore` also gives its canvas a generation from a
`FrameLedger` of its own, and each draw call aborts unless that generation is current. Painting ends before the
pixels can move. `docs/SECURITY.md` says exactly what remains possible.

## Hidden implementation types

`WaylandImpl` and `TextEngineImpl` stay out of the public headers, each behind a `std::unique_ptr` of its
one concrete type with a stateless deleter. They exist so that no libwayland, xkbcommon, FreeType, HarfBuzz
or fontconfig type appears above the backend line, and so that a program using the toolkit is not recompiled
when those details change. Nothing is erased: each has exactly one implementation, known at link time. The
owner approved keeping them. `WaylandImpl` gets revisited with our own Wayland client.

## OS and version layout

```
Lin/v001/   C++23, self-contained: bsir.sh, CMakeLists.txt, CMakePresets.json, Setup/,
            include/jCppGUI/v001/, src/, tests/, demo/
            tests/fake_compositor/ is a scripted Wayland compositor, for tests only: see docs/Maturity.md
```

The namespace is `jgui::inline v001`, so `jgui::Button` works for consumers. Toolkit code is header
templates, and the parts that need C libraries stay in `src/` to limit compile time. All paths are
found relative to the script or to `CMakeLists.txt`.

**The paths rule.** Every location derives from one base directory, the script's own directory or the
CMake source directory, plus relative paths. No absolute path is allowed in the repository, with no
allowlist, which also rules out shebangs. Scripts run as `bash bsir.sh` and
`bash tests/check_source_rules.sh`.

## The window frame

GNOME does not draw title bars for Wayland clients, so the toolkit draws a minimal frame: a title
strip with no text, a close box, and resize edges and corners. It also draws its own pointer arrow,
because the protocol leaves the pointer's look undefined until a client sets one.

`WindowFrame<Metrics, Children...>` is a widget like any other, and it holds the window's content the
way `MenuBar` holds its buttons:

```cpp
jgui::window(jgui::window_frame(jgui::menu_bar(...), content));          // DefaultFrameMetrics
jgui::window(jgui::window_frame<MyMetrics>(jgui::menu_bar(...), content));
```

- **Metrics.** Sizes are `static constexpr` fields of a type that satisfies the `FrameMetrics` concept,
  so different frame sizes are different types. `WindowFrame<M>` with no children is enough to reach
  the geometry: `WindowFrame<M>::client_rect(size)`, `close_box` and `hit`.
- **Why a parent and not a sibling.** The frame decides which events the content sees: a left press on
  the frame stays out of the content, motion off the client area is a leave for it, and a press in the
  client area clears the focus first. The content paints clipped to the client area, and the frame
  paints on top. The frame's own close-box state survives the pointer crossing a widget, which a
  sibling could not do, because the tree tells a sibling below the widget that the pointer left.
- **Reaching the window system.** A widget cannot see the backend. It asks the `Window` it receives as
  `ui`: `request_move(serial)`, `request_resize(serial, edge)` or `request_close()`. These are typed
  fields, and `ApplicationCore` carries them out through the backend when the dispatch returns, in the
  same call that delivered the press. `PointerPress` carries the press's serial for this. Close goes
  through the same `on(CloseEvent)` as a close from the compositor.
- **Size.** The `Window` holds the validated window size, set as soon as a resize arrives, so a press
  queued behind a resize is judged at the new size even before the next layout.
- **A window with no frame** also works, with no chrome. Clearing the focus on a press is the frame's
  job, so such a window does not do it.
