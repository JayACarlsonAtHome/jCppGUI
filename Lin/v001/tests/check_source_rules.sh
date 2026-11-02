#!/usr/bin/env bash
# File: tests/check_source_rules.sh
# Purpose: Fails on banned type-erasure tokens in all our source, on raw new and delete and C-style
#          casts, on absolute paths anywhere in the repository, and on Wayland leaking above its line.
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

# --- No absolute paths in the repository. The two allowed are system contracts, not locations. ---
cd "$REPO_DIR" || exit 2
paths="$(git ls-files --cached --others --exclude-standard \
  | grep -v '^LICENSE$' \
  | while IFS= read -r file; do
      [ -f "$file" ] && grep -nHE '(^|[^[:alnum:]_.~}/-])/(home|mnt|usr|tmp|etc|opt|var|root|run|srv)/' "$file"
    done \
  | grep -vE '#!/usr/bin/env|/etc/os-release')"
report "absolute path" "$paths"

if [ "$failures" -ne 0 ]; then
  echo "$failures rule(s) broken"
  exit 1
fi
echo "source rules hold over ${#sources[@]} files"
