# Maturity

**This is a skeleton.** It proves foundations. It is not a usable toolkit.

## What works

- A native Wayland window with a toolkit-drawn frame, a menu bar and a Help button that toggles a line
  of text. The frame is a widget in the tree, `WindowFrame<Metrics, Children...>`, with its sizes fixed
  at compile time by the metrics type. `demo --smoke` passes against a live GNOME session.
- The typed tree, typed pools with deferred destruction, and compile-time message routing.
- The same behaviour with no screen, through `HeadlessBackend`, down to rendered pixels.
- The core logic run in `static_assert`, including the demo tree from events to pixels.
- Compile-fail tests that prove the compiler rejects the mistakes it is meant to catch.
- The real `WaylandBackend` against a scripted compositor, with no screen, over a real socket.
- `DataGrid`, a read-only typed table with virtual drawing, scroll bars, wheel and smooth scrolling, selection, sorting and column resizing. Only through tests so far: no program uses it yet.
- A redraw monitor for development, `jcppgui-demo --redraw-monitor`, which records each frame to a jText file. A file from a live
  `--smoke` run passed jText's own parser and validator, in a throwaway check that is not part of this repository.

## What is tested through the real backend

`tests/fake_compositor/` is a compositor in our own C++, with no libwayland-server, which speaks the wire protocol with typed,
bounds-checked code. `tests/test_fake_compositor.cpp` runs the real backend and the demo's real tree against it, in a forked
child, each scenario as its own CTest entry. As well as what each script checks, it holds the client to the protocol: the
ack before a commit, no write to a held buffer, no attach of a held buffer, ids, sizes, the order of destruction and an orderly end.
`tests/test_fake_compositor_rules.cpp` breaks each of those rules on purpose, and requires that it is noticed.

- **The whole path.** Pointer to pixels on the wire: clicking Help shows its line, read back from the buffer file. Key to action,
  through a real keymap. Ping and pong, the title and app id, and a move or resize with the press's own serial.
- **Sizes.** 1x1, 0x0, -5x-7, `INT32_MAX`, 8192x1, 1x8192 and 119x119, and 2000 configures at once.
- **Buffers.** A release before attach, three at once, none ever (the client stops at four and waits, still answering, and closes
  cleanly), and a late one (the wait ends in a frame at the latest size). The compositor trying to truncate or grow the file.
- **The seat.** Capabilities flapping, the seat going with a button held, and every missing or too old global.
- **Keymaps.** A pipe, a directory, a short file, 1 MiB with no terminator, size 0xFFFFFFFF, format 7, and none at all. A valid
  one followed by extreme keys, modifiers and states.
- **The pointer.** `wl_fixed` extremes, bogus buttons and states, a leave with no enter, a burst of motion, and events of later versions. Scrolling: `INT32_MIN`, `INT32_MAX` and zero as pixels and as steps, an unknown axis and source, a flood in one frame and a flood of frames.
- **The pointer's look.** `cursor-shape-v1` over all 8 edges and the middle, with each request's serial, device and shape value held to the protocol, only changes
  sent, and re-entering on a corner. A compositor without it, the global going away, the seat or the pointer going with a shape set, and a drag across the edges.
- **The connection.** 70,000 events at once, a sudden disconnect at each stage, part of a message and then a hang-up, a size that
  is too small, too large or not aligned, an unknown opcode or object, arguments too short, a string with no terminator, a
  keymap with no descriptor, a protocol error on an object (`EPROTO`, which had never run) and on `wl_display`.
- **Signals.** A real `EINTR` in `poll`, with a timer interrupting the client's sleep.

`tests/test_injection.cpp` makes each C call fail in turn through the real implementation, with no test branch in it: a null
from every proxy-making call and every refused listener, the `EINVAL` fallback of `memfd_create`, and failing reservations and seals.
It also does a real short write and a real short read, under `RLIMIT_FSIZE`, and checks the compositor's request numbers against
libwayland's. `tests/fuzz/fuzz_backend.cpp` fuzzes the backend's input path through a real socket.

## The data grid

`tests/test_grid_helpers.cpp` has each small helper in `static_assert`. `tests/test_datagrid.cpp` runs the grid in `static_assert`, from injected events to state and pixels, with a shaper that needs no font: selection, keys, wheel, thumb drag, trough paging, sorting, stability, column drags,
cursor shapes, the frame's edges, damage, a leave and a resize during a capture, and windows of 1x1. At run time, with the real font, it has the row limit and the picture. Compile-fail pairs cover a member with no format, a member of another row, a duplicate column and an ignored `set_rows` answer. `tests/test_fake_compositor.cpp` has `grid_*` scenarios through the real backend: hostile axis values and floods, scrolling with no pointer here, a wheel notch and then pixels read back from the buffer file, a resize during a thumb drag, the pointer and the seat going during a capture, and the column cursor on the wire.

**Measured** in a `gcc-release` build by `tests/measure_datagrid.cpp`, by hand and not under CTest, through `FrameMonitor` with the steady clock, headless, at 1280x800 with 6 columns and 32 rows showing, on the machine it was run on. These leave out what the real backend adds to present, which is writing the frame to the buffer file.

| Rows | Layout, median | Paint, median | Wheel-notch frame total, median | Worst |
|---|---|---|---|---|
| 100 | 0.84 ms | 1.30 ms | 2.14 ms | 2.73 ms |
| 1,000,000 | 0.84 ms | 1.30 ms | 2.14 ms | 2.19 ms |
| 1,000 with 100,000 bytes in one column of each | 4.80 ms | 1.46 ms | 6.26 ms | 6.90 ms |

Sorting a million rows took about 170 ms by a number and 105 ms by a string, with its frame, in one delivery. So a sort of that size misses frames.

Not shown: a real wheel, a real touchpad and real keys against a real compositor. The navigation keys are checked against xkbcommon's values at compile time and run in `static_assert`, but not through the keyboard on the wire, whose test keymap is a minimal one. Hover has no look of its own.

## The redraw monitor

`tests/test_redraw_monitor.cpp` runs it with a test clock in `static_assert`, through the application and the demo's tree, and at run time
with the real font and the real file: a directory, a symlink, a dangling symlink, a pipe, a missing or unwritable directory and a real
failed write under `RLIMIT_FSIZE`, each with the program going on. `tests/test_fake_compositor.cpp` runs it through the real backend, and shows the
same frames, pixel for pixel, reach the compositor with a monitor and without. It also starts the real demo program from elsewhere, as built and as copies,
and shows where the file lands, or the refusal when no `vNNN` folder is above it. `tests/test_base_directory.cpp` has the rule in `static_assert`, and at run time copies, links, and an executable
replaced or removed while running. Not tested: the kernel's link itself failing to read, which needs a system with no `proc` mounted.

Not shown: a long interactive session against a real compositor, and how long writing takes with a full 65536 rows. Nothing in this repository
checks the file with jText's validator, because it does not depend on jText. A crash loses more than about a second after a quiet
time, and a system crash may lose more than that: see `docs/Architecture.md`.

## What has not been shown

- **Interactive behaviour in a real session has been checked only by hand, if at all.** Clicking, dragging, resizing and
  closing depend on the compositor. No test presses a real pointer. The test compositor only shows the toolkit's side of them,
  against what this project understood the protocol to be.
- **No test has seen a resize cursor on a real screen.** GNOME Shell's library names `wp_cursor_shape_manager_v1`, but what a cursor looks like, and that the
  compositor accepts these requests during real pointer motion, can only be checked by hand. The test compositor enforces what this project read in the protocol.
- **By default, no test touches a real compositor or opens a window.** Every test that connects reaches only the test compositor's private
  socket, and that includes the real demo program in the `fake_demo_*` scenarios. So what the default run covers is this project's reading of the protocol.
- **Coverage against a real compositor comes from the owner's hand check, or from configuring with `-DJCPPGUI_LIVE_TESTS=ON`,** which no preset
  does. It adds `demo_smoke` and `demo_smoke_redraw_monitor`, which open real windows, and which are skipped with no `WAYLAND_DISPLAY`. The keymap rule
  was checked against one real keymap, from GNOME. Others have not been tried.
- **Paths still with no real test:**
  - A real `EINTR` from `pread`, `pwrite` or `posix_fallocate`. On the kernel tried, tmpfs reads and writes ran to the end under
    thousands of timer signals, and reserving 256 MiB finished too quickly for one to land. The loops are tested with made-up results.
  - A short transfer that then succeeds. The real short write ends in `EFBIG`. Going on after one is tested with made-up results.
  - Out of memory: each `std::bad_alloc` handler in `src/wayland/`.
  - A failed `fstat` or `fstatfs` on a keymap, `poll` itself failing, a failed `wl_display_flush`.
  - The memory budget at its byte limit against the real backend, because each frame there is 256 MiB. It is tested on its
    own, in `static_assert`, and the limit on four buffers is tested through the wire.
  - A buffer over 16 MiB is not checked for writes while held, because each check reads the whole file.
  - The clipboard and everything else not implemented.

## Known limits

- **`bsir.sh` has not been run end to end**, on any system. **It still needs the owner to run it**, because it needs sudo. It is
  written for Fedora only and stops on a system with no `dnf`. Only its syntax, its usage text and its recipes' check lines
  have been tried.
- A compositor that stays connected and never answers makes `connect` wait for ever. There is no timeout.

- One font, one size, one line. No text editing, input methods or font selection. No bidirectional
  text ordering beyond what HarfBuzz does for one segment.
- A damaged window repaints in full, and the frame is not throttled to `wl_surface.frame`.
- **Each frame is copied once more than it must be.** A frame is drawn in ordinary memory and written
  to the buffer file with a system call, because nothing of ours is mapped. Secure first, efficient
  second. The cost has not been measured.
- Layout is a simple flow, not a layout manager. Overlapping widgets are possible but untested.
- A window's side is at most 8192 pixels, and frames wait if the compositor holds too much.
- `DataGrid` holds at most 1,000,000 rows, has no editing and no multiple selection, and keeps neither selection nor sort when given new rows. Pressing in a scroll trough pages once, with no repeat while held.
- Scale is fixed at 1, so the window is upscaled on HiDPI outputs.
- The frame has no maximize or minimize control.
- A compositor without `cursor-shape-v1` gets no resize cursors, only the drawn arrow.
- No key repeat, popups, drop-down menus, clipboard, drag and drop, themes or accessibility.
- No X11, Windows or macOS, and no C++26 or modules.
- No CI.
