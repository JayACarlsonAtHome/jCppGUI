# jCppGUI hard review checklist

Go through this in every hard review. A pass from `tests/check_source_rules.sh` is not enough: text
searches cannot see an untyped pointer held through `auto*` or an implicit conversion, so read the
code at each boundary.

## Untyped pointers (the owner's rule, 2026-11-02)

C++ compiles straight to machine code, not through C. But the operating system and C libraries have
C interfaces, and some take `void*`. Everything below our code ends in untyped bytes, so "no `void*`
anywhere" is impossible. The rule we can enforce is:

**Our code never names `void*`, never stores an untyped pointer, and never casts back from one.**

Check each of these:

1. **OS byte interface:** typed pointers convert to untyped bytes in one small helper only, behind
   typed signatures such as `write_at(fd, std::span<const T>, offset)`. Nothing calls `pread` or
   `pwrite` anywhere else, and nothing there casts back.
2. **Temporary exception:** the only `void*` written in our code is libwayland's `void* data`, in
   `src/wayland/trampoline.hpp`. It casts back, which makes it real type erasure, so it must go when
   our own Wayland client replaces libwayland. Confirm it has not spread, and ask whether it can go
   yet.
3. **Hidden forms:** look for untyped results held through `auto*`, implicit conversions to `void*`,
   `const_cast`, and any new C library call that takes or returns `void*`. Each new case must be
   removed or justified to the owner. Do not allow one quietly.
4. **C pointer and count pairs:** these are read only through `detail::copy_from_c` in
   `src/c_resource.hpp`, into a vector we own, and nothing works through a view of C-owned memory. Look
   for spans or pointer walks over library memory, however they are spelled. A text search for
   `views::counted` is not enough.
5. **New helpers:** a new wrapper around an untyped interface needs the owner's approval. It must not
   become a second general-purpose route around the rule.
