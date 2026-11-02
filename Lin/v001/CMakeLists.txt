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
  $<$<CONFIG:Release>:-O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3>)
target_link_options(jgui_hardening INTERFACE -pie -Wl,-z,relro,-z,now)
if(JGUI_SANITIZE)
  target_compile_options(jgui_hardening INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(jgui_hardening INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all)
endif()

# --- Warnings: our code only. A warning is a failure. ---
set(JGUI_WARNING_FLAGS -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast -Werror)
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

# --- Demo ---
add_executable(jcppgui-demo demo/demo.cpp)
target_link_libraries(jcppgui-demo PRIVATE jgui_wayland jgui_text jgui_warnings)

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

jgui_test(test_untrusted)
target_include_directories(test_untrusted PRIVATE src/wayland)
target_link_libraries(test_untrusted PRIVATE jgui_wayland)

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

# --- Source rules: banned type-erasure tokens, casts, paths and layering ---
add_test(NAME source_rules
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_source_rules.sh)
