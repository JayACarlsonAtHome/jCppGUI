# Architecture

## The type rule

Objects come from templates, each item is its own type, and there is no type erasure. These are
banned in all our code, including tests and the demo: `std::function` and its relatives, `std::any`,
`std::shared_ptr`, `dynamic_cast`, `typeid`, `virtual`, abstract base classes, base-class pointers or
containers of widgets, `void*`, function pointers used to hide a type, and hand-written erasure under
any name. `std::variant` is a last resort and needs a comment saying why. There are none today.

**The one exception** is `src/wayland/trampoline.hpp`. libwayland hands listeners an untyped pointer
and requires C function pointers. The helper casts straight back to the exact type registered, and
the pointer goes no further. The exception ends when our own Wayland client replaces libwayland.

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
  `std::vector` to run the same logic. A vector-backed pool aborts if used at run time.

Non-`constexpr` code is limited to what needs the OS or a C library:

| Code | Why |
|---|---|
| `src/wayland/wayland_backend.cpp` | libwayland, sockets, `poll`, `memfd_create`, `posix_fallocate` |
| `load_keymap`, `UniqueFd` in `src/wayland/untrusted` | `close`, xkbcommon. The checks on values are `constexpr` |
| `read_at`, `write_at` in `src/wayland/file_io.hpp` | `pread`, `pwrite`. Their loop and arithmetic are `constexpr` and tested with made-up system calls |
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

## The typed tree

A window is one composite type built from `std::tuple`s, so the compiler knows its whole shape.

- `u.get<Tag>()` is resolved at compile time. An unknown tag and a duplicate tag are compile errors.
- Paint, hit testing and event delivery are fold expressions over the tuple. There is no run-time
  widget list and no dynamic dispatch.
- `Widget` is a concept and not a base class. `handle` takes each event that has an answer, as its own
  type, and `notify` takes each notice, which has no answer and so nothing to discard. A widget with no
  interest in some of them gives one catch-all template of each.

**Callbacks are generic lambdas with an explicit `-> void`.** This is required, not style. The tree's
type contains the callback's type, so the body may only be instantiated after the tree is complete. A
deduced return type would force it too early. For the same reason, do not constrain callbacks with
`std::invocable` on the tree type. The `Widget` concept is checked against an empty probe type, and
declared return types keep that from instantiating any handler body.

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
