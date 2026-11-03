# jCppGUI hard review checklist

Go through this in every hard review. A pass from `tests/check_source_rules.sh` and `tests/check_ast_rules.sh` is not
enough. The first is a text search, and the second sees only what its matchers name, and only on a clang build, so read
the code at each boundary.

## Untyped pointers (the owner's rule, 2026-11-02)

C++ compiles straight to machine code, not through C. But the operating system and C libraries have
C interfaces, and some take `void*`. Everything below our code ends in untyped bytes, so "no `void*`
anywhere" is impossible. The rule we can enforce is:

**Our code never names `void*`, never stores an untyped pointer, and never casts back from one.**

Check each of these:

1. **OS byte interface:** typed pointers convert to untyped bytes in named helpers only, behind typed signatures
   such as `write_at(fd, std::span<const T>, offset)`. Nothing calls `pread` or `pwrite` outside
   `src/file_io.hpp`, and nothing there casts back. In tests only, `tests/fake_compositor/socket_io.hpp` does
   the same for `sendmsg` and `recvmsg`. **The owner approved that helper on 2026-11-02, for test code only.** Production
   code, including our later Wayland client, needs its own approval. Confirm nothing outside `tests/` includes it.
2. **Temporary exception:** `src/wayland/trampoline.hpp` is the one file that names `void*`, for libwayland's
   `void* data`, and it casts back from an untyped pointer in two places: the trampoline, and `bind_global` on
   what `wl_registry_bind` returns. Both are real type erasure, so they must go when our own Wayland client replaces
   libwayland. Each is tied by type to the one type it can be: a listener carries its `Owner`, and a bound global's
   interface comes from the type asked for. Confirm this has not spread, and ask whether it can go yet.
3. **Hidden forms:** look for untyped results held through `auto*`, implicit conversions to `void*`,
   `const_cast`, and any new C library call that takes or returns `void*`. Each new case must be
   removed or justified to the owner. Do not allow one quietly.
4. **C pointer and count pairs, and C strings:** these are read only through `detail::copy_from_c` and
   `detail::string_from_c` in `src/c_resource.hpp`, into memory we own. Look for spans or pointer walks over library
   memory, however they are spelled. A text search for `views::counted` is not enough. Single fields of a C struct,
   such as an `FT_GlyphSlot`'s metrics, are read in place and must be checked before use.
5. **New helpers:** a new wrapper around an untyped interface needs the owner's approval. It must not
   become a second general-purpose route around the rule.
