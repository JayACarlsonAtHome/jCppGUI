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

Then the first stages of a file manager, `jcppgui-files`: two panes side by side, each an independent tree, in the manner of TreeSync, with copy and paste between them and dragging to copy, each file checked by SHA-256, several things selected at once, the selection matched by SHA-256, the two sides compared, moving to the trash after a count and a question, a diff of two text files, Cat, Edit, Touch and Rename, and status lines that copy to the clipboard through a sanitizer.

## Next

**The owner's next major goal, from 2026-11-03, is a working file manager that can be brought up when wanted.** It rebuilds what TreeSync does, with none of TreeSync's code. It has no move, and nothing in it removes anything for good. It first only copied, by the owner's decision, and on 2026-11-04 the owner asked for a Delete button that goes to the trash, with a popup that asks and gives the count, and a Diff button, then Cat, Touch and Edit buttons, and status lines that can be copied, with a sanitizer that asks before anything unsafe reaches the clipboard. Later that day the owner asked for Touch to make a new file from a typed name, and for a Rename command, which stays in its folder, so there is still no move. Reading from the clipboard, through the same sanitizer, is not done, though there is now a box it could paste into. Copying with a match, multiple selection, matching a selection, comparing what shows, moving to the trash, diffing, Cat, Edit, Touch, Rename and copying status lines are done. The owner has copied 22 GiB with it on a real screen and has used matching a selection on one. Deleting and diffing have not been used on one yet. Parts of it are also to be used for searching for files and picking a file. A remote side is undecided.

The order below was set before that goal, and how the two fit together is not decided.

1. Replace libwayland with our own typed C++ Wayland client: a generator from the protocol XML, plus
   a fuzzed wire parser. That removes the one untyped-pointer exception.
2. A real drop-down Help menu using `xdg_popup`.
3. An editable text field.

A later idea from the owner, not started: a digital clock that hovers above the work area and can be moved around, wired to a test so that it shows the elapsed time from the test's start to its stop. Its time can be copied out to a log by hand, or a button logs the wired event numbers to the default sink. What that sink is has not been decided.

Another later idea from the owner, not started: a data grid of the jac313 store across the top half of a window, with a data grid of integers at the bottom left and one of floats at the bottom right, which follow the store record selected, by ID.

GPU drawing is a known cost of step 1, because standard EGL and Vulkan Wayland support expects
libwayland's `wl_display`.
