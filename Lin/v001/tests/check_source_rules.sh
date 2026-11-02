# File: tests/check_source_rules.sh
# Purpose: Fails on banned type-erasure tokens and memory mapping in all our source, on file I/O outside
#          its helper, on raw new and delete and C-style casts, on absolute paths and shebangs anywhere
#          in the repository, and on Wayland leaking above its line. Run it as: bash tests/check_source_rules.sh
set -u
VERSION_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPO_DIR="$(cd "$VERSION_DIR/../.." && pwd)"
cd "$VERSION_DIR" || exit 2

failures=0
report() {  # report <rule> <grep output>
  if [ -n "$2" ]; then
    echo "RULE BROKEN: $1"
    echo "$2"
    failures=$((failures + 1))
  fi
}

mapfile -t sources < <(find include src tests demo -type f \( -name '*.hpp' -o -name '*.cpp' \))
[ "${#sources[@]}" -gt 20 ] || { echo "too few sources found: ${#sources[@]}"; exit 2; }

# --- Type rule ---
banned='std::function|move_only_function|copy_only_function|function_ref|std::any\b|shared_ptr|dynamic_cast|typeid|\bvirtual\b'
report "banned type-erasure token" "$(grep -nE "$banned" "${sources[@]}")"

# The untyped pointer is allowed only in the trampoline helper.
report "void pointer outside the trampoline helper" \
  "$(grep -nE '\bvoid[[:space:]]*\*' "${sources[@]}" | grep -v '^src/wayland/trampoline\.hpp:')"
report "reinterpret_cast outside src/wayland" \
  "$(grep -n 'reinterpret_cast' "${sources[@]}" | grep -v '^src/wayland/')"

# Nothing is mapped into this process by our code.
report "memory mapping" "$(grep -nE '\b(mmap(64)?|munmap|mremap|msync|mprotect)\b' "${sources[@]}")"

# Typed memory meets the OS's untyped byte interface in one helper only.
report "positioned file I/O outside the helper" \
  "$(grep -nE '\bp(read|write)(v2?|64|64v2?)?\b' "${sources[@]}" | grep -v '^src/wayland/file_io\.hpp:')"

# A C pointer and count are read in one helper only, and the helper it replaced stays gone.
report "views::counted outside the helper" \
  "$(grep -n 'views::counted' "${sources[@]}" | grep -v '^src/c_resource\.hpp:')"
report "the removed view of C-owned memory" "$(grep -nE '\bc_array\b' "${sources[@]}")"

# The number inside a wire value can be read only below the backend line.
report "wire access above the backend line" \
  "$(grep -n 'WireAccess' "${sources[@]}" | grep -vE '^(src/wayland/|include/jCppGUI/v001/events\.hpp:)')"

# std::variant needs a reason on the same or the previous line. There are none today.
report "std::variant without a comment" "$(grep -n 'std::variant' "${sources[@]}" | grep -v '//')"

# --- Code rules. The compilers also enforce the cast rule, through -Wold-style-cast. ---
# Comments and the insides of string literals are removed first, so prose cannot trip these.
strip_text() { sed -E -e 's/"([^"\\]|\\.)*"/""/g' -e 's://.*$::' "$1"; }
raw_memory=""
c_casts=""
for file in "${sources[@]}"; do
  found="$(strip_text "$file" | grep -nP '(?<![\w.])new\s+[\w:]+|(?<![\w=\s])\s*\bdelete\s+[\w*\[]|^\s*delete\b')"
  [ -n "$found" ] && raw_memory+="$file: $found"$'\n'
  # A type in parentheses followed by a value. An unnamed parameter is followed by a keyword instead.
  found="$(strip_text "$file" | grep -nP '\((const\s+)?(u?int(8|16|32|64)_t|std::u?int(8|16|32|64)_t|int|unsigned|long|short|char|float|double|size_t|std::size_t)\s*[*&]?\)\s*(?!const\b|noexcept\b|override\b)[\w(]')"
  [ -n "$found" ] && c_casts+="$file: $found"$'\n'
done
report "raw new or delete" "$raw_memory"
report "C-style cast" "$c_casts"

# --- Layering: only src/wayland names libwayland or xkbcommon ---
report "Wayland or xkb header above the backend line" \
  "$(grep -nE '#include.*(wayland-client|xkbcommon|xdg-shell)' "${sources[@]}" | grep -vE '^(src/wayland/|tests/test_untrusted\.cpp)')"
report "font library header outside src/text" \
  "$(grep -nE '#include.*(ft2build|fontconfig|hb\.h|hb-ft)' "${sources[@]}" | grep -v '^src/text/')"

# --- No absolute paths in the repository, with no exceptions, and so no shebangs ---
cd "$REPO_DIR" || exit 2
paths=""
shebangs=""
while IFS= read -r file; do
  [ -f "$file" ] || continue
  found="$(grep -nHE '(^|[^[:alnum:]_.~}/-])/(home|mnt|usr|tmp|etc|opt|var|root|run|srv|bin|sbin|lib|lib64|dev|proc|sys|boot|media)/' "$file")"
  [ -n "$found" ] && paths+="$found"$'\n'
  found="$(grep -nH '^#!' "$file")"
  [ -n "$found" ] && shebangs+="$found"$'\n'
done < <(git ls-files --cached --others --exclude-standard)
report "absolute path" "$paths"
report "shebang" "$shebangs"

if [ "$failures" -ne 0 ]; then
  echo "$failures rule(s) broken"
  exit 1
fi
echo "source rules hold over ${#sources[@]} files"
