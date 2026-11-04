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

Then the first stages of a file manager, `jcppgui-files`: two panes side by side, each an independent tree, in the manner of TreeSync, with copy and paste between them and dragging to copy, each file checked by SHA-256, several things selected at once, and the two sides compared. It only copies: a person deletes an original by hand.

## Next

**The owner's next major goal, from 2026-11-03, is a working file manager that can be brought up when wanted.** It rebuilds what TreeSync does, with none of TreeSync's code. It only copies, and never moves or deletes, by the owner's decision. Copying with a match, multiple selection and comparing what shows on the two panes are done, though not yet used on a real screen. Still wanted: showing the difference between two files. Parts of it are also to be used for searching for files and picking a file. A remote side is undecided.

The order below was set before that goal, and how the two fit together is not decided.

1. Replace libwayland with our own typed C++ Wayland client: a generator from the protocol XML, plus
   a fuzzed wire parser. That removes the one untyped-pointer exception.
2. A real drop-down Help menu using `xdg_popup`.
3. An editable text field.

A later idea from the owner, not started: a digital clock that hovers above the work area and can be moved around, wired to a test so that it shows the elapsed time from the test's start to its stop. Its time can be copied out to a log by hand, or a button logs the wired event numbers to the default sink. What that sink is has not been decided.

Another later idea from the owner, not started: a data grid of the jac313 store across the top half of a window, with a data grid of integers at the bottom left and one of floats at the bottom right, which follow the store record selected, by ID.

GPU drawing is a known cost of step 1, because standard EGL and Vulkan Wayland support expects
libwayland's `wl_display`.
