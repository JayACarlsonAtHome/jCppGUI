# Security

## Status

This is an early prototype, still experimental and changing constantly. An internal security review was done on 2026-11-02, with AI tooling, and no independent review has
been done. What follows is the design intent and what the tests cover. It is not a claim that the code is secure.

## Reporting a vulnerability

Please report it privately, through **Report a vulnerability** on the repository's GitHub Security tab, and not in a public issue.
This is a small project, with no promised response time.

## Trust boundaries

| Input | Treatment |
|---|---|
| Window sizes from the compositor | Non-positive means the client chooses, and values over 8192 are clamped. All sizes pass `PixelBuffer::byte_size` (8192 per side, so 256 MiB at most) before any other arithmetic. |
| Pointer positions and key codes | Coordinates go through saturating geometry. Key codes are checked before conversion. |
| Keymap descriptor | xkb v1 format only, with a 1 MiB limit checked before anything is read. It must be a regular file on tmpfs, as `fstat` and `fstatfs` report, so that reading it cannot wait on a pipe or a FUSE mount. **This rule is the owner's decision of 2026-11-02, and it stays.** Its known consequence is that a compositor keeping its keymap off tmpfs is refused, even though its file may be harmless. The live GNOME session's is on tmpfs. It compiles under an xkb context with no include path and no names from the environment, so its text cannot make xkbcommon read a file. A keyboard with the protocol's no-keymap format is treated as having no keys, and any other wrong format is refused. The bytes are read into a private copy, so the compositor cannot change them after they are checked. A file shorter than the size claimed is refused, as is one with no NUL inside that size, and only the text before the first NUL reaches xkbcommon. The descriptor is always closed. |
| Buffer files | `memfd_create` with `MFD_CLOEXEC` and `MFD_NOEXEC_SEAL`, or `MFD_ALLOW_SEALING` on a kernel too old for that. Sized with `posix_fallocate` so that a shortage is a clean `Error`, then sealed against shrinking and growing, so nobody can truncate a file under the compositor. Closed by RAII. Frames are written with `write_at`, never while the compositor holds the file. A buffer is never resized, and it is destroyed in normal running only after `wl_buffer.release`. |
| Wayland objects | A null result from binding a global or creating a pointer or keyboard is a clean `Error`, and no listener is added to it. Losing the seat counts as the pointer leaving, so no capture is left stale. |
| Window title and app id | Valid UTF-8 with no NUL, and at most 4083 bytes, before they reach libwayland, which ends the connection on a message over 4096 bytes. |
| Memory for one window | All its frame buffers, plus the memory a frame is drawn in, must fit in 1 GiB, at most four buffers. A new frame first drops every buffer the compositor does not hold. If what it holds still leaves no room, the frame waits for releases, with no error. Only a frame that could never fit alone is an `Error`. A compositor that never releases stops its own window from redrawing, and nothing more. The cursor's small buffer is not counted. |
| Protocol errors | End `run()` with an `Error` that includes `wl_display_get_error`, and on `EPROTO` the protocol error's code and interface. |
| The connection ending | A hang-up ends `run()` with an `Error`, also with part of a message left over, which libwayland never reports by itself. A roundtrip goes through the same loop, so it ends too. A compositor that stays connected and never answers makes a roundtrip wait for ever: there is no timeout. |
| Events of later versions | libwayland passes on an event of a version never bound, and aborts on an empty listener entry. Every entry is set, so none is empty, and a new event in a later header is a compile error. |
| Font files | **Untrusted input.** Metrics, bitmap sizes, pitch and pixel mode are checked. A shaped run over 8192 glyphs is refused before anything adds up or renders, because a font's substitution rules can expand one character into many. HarfBuzz's two glyph counts must agree. Glyph bitmaps over 512 pixels per side are refused before they are copied, a pitch is limited before it is multiplied, and every position is clipped to the run. FreeType and HarfBuzz themselves still parse the font file, and a bug there is not something our checks can stop. Hinting is off: see below. |
| The monospace font | The same treatment as any font file, through the same code, and only once a column asks. Its letters `i` and `W` must have the same advance. A refusal is kept, with no second search, and is the column's trouble alone: it stays regular and the reason is readable. **Once loaded, a failure while shaping with it, such as FreeType failing to load a glyph, ends `run()`, as with the regular font.** So a hostile font in that family can end a program that way, as one could already. |
| Deadlines | A deadline comes from a widget and never from outside. `poll_timeout_ms` clamps a huge one to the longest `int`, gives 0 for one past, and takes the difference as unsigned, so no two values overflow, and `MonotonicTime::after` stops at the limit, each shown at both ends in `static_assert`. The wait is worked out again after an `EINTR`, so a flood of signals cannot put it off. Events going past do not hold a deadline back, because the loop asks on every turn. A flood of presses on one header button does put off that one column's change, and only that, for as long as it lasts, with the loop running. |
| Text | Strict UTF-8 and a 4096-byte limit, checked when the text is set and again before shaping. Text known at compile time is checked at compile time through `CheckedText`, and a widget takes nothing else: text from outside goes through `CheckedText::check`, and `Label::set_text` answers with a `std::expected` and keeps the old text on refusal. A refusal while shaping that is about one widget's text (`InvalidUtf8`, `TextTooLong`, `GlyphTooLarge`, such as a font expanding it past the 8192-glyph limit) does not end `run()`: that widget draws no text, its `text_error()` holds the reason, and it is not shaped again until the text changes. Anything else, such as FreeType failing to load a glyph, still ends `run()`, so a hostile font can still end a program that way. A `TextRun` whose coverage does not hold one byte per pixel aborts, in every build. |
| Program arguments | `program_arguments` refuses a negative count, more than 4096 arguments, a null argument and one over 128 KiB, and returns copies. |
| C strings | `string_from_c` stops at the terminator or at a limit, and refuses a string over the limit: a global's name, a protocol error's interface, fontconfig's path and each argument. A name compared with it comes from the generated glue, which is built into this program. |
| The redraw monitor's file | Output, and only with `--redraw-monitor` or a program that asks for `FrameMonitor`. The directory is the application's base directory, found from the kernel's link to the running executable and never from `argv[0]`, the environment or where the program was started, or a directory the program names. It is opened once with `O_DIRECTORY` and `O_CLOEXEC`, and the name must be a plain name in it. With no base directory, nothing is written and the reason is kept for the program to report. A new file is made beside the old with `O_CREAT`, `O_EXCL`, `O_NOFOLLOW`, `O_CLOEXEC` and mode 0600, and renamed over it. If a symlink, a directory, a pipe or anything else not a regular file stands at either name, it is refused and left alone, and writing stops, with the program going on and the reason kept for it to report. A rename replaces a link and never follows it, so swapping one in after the check points nothing anywhere. **What a local attacker can still do:** someone who can write to that directory can replace or remove the file between writes, so what is read there is only as good as that directory. So keep it out of a directory other users can write to. A leftover temporary file that is a regular file is removed, whoever made it. **What it holds is development information:** frame timings, window sizes, class and tag names. Timings say something about what the machine and the program are doing, so treat the file as private and do not ship with the monitor on. No window content is in it: a frame's hash is compared and not written. Values cannot break its structure: see `docs/Architecture.md`. |
| jText files | **Untrusted input.** Anyone may have written one, including the viewer's argument. The path itself must be a regular file: `fstatat` without following a link, then `open` with `O_NOFOLLOW`, `O_NONBLOCK` and `O_CLOEXEC`, then `fstat` on what was opened, so a symbolic link, a pipe, a directory or a device is refused, and a swap between the look and the open finds nothing to follow. Links in the folders above it are followed, and it can be any file the user can read. Its size is checked against a limit, 64 MiB by default, before anything is read, and it is read through `read_at` into memory we own. Every limit is in `docs/Architecture.md`, and the limit on values keeps a short file from asking for much memory. Each line must be valid UTF-8 with no control character but a tab, so no text from a file can break a line or a display. An include is refused, so a file cannot make this read another. No error message quotes the file, so what a label shows on refusal is our own text and, at most, the path given. What a file puts in a cell goes to the shaper as any text does, under its own limits: `docs/Architecture.md`. A file changed during a read ends in an `Error` or a structure check, and a file changed afterwards is not seen. |
| The settings record's file | Output, and only with `--record-settings` or a program that asks for `SettingsRecorder`. It goes through `FileSink`, with exactly the place, refusals, temporary file, mode 0600 and failure behaviour of the redraw monitor's file above, and the same local attacker can replace or remove it between writes. **So it is only as good as that directory, and nothing is built from it.** No part of the toolkit reads it. The viewer reads it as untrusted input like any other file. **What it holds is development information:** class and tag names, and where each item is on the screen. It holds no window content, no text of any widget, and no timing. Treat it as private. |
| Viewer's paths | Each argument must be text that can be shown, at most 4096 bytes, or the window says it is not, at that file's buttons only. It is used as given, relative to where the program started. With no argument for a file, its path comes from the application's base directory, as the monitor's does. A title takes at most 255 bytes of its file name, so a long argument cannot cost the window its title. |
| What the file manager moves to the trash | **Delete moves real things to the freedesktop.org trash, and nothing in the program removes anything for good.** It counts first, and a count that cannot finish moves nothing. Only a click on the popup's Delete button goes ahead: every key says no. The count walks through descriptors with `O_NOFOLLOW` and refuses a mounted disk inside, by `STATX_ATTR_MOUNT_ROOT` and by device, because moving a folder would take it along. The move is one `renameat2` with `RENAME_NOREPLACE`, so a thing is either where it was or wholly in the trash, and a link goes as a link. On a disk where that flag is not supported, the name is looked at first and a plain rename used. The info file is made first with `O_EXCL`, and the only `unlinkat` is of that file when the move did not happen. If no trash can be used it refuses, and the thing stays. **A trash must be a real folder owned by the user, not a link, and not writable by everyone, but one a group can write to is accepted,** because on a setgid disk shared with a group those people can already change everything there: they could also change or remove what is in that trash, or what an info file says it will restore to. **These do not stop everything.** Between the count and the click, someone who can write there could swap what is counted for something else, and that would go to the trash instead. The folders above the selected thing are opened by path with links followed. The home trash's own location comes from `XDG_DATA_HOME` or `HOME`, read with `secure_getenv`, and is reached by following links, because that is the person's own setting. Refusing a mount inside a folder has been tested only on `/proc` as the selected thing, because building one needs privileges. |
| What a diff reads and shows | Two files, chosen by the person, whose contents are untrusted. Each must be a regular file reached with `O_NOFOLLOW`, of at most 16 MiB and 200,000 lines, and is read whole on the window's thread. A NUL refuses it. The search stops at 4,000 changes and says so, which bounds its memory at about 32 MiB, though one round on a long repetitive file can still hold up the window. Each line shown has control characters, line separators and direction overrides replaced, so text cannot show as other than it is, and a line that is not UTF-8 is not shown. `fuzz_line_diff` fuzzes this. |
| What the file manager writes | **It changes real files, on the person's say.** A copy has no call that moves or replaces what was there. `O_EXCL`, `mkdirat` and `symlinkat` refuse a taken name in the kernel, so a swap between a look and an act cannot make it replace something. Every part of a copy is opened through a descriptor without following a link, so a link swapped in cannot send it elsewhere, and links are recreated rather than followed. **Each regular file is flushed, read back from both sides, and counted only if the two SHA-256 sums match.** The sum finds accidents, such as a bad disk or a file changed during the copy. It is no defence against someone who can write to both files, who could change both alike or change either after the check. Links and folders are not summed, and the line counts what was not copied, so a person should read it before deleting an original. A copy's only removal is `unlinkat` of the file that job made and did not finish or that did not match, by its name in its folder: **someone who can write to that folder could swap another file in under that name just before that, and it would be removed.** A swap found when reading back is left alone. New files are mode 0600 until they have matched, and set-user-ID, set-group-ID and sticky bits are dropped. A failure from `close` is not seen, though `fsync` has reported by then. A copy shows nothing on a file's contents and runs as the user who started it. **A drag has no confirmation,** and what it makes is for the person to delete if it was a mistake. Bytes go through `read_at` and `write_at`. |
| Folders the file manager shows | **Untrusted input:** anyone may have made a name. `read_directory` opens the path with `O_DIRECTORY`, `O_NOFOLLOW`, `O_NONBLOCK` and `O_CLOEXEC`, so a link, a file or a pipe at the end of a path is refused, and looks at each thing with `fstatat` through that descriptor, without following it. No file's contents are read, and nothing a link points to is opened. Links in the folders above the path are followed, and the two starting folders are resolved with every link followed, because the person named them. A name must be valid UTF-8 with no control character of either range, no slash, at most 255 bytes, and not `.` or `..`, or it is counted and left out and cannot be reached from a pane. **Bidirectional and look-alike characters are not refused,** so a name can look like another. A folder holds at most 1,000,000 things, a pane the same, and a tree at most 128 levels, each refused with a line. Reading happens on the UI thread with no timeout, so a mount that hangs holds up the window. |
| What the file manager matches | **Untrusted input, and only read.** `LocalMatchJob` opens each thing with `O_NOFOLLOW`, `O_NONBLOCK` and `O_CLOEXEC`, and looks at it first with `fstatat` without following it, so a link is compared by what it says and never opened, and a pipe or device is never opened and is listed. A name can be any bytes: it is compared as bytes, and one that cannot be shown appears in a finding as a set phrase. At most 128 levels and 1,000,000 names in a folder, each beyond that a finding, and 10,000 findings kept and the rest counted. What could not be looked at is a finding, and so is one and the same thing on both sides, because someone told those matched could lose the only copy by deleting one. **"They all match" says only that the sums, names and links were the same when read.** The drop from the cache is only advice, so a read may come from memory. A file that changes during its read is listed if its size or modified time moved. Reading happens on the UI thread with no timeout, so a mount that hangs holds up the window. |
| The clipboard | **It only gives, and only when a line is clicked.** Nothing is read from it: an offer from another program is destroyed unopened, so no outside clipboard data enters the program. Text goes through `inspect_clipboard_text` first, and a backend takes only `SafeClipboardText`, which nothing else can make, as a compile-fail test checks. Clean text goes at once. Text with control, direction or invisible characters, bytes that are not UTF-8, the shell characters `; \| & $ \` < >`, or more than 4096 bytes goes nowhere until a person has seen a popup naming what was found and clicked Continue, and every key says abort. On Continue the first three are replaced and shell characters are kept, because they may be part of a real name. **That is a warning, not protection:** a name like `x; rm -rf ~` pasted into a shell and run still runs, and quotes, parentheses, `*` and `~` are not flagged, because ordinary text uses them. The text is written to the descriptor the compositor gives, once, set not to block, with SIGPIPE held off for the write. Other programs may read what is on the clipboard. Text coming the other way has no path in at all, because there is nothing to paste into; it should go through the same sanitizer when there is. Writing to a pipe needs plain `write`, so `src/file_io.hpp` is the one exception to the rule against it, which the owner approved on 2026-11-04. |
| Cat, Edit, Touch and Rename | Each file name is one argument, with no shell reading it as a command: Cat's script takes names through `"$@"`, and an editor is told `--` first. A few editors may not understand `--`. **`cat` sends a file's raw bytes to the terminal, so a hostile file can send escape sequences,** which can change the title or rewrite what shows, and on some terminals do worse. Diff replaces those characters, and Cat does not, because the owner asked for real `cat`. **An editor may act on what a file says,** as Vim's modelines and Emacs's file-local variables can, within what that editor allows. `EDITOR` and `TERMINAL` are the person's own settings and are run as given, read with `secure_getenv`, and an `EDITOR` with a quote or backslash is refused rather than guessed at. A terminal is started by two forks, with other descriptors closed on exec, and goes on when this program ends. Touch and Rename work through the folder's descriptor, opened without following a link at its own name, and take a name a person typed, which `check_given_name` keeps to one name in that folder that can show. **A typed name can still hold a text-direction override,** which is shown as it is, as a name already on a disk would be. Touch makes a file with `O_CREAT \| O_EXCL \| O_NOFOLLOW`, so it cannot follow a link or open what is there, and only on `EEXIST` sets the time with `utimensat`, not following a link. Rename uses `renameat2` with `RENAME_NOREPLACE`, so the kernel refuses a name that is taken with no gap between a look and an act, and a disk without that flag is refused, which may include some network disks. |
| Images | Not implemented. It will be a trust boundary when it exists. |

**Font hinting is off, so that an untrusted font's bytecode does not run.** Every glyph loads with `FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT`, set
once in `src/text/text.cpp` and given to both FreeType and HarfBuzz's `hb-ft`. Both flags are needed. FreeType ignores
`FT_LOAD_NO_HINTING` alone for a "tricky" font, as `freetype.h` documents, and `tt_check_trickyness` takes a font as tricky by the
family name it claims, so a hostile font could otherwise ask for its bytecode to run. This was read in the source of FreeType
2.14.3, the installed version: `tt_glyph_load` keeps hinting off only with both flags, `tt_loader_init` runs `fpgm` and `prep` only for a
hinted load, and `FT_Load_Glyph` uses the autohinter only when neither flag is set. So the autohinter stays off too.
**The limit is plain:** the interpreter is unreachable only through our load path, and only as far as FreeType honours these
flags. No test runs a hostile font to show it, and FreeType still parses every table of the font.

Exceptions never cross a C callback. Every listener handler must be `noexcept`, which the trampoline's
template pattern enforces at compile time, and failures are recorded for the event loop to report
after dispatch returns.

## Untyped memory and mapping

- **The OS byte interface.** `src/file_io.hpp` is the only place in the toolkit where typed pointers convert to
  the OS's untyped byte interface, and nothing there casts back. `read_at` and `write_at` take typed spans of
  trivially copyable elements, compute byte counts and offsets with checks, and handle short transfers
  and interrupted calls. The rule scripts refuse `pread` and `pwrite` anywhere else, and all other byte I/O and raw
  byte copies everywhere. In tests only, `tests/fake_compositor/socket_io.hpp` does this for `sendmsg` and `recvmsg`,
  because a descriptor cannot be passed any other way. **The owner approved that helper on 2026-11-02, for test code only:**
  production code, including our later Wayland client, needs its own approval.
- **C pointer and count pairs, and C strings.** `detail::copy_from_c` and `detail::string_from_c` in `src/c_resource.hpp` are the
  only places they are read. `copy_from_c` refuses a null pointer with a count and a count over the caller's limit before
  allocating, and copies into a vector we own. So no array or string from a C library is worked on in place. A count is
  still the C side's word for it, and no check can prove it true, and so is a terminator: the limit bounds the damage.
  **Single fields of a C struct are read in place**, such as the metrics and the bitmap description in FreeType's face and
  glyph slot, and each value is checked before it is used. The rule scripts refuse `views::counted` and `strlen` and its
  relatives anywhere else.
- **The redraw monitor's file and the settings record** use `write_at` too, through `FileSink`, and add no new untyped call. `fstat` and `fstatat` take a typed `struct stat`.
- **Reading a jText file** uses `read_at` and those same calls, and adds no new untyped call.
- **Nothing is shared-mapped by our code**, and the same script refuses the mapping calls. The buffer
  file is shared with the compositor by descriptor only, and this process holds no mapping of it. That
  was checked once on the live demo, by reading its mappings while its buffers were open.
- **C libraries are outside that statement.** On the checked system, fontconfig maps its own cache files
  read-only and shared inside our process. Those files are system files, not compositor input.
- **Untyped pointers we name or cast back** are all in `src/wayland/trampoline.hpp`, and end with libwayland: listener
  data in the trampoline, and what `wl_registry_bind` returns in `bind_global`. Each is tied by type to the one type it can
  be, so a mismatch does not compile. See `docs/Review_Checklist.md`.

`EventQueue` is accepted by the owner as input buffering at the system boundary only, not as a message
queue. It should go away if our own Wayland client delivers events directly.

## Hardening flags

In every configuration: `-D_GLIBCXX_ASSERTIONS`, `-fstack-protector-strong`,
`-fstack-clash-protection`, `-fcf-protection`, `-fPIE -pie`, `-Wl,-z,relro,-z,now`, `-ftrivial-auto-var-init=zero`,
`-fstrict-flex-arrays=3`, `-fno-delete-null-pointer-checks`, `-Wl,-z,noexecstack`. Every build type but Debug adds
`-D_FORTIFY_SOURCE=3 -fno-strict-overflow -fno-strict-aliasing`. CMake's defaults for those types are replaced, so there is
no `-O3` and no `-DNDEBUG`: Release is `-O2`, RelWithDebInfo is `-O2 -g` and MinSizeRel is `-Os`. A configure with an unnamed or
unknown build type stops. Every preset names one. The `gcc-asan` preset adds ASan and UBSan with
`-fno-sanitize-recover=all`.

### Warnings

Every warning is an error, under the policy in `PROCESS.md`, and `Lin/v001/CMakeLists.txt` is the source
of truth for these lists. Both compilers use `-Werror -pedantic-errors` with:

`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast -Wcast-qual -Wcast-align
-Wformat=2 -Wnull-dereference -Wdouble-promotion -Wfloat-equal -Wfloat-conversion -Wundef -Wredundant-decls
-Wextra-semi -Wimplicit-fallthrough -Wswitch-enum -Wswitch-default -Wzero-as-null-pointer-constant
-Wnon-virtual-dtor -Woverloaded-virtual -Wunused -Wmissing-declarations`

g++ adds:

`-Wuseless-cast -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Warith-conversion -Wstrict-overflow=2
-Wctor-dtor-privacy -Wsign-promo -Wstrict-null-sentinel -Wnoexcept -Wcomma-subscript -Wvolatile -Walloc-zero
-Walloca -Warray-bounds=2 -Wattribute-alias=2 -Wcast-align=strict -Wcatch-value=3 -Wconditionally-supported
-Wdate-time -Wdeprecated-copy-dtor -Wdisabled-optimization -Wformat-overflow=2 -Wformat-signedness
-Wformat-truncation=2 -Wimplicit-fallthrough=5 -Winvalid-pch -Wmismatched-tags -Wmissing-include-dirs
-Wmissing-requires -Wmissing-template-keyword -Wpacked -Wplacement-new=2 -Wpointer-arith -Wredundant-tags
-Wshift-overflow=2 -Wstrict-aliasing=3 -Wstringop-overflow=4 -Wsuggest-override -Wtrampolines
-Wunused-const-variable=2 -Wunused-macros -Wvector-operation-performance -Wvirtual-inheritance -Wvla
-Wwrite-strings -Weffc++ -Wnrvo -Wstack-protector -Wsuggest-attribute=const -Wsuggest-attribute=noreturn
-Wsuggest-final-methods -Wsuggest-final-types -Wunsafe-loop-optimizations`

Three g++ warnings were run on this code and left out:

- `-Waggregate-return` reports every call that returns a class. Returning `std::expected` by value is how this
  code reports every failure, so no change short of abandoning that design would make it clean.
- `-Winline` reports when the optimizer declines to inline a function. Every header template is implicitly
  inline, so it follows optimizer heuristics and not any defect in the code, and only forcing inlining would quiet it.
- `-Wsuggest-attribute=pure` would have us add `[[gnu::pure]]` to about 30 functions. That is a promise the optimizer
  relies on with no check, so a wrong one miscompiles silently, and some are flagged only "if known to return
  normally", when this code can abort. Adding unchecked promises is less safe than leaving the warning off.

clang++ adds `-Weverything`, except:

| Exclusion | Why |
|---|---|
| `-Wno-c++98-compat-pedantic` | This code is C++23 only. Warnings about older standards contradict the language in use. This one group also covers `c++98-compat` and the `pre-c++14`, `17`, `20` and `23` compat families. |
| `-Wno-padded` | Reports normal alignment gaps. We do not lay fields out by hand. |
| `-Wno-covered-switch-default` | Contradicts `-Wswitch-default`. A switch over an enum names every value, checked by `-Wswitch-enum`, and also has a default. |

**Exempt:** the C glue generated by `wayland-scanner`, which is not our code, and system headers, which stay
`SYSTEM` includes. Nothing else.

`-Wunsafe-buffer-usage` is live, with no exclusion. The one conversion it would flag is inside
`copy_from_c`, through `std::views::counted`, so the compiler does not report that line. It is the one place
a reviewer must read by hand.

**The `gcc-warn` preset.** `-fno-strict-aliasing`, `-fno-strict-overflow` and `-fno-delete-null-pointer-checks`
stop g++'s `-Wstrict-aliasing`, `-Wstrict-overflow` and `-Wnull-dereference`, and the first and last of those
report only when optimizing. So `gcc-warn` builds everything at `-O2` without those three flags, with the same
warnings and `-Werror`, and is never shipped. Each warning was shown firing under its flags, and silent under
`gcc-release`'s. `-ftrivial-auto-var-init=zero` was tested too and does not stop `-Wuninitialized` or
`-Wmaybe-uninitialized`, so it stays.

Constant evaluation is used as a further check: `tests/test_constexpr.cpp` runs the core logic in
`static_assert`, where overflow, out-of-bounds access, use after destruction and leaks are compile
errors. It covers only the paths those tests evaluate.

## Fuzzing

The `clang-fuzz` preset builds libFuzzer harnesses in `tests/fuzz/`, with ASan and UBSan, around the production
code and with no copy of its logic:

| Harness | Runs | Seed |
|---|---|---|
| `utf8` | `valid_utf8` | none |
| `keymap` | `load_keymap` under the real compositor context, through a real descriptor | `tests/fuzz/seeds/keymap/minimal.xkb` |
| `font` | `TextEngine::from_font_bytes` and `shape`, through every font check, HarfBuzz, FreeType and our glyph copying | the default sans-serif, found by `fc-match` at run time and never committed |
| `jtext` | The jText parser, the helpers and typed rows, with an accepted file's every value checked, and whatever the monitor's writer takes written and read back as the same values. **Here the code being fuzzed is ours and instrumented,** so its coverage guides the search, and it has a dictionary of the grammar | two real monitor files, in `tests/fuzz/seeds/jtext/` |
| `backend` | The real `WaylandBackend`, reading through a real socket. The input is read as records of an object, an event and arguments, and optionally a keymap, so most of it passes libwayland's own checks and reaches every listener handler | none |

Run one with `bash tests/fuzz/run.sh <utf8|keymap|font|backend|jtext|line_diff> <seconds>`, after `cmake --preset clang-fuzz` and
`cmake --build --preset clang-fuzz`. Normal CTest does not run these. `ctest --preset clang-fuzz` runs only a tiny
smoke of each.

**The limit is plain, except for `jtext`.** System FreeType, HarfBuzz and xkbcommon are not instrumented. libFuzzer gets coverage only
from our code, so nothing guides it through their parsers: it mutates whole seed files blindly, and only a crash
or abort inside a library is noticed. ASan does not see a library's internal memory errors, except where they
cross an interceptor such as `memcpy`. Finding deep library bugs would need those libraries built from source
with the same flags, which has not been done.

Only short runs have been done so far: about 60 seconds each of `utf8`, `keymap` and `font` on 2026-11-02, with no crash, and
45 seconds and 320,000 inputs of `backend`, with none after the fix for the abort it found within its first 30 seconds:
an event of a later version reaching an empty listener entry. That is not evidence of safety, and the font run managed
only about 35,000 inputs. The `backend` harness passes no descriptor other than a keymap's, and it does not draw.

The `jtext` harness ran for 120 seconds on 2026-11-03 under `clang-fuzz`, and 2.26 million inputs found no crash, with coverage of 1704 points. That run followed one of the same length with the same result. Two parser mutations, control characters let through and values left untrimmed, each ended a short run in an abort, so the harness can notice. By hand, its 809 corpus files were also compared with jText's own parser and validator: no file this reader took was refused by that parser, and none this reader refused had passed both. 350 files it took failed that validator, for values or declared types the validator checks and this reader does not. An earlier comparison found one difference, a comment line that did not start in the first column, now refused. These are short runs and one comparison, and not evidence of safety.

## Accepted risks

- **A `Canvas` kept past its frame.** A widget is given a `Canvas` during `paint`, which is a non-owning view of pixels that
  the next `acquire_frame` can move. Storing one by value does not compile, so that use after free can no longer happen as it
  was reproduced. What remains:
  - A canvas from `clipped` can still initialize a member, and so be kept alive. Each draw call on it aborts once its
    frame is over, which `tests/test_canvas.cpp` shows, with no write. Its frame's generation is checked against a ledger on the
    heap, which lives as long as the application: a canvas kept in static storage past the end of the application reads
    a freed ledger, which is undefined.
  - A stored *pointer* to a canvas is undefined behaviour from the moment that canvas's scope ends, and C++ gives no way to
    forbid taking one. A destroyed canvas empties itself, so if nothing has overwritten its memory, a draw call aborts
    or draws nothing. If another canvas has taken the same stack slot, during a later frame's paint, the call draws in
    that frame, which is valid memory. If anything else has overwritten it, nothing can be said. ASan reports this.
  - During its own frame, a kept canvas works, and draws anywhere inside its clip.
- **Friends declared by name.** A program can define `detail::TestAccess`, `WireAccess` or `GeometryAccess` itself and get
  past the private parts they open. The rule scripts stop this in this repository only.

## Not yet done

- **Our own Wayland protocol code is not fuzzed**, because libwayland still parses the protocol. That comes with
  our own Wayland client.
- **libwayland parses the protocol, not us,** so what the test compositor shows is how our code behaves on top of it.
  The compositor, `tests/fake_compositor/`, runs the real backend over a real socket through its scripted scenarios, also
  under ASan: see `docs/Maturity.md`. It has its own wire code, which is new and has only been used by these tests.
- The keymap text is compiled by xkbcommon, which is also exposed to it. Our checks limit only size
  and format.
- The event queue limit (65536 events) ends the run with an `Error`, and a compositor can reach it. Nothing drains the
  queue while a `sync` waits, so events sent ahead of the answer all stay in it, and in `run`'s own loop a flood of 70,000
  motions reached it on a loaded machine. Motion in a row now merges into one slot, so motion alone no longer reaches
  it. Motion mixed with other events still can: `event_flood_mixed` ends in that `Error` every time.
