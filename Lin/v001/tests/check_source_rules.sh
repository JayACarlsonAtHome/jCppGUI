# File: tests/check_source_rules.sh
# Purpose: The text layer of the source rules. Comments and the insides of strings are removed first, so prose
#          cannot trip it, and each file is also checked with its lines joined, so a token split over lines
#          cannot hide. Text matching can never be complete: tests/check_ast_rules.sh is a second layer over the
#          compiler's own view of the code, and the compile-fail tests and a reviewer's reading are the rest.
#          Run it as: bash tests/check_source_rules.sh
set -u -o pipefail
VERSION_DIR="$(cd "$(dirname "$0")/.." && pwd)" || exit 2
REPO_DIR="$(cd "$VERSION_DIR/../.." && pwd)" || exit 2
cd "$VERSION_DIR" || exit 2

failures=0
report() {  # report <rule> <what was found>
  if [ -n "$2" ]; then
    echo "RULE BROKEN: $1"
    echo "$2"
    failures=$((failures + 1))
  fi
}

# --- What is here: every file under these four directories is looked at ---
listing="$(find include src tests demo \( -type f -o -type l \))" || { echo "find failed"; exit 2; }
mapfile -t everything <<<"$listing"
sources=()
refused=""
links=""
for file in "${everything[@]}"; do
  [ -L "$file" ] && links+="$file"$'\n'
  case "$file" in
    *.hpp | *.cpp) sources+=("$file") ;;
    *.sh | *.cmake | *.query) ;;
    tests/fuzz/seeds/*) ;;
    *) refused+="$file"$'\n' ;;
  esac
done
report "a file type that is not allowed here: source is .hpp and .cpp, and only those are checked as source" "${refused%$'\n'}"
report "a symbolic link" "${links%$'\n'}"
[ "${#sources[@]}" -gt 20 ] || { echo "too few sources found: ${#sources[@]}"; exit 2; }

work="$(mktemp -d)" || { echo "mktemp failed"; exit 2; }
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/stripped" "$work/flat" || exit 2

# Removes comments and the insides of string and character literals, and keeps every newline, so that line
# numbers still hold. A raw string is read to its own closing delimiter.
strip_awk='
function emit(text) { printf "%s", text }
BEGIN { state = "code" }
{
  line = $0; n = length(line); i = 1
  while (i <= n) {
    c = substr(line, i, 1); two = substr(line, i, 2)
    if (state == "block") {
      if (two == "*/") { state = "code"; i += 2 } else i++
    } else if (state == "raw") {
      closing = ")" delim "\""
      where = index(substr(line, i), closing)
      if (where == 0) i = n + 1
      else { i += where - 1 + length(closing); emit("\"\""); state = "code" }
    } else if (two == "//") {
      i = n + 1
    } else if (two == "/*") {
      state = "block"; i += 2
    } else if (c == "\"") {
      previous = substr(line, i - 1, 1)
      if (previous == "R" && match(substr(line, i + 1), /^[^()\\ ]*\(/)) {
        delim = substr(line, i + 1, RLENGTH - 1); i += RLENGTH + 1; state = "raw"
      } else {
        i++
        while (i <= n && substr(line, i, 1) != "\"") { if (substr(line, i, 1) == "\\") i++; i++ }
        i++; emit("\"\"")
      }
    } else if (c == "\047" && substr(line, i - 1, 1) !~ /[[:alnum:]_]/) {
      i++
      while (i <= n && substr(line, i, 1) != "\047") { if (substr(line, i, 1) == "\\") i++; i++ }
      i++; emit("\047\047")
    } else { emit(c); i++ }
  }
  printf "\n"
}'
for file in "${sources[@]}"; do
  mkdir -p "$work/stripped/$(dirname "$file")" "$work/flat/$(dirname "$file")" || exit 2
  awk "$strip_awk" "$file" > "$work/stripped/$file" || { echo "could not read $file"; exit 2; }
  tr '\n' ' ' < "$work/stripped/$file" | sed -E 's/[[:space:]]+/ /g' > "$work/flat/$file" || exit 2
done

# check <rule> <perl regex> [paths allowed, as a regex on the path]. Looks line by line, for where it is, and then
# over the joined lines, for what is split up.
check() {
  local rule="$1" regex="$2" allowed="${3:-^$}" found split
  found="$(cd "$work/stripped" && grep -rnP -- "$regex" include src tests demo | grep -vP "$allowed")"
  split="$(cd "$work/flat" && grep -rlP -- "$regex" include src tests demo | grep -vP "$allowed")"
  [ -z "$found" ] && [ -n "$split" ] && found="split over lines in: $split"
  report "$rule" "$found"
}

# --- The type rule ---
check "banned type-erasure token" \
  '\bstd\s*::\s*function\b|\bfunction\s*<|\b(move_only_function|copyable_function|copy_only_function|function_ref|any_cast|shared_ptr|weak_ptr|make_shared|allocate_shared|enable_shared_from_this|dynamic_cast|dynamic_pointer_cast|typeid|type_info|type_index|virtual)\b|::\s*any\b'
check "void pointer outside the trampoline" '\bvoid\b(\s|const\b|volatile\b)*\*' '^src/wayland/trampoline\.hpp:?'
check "another name for void" '\busing\s+\w+\s*=\s*(const\s+)?void\b|\btypedef\s+(const\s+)?void\b|\badd_pointer(_t)?\s*<\s*(const\s+)?void\b'
check "a pointer as an integer" '\bu?intptr_t\b'
check "reinterpret_cast outside the socket helper" '\breinterpret_cast\b' '^tests/fake_compositor/socket_io\.hpp:?'
check "const_cast outside the socket helper" '\bconst_cast\b' '^tests/fake_compositor/socket_io\.hpp:?'
check "a macro with a body, which could hide any of these" '^\s*#\s*define\s+\w+(\(|\s+\S)|##'
check "a namespace alias of std, which could hide any of these" '\bnamespace\s+\w+\s*=\s*(::\s*)?std\b|\busing\s+namespace\s+(::\s*)?std\b'
check "a pragma, other than once" '^\s*#\s*pragma\s+(?!once\b)|\b_Pragma\b'
check "a listener entry made by hand" '\bTrampoline\s*<' '^src/wayland/trampoline\.hpp:?'

# --- Nothing is mapped into this process by our code ---
check "memory mapping" '\b(mmap(64)?|munmap|mremap|msync|mprotect)\b'

# --- Typed memory meets the OS's untyped byte interface in a few named helpers only ---
check "positioned file I/O outside its helper" '\bp(read|write)(v2?|64|64v2?)?\b' '^src/file_io\.hpp:?'
expression='(?:^|[(=,;{}!?:]|&&|\|\||\breturn\b|\belse\b)\s*'
# The one exception, which the owner approved on 2026-11-04: src/file_io.hpp, for read_stream and write_stream. A pipe has no offsets, and it is how a clipboard hands text to another program.
check "read or write outside src/file_io.hpp" "$expression(read|write)\s*\(" '^src/file_io\.hpp:?'
check "byte I/O or a copy of raw bytes outside the named helpers" \
  "$expression(recv|send|fread|fwrite|fgets|fputs|fprintf|readv|writev|recvfrom|sendto|recvmmsg|sendmmsg|sendfile|splice|tee|vmsplice|copy_file_range|memcpy|memmove|memccpy|mempcpy|bcopy|wmemcpy|wmemmove)\s*\("
check "descriptor passing outside the socket helper, which the owner approved for tests only" \
  '\b(sendmsg|recvmsg|iovec|msghdr|cmsghdr|CMSG_\w+|SCM_RIGHTS|sockaddr\w*)\b' '^tests/fake_compositor/socket_io\.hpp:?'
check "a way around the C library" '\bsyscall\s*\(|\basm\b|\b__asm\w*\b|\bdlopen\b|\bdlsym\b|\b__builtin_\w*(mem|str)\w*\b'
check "a stream or C file class" '\b(basic_)?(i|o)?fstream\b|\b(basic_)?filebuf\b|\bstdio_filebuf\b|\bFILE\s*\*|\b(fopen|freopen|fdopen)\b'

# --- C pointer and count pairs and C strings are read in one helper only ---
check "views::counted outside the helper" '\bviews\s*::\s*counted\b' '^src/c_resource\.hpp:?'
check "a C string walked outside the helper" '\b(strlen|strnlen|wcslen|strcpy|strncpy|strcat|strncat|sprintf)\b|\bchar_traits\s*<[^>]*>\s*::\s*length\b'
check "the removed view of C-owned memory" '\bc_array\b'

# --- Who can read or make what ---
check "wire access above the backend line" '\bWireAccess\b' '^(src/wayland/|include/jCppGUI/v001/events\.hpp:?)'
check "geometry access above the backend line" '\bGeometryAccess\b' '^(src/|include/jCppGUI/v001/geometry\.hpp:?)'
check "the jText writer's geometry reader outside its users" '\bFileNumbers\b' '^(include/jCppGUI/v001/(geometry|jtext_writer|redraw_monitor|settings_recorder)\.hpp:?)'
check "the cell text's geometry reader outside its file" '\bCellNumbers\b' '^(include/jCppGUI/v001/(geometry|cell_text)\.hpp:?)'
found="$(cd "$work/stripped" && grep -rnP '\bTestAccess\b' include src demo | grep -vP '(struct TestAccess;|friend struct detail::TestAccess;)')"
report "test access outside tests" "$found"

# --- Raw memory. The compilers also enforce the cast rule, through -Wold-style-cast, except for a cast to void. ---
check "raw new" '(?<!<)\bnew\b'
check "raw delete" '(?<!=)(?<!=\s)\bdelete\b'
check "raw allocation" '\b(malloc|calloc|realloc|aligned_alloc|posix_memalign|alloca)\b'
check "C-style cast" '\((const\s+)?(u?int(8|16|32|64)_t|std::u?int(8|16|32|64)_t|int|unsigned|long|short|char|float|double|size_t|std::size_t|void)\s*[*&]?\)\s*(?!const\b|noexcept\b|override\b)[\w(]'

# --- What comes in through an include. These look at the file as written, because the path is inside a string. ---
in_raw() { grep -nP -- "$1" "${sources[@]}"; }
report "std::variant without a comment" "$(in_raw 'std::variant' | grep -v '//')"
report "a quoted include that is not .hpp, so a checked file could pull in an unchecked one" \
  "$(in_raw '^\s*#\s*include\s*"[^"]*"' | grep -vP '\.hpp"' | grep -vP '^src/wayland/\w+\.(cpp|hpp):\d+:\s*#\s*include\s*"(xdg-shell|cursor-shape-v1)-client-protocol\.h"')"
report "an include through a macro" "$(in_raw '^\s*#\s*include\s+[A-Za-z_]' | grep -v '^src/text/text\.cpp:')"
report "an embedded file" "$(in_raw '^\s*#\s*embed\b|__has_embed')"
report "Wayland or xkb header above the backend line" \
  "$(in_raw '^\s*#\s*include.*(wayland-client|xkbcommon|xdg-shell|cursor-shape)' | grep -vP '^(src/wayland/|tests/test_untrusted\.cpp:|tests/test_injection\.cpp:)')"
report "libwayland-server, which brings untyped callbacks in" "$(in_raw '^\s*#\s*include.*wayland-server')"
report "libwayland in the test compositor, which speaks the wire protocol by itself" "$(in_raw "^\\s*#\\s*include.*wayland-" | grep "^tests/fake_compositor/")"
report "font library header outside src/text" \
  "$(in_raw '^\s*#\s*include.*(ft2build|fontconfig|hb\.h|hb-ft)' | grep -v '^src/text/')"

# --- No absolute paths in the repository, and so no shebangs. Source anywhere else is refused. There is one exception, which
#     the owner approved on 2026-11-02: the kernel's link to the running executable, as this one quoted literal, once, in this
#     one file. It is built from parts so that this script stays clean. Everything else in that file is scanned as usual.
#     Source may also sit in a vendored copy, which the owner approved on 2026-11-20: a folder in Lin/v001 that holds a
#     VENDORED.jtext manifest (Setup/vendor.sh), the way jac313 keeps jText. Its files are another repo's, so the text rules
#     above do not read them; vendor-check reports one edited in place or not from canonical. They are still scanned here
#     for absolute paths and shebangs. ---
cd "$REPO_DIR" || exit 2
slash=/
excepted_file="Lin/v001/src/base_directory.cpp"
excepted_literal="\"${slash}proc/self/exe\""
tracked="$(git ls-files --cached --others --exclude-standard)" || { echo "git ls-files failed: these rules need a git checkout"; exit 2; }
[ -n "$tracked" ] || { echo "git ls-files found nothing"; exit 2; }
absolute_path='(^|[^[:alnum:]_.~}/-])/(home|mnt|usr|tmp|etc|opt|var|root|run|srv|bin|sbin|lib|lib64|dev|proc|sys|boot|media)/'
paths=""
shebangs=""
strays=""
# The vendored copies: each folder in Lin/v001 that holds a VENDORED.jtext, as "Lin/v001/<folder>/".
vendored_roots="$(printf '%s\n' "$tracked" | sed -n 's#^\(Lin/v001/[^/]*\)/VENDORED\.jtext$#\1/#p')"
in_vendored_copy() {
  local root
  while IFS= read -r root; do
    [ -n "$root" ] && [ "${1#"$root"}" != "$1" ] && return 0
  done <<<"$vendored_roots"
  return 1
}
while IFS= read -r file; do
  [ -f "$file" ] || continue
  if [ "$file" = "$excepted_file" ]; then
    found="$(grep -oF -- "$excepted_literal" "$file" | wc -l)"
    [ "$found" -gt 1 ] && paths+="$file: the one excepted literal is there $found times"$'\n'
    found="$(sed "s|$excepted_literal||" "$file" | grep -nHE --label="$file" "$absolute_path")"
  else
    found="$(grep -nHE "$absolute_path" "$file")"
  fi
  [ -n "$found" ] && paths+="$found"$'\n'
  found="$(grep -nH '^#!' "$file")"
  [ -n "$found" ] && shebangs+="$found"$'\n'
  in_vendored_copy "$file" && continue
  case "$file" in
    Lin/v001/include/* | Lin/v001/src/* | Lin/v001/tests/* | Lin/v001/demo/*) ;;
    *.h | *.hh | *.hxx | *.h++ | *.hpp | *.inl | *.ipp | *.tcc | *.tpp | *.c | *.cc | *.cxx | *.c++ | *.cpp | *.ixx | *.cppm) strays+="$file"$'\n' ;;
  esac
done <<<"$tracked"
report "absolute path" "${paths%$'\n'}"
report "shebang" "${shebangs%$'\n'}"
report "source outside include, src, tests and demo" "${strays%$'\n'}"

if [ "$failures" -ne 0 ]; then
  echo "$failures rule(s) broken"
  exit 1
fi
echo "source rules hold over ${#sources[@]} files"
