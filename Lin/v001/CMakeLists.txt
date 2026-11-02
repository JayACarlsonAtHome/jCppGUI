# File: CMakeLists.txt
# Purpose: Builds the jCppGUI v001 libraries, demo and tests. Everything is found relative to this file.
cmake_minimum_required(VERSION 3.26)
project(jCppGUI_v001 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

option(JGUI_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(JGUI_FUZZ "Build the libFuzzer harnesses with ASan and UBSan, instead of the demo and tests" OFF)
option(JGUI_WARNINGS_ONLY "Optimize, without the hardening flags that stop warnings firing. Never ship this." OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# --- Hardening: applies to everything, in every configuration, generated code included ---
add_library(jgui_hardening INTERFACE)
target_compile_definitions(jgui_hardening INTERFACE _GLIBCXX_ASSERTIONS)
target_compile_options(jgui_hardening INTERFACE
  -fstack-protector-strong -fstack-clash-protection -fcf-protection -fPIE
  -ftrivial-auto-var-init=zero -fstrict-flex-arrays=3
  $<$<CONFIG:Release>:-O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3>)
# These three change what the language promises, which is hardening. They also stop g++'s warnings about the
# same promises: -Wnull-dereference, -Wstrict-aliasing and -Wstrict-overflow. The gcc-warn preset leaves them
# out so that those warnings can report. A build that is shipped always has them.
if(NOT JGUI_WARNINGS_ONLY)
  target_compile_options(jgui_hardening INTERFACE
    -fno-delete-null-pointer-checks
    $<$<CONFIG:Release>:-fno-strict-overflow -fno-strict-aliasing>)
endif()
target_link_options(jgui_hardening INTERFACE -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack)
if(JGUI_SANITIZE)
  target_compile_options(jgui_hardening INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(jgui_hardening INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all)
endif()

if(JGUI_FUZZ)
  # Our code gets coverage feedback and the sanitizers. System FreeType, HarfBuzz and xkbcommon get neither.
  target_compile_options(jgui_hardening INTERFACE
    -O1 -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(jgui_hardening INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all)
endif()

# --- Warnings: our code only. A warning is a failure. ---
set(JGUI_WARNING_FLAGS
  -Werror -pedantic-errors
  -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
  -Wcast-qual -Wcast-align -Wformat=2 -Wnull-dereference -Wdouble-promotion -Wfloat-equal -Wfloat-conversion
  -Wundef -Wredundant-decls -Wextra-semi -Wimplicit-fallthrough -Wswitch-enum -Wswitch-default
  -Wzero-as-null-pointer-constant -Wnon-virtual-dtor -Woverloaded-virtual -Wunused -Wmissing-declarations)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(APPEND JGUI_WARNING_FLAGS
    -Wuseless-cast -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Warith-conversion -Wstrict-overflow=2
    -Wctor-dtor-privacy -Wsign-promo -Wstrict-null-sentinel -Wnoexcept -Wcomma-subscript -Wvolatile
    -Walloc-zero -Walloca -Warray-bounds=2 -Wattribute-alias=2 -Wcast-align=strict -Wcatch-value=3
    -Wconditionally-supported -Wdate-time -Wdeprecated-copy-dtor -Wdisabled-optimization
    -Wformat-overflow=2 -Wformat-signedness -Wformat-truncation=2 -Wimplicit-fallthrough=5 -Winvalid-pch
    -Wmismatched-tags -Wmissing-include-dirs -Wmissing-requires -Wmissing-template-keyword -Wpacked
    -Wplacement-new=2 -Wpointer-arith -Wredundant-tags -Wshift-overflow=2 -Wstrict-aliasing=3
    -Wstringop-overflow=4 -Wsuggest-override -Wtrampolines -Wunused-const-variable=2 -Wunused-macros
    -Wvector-operation-performance -Wvirtual-inheritance -Wvla -Wwrite-strings
    -Weffc++ -Wnrvo -Wstack-protector -Wsuggest-attribute=const -Wsuggest-attribute=noreturn
    -Wsuggest-final-methods -Wsuggest-final-types -Wunsafe-loop-optimizations)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND JGUI_WARNING_FLAGS
    -Weverything
    # This code is C++23 only, so warnings about older standards contradict the language in use. This
    # one group also covers c++98-compat and the pre-c++14, 17, 20 and 23 compat families.
    -Wno-c++98-compat-pedantic
    # Warns about the normal, unavoidable alignment gaps in a struct. We do not lay fields out by hand.
    -Wno-padded
    # Contradicts -Wswitch-default, which the owner requires: a switch over an enum names every value,
    # checked by -Wswitch-enum, and also has a default, because a value from outside can be out of range.
    -Wno-covered-switch-default)
endif()
add_library(jgui_warnings INTERFACE)
target_compile_options(jgui_warnings INTERFACE ${JGUI_WARNING_FLAGS})

# --- xdg-shell, generated at build time into the build directory ---
set(XDG_SHELL_XML ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})
add_custom_command(
  OUTPUT ${GENERATED_DIR}/xdg-shell-client-protocol.h
  COMMAND ${WAYLAND_SCANNER} client-header ${XDG_SHELL_XML} ${GENERATED_DIR}/xdg-shell-client-protocol.h
  DEPENDS ${XDG_SHELL_XML} VERBATIM)
add_custom_command(
  OUTPUT ${GENERATED_DIR}/xdg-shell-protocol.c
  COMMAND ${WAYLAND_SCANNER} private-code ${XDG_SHELL_XML} ${GENERATED_DIR}/xdg-shell-protocol.c
  DEPENDS ${XDG_SHELL_XML} VERBATIM)

# The generated glue is compiled without our warning flags.
add_library(jgui_xdg_glue STATIC
  ${GENERATED_DIR}/xdg-shell-protocol.c
  ${GENERATED_DIR}/xdg-shell-client-protocol.h)
target_include_directories(jgui_xdg_glue SYSTEM PUBLIC ${GENERATED_DIR})
target_link_libraries(jgui_xdg_glue PUBLIC PkgConfig::WAYLAND_CLIENT PRIVATE jgui_hardening)

# --- Libraries ---
add_library(jgui_text STATIC src/text/text.cpp)
target_include_directories(jgui_text PUBLIC include)
target_link_libraries(jgui_text
  PUBLIC jgui_hardening
  PRIVATE jgui_warnings PkgConfig::FREETYPE PkgConfig::HARFBUZZ PkgConfig::FONTCONFIG)

add_library(jgui_wayland STATIC src/wayland/wayland_backend.cpp src/wayland/untrusted.cpp)
target_include_directories(jgui_wayland PUBLIC include)
target_link_libraries(jgui_wayland
  PUBLIC jgui_hardening PkgConfig::XKBCOMMON jgui_xdg_glue
  PRIVATE jgui_warnings)

add_library(jgui_program STATIC src/program_arguments.cpp)
target_include_directories(jgui_program PUBLIC include)
target_link_libraries(jgui_program PUBLIC jgui_hardening PRIVATE jgui_warnings)

find_program(JGUI_BASH bash REQUIRED)

# --- Fuzzing: this preset builds the harnesses and a tiny smoke run of each, and nothing else ---
if(JGUI_FUZZ)
  enable_testing()
  foreach(harness utf8 keymap font)
    add_executable(fuzz_${harness} tests/fuzz/fuzz_${harness}.cpp)
    target_include_directories(fuzz_${harness} PRIVATE src src/wayland)
    target_link_libraries(fuzz_${harness} PRIVATE jgui_wayland jgui_text jgui_warnings)
    target_link_options(fuzz_${harness} PRIVATE -fsanitize=fuzzer)
    add_test(NAME fuzz_smoke_${harness}
      COMMAND ${JGUI_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/run.sh ${harness} smoke)
    set_tests_properties(fuzz_smoke_${harness} PROPERTIES TIMEOUT 120)
  endforeach()
  return()
endif()

# --- Demo ---
add_executable(jcppgui-demo demo/demo.cpp)
target_link_libraries(jcppgui-demo PRIVATE jgui_wayland jgui_text jgui_program jgui_warnings)

# --- Tests ---
enable_testing()

function(jgui_test name)
  add_executable(${name} tests/${name}.cpp)
  target_include_directories(${name} PRIVATE tests demo)
  target_link_libraries(${name} PRIVATE jgui_text jgui_warnings)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

# Runs the real tree, pool and application logic in static_assert. The whole window is painted during
# compilation, which needs a higher step budget than Clang's default.
jgui_test(test_constexpr)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(test_constexpr PRIVATE -fconstexpr-steps=50000000)
endif()

jgui_test(test_geometry)
jgui_test(test_canvas)
jgui_test(test_pool)
jgui_test(test_event_queue)
jgui_test(test_widgets)
jgui_test(test_frame)
jgui_test(test_help)
jgui_test(test_text)
jgui_test(test_messages)
jgui_test(test_thread)
jgui_test(test_callback_access)

jgui_test(test_untrusted)
target_include_directories(test_untrusted PRIVATE src/wayland)
target_link_libraries(test_untrusted PRIVATE jgui_wayland jgui_program)

add_test(NAME demo_smoke COMMAND jcppgui-demo --smoke)
set_tests_properties(demo_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)

# --- Compile-fail tests: each case must fail with its diagnostic, and the same source must compile
#     when the one mistake is left out, so a failure for an unrelated reason is caught. ---
list(JOIN JGUI_WARNING_FLAGS " " JGUI_WARNING_STRING)
function(jgui_compile_fail name regex)
  set(script ${CMAKE_CURRENT_SOURCE_DIR}/tests/compile_fail/run_compile.cmake)
  set(common
    -DCXX=${CMAKE_CXX_COMPILER}
    "-DFLAGS=-std=c++23 ${JGUI_WARNING_STRING} -fsyntax-only"
    -DINCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/include
    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/compile_fail/${name}.cpp)
  add_test(NAME compile_ok_${name} COMMAND ${CMAKE_COMMAND} ${common} -DEXPECT=ok -P ${script})
  add_test(NAME compile_fail_${name} COMMAND ${CMAKE_COMMAND} ${common} -DEXPECT=fail "-DREGEX=${regex}" -P ${script})
  set_tests_properties(compile_ok_${name} compile_fail_${name} PROPERTIES TIMEOUT 120)
endfunction()

jgui_compile_fail(unknown_tag "no widget with this tag")
jgui_compile_fail(duplicate_tag "duplicate widget tag")
jgui_compile_fail(wrong_handle "no matching|no viable|cannot convert|could not convert")
jgui_compile_fail(handle_outside_pool "private|inaccessible|no matching|no viable")
jgui_compile_fail(not_a_widget "constraints? not satisfied|not satisfied")
jgui_compile_fail(receiver_not_in_tree "message receiver is not in this tree")
jgui_compile_fail(receiver_without_handler "message receiver has no handler")
jgui_compile_fail(wrong_value_type "message value has the wrong type")
jgui_compile_fail(missing_value "wrong number of values")
jgui_compile_fail(empty_receivers "constraints? not satisfied|not satisfied")
jgui_compile_fail(frame_metrics_missing_field "FrameMetrics")
jgui_compile_fail(serial_as_button "no matching|could not convert|cannot convert|no viable|cannot be used|cannot initialize")
jgui_compile_fail(keysym_as_modifier "no matching|could not convert|cannot convert|no viable|cannot be used|cannot initialize")
jgui_compile_fail(four_int_rect "no matching|no viable")
jgui_compile_fail(size_swapped "no matching|no viable|could not convert|cannot convert")
jgui_compile_fail(size_from_ints "no matching|no viable|could not convert|cannot convert")
jgui_compile_fail(left_as_top "no matching|no viable|could not convert|cannot convert")
jgui_compile_fail(left_plus_left "no match|invalid operands")
jgui_compile_fail(frame_copied "deleted")
jgui_compile_fail(frame_presented_without_move "deleted")
jgui_compile_fail(vector_pool_at_run_time "consteval|immediate|constant expression|not a constant")
jgui_compile_fail(callback_sets_size "private|inaccessible")
jgui_compile_fail(callback_dispatches "private|inaccessible")
jgui_compile_fail(callback_notifies "private|inaccessible")
jgui_compile_fail(callback_clears_damage "private|inaccessible")
jgui_compile_fail(callback_lays_out "private|inaccessible")
jgui_compile_fail(callback_paints "private|inaccessible")
jgui_compile_fail(callback_makes_key "private|inaccessible")
jgui_compile_fail(callback_copies_window "deleted")
jgui_compile_fail(warn_narrowing "conversion\\]")
jgui_compile_fail(warn_sign_conversion "sign-conversion\\]")
jgui_compile_fail(warn_shadow "shadow[-a-z]*\\]")

# --- Source rules: banned type-erasure tokens, casts, paths and layering ---
add_test(NAME source_rules
  COMMAND ${JGUI_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_source_rules.sh)
