# Security

## Status

A first internal security review was done on 2026-11-02, and an independent review follows. This is a
skeleton, and what follows is the design intent and what the tests cover.

## Trust boundaries

| Input | Treatment |
|---|---|
| Window sizes from the compositor | Non-positive means the client chooses, and values over 8192 are clamped. All sizes pass `PixelBuffer::byte_size` (8192 per side, so 256 MiB at most) before any other arithmetic. |
| Pointer positions and key codes | Coordinates go through saturating geometry. Key codes are checked before conversion. |
| Keymap descriptor | xkb v1 format only, with a 1 MiB limit checked before anything is read. It compiles under an xkb context with no include path and no names from the environment, so its text cannot make xkbcommon read a file. A keyboard with the protocol's no-keymap format is treated as having no keys, and any other wrong format is refused. The bytes are read into a private copy, so the compositor cannot change them after they are checked. A file shorter than the size claimed is refused, as is one with no NUL inside that size, and only the text before the first NUL reaches xkbcommon. The descriptor is always closed. |
| Buffer files | `memfd_create` with `MFD_CLOEXEC` and `MFD_NOEXEC_SEAL`, or `MFD_ALLOW_SEALING` on a kernel too old for that. Sized with `posix_fallocate` so that a shortage is a clean `Error`, then sealed against shrinking and growing, so nobody can truncate a file under the compositor. Closed by RAII. Frames are written with `write_at`, never while the compositor holds the file. A buffer is never resized, and it is destroyed in normal running only after `wl_buffer.release`. |
| Wayland objects | A null result from binding a global or creating a pointer or keyboard is a clean `Error`, and no listener is added to it. Losing the seat counts as the pointer leaving, so no capture is left stale. |
| Window title and app id | Valid UTF-8 with no NUL, and at most 4083 bytes, before they reach libwayland, which ends the connection on a message over 4096 bytes. |
| Memory for one window | All its frame buffers, plus the memory a frame is drawn in, must fit in 1 GiB, at most four buffers. A new frame first drops every buffer the compositor does not hold. If what it holds still leaves no room, the frame waits for releases, with no error. Only a frame that could never fit alone is an `Error`. A compositor that never releases stops its own window from redrawing, and nothing more. The cursor's small buffer is not counted. |
| Protocol errors | End `run()` with an `Error` that includes `wl_display_get_error`. |
| Font files | **Untrusted input.** Metrics, bitmap sizes, pitch and pixel mode are checked. A shaped run over 8192 glyphs is refused before anything adds up or renders, because a font's substitution rules can expand one character into many. HarfBuzz's two glyph counts must agree. Glyph bitmaps over 512 pixels per side are refused before they are copied, a pitch is limited before it is multiplied, and every position is clipped to the run. FreeType and HarfBuzz themselves still parse the font file, and a bug there is not something our checks can stop. |
| Text | Strict UTF-8 validation and a 4096-byte limit before shaping. A `TextRun` whose coverage does not hold one byte per pixel aborts, in every build. |
| Program arguments | `program_arguments` refuses a negative count, more than 4096 arguments and a null argument. |
| Clipboard, files, images | Not implemented. They will be trust boundaries when they exist. |

Exceptions never cross a C callback. Every listener handler must be `noexcept`, which the trampoline's
template pattern enforces at compile time, and failures are recorded for the event loop to report
after dispatch returns.

## Untyped memory and mapping

- **The OS byte interface.** `src/wayland/file_io.hpp` is the only place typed pointers convert to the
  OS's untyped byte interface, and nothing there casts back. `read_at` and `write_at` take typed spans of
  trivially copyable elements, compute byte counts and offsets with checks, and handle short transfers
  and interrupted calls. `tests/check_source_rules.sh` refuses `pread` and `pwrite` anywhere else.
- **C pointer and count pairs.** `detail::copy_from_c` in `src/c_resource.hpp` is the only place they are
  read. It refuses a null pointer with a count and a count over the caller's limit before allocating, and
  it copies into a vector we own, so nothing works through a view of C-owned memory. The count is still
  the C side's word for it, and no check can prove it true: the limit bounds the damage. The script
  refuses `views::counted` anywhere else.
- **Nothing is shared-mapped by our code**, and the same script refuses the mapping calls. The buffer
  file is shared with the compositor by descriptor only, and this process holds no mapping of it. That
  was checked once on the live demo, by reading its mappings while its buffers were open.
- **C libraries are outside that statement.** On the checked system, fontconfig maps its own cache files
  read-only and shared inside our process. Those files are system files, not compositor input.
- **The one `void*` we write** is libwayland's listener data in `src/wayland/trampoline.hpp`, which ends
  with libwayland. See `docs/Review_Checklist.md`.

`EventQueue` is accepted by the owner as input buffering at the system boundary only, not as a message
queue. It should go away if our own Wayland client delivers events directly.

## Hardening flags

In every configuration: `-D_GLIBCXX_ASSERTIONS`, `-fstack-protector-strong`,
`-fstack-clash-protection`, `-fcf-protection`, `-fPIE -pie`, `-Wl,-z,relro,-z,now`, `-ftrivial-auto-var-init=zero`,
`-fstrict-flex-arrays=3`, `-fno-delete-null-pointer-checks`, `-Wl,-z,noexecstack`. Release
adds `-O2 -D_FORTIFY_SOURCE=3 -fno-strict-overflow -fno-strict-aliasing`. The `gcc-asan` preset adds ASan and
UBSan with `-fno-sanitize-recover=all`.

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
-Wwrite-strings`

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

Run one with `bash tests/fuzz/run.sh <utf8|keymap|font> <seconds>`, after `cmake --preset clang-fuzz` and
`cmake --build --preset clang-fuzz`. Normal CTest does not run these. `ctest --preset clang-fuzz` runs only a tiny
smoke of each.

**The limit is plain.** System FreeType, HarfBuzz and xkbcommon are not instrumented. libFuzzer gets coverage only
from our code, so nothing guides it through their parsers: it mutates whole seed files blindly, and only a crash
or abort inside a library is noticed. ASan does not see a library's internal memory errors, except where they
cross an interceptor such as `memcpy`. Finding deep library bugs would need those libraries built from source
with the same flags, which has not been done.

Only short runs have been done so far: about 60 seconds each on 2026-11-02, with no crash. That is not evidence of
safety, and the font run managed only about 35,000 inputs.

## Accepted risks

- `PixelBuffer` and `Canvas` are non-owning views, and the next `acquire_frame` invalidates them. A `Frame` is
  checked every time its pixels are asked for, but a view asked for earlier can still be kept and used by
  mistake. `ApplicationCore` does not keep one, and no widget is given a way to reach a frame.

## Not yet done

- **Our own Wayland protocol code is not fuzzed**, because libwayland still parses the protocol. That comes with
  our own Wayland client.
- Protocol handling in `src/wayland/` is exercised only by `demo --smoke` against a real compositor.
  There is no hostile-compositor test, because no nested or headless compositor is available here.
- The keymap text is compiled by xkbcommon, which is also exposed to it. Our checks limit only size
  and format.
- The event queue limit (65536 events) ends the run with an `Error`. It has been tested only on the
  queue itself.
