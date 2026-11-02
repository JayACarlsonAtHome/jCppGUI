# Security

## Status

**This code has not had a security review.** It is a skeleton, and what follows is the design intent
and what the tests cover.

## Trust boundaries

| Input | Treatment |
|---|---|
| Window sizes from the compositor | Non-positive means the client chooses, and values over 16384 are clamped. All sizes pass `PixelBuffer::byte_size` (16384 per side, 1 GiB total) before any other arithmetic. |
| Pointer positions and key codes | Coordinates go through saturating geometry. Key codes are checked before conversion. |
| Keymap descriptor | xkb v1 format only, with a 1 MiB limit checked before mapping. The file's real size is checked against the size claimed, because a shorter file would fault when read. Mapped `MAP_PRIVATE`, and the descriptor is always closed. |
| Shared-memory buffers | `memfd_create` with `MFD_CLOEXEC`, unmapped and closed by RAII. A buffer is never resized, and it is destroyed in normal running only after `wl_buffer.release`. |
| Protocol errors | End `run()` with an `Error` that includes `wl_display_get_error`. |
| Font files | **Untrusted input.** Metrics, bitmap sizes, pitch and pixel mode are checked. Glyph bitmaps over 512 pixels per side are refused before they are copied, and every position is clipped to the run. FreeType and HarfBuzz themselves still parse the font file, and a bug there is not something our checks can stop. |
| Text | Strict UTF-8 validation and a 4096-byte limit before shaping. |
| Clipboard, files, images | Not implemented. They will be trust boundaries when they exist. |

Exceptions never cross a C callback. Every listener handler must be `noexcept`, which the trampoline's
template pattern enforces at compile time, and failures are recorded for the event loop to report
after dispatch returns.

## Hardening flags

In every configuration: `-D_GLIBCXX_ASSERTIONS`, `-fstack-protector-strong`,
`-fstack-clash-protection`, `-fcf-protection`, `-fPIE -pie`, `-Wl,-z,relro,-z,now`. Release adds
`-O2 -D_FORTIFY_SOURCE=3`. The `gcc-asan` preset adds ASan and UBSan with `-fno-sanitize-recover=all`.

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
