# Maturity

**This is an early prototype, still experimental and changing constantly.** It proves foundations. It is not a usable toolkit, and this page can fall behind the code.

## What works

- A native Wayland window with a toolkit-drawn frame, a menu bar and a Help button that toggles a line
  of text. The frame is a widget in the tree, `WindowFrame<Metrics, Children...>`, with its sizes fixed
  at compile time by the metrics type. `demo --smoke` passes against a live GNOME session.
- The typed tree, typed pools with deferred destruction, and compile-time message routing.
- The same behaviour with no screen, through `HeadlessBackend`, down to rendered pixels.
- The core logic run in `static_assert`, including the demo tree from events to pixels.
- Compile-fail tests that prove the compiler rejects the mistakes it is meant to catch.
- The real `WaylandBackend` against a scripted compositor, with no screen, over a real socket.
- `DataGrid`, a read-only typed table with virtual drawing, scroll bars, wheel and smooth scrolling, selection, sorting, column resizing, and each column's font and alignment from buttons in its header. **The automated tests of the buttons, the wait after a press and monospace text run only against the test compositor and headless. The owner has used the grid on a real screen, as noted for the viewer below.**
- A timer in the event loop, on the monotonic clock, that a widget asks through `TimersDue`. **That an idle program uses no CPU is shown by counting the loop's turns against the test compositor, and has not been measured on a real session.**
- `jcppgui-viewer`, which shows a redraw monitor's file and a settings record in those grids, one section at a time. **The automated tests run only against the test compositor. On 2026-11-03 the owner used it on a real screen and accepted the grid by eye, which was not a check of each feature.**
- `jcppgui-files`, two independent trees of local folders side by side, with copy and paste between them and dragging to copy, each file checked by SHA-256, several selected at once, the selection matched by SHA-256 with a popup saying how it came out, what shows compared by size and time, moving to the trash after a count and a question, a diff of two text files, Cat, Edit, Touch and Rename with a box to type the name in, and status lines that copy to the clipboard through a sanitizer. There is no move, and nothing is removed for good. **It changes real files. Tested on temporary folders, against the test compositor and headless. On 2026-11-04 the owner copied 22 GiB with it on a spindle drive, on a real screen, in about 20 minutes, and it worked, and used matching a selection on one and said it works. Moving to the trash, diffing, Cat, Touch, Rename, typing in the box and the clipboard have not been reported used on a real screen. Typing has only been through the test keymap, which has one letter, so a real layout, Shift and non-English characters are untested off the wire. A held key does not repeat, and dead keys and input methods do not work. On 2026-11-04 the owner opened a file with Edit on a real screen, in a real terminal. Cat has only been tested with a stand-in for a terminal, and the clipboard only against the test compositor.**
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
- **The connection.** 70,000 motions at once, in the event loop and during a `sync`, which merge into one slot; 140,000 mixed events during a `sync`, which end in the queue limit's `Error`; a sudden disconnect at each stage, part of a message and then a hang-up, a size that
  is too small, too large or not aligned, an unknown opcode or object, arguments too short, a string with no terminator, a
  keymap with no descriptor, a protocol error on an object (`EPROTO`, which had never run) and on `wl_display`.
- **Signals.** A real `EINTR` in `poll`, with a timer interrupting the client's sleep.

`tests/test_injection.cpp` makes each C call fail in turn through the real implementation, with no test branch in it: a null
from every proxy-making call and every refused listener, the `EINVAL` fallback of `memfd_create`, and failing reservations and seals.
It also does a real short write and a real short read, under `RLIMIT_FSIZE`, and checks the compositor's request numbers against
libwayland's. `tests/fuzz/fuzz_backend.cpp` fuzzes the backend's input path through a real socket.

## The data grid

`tests/test_grid_helpers.cpp` has each small helper in `static_assert`. `tests/test_datagrid.cpp` runs the grid in `static_assert`, from injected events to state and pixels, with a shaper that needs no font: selection, keys, wheel, thumb drag, trough paging, sorting, stability, column drags,
the keys that sort a current column and every case of them, ties across keys in mixed directions, a tenth key, the digits and the mark's two directions in pixels, cursor shapes, the frame's edges, damage, a leave and a resize during a capture, and windows of 1x1. At run time, with the real font, it has the row limit and the picture. Compile-fail pairs cover a member with no format, a member of another row, a duplicate column and an ignored `set_rows` answer. `tests/test_fake_compositor.cpp` has `grid_*` scenarios through the real backend: hostile axis values and floods, scrolling with no pointer here, a wheel notch and then pixels read back from the buffer file, a resize during a thumb drag, the pointer and the seat going during a capture, the column cursor on the wire, and in `fake_grid_ctrl_keys_sort` a header clicked, Ctrl from a modifiers event, then Down, Up and Right off the wire, and with Ctrl let go, Up moving only the selection.

**The column controls and time in the loop.** `tests/test_timer.cpp` has what poll is told at every edge, `DelayedChoice`, and the loop with widgets asking to be woken, shown, not shown and in a `Dynamic`, with a stream of other events going past. `tests/test_datagrid.cpp` has, in `static_assert`, each letter and the blank state, the letter changing at once, frames and shaping counted across repeated presses, the wait restarting, each alignment's pixels in a title and a cell, what a press hits at the header's right end, settings through sorting, a drag and new rows, row height with two fonts, a monospace font that will not load, a letter refused, a column that is not there, a long title whole, a drag cutting it and stopping at the floor, a dragged column keeping its width against title and font changes, one not dragged following them, the sort mark hiding and coming back, a title past the limit, and closing with a change waiting. At run time, the real faces. `tests/test_text.cpp` has the real monospace font, which needs one on the system. `tests/test_viewer.cpp` has the font button, its caption across sections, and a refusal on the status line. The `fake_timer_wakes_and_sleeps`, `fake_grid_column_button_waits` and `fake_grid_buttons_under_floods` scenarios run the real loop: it wakes for a deadline with nothing sent, takes no turns once none is pending, a press draws for its letter and again with no event, and floods of presses and motion leave it running and end at the last press. Not shown: any of it on a real screen: how the letters and the buttons look at their real size, how it feels to click them, 300 ms in real use, or a column in a real monospace font beside a regular one. A column whose real monospace font is taller than the regular one has not been seen: on the machine tried, both give the same row height. A real font failing to load has been run only through a shaper in the tests and through a monospace face asked for its own monospace.

**Measured** in a `gcc-release` build by `tests/measure_datagrid.cpp`, by hand and not under CTest, through `FrameMonitor` with the steady clock, headless, at 1280x800 with 6 columns and 32 rows showing, on the machine it was run on. These leave out what the real backend adds to present, which is writing the frame to the buffer file.

| Rows | Layout, median | Paint, median | Wheel-notch frame total, median | Worst |
|---|---|---|---|---|
| 100 | 0.84 ms | 1.30 ms | 2.14 ms | 2.73 ms |
| 1,000,000 | 0.84 ms | 1.30 ms | 2.14 ms | 2.19 ms |
| 1,000 with 100,000 bytes in one column of each | 4.80 ms | 1.46 ms | 6.26 ms | 6.90 ms |

Sorting a million rows, each with its frame, in one delivery: 187 ms by a number, 118 ms by a string, 152 ms by a bool with half the rows tied, 619 ms by that bool and a string, and 617 ms by those two and a number. Going back to the rows' own order took 5 ms. So a sort of that size misses frames, and one of two or more keys misses many, and the program shows nothing until it ends.

Not shown: a real wheel, a real touchpad and real keys against a real compositor. The arrow keys have been through the keyboard on the wire, with a test keymap that has them. The rest of the navigation keys are checked against xkbcommon's values at compile time and run in `static_assert`, but not on the wire. Hover has no look of its own. How readable the current column's tint and a digit in its mark are on a real screen is unconfirmed.

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

## The settings record

A record of what the application created, for looking up, reporting and checking. Nothing is built from it, and no program but the viewer reads it.

`tests/test_settings_recorder.cpp` runs in `static_assert`, through the real application and the demo's and the viewer's trees: each check and each way it can fail, how items are numbered as they come and go, exact rows for a known tree, a resize, no change and no write, the interval, a failed write, a widget not shown, a composite inside a `Dynamic`, the limit, and `BothMonitors`, with the frame monitor's file the same as with no recorder beside it. A deliberately broken widget gives a `Problems` row, and one pixel less does not. At run time: the real font and the real file read back with the viewer's own code, a symlink, a directory and a missing directory at its place, each with the program going on, and the same pixels with a recorder and without. The `fake_settings_demo_*` scenarios start the real demo from a copy, against the test compositor only: the file in the base directory and nowhere else, with the item from a real click on Help, both flags together, and no base directory. The `fake_settings_viewer_*` scenarios start the real viewer, compare every pixel, and look for named text in a row of `Items` and of `Problems`: two good files, a bad row, a settings file missing, malformed, or the monitor's file missing instead, and both default files in a copy's base directory.

A file from the real demo through the test compositor, and one with a row in `Problems`, passed jText's own parser and validator with no errors, in a throwaway check that is not part of this repository. It gives its warning for an empty section, which is the usual `Problems`.

Not shown: any of this on a real screen, a real resize against a real compositor, how long a write takes at the item limit, or a widget of a real program failing a check: the only failures seen were made on purpose. `tests/fuzz/fuzz_jtext.cpp` round-trips what the shared line writer takes, and has no case of its own for this file.

## The jText reader and the viewer

`tests/test_jtext.cpp` runs in `static_assert`: each small helper, the line grammar, a whole file in every form of record, a file cut at every byte, every refusal and limit, typed rows, and everything `FrameMonitor` writes reading back as the same values, including labels that force each fallback formatter. At run time: a real file, a missing one, a directory, a symbolic link, a dangling one, a pipe, one byte over the limit and a 1 TiB sparse file. `tests/fuzz/fuzz_jtext.cpp` also round-trips whatever the monitor's writer will take. `tests/test_panels.cpp` has the panel helpers in `static_assert`.

`tests/test_viewer.cpp` runs `OneShown` and the viewer's whole window in `static_assert`. The `fake_viewer_*` scenarios start the real program, against the test compositor only. Each compares every pixel that arrives with a headless window made in the compositor's process from the same file, and also looks for text the test names itself, drawn alone with the real font: header text, row text and the status line. They cover the first frame, a click on two section buttons, a wheel notch, three sizes, a missing file, a malformed file, a symbolic link, a path that is not text, the default file in a copy's base directory, no base directory, and 65536 rows.

Files used as test inputs passed jText's own parser and validator, in a throwaway check that is not part of this repository. Over the fuzz corpus, the reader was compared with that parser, again by hand: see `docs/SECURITY.md`.

**Measured** in a `gcc-release` build by `tests/measure_viewer.cpp`, by hand and not under CTest, headless, at 1280x800, on the machine it was run on, with a file of 65536 rows in each of `Frames` and `Damage`, leaving out what the real backend adds to present.

| What | Time |
|---|---|
| A 10.4 MB file, `read_jtext_file` as a whole | about 35 ms |
| Of that, parsing | 28 ms |
| Of that, reading, from the page cache | about 6 to 10 ms |
| Typed rows for all four sections | 14 ms |
| The first frame | 3.7 ms |
| A wheel-notch frame, total, median and worst of 200 | 2.3 ms, 2.4 ms |

Not shown: any automated test of the viewer on a real screen or with a real pointer, beyond the owner's use noted above, or a file that changes while it runs: there is no reload or watching. The light profile, raw lines, multiline values, includes and templates are refused, so `samples/workshop_tools` in the jText repository is refused too: one is light profile, and the other has templates.

## The file manager

Two trees, with copy and paste and dragging to copy, several things selected at once, comparing the two sides, moving to the trash and diffing, on local folders. There is no move, on purpose, and no removal for good. **The trash has been used by tests in temporary folders and once on an ext4 data disk, and it has not been checked that a desktop's trash can put something back. Refusing a mounted disk inside a folder is by code tested only on `/proc` as the selected thing, because setting up a mount inside a folder needs privileges.** Diff has no colours, because a grid has no way to colour a row yet, and it reads each file whole on the window's thread.

`tests/test_files.cpp` has, in `static_assert` on a made-up disk: names, paths, sizes and dates with their limits, each pane starting in its own folder, and the left pane selecting, opening, closing, sorting and going elsewhere by each button with the right looked at after each. Also a key going only to the focused pane, marks at each depth, a mark scrolled away and a column aligned otherwise, every key, refusals changing only the status line, sorting and selection kept across opening, the panes held in a `OneShown` and brought up from a button, windows down to 1x1, and a paint. At run time: a real folder holding a folder, a file, links, a dangling link, a pipe and names that cannot be shown, what is refused, and a pane on it. The `fake_files_*` scenarios start the real program against the test compositor and compare every pixel with a headless window on the same folders: a click on the left's first mark off the wire leaves every pixel of the right pane as it was, and a missing folder costs its pane a line.

**Copying.** In `static_assert` on a made-up disk: copy on one side and paste on the other by buttons and keys, where each goes, a step a turn, what is refused, Escape, a failure, a drag each way across leaving the original, onto a folder, a file and past the last row, a shaky click, a drag out of any list, a whole drag in one motion event, the pointer going during one, a second copy refused by a drop and by a paste, and reading again keeping what is open and selected. At run time, on real files: a tree with a file over a chunk compared byte for byte, an empty file, a name that cannot be shown, links as links, a pipe left, permissions, times, a read-only folder, no set-user-ID, taken names of each kind with nothing changed, a folder into itself by path and through a link, a copy stopped part way, and a real failed write under `RLIMIT_FSIZE`, each leaving no part of a file. `fake_files_copy_and_drag` runs the real program off the wire onto a real disk: a file copied with the buttons, a drag refused for a taken name, and a folder dragged across with the original still there.

**Several at once, and comparing.** In `static_assert` on a made-up disk: Ctrl and Shift on the rows both ways, a plain click narrowing only when let go, Shift and an arrow, Ctrl+A, the other side unaffected, a sort keeping the rows with Shift going by the order shown, opening, closing and looking again keeping several, something inside a selected folder going once with it, several pasted and dragged, a drag stopped keeping them selected, and one that cannot go stopping the rest with how many were done. For comparing: each result, a time or size not known, what is only here against what nobody has looked at, the button on both sides with a folder open on one, marks gone on reading again, no folder on a side, and `/` as the top. `fake_files_several` runs the real program off the wire with Ctrl from a modifiers event, onto a real disk, and then presses Compare, but looks only at the disk and that it did not fail: the marks themselves are checked in `static_assert` and were not looked at on the wire. A very large selection or a compare of very many rows has not been measured.

**Matching a selection.** In `static_assert` on a made-up disk: how selections become pairs, the button to the popup saying they all match, the pointer and keys not reaching below an open popup, a list of what did not match with the others still going, one that cannot start being a finding, OK, Return and Escape closing it, a match and a copy refusing each other, and a stopped match not saying they match. On real files under `tests/test_files.cpp`: a whole tree matching, a changed byte of the same size, a file and a folder on one side only, sizes, kinds and links differing, what a link points to not being read, two pipes not being opened or called a match, a folder against itself and a hard link not being a match, and what cannot be read being listed with the rest still checked. `fake_files_several` presses Compare off the wire and sees the popup come and Return take it away, by the pixels of the lower half of the window: the words in the popup are checked only in `static_assert` and on the real tree. A picture of each popup was checked by eye. Limits: the drop from the cache is only advice, so a match may read what is in memory; a file that reports size 0, as in `/proc`, matches as empty; a path in the list can be cut off at its end; and large matches have been measured only once, by hand, in a throwaway `-O2` program stepping the job with no window, on this machine's ext4 solid-state disk: 500 MiB against a copy took 2.6 s with a longest step of 54 ms, and 2000 small files took 0.67 s. Never on a spindle, where both sides are read in turn and it will be far slower.

**The SHA-256 match.** `tests/test_sha256.cpp` checks the sum in `static_assert` against the FIPS 180-4 examples and against another implementation's answers on each side of the padding limits, split at every place, with a million letters at run time. `tests/test_files.cpp` interferes with real files between the writing and the reading back: a byte of the copy changed, a byte of the original changed, the copy cut short, each side grown. Each one stops the copy, counts no file, leaves the original and removes that copy. Something else put under the copy's name stops it too, and is left alone. A good copy is private until it has matched. **Only regular files are summed.** A link is recreated and not compared, a folder has no sum, and a pipe or a device is counted as not copied. A special file that reports a size of 0 while holding content, as in `/proc`, comes out empty and matches, because each side is read only as far as the size it had at the start. Reading back drops the copy from the cache by `posix_fadvise`, which is only advice: a system may keep it, so on some file systems the read may still come from memory. A network file system was not tried. A folder's own entry is not flushed, so a crash of the system could still lose a name that was flushed and matched, on a file system that does not write it with the file.

A picture of the window was drawn headless with the real font, on real folders, and looked at by eye once, in a throwaway program that is not part of this repository. That is how the columns came to fit a window of the default size.

**Measured once,** by hand, with the match on, in a throwaway `-O2` program stepping the job directly on this machine's real disks, so with no window and no frames: a 500 MiB file took 3.95 s on ext4 with the longest step 58 ms, and 10.1 s on btrfs with the longest step 230 ms. Flushing only at the end had given 1.2 s and 4.5 s for that step, so it now flushes every 16 MiB. 2000 small files took 1.5 s on ext4 with the longest step 22 ms, and 62 s on btrfs with the longest step 392 ms, because each file is flushed. All came out identical by `cmp` and `diff`. A step takes as long as the disk does.

Not shown: a real pointer or real keys against a real compositor, `+`, `-`, Backspace, Ctrl+C, Ctrl+V and Escape off the wire (the test keymap does not have them, and their values are checked against xkbcommon's at compile time), a very large folder, a tree with many rows open, a hanging network mount, a read failing part way through a folder, a full disk, or a real network file system. No fuzz harness covers names.

Left out on purpose: moving and deleting.

Missing: renaming, new folders, replacing a file on request, typing a path, opening a file, following a link, a remote side, a closed folder's insides when comparing what shows, hidden files as a choice, a key to move between panes, and anything drawn under the pointer during a drag, which is told on the line above the panes. A tree sorted by a column is a flat list.

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
  - Reading from the clipboard, which does not exist, and everything else not implemented.

## Known limits

- **`bsir.sh` has been run end to end only once that is recorded**: by the owner, on Fedora 44, reported on 2026-11-03. It is
  written for Fedora only and stops on a system with no `dnf`. Its install path has only been tried as far as that machine needed.
- A compositor that stays connected and never answers makes `connect` wait for ever. There is no timeout.

- One size, one line, and two fonts: the default regular and the default monospace, with no choice beyond that. No text editing, input methods or font selection. No bidirectional
  text ordering beyond what HarfBuzz does for one segment.
- A damaged window repaints in full, and the frame is not throttled to `wl_surface.frame`.
- **Each frame is copied once more than it must be.** A frame is drawn in ordinary memory and written
  to the buffer file with a system call, because nothing of ours is mapped. Secure first, efficient
  second. The cost has not been measured.
- Layout is a simple flow, not a layout manager. Overlapping widgets are possible but untested.
- A window's side is at most 8192 pixels, and frames wait if the compositor holds too much.
- The jText reader takes the full profile only, keeps no comments, and does not check values against their declared types: see `docs/Architecture.md`.
- `DataGrid` holds at most 1,000,000 rows, has no editing and no multiple selection, sorts by at most 9 columns, has no key for picking a current column, only a click, and keeps neither selection nor sort when given new rows, though it keeps columns' settings and the current column. Those are not saved across runs. A letter press draws a whole frame, with its usual shaping of what shows. Pressing in a scroll trough pages once, with no repeat while held.
- Scale is fixed at 1, so the window is upscaled on HiDPI outputs.
- The frame has no maximize or minimize control.
- A compositor without `cursor-shape-v1` gets no resize cursors, only the drawn arrow.
- No key repeat, real popups (they are drawn inside the window), drop-down menus, reading from the clipboard, drag and drop of data between programs, themes or accessibility.
- No X11, Windows or macOS, and no C++26 or modules.
- No CI.
