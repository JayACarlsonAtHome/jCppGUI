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

## What has not been shown

- **Interactive behaviour in a real session has been checked only by hand, if at all.** Clicking,
  dragging, resizing and closing depend on the compositor, and only the toolkit's side of them has an
  automatic test. No test presses a real pointer.
- The pointer arrow, seat changes, keymap delivery and buffer release are exercised at most by the
  smoke test and by hand.

## Known limits

- One font, one size, one line. No text editing, input methods or font selection. No bidirectional
  text ordering beyond what HarfBuzz does for one segment.
- A damaged window repaints in full, and the frame is not throttled to `wl_surface.frame`.
- Layout is a simple flow, not a layout manager. Overlapping widgets are possible but untested.
- Scale is fixed at 1, so the window is upscaled on HiDPI outputs.
- The frame has no maximize or minimize control, and the resize edges show no resize cursor.
- No key repeat, popups, drop-down menus, clipboard, drag and drop, themes or accessibility.
- No X11, Windows or macOS, and no C++26 or modules.
- No CI.
