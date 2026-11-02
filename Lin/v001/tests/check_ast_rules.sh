# File: tests/check_ast_rules.sh
# Purpose: The second layer under tests/check_source_rules.sh: runs tests/ast_rules.query over every source file the
#          build compiled, so an alias, a macro or a split token cannot hide. Each match, or any file that could not
#          be parsed, is a failure. It needs a clang build's compile_commands.json, so only clang presets run it.
#          Run it as: bash tests/check_ast_rules.sh <clang-query> <build directory>
set -u -o pipefail
QUERY_TOOL="$1"
BUILD_DIR="$2"
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)" || exit 2
ROOT="$(cd "$TESTS_DIR/.." && pwd)" || exit 2
cd "$ROOT" || exit 2

[ -f "$BUILD_DIR/compile_commands.json" ] || { echo "no compile_commands.json in $BUILD_DIR"; exit 2; }
listing="$(grep -oE '"file": "[^"]*\.cpp"' "$BUILD_DIR/compile_commands.json" | sed -E 's/"file": "(.*)"/\1/' | sort -u)" || { echo "no sources listed"; exit 2; }
mapfile -t files <<<"$listing"
[ "${#files[@]}" -gt 10 ] || { echo "too few sources listed: ${#files[@]}"; exit 2; }

broken=0
for file in "${files[@]}"; do
  output="$("$QUERY_TOOL" -p "$BUILD_DIR" -f "$TESTS_DIR/ast_rules.query" "$file" 2>&1)"
  status=$?
  if [ "$status" -ne 0 ] || grep -qE 'error:|Matcher not found|Error ' <<<"$output"; then
    echo "NOT CHECKED: $file (exit $status)"
    grep -E 'error:|Matcher not found|Error ' <<<"$output" | head -20
    broken=$((broken + 1))
  elif grep -qE '^[1-9][0-9]* match(es)?\.' <<<"$output"; then
    echo "RULE BROKEN in or through: $file"
    grep -E 'note: "root" binds here' <<<"$output" | sort -u | head -40
    broken=$((broken + 1))
  fi
done
if [ "$broken" -ne 0 ]; then
  echo "$broken file(s) broke a rule or could not be checked"
  exit 1
fi
echo "AST rules hold over ${#files[@]} translation units"
