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
| `read_jtext_file` in `src/jtext/read_file.cpp` | `open`, `fstat`, `fstatat`. Parsing what it read is `constexpr` |
| `read_directory`, `starting_folder` in `src/files/read_directory.cpp` | `open`, `fdopendir`, `readdir`, `fstatat`, `std::filesystem::canonical`. Everything about what comes back, and the panes, are `constexpr` |
| `FileSink` in `src/monitor/file_sink.cpp`, `SteadyClock` | `openat`, `fstat`, `fstatat`, `unlinkat`, `renameat`, the clocks. `FrameMonitor` and `SettingsRecorder` are `constexpr` over a clock and a sink given as template parameters |
| `TextEngine` in `src/text/text.cpp` | fontconfig, HarfBuzz, FreeType. Validation, rounding and glyph copying are `constexpr` |
| `Application` | `std::this_thread::get_id`, and it owns a `TextEngine` |
| `monotonic_now`, `WaylandBackend::now` | The real clock. `HeadlessBackend::now` is `constexpr`, and a test moves it by hand |

## Layers

```
demo, or your program
  Application                  the real font engine and the thread check
  ApplicationCore              sends events into the tree, carries out window requests, damage, drawing
  Window, WindowFrame, MenuBar,  the typed tree, messages and pools
  Button, Label, DataGrid,
  FilePane, TwoPanes,
  OneShown, Dynamic, Pool
  ScrollBar, and small helpers  scroll_bar, spans, selection_step, index_sort, cell_text, panels, timer
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
in `src/` through `detail::GeometryAccess`, for libwayland and the font libraries, by `detail::FileNumbers`, defined in `jtext_writer.hpp`, only to write it in a
jText file, and by `detail::CellNumbers`, defined only in `cell_text.hpp`, only to show it as a cell's text. `tests/check_source_rules.sh` keeps each to its file.
A length can also be multiplied and divided: `repeated_extent`, `whole_units_in` and `rescaled_extent` give a length or a count, in 64 bits and saturating, so nothing above the backend line reads the number to do those sums. `whole_units_in(x, Width{1})` does give the number as a count, and no rule stops that. These stay plain `int32_t`, because each
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

A `PointerMove` pushed straight after another one that is still held, with the same flags, replaces its
position, so a flood of motion takes one slot and only the newest position is delivered. Order against
every other event is unchanged. Any other flood can reach the limit of 65536 events, which ends the run with an `Error`.

**The owner accepted `EventQueue` as input buffering at the system boundary only. It is not a message
queue, and it should go away if our own Wayland client delivers events directly.**

## No shared mapping

Our code maps nothing, and no mapping call may appear in our source. A frame is drawn in a
`std::vector<std::uint32_t>` we own and written to the `memfd_create` buffer file with `write_at`. The
file is sized with `posix_fallocate`, and a file the compositor still holds is never written. The
keymap is read with `read_at` into a private copy. The C libraries we link may map things of their own:
see `SECURITY.md`.

## What a callback can reach

A callback receives the `Window`, and can use only `get<Tag>()`, `send<Message>()`, `size()` and the requests: move, resize, cursor, a cursor suggestion and close. `wake_at` is there too, and means something only during `TimersDue`: see below.

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
offers: `Dynamic::create`, `destroy`, `find`, `show`, `hide`, `captured` and `focused`, `Label::set_text`, `Button::set_caption`, `DataGrid::set_rows`, `set_column_font`, `set_column_align`, `set_every_column_font`, `set_column_title`, `set_sort_keys`, `take_font_refusal`, the state getters, and
`children()` on a `MenuBar` or `WindowFrame`, which gives only the same widgets. A pointer from `Dynamic::find` can be kept past
the item's life, as any C++ pointer can. A callback can loop for ever, recurse through `send` until the stack runs out, or
call `std::exit`. Through a pointer to its `Application`: `ui()`, `closed()`, `frames()` and `size()`, which are harmless.

## The redraw monitor

A development tool, chosen by type: `ApplicationCore<B, Root, Monitor = NoMonitor>`, and likewise `Application` and `make_application`.
`NoMonitor` is empty and every point on it does nothing, and it is held with `[[no_unique_address]]`, so a core without a monitor is the
size it would be with no such member, as `tests/test_redraw_monitor.cpp` shows against a struct of the same members. `FrameMonitor` is in
`redraw_monitor.hpp`, which `jgui.hpp` does not include, and a program using its default file links `jgui_monitor`.

**The core reads no clock of its own, and no file.** For the monitor it tells the monitor what happens at named points, listed with the `RedrawMonitor` concept. It
asks the tree nothing extra for this except, with a monitor that watches, the damage walk below. `FrameMonitor<Clock, Sink>` owns the time and
the output, so the same logic and the same file text run in `static_assert` with a test clock and a sink in memory.

**One file, four sections.** `redraw_monitor.jtext` is a whole jText file in the full profile, written by a small writer of our own in `jtext_writer.hpp`. It does not link the jText library.

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

**The limits of "a crash loses about a second".** That holds while frames keep coming. The monitor asks for no wake-up, so rows recorded just before a quiet time wait for the next event, and a crash in the quiet time loses them. The file is not flushed to the disk with `fsync`, so a crash of the whole system can lose more.

The file's place is the application's base directory (see below), or a directory given to `FileSink`. With no base directory to be found, the monitor refuses: the program carries on, nothing is written, and the reason is readable at exit. See `docs/SECURITY.md`.

**The application's base directory** is the `vNNN` folder that contains the running executable, so for `Lin/v001/build/gcc-release/jcppgui-demo` it is `Lin/v001`, and the file is `Lin/v001/redraw_monitor.jtext`. `application_base_from`, in `base_directory.hpp`, is the rule as a pure function: from the executable's parent folder upward, the nearest name that is exactly `v` and three digits, and the file name never counts. It refuses a relative or empty path and a path with no such folder, and also a repeated or trailing slash and a `.` or `..` name, which the kernel never reports and which walking up by text could get wrong. `application_base_directory` applies it to the kernel's `proc/self/exe` link. It never looks at `argv[0]`, the environment or where the program was started from. So a symbolic link to the executable finds the base of what it leads to, and a copy or a hard link stands for itself. An executable replaced or removed while running is refused, as is one whose real name ends the same way as the kernel's ` (deleted)` mark.

## The settings record

**It is a record, and nothing is built from it.** The application creates what it needs in code, as it always has, and then writes down what it created, as a way to look things up, report, and check that things are working. No program but the viewer reads this file, and nothing in the toolkit does.

`SettingsRecorder<Clock, Sink>`, in `settings_recorder.hpp`, fits the same slot as a redraw monitor, so a program pays nothing unless it names it, and `jgui.hpp` does not include it. `make_settings_recorder()` gives the usual one, and a program that uses it links `jgui_monitor`. `BothMonitors<A, B>` puts two monitors in that one slot, told everything in order, and is its own type. Two recorders in it do not compile, because each would number the same items. With a recorder in the slot, `ApplicationCore` walks the tree after each frame presented, so after everything a monitor measures, and with any other monitor that walk is not compiled at all.

**One file, two sections.** `settings.jtext`, in the application's base directory, in the full profile.

| Section | One row per | Holds |
|---|---|---|
| `Items` | item in the tree, in order of ID | `ID`, `Parent_ID`, `Class_Name`, `Name`, `Left`, `Top`, `Bottom`, `Right`, `Height`, `Width` |
| `Problems` | thing found wrong | `Record_ID`, `Item_ID`, `What_Failed`. Empty is the usual case, and jText's validator gives its warning for an empty section |

- **`ID` is field 1,** the record id the format requires. The `Window` is 1, with a `Parent_ID` of 0, and it is a row of its own, which the redraw monitor's `Tree_Position` does not count. Everything the compiler knows of follows depth first from 2, so its ID is the same in every run of that program. A `Dynamic` is one row, however many items it holds.
- **Items of a `Dynamic` are numbered above all of those,** in the order frames first see them. An item keeps its ID for as long as it lives. An ID is never used again in a run, and an item in a slot used again is a new item with a new ID. So a fixed widget's ID does not move as items come and go, and an ID in one file means the same thing in a later file of that run, but not of another run. An item that comes and goes between two frames takes no ID. A widget inside such an item is numbered the same way, and goes with it. The recorder holds this table, by slot and generation, so a `Dynamic` holds nothing extra for it.
- **A widget that holds a `Dynamic` privately** can declare `holds_items` and `visit_items`, which takes a `TreeKey`, and its items become its children. `demo::HelpPanel` does. A widget that does not say so is one row.
- **`Class_Name`** is the redraw monitor's compile-time name. **`Name`** is the name of the widget's `tag`, the compile-time name each widget already declares, and it is empty only for the `Window`, which has no tag. Items of one `Dynamic` share a name. Both compilers print these differently, as in the redraw monitor.
- **Geometry** is each item's `bounds()` after the frame's layout. A child that `OneShown` is not showing, and all that is under it, is written with a zero rectangle at the corner, because the one it last had is out of date. It keeps its ID.

**The checks,** each a small `constexpr` helper, run on every row whenever the rows change. A failure is a row in `Problems`, and ends nothing.

| Check | `What_Failed` |
|---|---|
| `Right - Left == Width` | `Right - Left is not Width` |
| `Bottom - Top == Height` | `Bottom - Top is not Height` |
| No far edge before the near one. `Width` and `Height` cannot be negative, as types, so this is how a size below zero would show | `Right is before Left`, `Bottom is before Top` |
| The parent is in the file. Only the row with ID 1 may have a parent of 0 | `The parent is not in the file` |
| A child's rectangle lies inside its parent's | `Outside its parent` |

**The last one is a promise the toolkit really makes:** every one of its widgets lays out inside the rectangle it was offered, by `Rect::intersect` or by taking it whole, and every parent offers rectangles inside its own. A rectangle with nothing in it is excused, because it has no pixels to be outside with, and a parent out of room can leave one just past its own edge: `inset` moves the corner even when the size has reached zero. A program's own widget can break this, and then this says so. The tests run the demo and the viewer's window at 1x1, 3x3, 20x5 and 200x120, and no check fails. Because every far edge is computed from the near one and the length, the first two checks can fail on a real widget only when an edge saturates at the limit of `int32_t`. They are there to catch a change in that. Two more rows are possible: `This row could not be written`, for a row that cannot stand in a line, which no compile-time name does; and a row with an `Item_ID` of 0 saying how many items were over the limit.

**Writing.** Only when something recorded has changed since the last write, so a frame that changes nothing writes nothing. The first write is straight after the first frame, then at most once per interval, default one second, and once at the end of `run` or `present_first_frame`, whether or not anything changed. A program with its own `process_pending` loop calls `finish_monitor`. The file, its refusals and what happens on a failure are the redraw monitor's: `FileSink`, a whole new file renamed over the old, writing stopping at the first failure, the program going on, and `monitor_error()` giving the reason at exit. `BothMonitors` gives both reasons when both failed. The recorder asks for no wake-up, so a change just before a quiet time waits for the next event, or the end.

**Limits.** All of the compile-time tree is always written, which the compiler already bounds. As well as that, at most 65536 items of `Dynamic`s and what is inside them, by default `SettingsRecorderOptions::item_limit`, and `Problems` at most that many, with one more row saying how many were not listed. An item over the limit is counted, and what is inside it is not.

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

## Time in the loop

The loop sleeps until something arrives, or until the earliest time any widget asked for, and with no such time it sleeps with no wake-ups at all, so an idle program uses no CPU. `timer.hpp` is small and knows nothing of grids.

- **The backend owns the clock.** `Backend::now()` gives a `MonotonicTime`, and `wait(sink, deadline)` sleeps to the deadline if it has one. `WaylandBackend` reads `steady_clock`. `HeadlessBackend` holds a time a test moves with `advance_time`, and a wait with no events and a deadline runs that time to it at once, so a whole `run()` to a deadline works in `static_assert`. With no deadline and no events it is an `Error`, as before.
- **`TimersDue` is a notice,** so every widget gets it, also one that `OneShown` is not showing, and an item of a `Dynamic`. `ApplicationCore` sends it on every turn, however the loop woke, with the time. A widget acts on what has come due and says with `ui.wake_at(when)` when next it wants to act. The earliest answer is the wait. Each turn starts from nothing, so no widget has to cancel anything, and one that dies takes its deadline with it. `next_wake()` gives the answer to a program with its own `process_pending` loop.
- **A turn** is `TimersDue`, drawing if anything is damaged, and if that drew a frame, `TimersDue` again and drawing again if that damaged anything. Layout is when a widget can learn something only a shaper knows, with no event to say so, and this is how it gets to act on that. It stops at two, so a widget cannot make it spin.
- **`poll_timeout_ms`** turns a deadline into poll's `int`: -1 with no deadline, 0 once it has come, and otherwise whole milliseconds, rounded up, because rounding down would spin through the last one. A longer wait than an `int` holds stops at the longest one, and the loop asks again. The difference is taken as unsigned, so any two times work. It is worked out afresh on each try at `poll`, so signals landing there cannot put a deadline off.
- **Floods.** Because the notice goes on every turn, other events going past do not hold a deadline back. Presses on one button do put off that button's own change, because each new press restarts its wait, as asked. Nothing else waits on it: the letter shows the choice, the loop goes on, and the change happens 300 ms after they stop. There is no ceiling on that. A compositor sending presses for ever is already able to do worse.
- `DelayedChoice<T>` is what a widget with a wait holds: what is chosen, which shows at once, and what is applied, which follows after the wait since the last choice. A press has no time to hand, so the wait starts at the `TimersDue` after that delivery.
- A deadline waiting when the window closes is dropped with everything else, and `present_first_frame` asks no deadline of the backend.

## Scrolling

`wl_pointer` is bound at version 5, so a wheel arrives as `axis_source`, `axis_discrete`, `axis` and `frame`. `axis_value120` is version 8 and is not used. Handlers add to a `detail::AxisTally`
per axis, in `src/wayland/untrusted.hpp`, and `frame` turns what has gathered into one `PointerScroll`. Steps win over pixels for an axis, because a wheel sends both for one notch. What is left of a pixel
waits for the next frame, until `axis_stop`, so slow smooth scrolling moves. Each total stays inside what one event can ask for, so no flood can overflow it.

`PointerScroll` carries a `ScrollDown` and a `ScrollAcross`, two types, each at most `max_scroll_steps` or `max_scroll_pixels` however it was made. Scrolling with no pointer in the window is dropped in
the backend. It goes to the topmost widget under the pointer, as a press does, and the frame passes it on only over the client area. A compositor that never sends `frame` gets no scrolling.

## The data grid

```cpp
struct Row { Left left; std::string name; };
using Picked = jgui::Message<"picked", jgui::To<Status>, jgui::GridRow>;
using Grid = jgui::DataGrid<GridTag, Row, Picked, jgui::Column<"Left", &Row::left>, jgui::Column<"Name", &Row::name, 240>>;
```

- **Columns are types.** The title and the member are template arguments, and the third is the width it starts at. The text comes from `CellFormat<T>`, chosen by the member's type: whole numbers, `bool`, `std::string`
  and the six geometry types, and a program can add one for its own type. No format, a member of another row type and a name used twice do not compile. A character type counts as no format, because it is not a number. A column's member must also be ordered, for sorting.
- **The widget holds only a grid's own logic.** The arithmetic and the moves are in small headers of their own, each usable alone and tested alone in `tests/test_grid_helpers.cpp`.
  `ScrollBar<Axis>` is a part a widget holds, not a widget: it keeps one offset clamped and draws its track and thumb.
- **Rows.** `set_rows` takes a `std::vector<Row>`, at most `max_grid_rows`, 1,000,000, and refuses more with an `Error`, keeping the old rows. New rows clear the selection and the sort, with no message, because the caller did it.
- **Text is shaped in `layout`,** because only `layout` is given a shaper, and only for the rows and columns in view. `paint` reads what was shaped and never the rows, so a paint costs the same for any number of rows, and that is measured:
  see `docs/Maturity.md`. There is no cache of shaped text across frames, because the measurement did not show one is needed. Only as much of a cell's text as could show is shaped, so a very long value costs what a short one does. A cell the shaper refuses as text is left blank and the program goes on.
- **The current column.** A plain click on a header, away from its buttons and the boundary's reach, makes that the grid's one current column, tints its header with `theme::grid_current_column`, and gives the grid the focus. It sorts nothing and clears nothing, and it outlasts new rows.
- **Sorting** is from the keys, with the grid focused and a current column. `Ctrl+Up` is that column ascending and `Ctrl+Down` descending. A column not in the sort comes in as the next number, up to 9, and a tenth does nothing. One going the other way turns and keeps its number. One already going that way is left alone, with no frame. `Ctrl+Left` or `Ctrl+Right` takes the column out, and later numbers close up. Out of the sort, or with no current column, each of these does nothing, and none of them falls through to what the arrow does with no Ctrl, which moves the selection or scrolls sideways. Other keys pay no attention to Ctrl, as before. `set_sort_keys` sets the whole sort, and refuses more than 9 keys, a column that is not there and a column twice. `sort_keys()` gives the keys in order.
- **How it sorts.** By `stable_sort_indices`, a merge sort of an index vector, because `std::stable_sort` is not `constexpr` in C++23. The rows do not move. The keys are a fixed array with a count, and a comparison goes through them in order, each reaching its typed column through a fold over the columns, with no allocation and nothing erased. A tie falls to the next key, and a tie on all of them to the rows' own order, because each sort starts from that order. A sort with no keys is that order. It runs in the delivery of the key, so a sort of many rows holds up that one delivery, and that does miss frames: see `docs/Maturity.md`. There are no threads.
- **The sort mark** is a solid triangle almost the header's height, its point up for ascending and down for descending, with the key's number, 1 to 9, at its fat end, including a `1` when it is the only key. The number is white on `theme::sort_mark`, about 7.7 to 1 by the WCAG formula, and it is drawn from a 5 by 7 table rather than shaped: a glyph's size is the font's to say and its soft edges fight the colour behind them, while this always fits the shape. A mark under 14 rows has no room for one, and a real header is well over that. The room it takes grows with the header, 24 at a real row of 24, which counts toward a column's whole title. In a column dragged narrower than its whole title it does not draw, and the sort still stands.
- **Selection** is held as a position on the screen and given out as a `GridRow`, an index into the rows as given. It follows its row across a sort, and when a sort is emptied. The optional `Notify` message goes out only when the selection really changes, last, so a receiver finds the grid settled, and may give it new rows from there. `set_rows` during that delivery is safe for this reason.
- **Damage.** Motion draws nothing. A scroll that clamps to the same offset draws nothing. Focus is compared with what was last painted, because the frame clears the focus before a press in the grid gives it back.
- **A column's font and alignment.** Each header has two buttons at its right end, side by side and almost its full height: the font button, blank for regular with its outline, `M` for monospace, and the alignment button, `L`, `C` or `R`, in that cycle, for both the title and the cells. A press acts at once on the letter, and the column follows `grid_column_change_delay`, 300 ms, after the last press on that button, so going through several draws the column once, and coming back to where it was draws nothing more. Settings last as long as the grid, through sorting, resizing, new rows and sections going away. They are not saved. `set_column_font`, `set_column_align` and `set_every_column_font` do the same with no wait, and a column that is not there is refused with an `Error`. The default is regular and left. The letters are shaped with the regular font, once, whatever a column's own font is. A letter the shaper refuses leaves its button blank, and a font failing goes the way it does for any caption.
- **In a header press, what is hit comes in this order:** the boundary's reach, then the buttons, then an ordinary press. The buttons stop short of the reach, so a drag always starts. Every height of the header counts as a button, and so does the pixel between them, so just missing one does not make the column current. Pressing one never holds the pointer, makes the column current, sorts or selects. The filler has none.
- **Monospace** comes through `HasMonospaceFace`, a concept with a `monospace_face()` that gives the face as its own type, so nothing is erased and a simple shaper with none is still a `TextShaper`. `TextEngine` finds the font the first time it is asked, through the same checks as the regular one, so a program that never asks pays nothing. Its letters `i` and `W` must have the same advance: asking the font's own flag would refuse the variable Noto Sans Mono, which does not set it, and fontconfig answers with some font even when no monospace one is installed. If it cannot be had, the column goes back to regular, the program goes on, and `take_font_refusal` gives why. A simple shaper is refused the same way. Each column has its title shaped in each font it has used.
- **Column widths.** Until it is dragged, a column shows at least its whole title: padding, the title in the column's font, padding, the sort mark's room, both buttons and the boundary's reach, or its declared width if that is more. So it follows `set_column_title` and font changes both ways. Once it is dragged, its width is the user's, and those changes do not move it. A drag stops at the floor: padding, the width of the shaped `M`, both buttons and the reach. A column's width is at most `grid_column_most`, 4096, also when the whole of its title is more, so a title past that is cut. A window too narrow for the columns gets the bar across. A cut title stops cleanly at the buttons. The sort mark needs the whole title's width to draw: in a column dragged narrower than that it does not, the sort still stands, and the mark comes back when the column is wide enough. Cell text is cut at its column, as before.
- **Rows** are all one height, from the tallest font in effect in that grid, and the header goes with them. Text shorter than its row sits in the middle.
- **Sizes.** A row's height comes from the font, at most `max_grid_row_height`, 2048, and a million of those still fit a length, as a `static_assert` shows. So the offsets are ordinary `Height` and `Width` values.

## The jText reader

`jtext.hpp` reads the full profile of a jText file, written by anything, and it is not in `jgui.hpp`. Parsing takes a `string_view` and is `constexpr`, so it runs in `static_assert`. The result owns a copy of the text and holds 32-bit offsets into it, so it holds no view of anything else, and it can move. `read_jtext_file`, in `src/jtext/`, reads a file through `read_at` and is the only part that needs a library, `jgui_jtext`. It does not link jText's library.

**Lines.** A line is what section 3 of jText's `SPEC.md` says: optional padding of one space, a number from 1 to 99, a period, whitespace, a formatter counted in code points, whitespace, data, and a comment marked by its character with a space on each side. Each value is trimmed of spaces and tabs only. The old longer markers keep their right two characters, as the spec says. A formatter's characters must be punctuation or symbols by Unicode's general category, from a table generated from Unicode 16 and kept in `jtext_characters.hpp`, so a character assigned one of those categories later is refused until the table is regenerated. A comment is recognised and dropped: the spec asks readers to keep comments, and no reader of ours needs them yet.

**Structure.** The `//` wrapper and other comment lines, which start in the first column, are skipped, a byte-order mark is skipped, and a line may end in `\r\n`. Every block must close, so a file cut short anywhere is refused. A record starts at field 1, a later line must be past what its record has filled, and a field its record leaves out is the empty string, so compact rows, one-line-per-field records and a mix of them all read. Field declarations must be numbered in order and need a name and at least one more level. What those levels say is not read: a typed row decides how a value is read.

**It refuses, naming the line, and never guesses.** Beyond what the spec forbids, it refuses what it does not do yet: the light profile, `[]` raw lines, `[*` multiline values, includes and templates. An include is refused so that no file can make this open another. It is stricter than the spec in three places, each to avoid a choice it would otherwise have to guess: two sections or two fields with the same name; a file with no `=== jText File ===` header, which the spec lets older logs omit, because a cut file could look like one; and a control character anywhere, including U+001F, the null mark of files before version 0.8. It also keeps to the spec where jText's own parser is looser: more than one space of padding, no whitespace after the period or after the formatter, and text after a formatter's last punctuation with no space between, are refused. A jText version in the header is not compared to anything. No error message quotes the file.

| Limit | Default |
|---|---|
| The whole file, `JTextLimits::file_bytes` | 64 MiB, and never over 4 GiB because offsets are 32 bits |
| Sections in a file | 256 |
| Rows in a section | 1,000,000, the same as a grid, as a `static_assert` shows |
| Values in the file, counted as rows times declared fields | 16,000,000, so a short file cannot ask for much memory: at most 128 MiB of offsets |
| A section's or a field's name | 256 bytes |
| Fields in a section | 99, as the format says |

**Helpers** each answer with a `std::expected`, and an empty or wrong value is an `Error`, never a default: `get_section`, `get_field_index`, `get_row_count`, `get_cell_string`, `get_cell_int`, `get_cell_bool`, `get_top_one_string`, `get_top_one_int`. Numbers are strict: an optional sign and digits, in range, and nothing else. A view from `get_cell_string` needs the file to stay alive.

**Typed rows.** `get_typed_rows<Row, Columns...>` takes the same `Column` types a `DataGrid` does, and `get_rows_for_grid<Grid>` takes them from a grid's own declaration, so a program writes them once. Columns are matched to the file's fields by name, in any order. A column the file lacks is an `Error`, and a field the program does not name is ignored. A row with a value that does not convert is left out and counted in `left_out`. `CellParse<T>` says how text becomes each type, and a program can add one. A negative number for a length is a conversion failure, not a zero, and a member with no `CellParse` does not compile.

## The viewer

`demo/viewer.cpp` is `jcppgui-viewer`. It shows `redraw_monitor.jtext` and `settings.jtext` from the application's base directory, or the paths given as its two optional arguments, in that order, and reads each once, at the start. `demo/viewer_ui.hpp` is its window, which the tests include: a menu bar with a button per section of both files and a font button, a line saying what shows, and the chosen section's grid in all the room left. It opens on `Frames`.

A path, a file or a section that cannot be read never ends the program. The window opens, the line says why, and no grid is shown. Either file's trouble shows only at that file's buttons. A section the file lacks, or that lacks a column, is that section's trouble alone. Rows left out are counted on the line. Each section's grid keeps its rows, its scroll, its sort and its columns' settings while another shows.

The font button sets every column of the grid showing, and no other, at once and with no wait. Its caption is what a press will do, `All monospace` or `All regular`, the second only when every column there is monospace. `ViewerWatch`, a widget with no size in the tree, keeps that caption true on each turn of the loop, and puts a refused monospace font on the status line.

`OneShown<Tag, Children...>` is the small widget this needed. It holds several widgets and only the one shown gets the layout, the paint and the events, so the others cost nothing. A callback holds no key, so the one going away is told the pointer and the focus have gone as soon as that delivery ends.

## The file manager

`demo/files.cpp` is `jcppgui-files`, and `demo/files_ui.hpp` is its window, which the tests include: a bar with Compare and Diff, a `FileTransfers` line, and `TwoPanes` holding two `FilePane`s, under six `PopupLayer`s. The behaviour follows TreeSync's two trees, in the owner's `jac317`, and none of its code is used. **Nothing in it removes anything for good, and a copy replaces nothing. Delete moves things to the trash, and needs a count and a click.** See below.

- **`TwoPanes<Tag, LeftPane, RightPane>`** gives each of any two widgets half the room, with `left_half_of` and `right_half_of`, and holds nothing else.
- **`FilePane<Tag, Source, Transfers>`** is a whole pane: Up, Show this folder, Reload, Copy and Paste, a path line, a status line and a grid. Each part's tag is made from the pane's own, such as `FilePaneGrid<LeftPane>`, so two panes share no state and neither names the other. A key goes to the pane whose grid has the focus.
- **`FileTransfers<Tag, Source>`** is the one thing the panes share, and each knows it only by the tag it was given: what was last copied, a copy under way, a drag, and a count of changes. When that count moves, each pane reads again by itself, keeping what is open and selected by path. It must come before the panes in the tree, because it is what turns a press into a drag, and a flood of motion can arrive as one event, which both panes must then see as a drag.
- **A copy needs no thread.** `FileCopyJob::step` does one small thing: one chunk of 1 MiB written, one chunk read back and summed from each side, a flush, or one thing in a folder. `FileTransfers` takes one step on each `TimersDue` and asks with `wake_at(now)` for the next turn at once. So events go on being delivered between steps, the compositor's pings are answered, and Escape stops it. A step that blocks on a slow disk still holds the window for as long as that step takes. The line is redrawn at most every 100 ms, because a frame costs more than a step.
- **Bringing it up.** The panes are ordinary widgets, so a program holds them in a `OneShown` and shows them when it wants, as `tests/test_files.cpp` does from a button. While hidden they cost nothing and keep what they hold. There is one window: the toolkit has no second window or `xdg_popup`, so `PopupLayer` draws a popup inside the window, over the rest.
- **`FileSource`** is a concept with `read`, `read_text`, `start_copy`, `start_match`, `start_count`, `move_to_trash`, `touch`, `rename`, `default_program`, `program_installed`, `start_program`, `picture_folder`, `save_picture`, `editor` and `run_in_terminal`, and each pane holds one by its own type, so nothing is erased. `LocalFiles` is the real one, a program links `jgui_files` for it, and the tests use a made-up disk in `static_assert`. A source for another machine would be another type, but `read` and `read_text` answer at once, on the UI thread, so a slow source would need more than this. So would a slow local disk: a network mount that hangs holds up the window. `LocalCopyJob` keeps its descriptors out of its header, behind one concrete `Impl` with a stateless deleter, as `WaylandImpl` does.
- **Deleting** goes in stages inside `FileTransfers`: a count of everything selected on one pane, then `FileDeletePopup` asks with that number, then each selected thing moves to the trash, one a turn. Only the popup's button calls `confirm_delete`. A count that cannot finish ends it with nothing moved. `move_to_trash` follows the freedesktop.org specification: the home trash for what is on its disk, otherwise `$top/.Trash/$uid` or `$top/.Trash-$uid`. It writes the info file with `O_EXCL`, which holds the name, then moves the thing by one rename, so it never copies, and nothing in the program removes anything for good.
- **Cat and Edit** build a list of separate arguments in `terminal_command.hpp`, all constexpr, and `run_in_terminal` starts the first terminal it can: `TERMINAL`'s, then those in `known_terminals`. Edit uses `EDITOR`, split on spaces with no shell, and Vim when that is empty or names a program that is not a runnable file at that path or in an absolute folder of `PATH`.
- **Touch and Rename** ask for a name in `FileNamePopup`, which holds a `TextEntry`, the toolkit's one line a person can type in. A `KeyEvent` carries a bounded `KeyText`, what the backend's keymap says the key types, and `TextEntry` takes it only if it can show, so a keymap cannot put a control character or invalid UTF-8 in a name. It has a caret and edits by whole UTF-8 characters, and text longer than the box slides to keep the caret in view. It has no selection or clipboard, no dead keys or input method, and a held key does not repeat, because the backend does not repeat keys yet. A popup passes it the keys, so it needs no focus of its own. `check_given_name` refuses a slash, `.`, `..`, nothing, and what cannot show. Touch opens with `O_CREAT | O_EXCL | O_NOFOLLOW`, so making it and finding it taken are one act, and only then sets the time. Rename uses `renameat2` with `RENAME_NOREPLACE` in the same folder, so it cannot move or replace anything, and a disk that does not support that flag is refused rather than looked at first. Return may say yes here, unlike in the popups that guard a delete, because neither can replace or remove anything.
- **Open** takes one selected file from either pane. `local_default_program` asks `xdg-mime` for the file's type and its default entry, reads that entry from the `applications` folders under `XDG_DATA_HOME` and `XDG_DATA_DIRS`, and `desktop_command_for` in `open_programs.hpp`, which is constexpr, makes a list of words with the file in place of the field code. `start_program` starts it with two forks and no shell, in the file's folder, and it goes on by itself, as a terminal does. If `xdg-mime` is missing, there is no default, the entry cannot be read or its `Exec` line would need a shell, or the program will not start, `FileOpenPopup` asks instead: one button of its own type for each of up to 12 places, filled from `common_programs` by the file's extension and what is installed, and a box for a typed name. `read_text_file` does not follow a link, so an entry that is a link, as a Flatpak's export is, also asks instead. Nothing it is told is remembered as a new default.
- **ScreenShot** asks for a name in the same `FileNamePopup`, which on OK closes and calls `Window::request_picture`. So the next frame has no popup in it, and `ApplicationCore` draws that frame even with nothing else damaged, and copies it between painting and presenting with `PixelBuffer::picture`. The display is asked nothing, so only this window can be captured. On the next `TimersDue`, `FileTransfers` takes the picture from the window, makes it a PNG with `png_of` in `png.hpp`, which is constexpr and needs no library, and saves it through the source. It stores with no compression, so a file is about three bytes for each pixel. The panes do not look again afterwards, because that would take away the marks of a comparison the picture may be of.
- **The clipboard** goes one way, out. A `CopyLine` offers its whole text with `propose_copy_text`, which sends it through `inspect_clipboard_text`. Clean, it becomes a request the `Application` carries out on the backend. Otherwise it waits as a `ClipboardQuestion`, which `FileClipboardPopup` takes, and only its Continue button lets it go. A window with no such popup never lets it go. The popup sits in the outermost `PopupLayer`, because a line inside another popup can be copied too.
- **Diffing** is `LineDiffer` in `line_diff.hpp`, all constexpr: Myers' search on line numbers, one round per turn, with limits of 16 MiB, 200,000 lines a side and 4,000 changes. `shown_line` makes each untrusted line safe to show, and `FileDiffPopup` lists changes with three lines around each. `PopupLayer` nests, so each popup is its own layer, and only one thing runs at a time, so only one is ever open.
- **What a copy does.** Everything made is made with `O_EXCL`, `mkdirat` or `symlinkat`, each of which the kernel refuses for a name that is taken, so there is no moment between a look and an act in which something could be replaced. A link is copied as a link, and what it points to is not touched. A pipe, a device or a socket is counted and left. Names that cannot be shown are copied all the same. Permissions and times are copied, with no set-user-ID, set-group-ID or sticky bit, and the owner and extended attributes are not. A file is private until it has matched. One not finished or not matching, from a failure, Escape or closing the window, is removed, and that is the one thing this code ever removes: a file it made itself. Folders already made stay, and the line says where. A folder going inside itself is refused by its path, and again during the copy by its device and inode, which a link cannot hide. A sparse file comes out whole.
- **The match.** Each regular file is flushed every 16 MiB and at its end, because one flush of a large file holds up the window for seconds, and then dropped from the cache where the system allows. The copy is opened afresh and checked to be the same file by its device and inode, and both sides are read back a chunk a turn and summed with `Sha256` from `sha256.hpp`, which is `constexpr` and needs no library. The original is read back too, rather than trusting what was read to write it, so a change during the copy is found. Only a match counts the file and gives it its permissions and time. The line says `every file matched by SHA-256` only when the job ended with at least one file counted, and `FileCopyJob` makes that a promise of any source: a file is counted only once it has matched. See `docs/Maturity.md` for what this does not cover.
- **Where it goes.** Paste goes into the folder selected, or the folder of the row selected, or with nothing selected the folder at the top. A drag asks the same of the row under the pointer, and past the last row it is the folder at the top. Six pixels of motion make a press a drag, so a click cannot copy anything. A drop starts a copy like a paste, and leaves what was last copied for a paste alone. Dropping into the folder it is already in does nothing.
- **Several at once.** `DataGrid` keeps a sorted list of selected rows by their own order, so a sort leaves them alone, along with the current row and where a Shift range starts. Ctrl adds or takes away, Shift or Shift and an arrow takes the range in the order shown, and `select_rows` and `select_all_rows` are for its owner. A plain press on one of several waits for the release to narrow to that row, so that a drag can take them all, and `keep_selection_on_release` is how a pane says one happened. So a press carries the modifiers held, and `PointerButton` and `PointerPress` have them for that. A pane leaves out what is inside a selected folder, and opening, closing and looking again keep the selection by row and path. `FileTransfers` runs one job after another. What can be refused from the paths alone refuses all of it before anything is made, including two with the same last name. A name taken in the folder is found only when that one's turn comes, and it stops there, with what went before left in place.
- **Matching a selection.** With two or more selected, `compare_panes` gives the selections to `plan_matches`, which is `constexpr`, and `FileTransfers` runs one `MatchJob` per pair a step a turn, as it does a copy, and never alongside one. `LocalMatchJob` opens everything without following a link and only reads: a step is one chunk from each side or one name of a folder, though reading a folder's names is one step however many there are. A file's sums are compared only at the end, so a large pair that differs early is still read whole, unless the sizes already differ. A difference does not stop it. `most_match_findings` are kept and the rest only counted. `FileMatchPopup` takes the report on the turn it ends. `PopupLayer` holds it last in its children, so the existing delivery asks it first about the pointer, and keys stop at it while it is open. It is drawn in the window, so it cannot go outside it.
- **Comparing.** With fewer than two selected, `compare_panes` builds a sorted `ComparedItem` for every row on each side, by the path below the side's top folder, and `match_against` gives each row a `FileMatch`, shown in the Compared column. It is `constexpr` over those lists and asks nothing of a disk, so it can be wrong in these ways: files with the same size and time, in whole seconds, can hold different bytes; a folder or a link in both says nothing of what it holds or points to; and a name that cannot be shown is not in the rows. A row below a folder that is closed on the other side is not compared, since nobody has looked there. Marks live on the rows, so reading again clears them, and opening or closing a folder keeps those of the rows that stay.
- **The tree is the rows.** A `FileRow` holds a `TreeName`, with its depth and its mark, and the rows are kept in tree order in the grid and nowhere else. What a row is inside is the nearest row before it that is less deep, so a row's path is worked out when it is needed. Opening a folder reads it only then, and puts its rows after its parent. Closing one takes away every deeper row after it. Each of these copies the rows, so it costs in proportion to how many are open, and that has not been measured.
- **Sorting** is the grid's own, so it sorts all the rows showing, as one flat list, whatever their depth, and the indents stop meaning anything until the sort is cleared. Opening, closing and reading again keep the sort and the selection. Going to another folder clears them, and closes everything.
- **Names and paths.** `FileEntry` values come through `read_directory`, which holds what is at the path to being a folder and not a link, and looks at each thing without following it, so a link shows as a `Link` and cannot be opened. `starting_folder` resolves the two arguments the person gave, and it is the one place a link is followed. A pane holds only absolute paths with no empty, `.` or `..` part, so the folder above is plain from the text. A name with invalid UTF-8 or a control character cannot be shown, so it is left out and counted on the status line.
- **`Modified UTC`** is in UTC, because that needs no system call and no time zone database, written `YYYY-MM-DD HH:MM`.
- **A mark's place** comes from shaping `[+] ` and four spaces in the name column's font, each layout. So a press finds a mark only with the name at the left of its column, which is where it starts. With the column aligned otherwise, the keys still work.

`DataGrid` gained `row_at`, `column_left` and `select_row` for this. `select_row` sends no message, because the caller knows.

## Panel helpers

`panels.hpp` is `constexpr` and small. `full_of`, `left_half_of`, `right_half_of`, `top_half_of`, `bottom_half_of` and the four quarters such as `top_left_quarter_of` each take a parent `Rect` and return a `Rect`. A quarter is built as a half of a half. With an odd size, **the left half and the top half take the extra pixel,** so two halves always meet and always make the parent. `split_remaining_space` takes a length and an ordered list of `PanelSize` along one axis, where filling is what a default-made one does and `PanelSize::fixed` must be asked for by name. Fixed panels take their size first, in order, and filling panels share the rest, the earlier ones taking any extra pixels. Too much asked for clips in order, so sizes never go negative or overlap. With no filling panel, what the fixed ones leave stays unused. There is no panel widget, splitter or layer yet. `OneShown` uses `full_of`, and nothing in a program uses `split_remaining_space` yet.

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
`bash tests/check_source_rules.sh`. The one exception, which the owner approved, is the kernel's `proc/self/exe` link as one
literal in `src/base_directory.cpp`, enforced by that script: see `PROCESS.md`.

## The window frame

GNOME does not draw title bars for Wayland clients, so the toolkit draws a minimal frame: a title
strip with no text, a close box, and resize edges and corners.

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
  `ui`: `request_move(serial)`, `request_resize(serial, edge)`, `request_cursor(shape)`, `suggest_cursor(shape)` or `request_close()`. These are typed
  fields, and `ApplicationCore` carries them out through the backend when the dispatch returns, in the
  same call that delivered the press. `PointerPress` carries the press's serial for this. Close goes
  through the same `on(CloseEvent)` as a close from the compositor.
- **Size.** The `Window` holds the validated window size, set as soon as a resize arrives, so a press
  queued behind a resize is judged at the new size even before the next layout.
- **A window with no frame** also works, with no chrome. Clearing the focus on a press is the frame's
  job, so such a window does not do it, and its pointer is the plain arrow, unless a widget in it asks for another.

### What the pointer looks like

The protocol leaves the pointer's look undefined at each enter, so a client must say. `CursorShape` is a typed request, and `set_cursor` is
part of the `Backend` concept, so all of this runs headless and in `static_assert`.

- **The shape comes from the frame's own hit test.** `cursor_for` maps each of the 8 `ResizeEdge` values to its shape and everything else to
  the arrow. The frame asks on each motion, and on an enter, which a backend delivers as a `PointerMove` marked `entered`.
- **Content can say what it wants over itself.** On motion the frame asks first and the content is delivered to after it, and the last request in one delivery wins.
  `DataGrid` does this to show `CursorShape::ResizeColumn` over a header boundary, and never asks outside its own bounds, so the frame's edges keep their shapes.
- **Only a change is sent.** `ApplicationCore` remembers the shape it last asked for, and forgets it at each enter.
- **Nothing changes while a press is captured,** such as a drag off the Help button. When that press is released, the frame suggests what is under the
  pointer then, because a drag can end over an edge with no motion to say so. A suggestion gives way to anything already asked for in that delivery, so content that
  has just answered for itself is left alone.
- **On Wayland it uses `cursor-shape-v1`, as an optional global.** The compositor draws the user's own themed cursors, so this process parses no XCursor file and
  does not use libwayland-cursor, which maps files in. Each request carries the serial of the latest enter. The device goes before its pointer on
  teardown, on losing the seat and on losing the pointer. The protocol has no events, so it needs no listener and no new untyped pointer. Its generated tables name
  the tablet protocol's tool, so that protocol's tables are built too, and nothing else of it is used.
- **A compositor without it gets no resize cursors.** It keeps the small arrow we draw, set at each enter, over the whole window. If the global
  goes away while running, or binding it or making the device fails, it is the same: none of those is an error.
- A window that is resized under a still pointer shows the old shape until the next motion.
