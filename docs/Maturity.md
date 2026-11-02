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
- **The pointer.** `wl_fixed` extremes, bogus buttons and states, a leave with no enter, a burst of motion, and events of later versions.
- **The connection.** 70,000 events at once, a sudden disconnect at each stage, part of a message and then a hang-up, a size that
  is too small, too large or not aligned, an unknown opcode or object, arguments too short, a string with no terminator, a
  keymap with no descriptor, a protocol error on an object (`EPROTO`, which had never run) and on `wl_display`.
- **Signals.** A real `EINTR` in `poll`, with a timer interrupting the client's sleep.

`tests/test_injection.cpp` makes each C call fail in turn through the real implementation, with no test branch in it: a null
from every proxy-making call and every refused listener, the `EINVAL` fallback of `memfd_create`, and failing reservations and seals.
It also does a real short write and a real short read, under `RLIMIT_FSIZE`, and checks the compositor's request numbers against
libwayland's. `tests/fuzz/fuzz_backend.cpp` fuzzes the backend's input path through a real socket.

## What has not been shown

- **Interactive behaviour in a real session has been checked only by hand, if at all.** Clicking, dragging, resizing and
  closing depend on the compositor. No test presses a real pointer. The test compositor only shows the toolkit's side of them,
  against what this project understood the protocol to be.
- **Against a real compositor, the only automatic check is `demo --smoke`,** which is skipped with no `WAYLAND_DISPLAY`. The keymap rule
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
- **`tests/fake_compositor/socket_io.hpp` awaits the owner's approval.** It is the one helper, for tests only, where `sendmsg`,
  `recvmsg`, `iovec` and `CMSG` meet untyped memory. A descriptor cannot be passed any other way. The rule scripts keep it the only place.
- A compositor that stays connected and never answers makes `connect` wait for ever. There is no timeout.

- One font, one size, one line. No text editing, input methods or font selection. No bidirectional
  text ordering beyond what HarfBuzz does for one segment.
- A damaged window repaints in full, and the frame is not throttled to `wl_surface.frame`.
- **Each frame is copied once more than it must be.** A frame is drawn in ordinary memory and written
  to the buffer file with a system call, because nothing of ours is mapped. Secure first, efficient
  second. The cost has not been measured.
- Layout is a simple flow, not a layout manager. Overlapping widgets are possible but untested.
- A window's side is at most 8192 pixels, and frames wait if the compositor holds too much.
- Scale is fixed at 1, so the window is upscaled on HiDPI outputs.
- The frame has no maximize or minimize control, and the resize edges show no resize cursor.
- No key repeat, popups, drop-down menus, clipboard, drag and drop, themes or accessibility.
- No X11, Windows or macOS, and no C++26 or modules.
- No CI.
