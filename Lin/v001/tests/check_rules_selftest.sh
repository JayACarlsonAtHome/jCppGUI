# File: tests/check_rules_selftest.sh
# Purpose: Proves the rule scripts catch what they claim to. Each bypass goes into a private copy of the tree, and the
#          text layer must refuse it with the rule that names it. So must the AST layer, if a query tool is given.
#          Run it as: bash tests/check_rules_selftest.sh [clang-query]
set -u -o pipefail
QUERY_TOOL="${1:-}"
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)" || exit 2
ROOT="$(cd "$TESTS_DIR/.." && pwd)" || exit 2

work="$(mktemp -d)" || exit 2
trap 'rm -rf "$work"' EXIT
tree="$work/repo/Lin/v001"
mkdir -p "$tree" || exit 2
(cd "$ROOT" && tar cf - include src tests demo) | (cd "$tree" && tar xf -) || { echo "could not copy the tree"; exit 2; }
git -C "$work/repo" init -q || { echo "git init failed"; exit 2; }

failed=0
ran=0
rules() { bash "$tree/tests/check_source_rules.sh" 2>&1; }

baseline="$(rules)" || { echo "the clean copy does not pass:"; echo "$baseline"; exit 1; }

# must_catch <file under the tree> <text the report must contain> <content>
must_catch() {
  local file="$1" wanted="$2" content="$3" output status
  ran=$((ran + 1))
  mkdir -p "$(dirname "$tree/$file")"
  printf '%s\n' "$content" > "$tree/$file"
  output="$(rules)"
  status=$?
  rm -f "$tree/$file"
  if [ "$status" -eq 0 ]; then
    echo "MISSED: $file: $content"
    failed=$((failed + 1))
  elif ! grep -qF -- "$wanted" <<<"$output"; then
    echo "CAUGHT FOR ANOTHER REASON: $file: $content (wanted: $wanted)"
    echo "$output"
    failed=$((failed + 1))
  fi
}

must_pass() {
  local file="$1" content="$2" output
  ran=$((ran + 1))
  printf '%s\n' "$content" > "$tree/$file"
  output="$(rules)" || { echo "A FALSE ALARM: $file: $content"; echo "$output"; failed=$((failed + 1)); }
  rm -f "$tree/$file"
}

# --- What the review reproduced ---
must_catch tests/evil.h "a file type that is not allowed" 'class A { virtual void f(); }; std::function<int()> g; void* p;'
must_catch tests/evil.hh "a file type that is not allowed" 'int x;'
must_catch tests/evil.inl "a file type that is not allowed" 'int x;'
must_catch tests/evil.txt "a file type that is not allowed" 'int x;'
must_catch tests/noextension "a file type that is not allowed" 'int x;'
must_catch tests/evil.cpp "a namespace alias of std" 'namespace s = std; s::function<int()> f;'
must_catch tests/evil.cpp "void pointer" 'void const* p = nullptr;'
must_catch tests/evil.cpp "void pointer" 'const void* p = nullptr;'
must_catch tests/evil.cpp "void pointer" 'void volatile const* p = nullptr;'
must_catch tests/evil.cpp "raw new" 'int* p = new (std::nothrow) int[4];'
must_catch tests/evil.cpp "raw delete" 'void f(int* q) { if (q) delete[] q; }'
must_catch tests/evil.cpp "raw delete" 'void f(int* q) { ++*q; delete q; }'

# --- Tokens split over lines, or hidden behind another name ---
must_catch tests/evil.cpp "void pointer" 'void
  * p = nullptr;'
must_catch tests/evil.cpp "banned type-erasure" 'std ::
  function<int()> f;'
must_catch tests/evil.cpp "another name for void" 'using V = void; V* p = nullptr;'
must_catch tests/evil.cpp "a macro with a body" '#define F function
std::F<int()> g;'
must_catch tests/evil.cpp "a macro with a body" 'int vi ## rtual;'
must_catch tests/evil.cpp "a pointer as an integer" 'std::uintptr_t p = 0;'
must_catch tests/evil.cpp "a quoted include" '#include "notes.txt"'

# --- Each banned token still fires ---
for token in 'virtual void f();' 'std::any a;' 'std::shared_ptr<int> p;' 'int x = dynamic_cast<int>(1);' 'auto& t = typeid(int);' 'std::move_only_function<int()> f;'; do
  must_catch tests/evil.cpp "banned type-erasure" "$token"
done
must_catch tests/evil.cpp "reinterpret_cast" 'auto* p = reinterpret_cast<int*>(1);'
must_catch tests/evil.cpp "const_cast" 'int* p = const_cast<int*>(q);'
must_catch tests/evil.cpp "memory mapping" 'auto p = ::mmap(nullptr, 1, 0, 0, 0, 0);'
must_catch tests/evil.cpp "a pragma" '#pragma GCC diagnostic ignored "-Wall"'

# --- Every kind of byte I/O, not only the positioned kind ---
must_catch tests/evil.cpp "positioned file I/O" 'auto n = ::pread(fd, b, 1, 0);'
must_catch tests/evil.cpp "positioned file I/O" 'auto n = pwrite64(fd, b, 1, 0);'
must_catch include/jCppGUI/v001/evil.hpp "Wayland or xkb header above" '#include <cursor-shape-v1-client-protocol.hpp>'
must_catch tests/evil.cpp "the jText writer's geometry reader" 'struct jgui::detail::FileNumbers {};'
must_catch tests/evil.cpp "the cell text's geometry reader" 'struct jgui::detail::CellNumbers {};'
for call in 'read(fd, b, 1)' '::write(fd, b, 1)'; do
  must_catch tests/evil.cpp "read or write outside" "void f() { $call; }"
done
for call in 'recv(fd, b, 1, 0)' '::send(fd, b, 1, 0)' 'fread(b, 1, 1, f)' 'std::fwrite(b, 1, 1, f)' 'std::memcpy(a, b, 1)' 'memmove(a, b, 1)'; do
  must_catch tests/evil.cpp "byte I/O" "void f() { $call; }"
done
# The exception the owner approved is for read and write in src/file_io.hpp and nothing else.
must_catch src/file_io.hpp "byte I/O" 'void f() { std::memcpy(a, b, 1); }'
must_catch src/file_io.hpp "byte I/O" 'void f() { recv(fd, b, 1, 0); }'
must_catch tests/evil.cpp "descriptor passing" 'void f() { sendmsg(1, nullptr, 0); }'
must_catch tests/evil.cpp "a stream or C file" 'std::ifstream in;'
must_catch tests/evil.cpp "a stream or C file" 'std::fstream both;'
must_catch tests/evil.cpp "a stream or C file" 'FILE* f = nullptr;'
must_catch tests/evil.cpp "a C string walked" 'auto n = std::strlen(s);'
must_catch tests/evil.cpp "a way around" 'void f() { syscall(1); }'

# --- Paths and shebangs. The fixture is split so this script itself stays clean. ---
slash=/
must_catch tests/evil.cpp "absolute path" "const char* p = \"${slash}usr/lib\";"
must_catch tests/evil.sh "absolute path" "cat ${slash}etc/passwd"
must_catch tests/evil.sh "shebang" "#!${slash}bin/sh"
must_catch tests/evil.cpp "C-style cast" 'int x = (int)y;'

# --- The one excepted literal: refused anywhere but its one file, and that file is still scanned for all else ---
excepted_file="src/base_directory.cpp"
kernel_link="${slash}proc/self/exe"
for elsewhere in tests/evil.cpp src/evil.cpp src/monitor/base_directory.cpp include/jCppGUI/v001/evil.hpp demo/evil.cpp; do
  must_catch "$elsewhere" "absolute path" "const char* p = \"$kernel_link\";"
done
must_catch tests/evil.sh "absolute path" "readlink $kernel_link"
must_catch notes.md "absolute path" "It reads \"$kernel_link\"."
# must_catch_in_excepted <what is added to the excepted file>
must_catch_in_excepted() {
  local saved="$work/saved_excepted"
  ran=$((ran + 1))
  cp "$tree/$excepted_file" "$saved" || exit 2
  printf '%s\n' "$1" >> "$tree/$excepted_file"
  output="$(rules)"
  status=$?
  cp "$saved" "$tree/$excepted_file" || exit 2
  if [ "$status" -eq 0 ]; then
    echo "MISSED in the excepted file: $1"
    failed=$((failed + 1))
  elif ! grep -qF "absolute path" <<<"$output"; then
    echo "CAUGHT FOR ANOTHER REASON in the excepted file: $1"
    echo "$output"
    failed=$((failed + 1))
  fi
}
must_catch_in_excepted "const char* p = \"${slash}etc/passwd\";"
must_catch_in_excepted "const char* p = \"${slash}proc/self/cwd\";"
must_catch_in_excepted "const char* p = \"$kernel_link/x\";"
must_catch_in_excepted "const char* p = \"${kernel_link}2\";"
must_catch_in_excepted "// $kernel_link"
must_catch_in_excepted "const char* again = \"$kernel_link\";"
must_catch_in_excepted "const char* a = \"${slash}etc/x\"; const char* b = \"$kernel_link\";"

# --- A source file outside the four directories ---
must_catch stray.cpp "source outside" 'int x;'

# --- Words in comments and strings are not code ---
must_pass tests/quiet.cpp '// virtual std::function void* new delete read(fd) memcpy(a, b, 1)
/* dynamic_cast typeid
   reinterpret_cast */
const char* text = "virtual void* new delete[] p read(x)";
const char* raw = R"(virtual void* new delete[] p)";
constexpr char quote = '"'"'"'"'"';
void removed(const int&) = delete;
struct S { S(const S&) = delete; };'

# --- Outside a checkout, the rules that need git must fail and not pass by finding nothing ---
ran=$((ran + 1))
rm -rf "$work/repo/.git"
if output="$(rules)"; then
  echo "MISSED: outside a git checkout it passed: $output"
  failed=$((failed + 1))
elif ! grep -qF "git ls-files failed" <<<"$output"; then
  echo "CAUGHT FOR ANOTHER REASON outside a checkout: $output"
  failed=$((failed + 1))
fi

# --- The AST layer sees through what text cannot ---
if [ -n "$QUERY_TOOL" ]; then
  ast="$work/ast/tests"
  mkdir -p "$ast"
  ast_must_catch() {
    ran=$((ran + 1))
    printf '%s\n' "$1" > "$ast/bad.cpp"
    if ! output="$("$QUERY_TOOL" -f "$TESTS_DIR/ast_rules.query" "$ast/bad.cpp" -- -std=c++23 2>&1)"; then
      echo "THE AST LAYER COULD NOT RUN: $1"
      echo "$output" | head -5
      failed=$((failed + 1))
    elif grep -qE 'error:|Matcher not found' <<<"$output" || ! grep -qE '^[1-9][0-9]* match(es)?\.' <<<"$output"; then
      echo "THE AST LAYER MISSED: $1"
      echo "$output" | grep -E 'error:|Matcher' | head -5
      failed=$((failed + 1))
    fi
  }
  ast_must_catch 'struct A { virtual void f() {} };'
  ast_must_catch 'struct B {}; struct D : B {};'
  ast_must_catch '#include <functional>
namespace s = std;
s::function<int()> f;'
  ast_must_catch '#include <memory>
template <class T> using Shared = std::shared_ptr<T>;
Shared<int> p;'
  ast_must_catch 'using V = void; V* p = nullptr;'
  ast_must_catch 'void const* p = nullptr;'
  ast_must_catch 'int x = 0; auto* p = static_cast<void*>(&x);'
  ast_must_catch 'void take(const void*); int y = 0; void f() { take(&y); }'
  ast_must_catch 'struct P { virtual ~P() = default; }; P* a(P* p) { return dynamic_cast<P*>(p); }'
  ast_must_catch '#include <typeinfo>
const char* name(int v) { return typeid(v).name(); }'
  ast_must_catch 'int* p = new int;'
  ast_must_catch 'void f(int* q) { delete q; }'
  ast_must_catch '#include <unistd.h>
#define R read
void f(int fd, char* b) { (void)R(fd, b, 1); }'
  ast_must_catch '#include <cstring>
void f(char* a, const char* b) { std::memcpy(a, b, 1); }'
fi

if [ "$failed" -ne 0 ]; then
  echo "$failed of $ran bypasses got through"
  exit 1
fi
echo "all $ran bypasses are caught"
