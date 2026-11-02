# Security

## Status

**This code has not had a security review.** It is a skeleton, and what follows is the design intent
and what the tests cover.

## Trust boundaries

| Input | Treatment |
|---|---|
| Window sizes from the compositor | Non-positive means the client chooses, and values over 16384 are clamped. All sizes pass `PixelBuffer::byte_size` (16384 per side, 1 GiB total) before any other arithmetic. |
| Pointer positions and key codes | Coordinates go through saturating geometry. Key codes are checked before conversion. |
| Keymap descriptor | xkb v1 format only, with a 1 MiB limit checked before anything is read. The bytes are read into a private copy, so the compositor cannot change them after they are checked. A file shorter than the size claimed is refused, as is one with no NUL inside that size, and only the text before the first NUL reaches xkbcommon. The descriptor is always closed. |
| Buffer files | `memfd_create` with `MFD_CLOEXEC`, sized with `posix_fallocate` so that a shortage is a clean `Error`, and closed by RAII. Frames are written with `write_at`, never while the compositor holds the file. A buffer is never resized, and it is destroyed in normal running only after `wl_buffer.release`. |
| Protocol errors | End `run()` with an `Error` that includes `wl_display_get_error`. |
| Font files | **Untrusted input.** Metrics, bitmap sizes, pitch and pixel mode are checked. Glyph bitmaps over 512 pixels per side are refused before they are copied, and every position is clipped to the run. FreeType and HarfBuzz themselves still parse the font file, and a bug there is not something our checks can stop. |
| Text | Strict UTF-8 validation and a 4096-byte limit before shaping. |
| Clipboard, files, images | Not implemented. They will be trust boundaries when they exist. |

Exceptions never cross a C callback. Every listener handler must be `noexcept`, which the trampoline's
template pattern enforces at compile time, and failures are recorded for the event loop to report
after dispatch returns.

## Untyped memory and mapping

- **The OS byte interface.** `src/wayland/file_io.hpp` is the only place typed pointers convert to the
  OS's untyped byte interface, and nothing there casts back. `read_at` and `write_at` take typed spans of
  trivially copyable elements, compute byte counts and offsets with checks, and handle short transfers
  and interrupted calls. `tests/check_source_rules.sh` refuses `pread` and `pwrite` anywhere else.
- **Nothing is shared-mapped by our code**, and the same script refuses the mapping calls. The buffer
  file is shared with the compositor by descriptor only, and this process holds no mapping of it. That
  was checked once on the live demo by reading its mappings at exit.
- **C libraries are outside that statement.** On the checked system, fontconfig maps its own cache files
  read-only and shared inside our process. Those files are system files, not compositor input.
- **The one `void*` we write** is libwayland's listener data in `src/wayland/trampoline.hpp`, which ends
  with libwayland. See `docs/Review_Checklist.md`.

`EventQueue` is accepted by the owner as input buffering at the system boundary only, not as a message
queue. It should go away if our own Wayland client delivers events directly.

## Hardening flags

In every configuration: `-D_GLIBCXX_ASSERTIONS`, `-fstack-protector-strong`,
`-fstack-clash-protection`, `-fcf-protection`, `-fPIE -pie`, `-Wl,-z,relro,-z,now`. Release adds
`-O2 -D_FORTIFY_SOURCE=3`. The `gcc-asan` preset adds ASan and UBSan with `-fno-sanitize-recover=all`.

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

`-Wunsafe-buffer-usage` is live. A C library's pointer and count have to become a span somewhere, so
`detail::c_array` in `src/c_resource.hpp`, and `main`'s arguments in `demo/demo.cpp`, go through
`std::views::counted`. The compiler no longer flags these two places, but the count is still only the C
caller's word for it, and no check of ours proves it true.

Constant evaluation is used as a further check: `tests/test_constexpr.cpp` runs the core logic in
`static_assert`, where overflow, out-of-bounds access, use after destruction and leaks are compile
errors. It covers only the paths those tests evaluate.

## Not yet done

- **Nothing is fuzzed.** There is no parser of ours yet. Fuzzing comes with our own Wayland client.
- Protocol handling in `src/wayland/` is exercised only by `demo --smoke` against a real compositor.
  There is no hostile-compositor test, because no nested or headless compositor is available here.
- The keymap text is compiled by xkbcommon, which is also exposed to it. Our checks limit only size
  and format.
- The event queue limit (65536 events) ends the run with an `Error`. It has been tested only on the
  queue itself.
