# File: bsir.sh
# Purpose: Bootstrap, Setup, Install and Run for jCppGUI Lin/v001.
#
#   1. Stop unless this is Fedora (dnf) or Linux Mint (apt and lsb_release): the systems this has been tested on.
#   2. Check each component with its `check` line in Setup/recipes.conf, this OS's own line where it has one.
#   3. Run the system update on every run, without asking.
#   4. Offer anything missing, once, and install it from Setup/recipes.conf.
#   5. Offer what is optional the same way. Declining it, or failing to install it, gives a warning and the build still goes on.
#   6. Build and test with one compiler and then the other, never at the same time, with this OS's compilers from
#      Setup/recipes.conf (Linux Mint: g++-15 and clang++-21, the same as jac313 there).
#   7. Open the demo, if there is a Wayland session (skip with --no-demo).
#
# Run it as: bash bsir.sh. Everything is found from this script's own directory. Re-run it any time.
set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
RECIPES="$ROOT/Setup/recipes.conf"
COMPONENTS="gcc clang compiler_rt cmake ninja pkgconfig wayland wayland_protocols xkbcommon freetype harfbuzz fontconfig vim obs"
OPTIONAL_COMPONENTS="meld"

ASSUME_YES=0
WANT_DEMO=1
for arg in "$@"; do
  case "$arg" in
    -y|--yes) ASSUME_YES=1 ;;
    --no-demo) WANT_DEMO=0 ;;
    -h|--help)
      echo "usage: bash bsir.sh [--yes] [--no-demo]"
      echo "  --yes      install everything that is missing, optional things too, without asking"
      echo "  --no-demo  build and test only, and do not open the window"
      exit 0 ;;
    *) echo "unknown argument: $arg (try --help)" >&2; exit 2 ;;
  esac
done

# --- 1. Fedora or Linux Mint, the systems this has been tested on ---
OS=""
if [ -n "$(command -v dnf)" ]; then
  OS=fedora
elif [ -n "$(command -v apt-get)" ] && [ -n "$(command -v lsb_release)" ] && [ "$(lsb_release -si)" = "Linuxmint" ]; then
  OS=linuxmint
fi
if [ -z "$OS" ]; then
  echo "This script supports Fedora and Linux Mint only, and this system is neither. Nothing was changed." >&2
  echo "To build by hand, see README.md." >&2
  exit 1
fi
echo "System: $OS"

# A recipe value, joining backslash-continued lines: recipe <component> <field>
# This OS's own <component>.<field>.<os> comes first, then the shared <component>.<field>.
recipe() {
  value="$(recipe_key "$1.$2.$OS")"
  [ -n "$value" ] || value="$(recipe_key "$1.$2")"
  printf '%s\n' "$value"
}

recipe_key() {
  awk -v full="$1" '
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

# Those of the given components that are not here, in $missing.
find_missing() {
  missing=""
  for component in "$@"; do
    present "$component" || missing="$missing $component"
  done
}

# What goes without each optional component. Nothing but a warning: the build does not need them.
warn_optional() {
  for component in $1; do
    case "$component" in
      meld) echo "Warning: Meld is not installed. Diff will use the built-in viewer, for two files only." >&2 ;;
      *) echo "Warning: $component is not installed." >&2 ;;
    esac
  done
}

# --- 2. what is here ---
find_missing $COMPONENTS $OPTIONAL_COMPONENTS
echo "Missing:${missing:- nothing}"

# --- 3. the system update runs on every run, without asking ---
echo
echo "=== system update ==="
sh -c "$(recipe system update)" || echo "  [FAIL] system update; going on"

# --- 4. offer anything missing, once ---
find_missing $COMPONENTS
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
    sh -c "$(recipe "$component" install)" || echo "  [FAIL] (see above)"
  done
  find_missing $COMPONENTS
  if [ -n "$missing" ]; then
    echo "Still missing:$missing. Fix the errors above, then re-run bash bsir.sh" >&2
    exit 1
  fi
fi

# --- 5. offer what is optional, once, and go on whatever becomes of it ---
find_missing $OPTIONAL_COMPONENTS
if [ -n "$missing" ]; then
  answer=y
  if [ "$ASSUME_YES" = 0 ]; then
    if [ -t 0 ]; then
      printf "Install%s, which is optional? [Y/n] " "$missing"
      read -r answer || answer=n
    else
      answer=n
    fi
  fi
  case "$answer" in
    [nN]*) ;;
    *)
      for component in $missing; do
        echo
        echo "=== $(recipe "$component" desc) ==="
        sh -c "$(recipe "$component" install)" || echo "  [FAIL] (see above)"
      done
      find_missing $OPTIONAL_COMPONENTS ;;
  esac
  warn_optional "$missing"
fi

# --- 6. build and test, one compiler at a time ---
# The compilers this OS names in Setup/recipes.conf, given on the command line, which wins over the preset's.
compilers() {
  echo "-DCMAKE_C_COMPILER=$(recipe "$1" c)" "-DCMAKE_CXX_COMPILER=$(recipe "$1" cxx)"
}
cd "$ROOT"
for preset in gcc-debug clang-debug; do
  echo
  echo "=== $preset ==="
  cmake --preset "$preset" $(compilers "${preset%%-*}")
  cmake --build --preset "$preset"
  ctest --preset "$preset"
done

# Build only: optimized, without the flags that stop some warnings firing. See docs/SECURITY.md.
echo
echo "=== gcc-warn ==="
cmake --preset gcc-warn $(compilers gcc)
cmake --build --preset gcc-warn

# --- 7. the window ---
DEMO="$ROOT/build/gcc-debug/jcppgui-demo"
echo
echo "Bootstrap complete. The demo: $DEMO"
if [ "$WANT_DEMO" = 1 ] && [ -n "${WAYLAND_DISPLAY:-}" ]; then
  DEMO_LOG="$ROOT/build/demo.log"
  : > "$DEMO_LOG"
  ( setsid "$DEMO" >"$DEMO_LOG" 2>&1 <"$DEMO_LOG" & )
  echo "Opened the demo. It keeps running when this terminal is closed. Its output: $DEMO_LOG"
fi
