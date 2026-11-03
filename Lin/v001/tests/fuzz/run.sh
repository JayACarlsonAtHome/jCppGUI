# File: tests/fuzz/run.sh
# Purpose: Runs one fuzz harness from the clang-fuzz preset: bash tests/fuzz/run.sh <utf8|keymap|font|backend|jtext> <seconds|smoke>.
#          The corpus and any crash go under the build directory. The font seed is the system's default
#          sans-serif, found by fc-match and copied at run time, so no font is in the repository.
#
# System FreeType, HarfBuzz and xkbcommon are not instrumented, so libFuzzer sees coverage only in our code. Left
# alone, it shrinks a seed down to whatever reaches the same lines of ours, which removes all the data the
# libraries parse. So seeds stay whole and nothing is reduced, and mutations land on real files. A crash inside
# a library is still caught, but its coverage guides nothing, and ASan sees only what crosses its interceptors.
# The jtext harness is the exception: its parser is our own code and is instrumented, so libFuzzer's coverage means something
# there. It gets ordinary settings, and a dictionary of the grammar.
set -eu

HARNESS="$1"
HOW_LONG="$2"
TESTS_FUZZ="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$TESTS_FUZZ/../.." && pwd)"
BUILD="$ROOT/build/clang-fuzz"
WORK="$BUILD/fuzz-work/$HARNESS"
mkdir -p "$WORK/corpus" "$WORK/seeds" "$WORK/crashes" "$WORK/smoke"

case "$HARNESS" in
  utf8 | backend) ;;
  keymap) cp "$TESTS_FUZZ/seeds/keymap/minimal.xkb" "$WORK/seeds/" ;;
  jtext) cp "$TESTS_FUZZ"/seeds/jtext/* "$WORK/seeds/" ;;
  font)
    FONT="$(fc-match --format='%{file}' sans-serif)"
    [ -f "$FONT" ] || { echo "fc-match found no default font" >&2; exit 1; }
    cp "$FONT" "$WORK/seeds/seed.font"
    chmod u+w "$WORK/seeds/seed.font" ;;
  *) echo "usage: bash tests/fuzz/run.sh <utf8|keymap|font|backend|jtext> <seconds|smoke>" >&2; exit 2 ;;
esac

LIMITS="-max_len=33554432 -rss_limit_mb=4096 -timeout=60 -reduce_inputs=0 -keep_seed=1 -len_control=0"
ARTIFACTS="-artifact_prefix=$WORK/crashes/"
# libwayland logs each message it refuses to standard error, which would be most of the output of this harness.
[ "$HARNESS" = backend ] && LIMITS="$LIMITS -close_fd_mask=2"
[ "$HARNESS" = jtext ] && LIMITS="-max_len=65536 -rss_limit_mb=4096 -timeout=60 -dict=$TESTS_FUZZ/seeds/jtext.dict"
if [ "$HOW_LONG" = smoke ]; then
  exec "$BUILD/fuzz_$HARNESS" $LIMITS "$ARTIFACTS" -runs=16 "$WORK/smoke" "$WORK/seeds"
fi
exec "$BUILD/fuzz_$HARNESS" $LIMITS "$ARTIFACTS" -max_total_time="$HOW_LONG" -print_final_stats=1 "$WORK/corpus" "$WORK/seeds"
