# File: bsir.sh
# Purpose: Bootstrap, Setup, Install and Run for jCppGUI Lin/v001.
#
#   1. Stop unless dnf is here: Fedora is the only system this has been written for.
#   2. Check each component with its `check` line in Setup/recipes.conf.
#   3. Run the system update on every run, without asking.
#   4. Offer anything missing, once, and install it from Setup/recipes.conf.
#   5. Build and test with one compiler and then the other, never at the same time.
#   6. Open the demo, if there is a Wayland session (skip with --no-demo).
#
# Run it as: bash bsir.sh. Everything is found from this script's own directory. Re-run it any time.
set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
RECIPES="$ROOT/Setup/recipes.conf"
COMPONENTS="gcc clang compiler_rt cmake ninja pkgconfig wayland wayland_protocols xkbcommon freetype harfbuzz fontconfig vim obs"

ASSUME_YES=0
WANT_DEMO=1
for arg in "$@"; do
  case "$arg" in
    -y|--yes) ASSUME_YES=1 ;;
    --no-demo) WANT_DEMO=0 ;;
    -h|--help)
      echo "usage: bash bsir.sh [--yes] [--no-demo]"
      echo "  --yes      install everything that is missing without asking"
      echo "  --no-demo  build and test only, and do not open the window"
      exit 0 ;;
    *) echo "unknown argument: $arg (try --help)" >&2; exit 2 ;;
  esac
done

# --- 1. Fedora only, until others are tested ---
if [ -z "$(command -v dnf)" ]; then
  echo "This script supports Fedora only, and this system has no dnf. Nothing was changed." >&2
  echo "To build by hand, see README.md." >&2
  exit 1
fi

# A recipe value, joining backslash-continued lines: recipe <component> <field>
recipe() {
  awk -v full="$1.$2" '
    function is_key(line,   eq, key) {
      eq = index(line, "=")
      if (eq == 0) return 0
      key = substr(line, 1, eq - 1); gsub(/^[ \t]+|[ \t]+$/, "", key)
      return key == full
    }
    !found && is_key($0) { found = 1; sub(/^[^=]*=[ \t]*/, "") }
    found {
      more = ($0 ~ /\\[ \t]*$/)
      sub(/\\[ \t]*$/, ""); sub(/[ \t]+$/, "")
      out = out (out == "" ? "" : " ") $0
      if (!more) { print out; exit }
    }
  ' "$RECIPES"
}

present() {
  check="$(recipe "$1" check)"
  [ -n "$check" ] && output="$(sh -c "$check" 2>&1)"
}

find_missing() {
  missing=""
  for component in $COMPONENTS; do
    present "$component" || missing="$missing $component"
  done
}

# --- 2. what is here ---
find_missing
echo "Missing:${missing:- nothing}"

# --- 3. the system update runs on every run, without asking ---
echo
echo "=== system update ==="
sudo dnf -y upgrade --refresh || echo "  [FAIL] system update; going on"

# --- 4. offer anything missing, once ---
find_missing
if [ -n "$missing" ]; then
  if [ "$ASSUME_YES" = 0 ]; then
    if [ ! -t 0 ]; then
      echo "Missing:$missing. Run bash bsir.sh in a terminal, or bash bsir.sh --yes" >&2
      exit 1
    fi
    printf "Install%s? [Y/n] " "$missing"
    read -r answer || answer=n
    case "$answer" in
      [nN]*) echo "Not installing. The build cannot go on without them." >&2; exit 1 ;;
    esac
  fi
  for component in $missing; do
    echo
    echo "=== $(recipe "$component" desc) ==="
    sh -c "$(recipe "$component" dnf)" || echo "  [FAIL] (see above)"
  done
  find_missing
  if [ -n "$missing" ]; then
    echo "Still missing:$missing. Fix the errors above, then re-run bash bsir.sh" >&2
    exit 1
  fi
fi

# --- 5. build and test, one compiler at a time ---
cd "$ROOT"
for preset in gcc-debug clang-debug; do
  echo
  echo "=== $preset ==="
  cmake --preset "$preset"
  cmake --build --preset "$preset"
  ctest --preset "$preset"
done

# Build only: optimized, without the flags that stop some warnings firing. See docs/SECURITY.md.
echo
echo "=== gcc-warn ==="
cmake --preset gcc-warn
cmake --build --preset gcc-warn

# --- 6. the window ---
DEMO="$ROOT/build/gcc-debug/jcppgui-demo"
echo
echo "Bootstrap complete. The demo: $DEMO"
if [ "$WANT_DEMO" = 1 ] && [ -n "${WAYLAND_DISPLAY:-}" ]; then
  DEMO_LOG="$ROOT/build/demo.log"
  : > "$DEMO_LOG"
  ( setsid "$DEMO" >"$DEMO_LOG" 2>&1 <"$DEMO_LOG" & )
  echo "Opened the demo. It keeps running when this terminal is closed. Its output: $DEMO_LOG"
fi
