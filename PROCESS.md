# Process

## Active tree

`Lin/v001` is the only active tree. `Win/` and `Mac/` will sit beside `Lin/` when they start. Each
`<OS>/<version>` is a self-contained world, and there is no shared source directory.

## Build order

Both compilers must build and pass. Build one compiler at a time; `-j` within a build is fine.

1. `gcc-debug`
2. `clang-debug`
3. `gcc-asan`
4. `gcc-release`
5. `clang-release`

A warning or a sanitizer report is a failure. Fix the cause and do not suppress it.

## Clean-room rule

jCppGUI takes concepts from U++ and no code. Do not open, read, search or copy from any U++ source,
and do not reuse U++ class names. The concepts carried over are a plain tree in which parents
contain children, clear ownership, and few dependencies with fast build cycles.

## Rules that a test enforces

`tests/check_source_rules.sh` runs under CTest. It fails on banned type-erasure tokens, raw `new`
and `delete`, C-style casts, absolute paths, and display or font library headers above their layer.
The compile-fail tests prove the compiler rejects the mistakes it is meant to catch.

Remove anything superseded in the same change. Leave no commented-out code or stubs.
