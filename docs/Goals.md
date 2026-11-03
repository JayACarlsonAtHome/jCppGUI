# Goals

## Purpose

jCppGUI is an experimental native C++ GUI toolkit, changing constantly, in which objects come from templates, each item is its own type,
and nothing is erased. The compiler can then catch mistakes such as a wrong widget name, a message
to a widget that cannot take it, or a handle used on the wrong pool.

## Priorities, in order

1. **Secure.** A check is removed only when a measurement shows that it matters.
2. **Efficient.**

## Reached so far

An empty window with a menu bar holding a Help button. It is a native Wayland window, and the same
behaviour is tested with no screen. It exists to prove the foundations that are expensive to change
later. Widget count does not matter yet.

After it came a read-only `DataGrid`, and a viewer that shows the toolkit's own jText records in it.

## Next

1. Replace libwayland with our own typed C++ Wayland client: a generator from the protocol XML, plus
   a fuzzed wire parser. That removes the one untyped-pointer exception.
2. A real drop-down Help menu using `xdg_popup`.
3. An editable text field.

GPU drawing is a known cost of step 1, because standard EGL and Vulkan Wayland support expects
libwayland's `wl_display`.
