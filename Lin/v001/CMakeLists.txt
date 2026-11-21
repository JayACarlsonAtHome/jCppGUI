# File: CMakeLists.txt
# Purpose: Builds the jCppGUI v001 libraries, demo and tests. Everything is found relative to this file.
cmake_minimum_required(VERSION 3.26)
project(jCppGUI_v001 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Every build uses this machine's pinned compilers, which Setup/pinned_compilers.cmake, the presets' toolchain file, reads.
# A build folder keeps the compiler it was first configured with, so one that no longer matches the pin is refused here.
if(NOT DEFINED JGUI_PINNED_CXX)
  message(FATAL_ERROR "Configure with a preset, such as cmake --preset gcc-debug, so that the build uses this machine's pinned "
    "compilers. Without one: -DCMAKE_TOOLCHAIN_FILE=Setup/pinned_compilers.cmake -DJGUI_COMPILER=gcc (or clang).")
endif()
file(REAL_PATH "${CMAKE_CXX_COMPILER}" JGUI_CXX_REAL)
file(REAL_PATH "${JGUI_PINNED_CXX}" JGUI_PINNED_CXX_REAL)
if(NOT JGUI_CXX_REAL STREQUAL JGUI_PINNED_CXX_REAL)
  message(FATAL_ERROR "This build folder builds with ${CMAKE_CXX_COMPILER}, and this machine (${JGUI_PIN_HOST}) is pinned to "
    "${JGUI_PINNED_CXX} in ${JGUI_PIN_FILE}. Remove ${CMAKE_BINARY_DIR} and configure again, without naming a compiler.")
endif()
# The oldest compilers that build this C++23 code, the same floors as Setup/recipes.conf. A pin written by another
# project could name an older one, which would fail far into the build with hundreds of errors.
set(JGUI_MIN_GNU 14)
set(JGUI_MIN_Clang 19)
if(DEFINED JGUI_MIN_${CMAKE_CXX_COMPILER_ID} AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS JGUI_MIN_${CMAKE_CXX_COMPILER_ID})
  message(FATAL_ERROR "This machine's pinned ${CMAKE_CXX_COMPILER} is ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}, and "
    "this code needs ${CMAKE_CXX_COMPILER_ID} ${JGUI_MIN_${CMAKE_CXX_COMPILER_ID}} or later. Pin a newer one in ${JGUI_PIN_FILE}.")
endif()
# Choosing no build type would quietly choose no optimization and none of the hardening below that needs it, so a
# build must name one. CMake's own defaults for the optimized types are not used: they add -DNDEBUG, and -O3 at
# Release. Nothing here depends on assert, but -O2 is the level these flags were tried at.
set(JGUI_BUILD_TYPES Debug Release RelWithDebInfo MinSizeRel)
if(NOT CMAKE_BUILD_TYPE IN_LIST JGUI_BUILD_TYPES)
  message(FATAL_ERROR "Name a build type, one of: ${JGUI_BUILD_TYPES}. Got: '${CMAKE_BUILD_TYPE}'. The presets do this.")
endif()
foreach(language C CXX)
  set(CMAKE_${language}_FLAGS_RELEASE "-O2")
  set(CMAKE_${language}_FLAGS_RELWITHDEBINFO "-O2 -g")
  set(CMAKE_${language}_FLAGS_MINSIZEREL "-Os")
endforeach()

option(JGUI_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(JGUI_FUZZ "Build the libFuzzer harnesses with ASan and UBSan, instead of the demo and tests" OFF)
option(JGUI_WARNINGS_ONLY "Optimize, without the hardening flags that stop warnings firing. Never ship this." OFF)
option(JCPPGUI_LIVE_TESTS "Add the tests that use the real compositor, which open real windows on the desktop. No preset sets this." OFF)

# --- Red Hat Enterprise Linux 9 alone: its libwayland and kernel headers are older than this code ---
# Its libwayland 1.21 has no wl_pointer axis_relative_direction, and cuts the connection on any event of a later
# wl_pointer. So RHEL 9 builds with libwayland 1.24.0, which Setup/rhel_wayland.sh builds into wayland_install beside the
# repository, as bsir.sh runs it; the build links it by its full path, so the programs load it from there. Its kernel
# headers (5.14) have no MFD_NOEXEC_SEAL, so it gets src/compat/rhel9. Every other OS uses its own libwayland, and
# src/compat/system.
cmake_host_system_information(RESULT JGUI_OS_ID QUERY DISTRIB_ID)
cmake_host_system_information(RESULT JGUI_OS_VERSION QUERY DISTRIB_VERSION_ID)
set(JGUI_RHEL9 OFF)
if(JGUI_OS_ID STREQUAL "rhel" AND JGUI_OS_VERSION MATCHES "^9(\\.|$)")
  set(JGUI_RHEL9 ON)
endif()
if(JGUI_RHEL9)
  set(JGUI_RHEL9_WAYLAND_VERSION "1.24.0")
  cmake_path(SET JGUI_RHEL9_WAYLAND NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/../../../wayland_install")
  if(NOT EXISTS "${JGUI_RHEL9_WAYLAND}/lib/pkgconfig/wayland-client.pc")
    message(FATAL_ERROR "Red Hat Enterprise Linux 9 builds with libwayland ${JGUI_RHEL9_WAYLAND_VERSION} from "
      "${JGUI_RHEL9_WAYLAND}, and it is not there. Run bash bsir.sh, which builds it.")
  endif()
  list(PREPEND CMAKE_PREFIX_PATH "${JGUI_RHEL9_WAYLAND}")
  include_directories(src/compat/rhel9)
else()
  include_directories(src/compat/system)
endif()

# --- jString: vendored from the jString repo (jString/VENDORED.jtext), the way jac313 keeps jText. Never edited here:
#     change jString, commit it, then bash Setup/vendor.sh sync jString/VENDORED.jtext --write. Header-only. ---
include_directories(jString/include)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
if(JGUI_RHEL9)
  if(NOT WAYLAND_CLIENT_VERSION STREQUAL JGUI_RHEL9_WAYLAND_VERSION OR NOT WAYLAND_CLIENT_LINK_LIBRARIES MATCHES "^${JGUI_RHEL9_WAYLAND}/")
    message(FATAL_ERROR "Red Hat Enterprise Linux 9 must build with libwayland ${JGUI_RHEL9_WAYLAND_VERSION} from ${JGUI_RHEL9_WAYLAND}, "
      "and found ${WAYLAND_CLIENT_VERSION} at ${WAYLAND_CLIENT_LINK_LIBRARIES}. Run bash bsir.sh, then configure again from nothing.")
  endif()
  if(NOT WAYLAND_SCANNER MATCHES "^${JGUI_RHEL9_WAYLAND}/")
    message(FATAL_ERROR "Red Hat Enterprise Linux 9 must use the wayland-scanner from ${JGUI_RHEL9_WAYLAND}, and found ${WAYLAND_SCANNER}.")
  endif()
endif()

# --- Hardening: applies to everything, in every configuration, generated code included ---
add_library(jgui_hardening INTERFACE)
target_compile_definitions(jgui_hardening INTERFACE _GLIBCXX_ASSERTIONS)
target_compile_options(jgui_hardening INTERFACE
  -fstack-protector-strong -fstack-clash-protection -fcf-protection -fPIE
  -ftrivial-auto-var-init=zero -fstrict-flex-arrays=3
  $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3>)
# These three change what the language promises, which is hardening. They also stop g++'s warnings about the
# same promises: -Wnull-dereference, -Wstrict-aliasing and -Wstrict-overflow. The gcc-warn preset leaves them
# out so that those warnings can report. A build that is shipped always has them.
if(NOT JGUI_WARNINGS_ONLY)
  target_compile_options(jgui_hardening INTERFACE
    -fno-delete-null-pointer-checks
    $<$<NOT:$<CONFIG:Debug>>:-fno-strict-overflow -fno-strict-aliasing>)
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
  # clang++ 23 brought the lifetime-safety analysis into -Weverything. Its checks for real dangling stay live; two of its
  # groups are left out. Older clang++ does not know these names and fails on an unknown one, so they are added only where known.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-Wlifetime-safety-invalidation JGUI_CXX_HAS_LIFETIME_SAFETY)
  if(JGUI_CXX_HAS_LIFETIME_SAFETY)
    list(APPEND JGUI_WARNING_FLAGS
      # False positive in clang++ 23.1: `out += 'a'; out += "b";` on a std::string& parameter reports `out` itself invalidated.
      -Wno-lifetime-safety-invalidation
      # Asks for [[clang::lifetimebound]], which g++ rejects as an error under -Wattributes, and a macro to hide it is banned.
      -Wno-lifetime-safety-suggestions)
  endif()
endif()
add_library(jgui_warnings INTERFACE)
target_compile_options(jgui_warnings INTERFACE ${JGUI_WARNING_FLAGS})

# --- xdg-shell, generated at build time into the build directory ---
set(XDG_SHELL_XML ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})
# cursor-shape is optional at run time, and it names the tablet protocol's tool in its tables, so that protocol's tables are
# generated too. Nothing here uses a tablet. This is why neither libwayland-cursor nor any cursor file is needed.
set(CURSOR_SHAPE_XML ${WAYLAND_PROTOCOLS_DIR}/staging/cursor-shape/cursor-shape-v1.xml)
set(TABLET_XML ${WAYLAND_PROTOCOLS_DIR}/stable/tablet/tablet-v2.xml)
add_custom_command(
  OUTPUT ${GENERATED_DIR}/cursor-shape-v1-client-protocol.h
  COMMAND ${WAYLAND_SCANNER} client-header ${CURSOR_SHAPE_XML} ${GENERATED_DIR}/cursor-shape-v1-client-protocol.h
  DEPENDS ${CURSOR_SHAPE_XML} VERBATIM)
add_custom_command(
  OUTPUT ${GENERATED_DIR}/cursor-shape-v1-protocol.c
  COMMAND ${WAYLAND_SCANNER} private-code ${CURSOR_SHAPE_XML} ${GENERATED_DIR}/cursor-shape-v1-protocol.c
  DEPENDS ${CURSOR_SHAPE_XML} VERBATIM)
add_custom_command(
  OUTPUT ${GENERATED_DIR}/tablet-v2-protocol.c
  COMMAND ${WAYLAND_SCANNER} private-code ${TABLET_XML} ${GENERATED_DIR}/tablet-v2-protocol.c
  DEPENDS ${TABLET_XML} VERBATIM)
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
  ${GENERATED_DIR}/xdg-shell-client-protocol.h
  ${GENERATED_DIR}/cursor-shape-v1-protocol.c
  ${GENERATED_DIR}/cursor-shape-v1-client-protocol.h
  ${GENERATED_DIR}/tablet-v2-protocol.c)
target_include_directories(jgui_xdg_glue SYSTEM PUBLIC ${GENERATED_DIR})
target_link_libraries(jgui_xdg_glue PUBLIC PkgConfig::WAYLAND_CLIENT PRIVATE jgui_hardening)

# --- Libraries ---
add_library(jgui_text STATIC src/text/text.cpp)
target_include_directories(jgui_text PUBLIC include)
target_link_libraries(jgui_text
  PUBLIC jgui_hardening
  PRIVATE jgui_warnings PkgConfig::FREETYPE PkgConfig::HARFBUZZ PkgConfig::FONTCONFIG)

add_library(jgui_wayland STATIC src/wayland/wayland_backend.cpp src/wayland/untrusted.cpp)
target_include_directories(jgui_wayland PUBLIC include PRIVATE src)
target_link_libraries(jgui_wayland
  PUBLIC jgui_hardening PkgConfig::XKBCOMMON jgui_xdg_glue
  PRIVATE jgui_warnings)

add_library(jgui_program STATIC src/program_arguments.cpp src/base_directory.cpp)
target_include_directories(jgui_program PUBLIC include)
target_link_libraries(jgui_program PUBLIC jgui_hardening PRIVATE jgui_warnings)

# The redraw monitor's file: only a program that uses FileSink links it.
add_library(jgui_monitor STATIC src/monitor/file_sink.cpp)
target_include_directories(jgui_monitor PUBLIC include PRIVATE src)
target_link_libraries(jgui_monitor PUBLIC jgui_hardening PRIVATE jgui_warnings jgui_program)

# Reading a jText file from a path: only a program that does so links it. Parsing is all in the header.
add_library(jgui_jtext STATIC src/jtext/read_file.cpp)
target_include_directories(jgui_jtext PUBLIC include PRIVATE src)
target_link_libraries(jgui_jtext PUBLIC jgui_hardening PRIVATE jgui_warnings)

# Reading and copying on this machine: only a program that does so links it.
add_library(jgui_files STATIC src/files/read_directory.cpp src/files/copy_job.cpp src/files/match_job.cpp src/files/count_job.cpp src/files/trash.cpp src/files/read_text.cpp src/files/terminal.cpp src/files/touch.cpp src/files/rename.cpp src/files/save_picture.cpp src/files/default_program.cpp)
target_include_directories(jgui_files PUBLIC include PRIVATE src)
target_link_libraries(jgui_files PUBLIC jgui_hardening PRIVATE jgui_warnings)

find_program(JGUI_BASH bash REQUIRED)

# --- Fuzzing: this preset builds the harnesses and a tiny smoke run of each, and nothing else ---
if(JGUI_FUZZ)
  enable_testing()
  foreach(harness utf8 keymap font backend jtext line_diff)
    add_executable(fuzz_${harness} tests/fuzz/fuzz_${harness}.cpp)
    target_include_directories(fuzz_${harness} PRIVATE src src/wayland tests)
    target_link_libraries(fuzz_${harness} PRIVATE jgui_wayland jgui_text jgui_warnings)
    target_link_options(fuzz_${harness} PRIVATE -fsanitize=fuzzer)
    add_test(NAME fuzz_smoke_${harness}
      COMMAND ${JGUI_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/run.sh ${harness} smoke)
    set_tests_properties(fuzz_smoke_${harness} PROPERTIES TIMEOUT 120)
  endforeach()
  target_include_directories(fuzz_jtext PRIVATE demo)
  return()
endif()

# --- Demo ---
add_executable(jcppgui-demo demo/demo.cpp)
target_link_libraries(jcppgui-demo PRIVATE jgui_wayland jgui_text jgui_program jgui_monitor jgui_warnings)

# --- The viewer: shows a redraw monitor's file ---
add_executable(jcppgui-viewer demo/viewer.cpp)
target_link_libraries(jcppgui-viewer PRIVATE jgui_wayland jgui_text jgui_program jgui_jtext jgui_warnings)

# --- The file manager: two folders side by side, each going its own way ---
add_executable(jcppgui-files demo/files.cpp)
target_link_libraries(jcppgui-files PRIVATE jgui_wayland jgui_text jgui_program jgui_files jgui_warnings)

# --- Tests ---
enable_testing()

# jString's own run-time tests, built here under jCppGUI's warnings and hardening, so the copy is tested as it is used.
foreach(name test_utf8 test_string test_constexpr test_error_in_expected)
  add_executable(jstring_${name} jString/tests/${name}.cpp)
  target_include_directories(jstring_${name} PRIVATE jString/tests)
  target_link_libraries(jstring_${name} PRIVATE jgui_hardening jgui_warnings)
  add_test(NAME jstring_${name} COMMAND jstring_${name})
  set_tests_properties(jstring_${name} PROPERTIES TIMEOUT 60)
endforeach()
add_test(NAME jstring_vendored_copy COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/Setup/vendor.sh check)

function(jgui_test_program name)
  add_executable(${name} tests/${name}.cpp)
  target_include_directories(${name} PRIVATE tests demo)
  target_link_libraries(${name} PRIVATE jgui_text jgui_warnings)
endfunction()

function(jgui_test name)
  jgui_test_program(${name})
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
# Fills a whole queue in static_assert, which needs a higher step budget on both compilers.
jgui_test(test_event_queue)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(test_event_queue PRIVATE -fconstexpr-steps=50000000)
else()
  target_compile_options(test_event_queue PRIVATE -fconstexpr-ops-limit=400000000)
endif()
jgui_test(test_widgets)
jgui_test(test_frame)
# A window too narrow for its row of buttons, made to fit with the real font.
jgui_test(test_fit)
jgui_test(test_help)
jgui_test(test_text)
jgui_test(test_bad_text)
jgui_test(test_messages)
jgui_test(test_thread)
jgui_test(test_callback_access)
jgui_test(test_grid_helpers)
jgui_test(test_timer)
# The jText reader. Whole files are parsed, cut and written back in static_assert, which needs a higher step budget on both compilers.
jgui_test(test_jtext)
target_include_directories(test_jtext PRIVATE src)
target_link_libraries(test_jtext PRIVATE jgui_jtext)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(test_jtext PRIVATE -fconstexpr-steps=200000000)
else()
  target_compile_options(test_jtext PRIVATE -fconstexpr-ops-limit=400000000)
endif()
jgui_test(test_panels)
# The whole grid runs in static_assert, many frames of it, which needs a higher step budget on both compilers.
jgui_test(test_datagrid)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(test_datagrid PRIVATE -fconstexpr-steps=200000000)
else()
  target_compile_options(test_datagrid PRIVATE -fconstexpr-ops-limit=400000000)
endif()
# The viewer's whole window runs in static_assert, many frames of it, which needs a higher step budget on both compilers.
jgui_test(test_viewer)
target_include_directories(test_viewer PRIVATE demo)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(test_viewer PRIVATE -fconstexpr-steps=400000000)
else()
  target_compile_options(test_viewer PRIVATE -fconstexpr-ops-limit=800000000)
endif()
# Sums run in static_assert, which needs a higher step budget on Clang.
jgui_test(test_sha256)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(test_sha256 PRIVATE -fconstexpr-steps=50000000)
endif()
# Two panes and what reads a folder. Whole windows run in static_assert, which needs a higher step budget on both compilers.
jgui_test(test_files)
target_include_directories(test_files PRIVATE src)
target_link_libraries(test_files PRIVATE jgui_files jgui_program)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(test_files PRIVATE -fconstexpr-steps=200000000)
else()
  target_compile_options(test_files PRIVATE -fconstexpr-ops-limit=400000000)
endif()
# What a frame of the grid costs, printed by hand, in a release build: docs/Maturity.md has what it said. Not a test.
jgui_test_program(measure_datagrid)
# What reading the largest monitor file costs, and a frame of it, the same way.
jgui_test_program(measure_viewer)
target_include_directories(measure_viewer PRIVATE src)
target_link_libraries(measure_viewer PRIVATE jgui_jtext)

jgui_test(test_untrusted)
target_include_directories(test_untrusted PRIVATE src src/wayland)
target_link_libraries(test_untrusted PRIVATE jgui_wayland jgui_program)

# The real WaylandBackend against a scripted compositor, with no screen. Each scenario is one test.
jgui_test_program(test_fake_compositor)
target_include_directories(test_fake_compositor PRIVATE src src/wayland)
add_dependencies(test_fake_compositor jcppgui-viewer jcppgui-files)
target_link_libraries(test_fake_compositor PRIVATE jgui_wayland jgui_program jgui_monitor jgui_jtext jgui_files)
set(JGUI_FAKE_SCENARIOS
  happy_path keyboard_path
  configure_1x1 configure_0x0 configure_negative configure_int32_max configure_8192x1 configure_1x8192 configure_119x119 configure_flood
  release_before_attach release_three_times never_release release_late pool_attack
  capabilities_flap seat_removed_while_held no_seat no_compositor no_shm no_wm_base old_compositor old_seat
  keymap_pipe keymap_directory keymap_short keymap_1mib_no_terminator keymap_size_0xffffffff keymap_format_7 keymap_none
  keys_extreme pointer_extremes events_of_later_versions pointer_warp event_flood event_flood_in_sync event_flood_mixed
  disconnect_at_once disconnect_after_frame
  bad_length_too_small bad_length_too_large bad_length_not_aligned unknown_opcode unknown_object
  truncated_then_gone truncated_during_connect truncated_during_first_frame
  arguments_too_short string_not_terminated keymap_with_no_descriptor
  protocol_error_on_surface protocol_error_on_display protocol_error_on_nothing signal_storm
  monitor_real_frames monitor_never_release monitor_versus_none monitor_cannot_write
  demo_as_built demo_copy_under_a_base demo_copy_with_no_base
  cursor_all_edges cursor_no_protocol cursor_seat_removed cursor_pointer_flap cursor_manager_removed cursor_manager_late cursor_during_drag
  grid_axis_hostile grid_axis_no_enter grid_wheel_then_pixels grid_resize_during_thumb_drag grid_pointer_gone_during_capture grid_seat_gone_during_capture grid_cursor timer_wakes_and_sleeps grid_column_button_waits grid_buttons_under_floods grid_ctrl_keys_sort
  viewer_first_frame viewer_section_switch viewer_wheel_scroll viewer_resize viewer_missing_file viewer_malformed_file viewer_refused_symlink viewer_path_not_text viewer_copy_with_no_base viewer_file_in_its_base viewer_65536_rows
  settings_demo_records settings_demo_both_flags settings_demo_no_base
  settings_viewer_items settings_viewer_bad_row settings_viewer_missing_file settings_viewer_only_settings settings_viewer_malformed_file settings_viewer_files_in_its_base
  files_open_on_the_left files_missing_folder files_copy_and_drag files_several files_delete files_touch_rename files_screenshot files_open files_record files_diff_with_meld files_copy_line)
foreach(scenario ${JGUI_FAKE_SCENARIOS})
  add_test(NAME fake_${scenario} COMMAND test_fake_compositor ${scenario})
  set_tests_properties(fake_${scenario} PROPERTIES TIMEOUT 300
    ENVIRONMENT "JGUI_TEST_KEYMAP=${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/seeds/keymap/minimal.xkb")
endforeach()
# These three start the real demo program itself, so the base directory it finds is a real one.
set_property(TEST fake_demo_as_built fake_demo_copy_under_a_base fake_demo_copy_with_no_base fake_settings_demo_records fake_settings_demo_both_flags fake_settings_demo_no_base APPEND PROPERTY
  ENVIRONMENT "JGUI_TEST_DEMO=$<TARGET_FILE:jcppgui-demo>" "JGUI_TEST_BASE=${CMAKE_CURRENT_SOURCE_DIR}")
# These start the real viewer program itself, against the same compositor.
foreach(scenario viewer_first_frame viewer_section_switch viewer_wheel_scroll viewer_resize viewer_missing_file viewer_malformed_file viewer_refused_symlink viewer_path_not_text viewer_copy_with_no_base viewer_file_in_its_base viewer_65536_rows settings_viewer_items settings_viewer_bad_row settings_viewer_missing_file settings_viewer_only_settings settings_viewer_malformed_file settings_viewer_files_in_its_base)
  set_property(TEST fake_${scenario} APPEND PROPERTY ENVIRONMENT "JGUI_TEST_VIEWER=$<TARGET_FILE:jcppgui-viewer>")
endforeach()
# wl_pointer.warp is newer than some libwayland, such as Linux Mint 22's 1.22 and Red Hat Enterprise Linux 10's 1.25.
# Without it in the header, libwayland ends the connection on that event before the toolkit sees it, so the scenario
# cannot run and is shown as disabled.
find_file(JGUI_WAYLAND_CLIENT_PROTOCOL wayland-client-protocol.h HINTS ${WAYLAND_CLIENT_INCLUDE_DIRS} ${WAYLAND_CLIENT_INCLUDEDIR} REQUIRED)
file(STRINGS "${JGUI_WAYLAND_CLIENT_PROTOCOL}" JGUI_POINTER_WARP REGEX "WL_POINTER_WARP_SINCE_VERSION")
if(NOT JGUI_POINTER_WARP)
  set_tests_properties(fake_pointer_warp PROPERTIES DISABLED TRUE)
endif()
# These start the real file manager itself, against the same compositor.
set_property(TEST fake_files_open_on_the_left fake_files_missing_folder fake_files_copy_and_drag fake_files_several fake_files_delete fake_files_touch_rename fake_files_screenshot fake_files_open fake_files_record fake_files_diff_with_meld fake_files_copy_line APPEND PROPERTY ENVIRONMENT "JGUI_TEST_FILES=$<TARGET_FILE:jcppgui-files>")
# One C call failing at a time, through the real implementation. It goes against the compositor above.
jgui_test(test_injection)
target_include_directories(test_injection PRIVATE src src/wayland)
target_link_libraries(test_injection PRIVATE jgui_wayland)
set_tests_properties(test_injection PROPERTIES TIMEOUT 300
  ENVIRONMENT "JGUI_TEST_KEYMAP=${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/seeds/keymap/minimal.xkb")

# The redraw monitor. It runs the application and the demo's tree in static_assert, as test_constexpr does.
jgui_test(test_redraw_monitor)
target_include_directories(test_redraw_monitor PRIVATE src)
target_link_libraries(test_redraw_monitor PRIVATE jgui_monitor)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(test_redraw_monitor PRIVATE -fconstexpr-steps=50000000)
endif()

# The settings recorder. It runs the application, the demo's tree and the viewer's window in static_assert.
jgui_test(test_settings_recorder)
target_include_directories(test_settings_recorder PRIVATE src)
target_link_libraries(test_settings_recorder PRIVATE jgui_monitor jgui_jtext jgui_program)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(test_settings_recorder PRIVATE -fconstexpr-steps=400000000)
else()
  target_compile_options(test_settings_recorder PRIVATE -fconstexpr-ops-limit=800000000)
endif()

# Where the base directory is: the rule in static_assert, and at run time this program started from copies and links of itself.
jgui_test(test_base_directory)
target_link_libraries(test_base_directory PRIVATE jgui_monitor jgui_program)
set_property(TEST test_base_directory APPEND PROPERTY
  ENVIRONMENT "JGUI_TEST_SELF=$<TARGET_FILE:test_base_directory>" "JGUI_TEST_BASE=${CMAKE_CURRENT_SOURCE_DIR}")

jgui_test(test_fake_compositor_rules)
target_include_directories(test_fake_compositor_rules PRIVATE src src/wayland)
target_link_libraries(test_fake_compositor_rules PRIVATE jgui_wayland)
list(LENGTH JGUI_FAKE_SCENARIOS JGUI_FAKE_SCENARIO_COUNT)
add_test(NAME fake_compositor_lists_match COMMAND test_fake_compositor --expect-count ${JGUI_FAKE_SCENARIO_COUNT})

# Every other test that connects reaches only a private socket. These two reach the real compositor, so they are opt-in.
if(JCPPGUI_LIVE_TESTS)
  add_test(NAME demo_smoke COMMAND jcppgui-demo --smoke)
  set_tests_properties(demo_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
  add_test(NAME demo_smoke_redraw_monitor COMMAND jcppgui-demo --smoke --redraw-monitor)
  set_tests_properties(demo_smoke_redraw_monitor PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
  # Both write the one file in the base directory, and each would remove the other's temporary file.
  set_tests_properties(demo_smoke_redraw_monitor fake_demo_as_built PROPERTIES RESOURCE_LOCK jgui_base_directory_file)
endif()

# --- Compile-fail tests: each case must fail with its diagnostic, and the same source must compile
#     when the one mistake is left out, so a failure for an unrelated reason is caught. ---
list(JOIN JGUI_WARNING_FLAGS " " JGUI_WARNING_STRING)
function(jgui_compile_fail name regex)
  set(script ${CMAKE_CURRENT_SOURCE_DIR}/tests/compile_fail/run_compile.cmake)
  set(common
    -DCXX=${CMAKE_CXX_COMPILER}
    "-DFLAGS=-std=c++23 ${JGUI_WARNING_STRING} -fsyntax-only"
    -DINCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/include
    -DSRC_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/src
    -DJSTRING_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/jString/include
    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/compile_fail/${name}.cpp)
  add_test(NAME compile_ok_${name} COMMAND ${CMAKE_COMMAND} ${common} -DEXPECT=ok -P ${script})
  add_test(NAME compile_fail_${name} COMMAND ${CMAKE_COMMAND} ${common} -DEXPECT=fail "-DREGEX=${regex}" -P ${script})
  set_tests_properties(compile_ok_${name} compile_fail_${name} PROPERTIES TIMEOUT 120)
endfunction()

jgui_compile_fail(unknown_tag "no widget with this tag")
jgui_compile_fail(forges_clipboard_text "private|no matching|no viable")
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
jgui_compile_fail(listener_wrong_owner "no matching|no viable|cannot bind|could not convert|invalid initialization")
jgui_compile_fail(listener_wrong_method "constraints? not satisfied|not satisfied|no matching|invalid")
jgui_compile_fail(callback_forges_serial "no matching|no viable|could not convert|cannot convert|constraints? not satisfied|not satisfied|deleted")
jgui_compile_fail(callback_runs_key_from_number "consteval|immediate|constant expression|not a constant")
jgui_compile_fail(canvas_stored "no match|no viable|deleted")
jgui_compile_fail(pixel_buffer_copied "deleted")
jgui_compile_fail(callback_sets_size "private|inaccessible")
jgui_compile_fail(callback_dispatches "private|inaccessible")
jgui_compile_fail(callback_notifies "private|inaccessible")
jgui_compile_fail(callback_clears_damage "private|inaccessible")
jgui_compile_fail(callback_lays_out "private|inaccessible")
jgui_compile_fail(callback_paints "private|inaccessible")
jgui_compile_fail(callback_makes_key "private|inaccessible")
jgui_compile_fail(callback_copies_window "deleted")
jgui_compile_fail(caption_invalid_at_compile_time "consteval|immediate|constant expression|not a constant")
jgui_compile_fail(caption_too_long_at_compile_time "consteval|immediate|constant expression|not a constant")
jgui_compile_fail(caption_unchecked_at_run_time "consteval|immediate|constant expression|not a constant|not usable")
jgui_compile_fail(set_text_result_ignored "unused-result|nodiscard|warn_unused_result|unused return value")
jgui_compile_fail(callback_reaches_monitor "no member|has no member")
jgui_compile_fail(application_hides_monitor "private|inaccessible")
jgui_compile_fail(monitor_not_in_the_umbrella "no template|FrameMonitor. is not a member|no member named|was not declared|has not been declared")
jgui_compile_fail(settings_not_in_the_umbrella "no template|SettingsRecorder. is not a member|no member named|was not declared|has not been declared")
jgui_compile_fail(callback_records_items "private|inaccessible")
jgui_compile_fail(two_item_recorders "only one of two monitors")
jgui_compile_fail(grid_member_no_format "no cell format")
jgui_compile_fail(grid_column_wrong_row "belongs to another row type")
jgui_compile_fail(grid_duplicate_column "have the same name")
jgui_compile_fail(jtext_column_no_parse "no way to be read from text")
jgui_compile_fail(jtext_result_ignored "unused-result|nodiscard|warn_unused_result|unused return value")
jgui_compile_fail(grid_rows_result_ignored "unused-result|nodiscard|warn_unused_result|unused return value")
jgui_compile_fail(panel_wrong_axis "no matching|no viable|could not convert|cannot convert|deduced conflicting|deduction")
jgui_compile_fail(warn_narrowing "conversion\\]")
jgui_compile_fail(warn_sign_conversion "sign-conversion\\]")
jgui_compile_fail(warn_shadow "shadow[-a-z]*\\]")

# --- Source rules: the text layer, a proof that it catches what it claims, and the AST layer on a clang build ---
add_test(NAME source_rules
  COMMAND ${JGUI_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_source_rules.sh)
set(JGUI_CLANG_QUERY_NAMES clang-query)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # The clang-query of the compiler's own version comes first. Linux Mint has only versioned ones, such as clang-query-21.
  string(REGEX MATCH "^[0-9]+" JGUI_CLANG_MAJOR "${CMAKE_CXX_COMPILER_VERSION}")
  list(PREPEND JGUI_CLANG_QUERY_NAMES clang-query-${JGUI_CLANG_MAJOR})
endif()
find_program(JGUI_CLANG_QUERY NAMES ${JGUI_CLANG_QUERY_NAMES})
set(JGUI_SELFTEST_TOOL "")
if(JGUI_CLANG_QUERY AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(JGUI_SELFTEST_TOOL ${JGUI_CLANG_QUERY})
  add_test(NAME source_rules_ast
    COMMAND ${JGUI_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_ast_rules.sh ${JGUI_CLANG_QUERY} ${CMAKE_CURRENT_BINARY_DIR})
  # One clang-query per file, one after another. On Red Hat Enterprise Linux 10.3, with clang 22 reading libstdc++ 16,
  # the 51 files took 610 seconds.
  set_tests_properties(source_rules_ast PROPERTIES TIMEOUT 1200)
elseif(NOT JGUI_CLANG_QUERY)
  message(STATUS "clang-query was not found, so the AST layer of the source rules will not run")
endif()
add_test(NAME source_rules_selftest
  COMMAND ${JGUI_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_rules_selftest.sh ${JGUI_SELFTEST_TOOL})
set_tests_properties(source_rules_selftest PROPERTIES TIMEOUT 300)
