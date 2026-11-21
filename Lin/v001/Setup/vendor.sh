# File: Setup/vendor.sh
# Purpose: Vendored copies, the way jac313 keeps jText (jac313's PROCESS.md and VENDORED_FORMAT.md): a snapshot of a
#          canonical repo's directory, copied into jCppGUI with a name prefix, and a VENDORED.jtext manifest beside
#          it that says where it came from and holds every file's SHA-256 on both sides. A vendored file is never
#          edited here: change the canonical repo, commit it, then sync. Drift is a warning, never a failure.
#
#   bash Setup/vendor.sh check [<manifest>...]       warnings only, always exit 0 (every manifest when none is named)
#   bash Setup/vendor.sh sync <manifest>             dry run: what a sync would write and delete
#   bash Setup/vendor.sh sync <manifest> --write     the forward-port: copy, transform, write a fresh manifest
#
# Where each vendored folder's source is comes from CANONICAL_DIRS below, relative to the projects dir (the folder that
# holds the git checkouts: the parent of jCppGUI's own). The manifest's source rows hold the transform and the local
# and exclude lists, and record the canonical repo, directory and commit that the last sync copied from. A first sync
# starts from a manifest that has only the transform, local and exclude rows.
#
# The transform jcppgui-prefix, for library jString (namespace jstr), in .h .hpp .cpp .inc .cppm files:
#   jstr::  ->  jgui::jstr::      where the character before jstr is not a letter, digit, _ or :
#   "jString/ and <jString/  ->  "jCppGUI/jString/ and <jCppGUI/jString/
#   a path's include/jString/  ->  include/jCppGUI/jString/
# Other files are copied unchanged. The transform none copies everything unchanged.
set -u -o pipefail

VERSION_DIR="$(cd "$(dirname "$0")/.." && pwd)" || exit 2
GIT_ROOT="$(git -C "$VERSION_DIR" rev-parse --show-toplevel)" || exit 2
PROJECTS_DIR="$(dirname "$GIT_ROOT")"
NULL_VALUE=$'\x1f'

# Where the source of each vendored folder is: its canonical directory, relative to the projects dir, keyed by the
# vendored folder's name in jCppGUI. This is the one place that names it: a canonical repo that is renamed or moved is
# changed here, and the next sync writes the new name into the manifest. The vendored folder keeps its name.
declare -rA CANONICAL_DIRS=(
  [jString]=jac331/Lin/v001
)

say() { printf 'vendor: %s\n' "$*"; }
die() { printf 'vendor: %s\n' "$*" >&2; exit 2; }

# The value of a source row: source_value <manifest> <key>. NULL reads as empty.
source_value() {
  awk -v key="$2" '
    /^=== Section: source ===/ { in_source = 1; next }
    /^=== Section: / { in_source = 0 }
    in_source && sub(/^ *1\. #\|# /, "") {
      split($0, f, "|")
      if (f[2] == key) { value = substr($0, length(f[1]) + length(f[2]) + 3); print value; exit }
    }' "$1" | tr -d "$NULL_VALUE"
}

# The files rows: "path|canonical_path|canonical_sha256|vendored_sha256", one per line.
file_rows() {
  awk '
    /^=== Section: files ===/ { in_files = 1; next }
    /^=== Section: / { in_files = 0 }
    in_files && sub(/^ *1\. #\|# /, "") { sub(/^[0-9]+\|/, ""); print }' "$1"
}

# Whether a /-separated path matches a comma list of globs. A pattern without / matches any one segment (so "build"
# matches a build folder anywhere, and "*.md" any Markdown file); one with / matches that path or anything under it.
matches_list() {
  local path="$1" list="$2" pattern segment
  local IFS=','
  for pattern in $list; do
    [ -n "$pattern" ] || continue
    if [[ "$pattern" == */* ]]; then
      # shellcheck disable=SC2053
      [[ "$path" == $pattern || "$path" == $pattern/* ]] && return 0
    else
      local IFS='/'
      for segment in $path; do
        # shellcheck disable=SC2053
        [[ "$segment" == $pattern ]] && return 0
      done
      IFS=','
    fi
  done
  return 1
}

sha_of() { sha256sum "$1" | cut -d' ' -f1; }

transform_path() {
  case "$1" in
    jcppgui-prefix) printf '%s\n' "$2" | sed -E 's#(^|/)include/jString/#\1include/jCppGUI/jString/#' ;;
    none) printf '%s\n' "$2" ;;
    *) die "unknown transform: $1" ;;
  esac
}

# Writes the transformed content of canonical file $2 to $3.
transform_file() {
  case "$1" in
    jcppgui-prefix)
      case "$2" in
        *.h | *.hpp | *.cpp | *.inc | *.cppm)
          sed -E -e 's/(^|[^[:alnum:]_:])jstr::/\1jgui::jstr::/g' -e 's#(["<])jString/#\1jCppGUI/jString/#g' "$2" > "$3" ;;
        *) cp "$2" "$3" ;;
      esac ;;
    none) cp "$2" "$3" ;;
    *) die "unknown transform: $1" ;;
  esac
}

# The canonical directory of the vendored folder that holds manifest $1, from CANONICAL_DIRS; empty when it has none.
canonical_dir_of() {
  local name; name="$(basename "$(dirname "$1")")"
  printf '%s\n' "${CANONICAL_DIRS[$name]:-}"
}

# --- sync ---
sync_manifest() {
  local manifest="$1" write="$2"
  [ -f "$manifest" ] || die "no manifest at $manifest"
  manifest="$(realpath "$manifest")"
  local root; root="$(dirname "$manifest")"
  local repo dir transform local_list exclude notes
  dir="$(canonical_dir_of "$manifest")"
  [ -n "$dir" ] || die "$manifest: its folder is not in CANONICAL_DIRS at the top of vendor.sh"
  repo="${dir%%/*}"
  transform="$(source_value "$manifest" transform)"
  local_list="$(source_value "$manifest" local)"
  exclude="$(source_value "$manifest" exclude)"
  notes="$(source_value "$manifest" notes)"
  [ -n "$transform" ] || die "$manifest: transform is needed"
  local canonical="$PROJECTS_DIR/$dir"
  [ -d "$canonical" ] || die "canonical $canonical is not there"
  local canonical_git; canonical_git="$(git -C "$canonical" rev-parse --show-toplevel)" || die "$canonical is not in a git checkout"
  local dirty=No
  [ -z "$(git -C "$canonical" status --porcelain -- .)" ] || dirty=Yes
  [ "$dirty" = No ] || [ "${ALLOW_DIRTY:-0}" = 1 ] || die "canonical $canonical has uncommitted changes: commit them first (ALLOW_DIRTY=1 overrides)"
  local commit; commit="$(git -C "$canonical_git" rev-parse --short=12 HEAD)"

  # What canonical has: its tracked files, minus the excluded.
  local relative rows="" written=0 path vendored_path canonical_sha vendored_sha
  local temporary; temporary="$(mktemp -d)"
  # shellcheck disable=SC2064
  trap "rm -rf '$temporary'" RETURN
  while IFS= read -r relative; do
    [ -n "$relative" ] || continue
    matches_list "$relative" "$exclude" && continue
    vendored_path="$(transform_path "$transform" "$relative")"
    matches_list "$vendored_path" "$local_list" && continue
    mkdir -p "$temporary/$(dirname "$vendored_path")"
    transform_file "$transform" "$canonical/$relative" "$temporary/$vendored_path"
    canonical_sha="$(sha_of "$canonical/$relative")"
    vendored_sha="$(sha_of "$temporary/$vendored_path")"
    rows+="$vendored_path|$relative|$canonical_sha|$vendored_sha"$'\n'
    if [ ! -f "$root/$vendored_path" ] || ! cmp -s "$root/$vendored_path" "$temporary/$vendored_path"; then
      say "write $vendored_path"
      written=$((written + 1))
    fi
  done < <(git -C "$canonical" ls-files -- . | sort)

  # What the old manifest listed that canonical no longer has.
  local removed=0
  while IFS='|' read -r path _ _ _; do
    [ -n "$path" ] || continue
    if ! printf '%s' "$rows" | cut -d'|' -f1 | grep -qxF "$path"; then
      say "delete $path"
      removed=$((removed + 1))
      [ "$write" = 1 ] && rm -f "$root/$path"
    fi
  done < <(file_rows "$manifest")

  if [ "$write" != 1 ]; then
    say "dry run: $written to write, $removed to delete, from $repo $commit. Add --write to do it."
    return 0
  fi
  (cd "$temporary" && find . -type f | sed 's#^\./##') | while IFS= read -r path; do
    mkdir -p "$root/$(dirname "$path")"
    cp "$temporary/$path" "$root/$path"
  done

  # The fresh manifest.
  local version="$NULL_VALUE"
  {
    printf '%s\n' "//File:    VENDORED.jtext" \
      "//Purpose: Vendored copy of $dir (do not edit the copied files here;" \
      "//         change the canonical repo, then sync). Drift is reported as warnings, never as failures." \
      "//         Written by \`bash Setup/vendor.sh sync\` (format: jac313's VENDORED_FORMAT.md)." \
      "//" "=== jText File ===" " 1. #?# VENDORED" " 3. #?# Vendored copy of $dir" "" "" \
      "=== Section: source ===" "" "" "=== Fields ===" \
      " 1. #/# id/Identity(1,1)/Not Null" " 2. #/# key/String/Not Null" " 3. #/# value/String" "=== End Fields ===" "" "" "=== Data ==="
    local id=0 key value
    for key in canonical_repo canonical_dir canonical_commit canonical_dirty canonical_version transform ported baseline local exclude notes; do
      case "$key" in
        canonical_repo) value="$repo" ;;
        canonical_dir) value="$dir" ;;
        canonical_commit) value="$commit" ;;
        canonical_dirty) value="$dirty" ;;
        canonical_version) value="$version" ;;
        transform) value="$transform" ;;
        ported) value="$(date +%F)" ;;
        baseline) value=No ;;
        local) value="${local_list:-$NULL_VALUE}" ;;
        exclude) value="${exclude:-$NULL_VALUE}" ;;
        notes) value="${notes:-$NULL_VALUE}" ;;
      esac
      id=$((id + 1))
      printf '1. #|# %s|%s|%s\n\n' "$id" "$key" "$value"
    done
    printf '%s\n' "=== End Data ===" "=== End Section ===" "" "" "=== Section: files ===" "" "" "=== Fields ===" \
      " 1. #/# id/Identity(1,1)/Not Null" \
      " 2. #/# path/String/Not Null             # vendored file, relative to the manifest dir" \
      " 3. #/# canonical_path/String/Not Null   # canonical file, relative to canonical_dir" \
      " 4. #/# canonical_sha256/String/Not Null # canonical file content when copied" \
      " 5. #/# vendored_sha256/String/Not Null  # vendored file content as written" \
      "=== End Fields ===" "" "" "=== Data ==="
    id=0
    while IFS= read -r row; do
      [ -n "$row" ] || continue
      id=$((id + 1))
      printf '1. #|# %s|%s\n\n' "$id" "$row"
    done <<<"$rows"
    printf '%s\n' "=== End Data ===" "=== End Section ===" "=== End File ==="
  } > "$manifest.tmp" && mv "$manifest.tmp" "$manifest"
  say "synced $repo $commit into ${root#"$GIT_ROOT"/}: $written written, $removed deleted"
}

# --- check ---
check_manifest() {
  local manifest; manifest="$(realpath "$1")"
  local root; root="$(dirname "$manifest")"
  local copy="${root#"$GIT_ROOT"/}"
  local repo dir local_list exclude commit warnings=0
  dir="$(canonical_dir_of "$manifest")"
  if [ -z "$dir" ]; then
    echo "vendor-check: WARN $copy: its folder is not in CANONICAL_DIRS at the top of vendor.sh"
    return 1
  fi
  repo="${dir%%/*}"
  local_list="$(source_value "$manifest" local)"
  exclude="$(source_value "$manifest" exclude)"
  commit="$(source_value "$manifest" canonical_commit)"
  local path canonical_path canonical_sha vendored_sha
  # 1. edited in place
  while IFS='|' read -r path canonical_path canonical_sha vendored_sha; do
    [ -n "$path" ] || continue
    if [ ! -f "$root/$path" ]; then
      echo "vendor-check: WARN $copy: $path is missing (fix it in $repo first, then sync)"; warnings=$((warnings + 1))
    elif [ "$(sha_of "$root/$path")" != "$vendored_sha" ]; then
      echo "vendor-check: WARN $copy: $path edited in place (fix it in $repo first, then sync)"; warnings=$((warnings + 1))
    fi
  done < <(file_rows "$manifest")
  # 2. not from canonical
  local listed; listed="$(file_rows "$manifest" | cut -d'|' -f1)"
  while IFS= read -r path; do
    [ "$path" = VENDORED.jtext ] && continue
    matches_list "$path" "$local_list" && continue
    if ! printf '%s\n' "$listed" | grep -qxF "$path"; then
      echo "vendor-check: WARN $copy: $path not from canonical"; warnings=$((warnings + 1))
    fi
  done < <(cd "$root" && find . -type f | sed 's#^\./##' | sort)
  # 3. canonical moved ahead
  local canonical="$PROJECTS_DIR/$dir"
  if [ ! -d "$canonical" ]; then
    echo "vendor-check: INFO $copy: canonical $dir is not here; nothing to compare"
  else
    local changed=0 head dirty=""
    head="$(git -C "$canonical" rev-parse --short=12 HEAD)"
    [ -z "$(git -C "$canonical" status --porcelain -- .)" ] || dirty=" dirty"
    local lines=""
    while IFS='|' read -r path canonical_path canonical_sha vendored_sha; do
      [ -n "$path" ] || continue
      if [ ! -f "$canonical/$canonical_path" ]; then lines+="  removed $canonical_path"$'\n'; changed=$((changed + 1))
      elif [ "$(sha_of "$canonical/$canonical_path")" != "$canonical_sha" ]; then lines+="  changed $canonical_path"$'\n'; changed=$((changed + 1)); fi
    done < <(file_rows "$manifest")
    local canonical_listed; canonical_listed="$(file_rows "$manifest" | cut -d'|' -f2)"
    while IFS= read -r path; do
      [ -n "$path" ] || continue
      matches_list "$path" "$exclude" && continue
      printf '%s\n' "$canonical_listed" | grep -qxF "$path" || { lines+="  added $path"$'\n'; changed=$((changed + 1)); }
    done < <(git -C "$canonical" ls-files -- .)
    if [ "$changed" -gt 0 ]; then
      echo "vendor-check: WARN $copy: canonical $repo changed $changed files since $commit (now $head$dirty)"
      printf '%s' "$lines" | sed 's/^/vendor-check: /'
      warnings=$((warnings + 1))
    else
      echo "vendor-check: OK $copy: matches $repo $commit"
    fi
  fi
  return "$warnings"
}

case "${1:-}" in
  sync)
    [ -n "${2:-}" ] || die "usage: bash Setup/vendor.sh sync <manifest> [--write]"
    sync_manifest "$2" "$([ "${3:-}" = --write ] && echo 1 || echo 0)" ;;
  check)
    shift
    manifests=("$@")
    if [ "${#manifests[@]}" -eq 0 ]; then
      mapfile -t manifests < <(find "$VERSION_DIR" -name VENDORED.jtext -not -path '*/build*' | sort)
    fi
    total=0
    for manifest in "${manifests[@]}"; do
      check_manifest "$manifest"
      total=$((total + $?))
    done
    echo "vendor-check: $total warning(s)"
    exit 0 ;;
  *)
    echo "usage: bash Setup/vendor.sh check [<manifest>...] | sync <manifest> [--write]" >&2
    exit 2 ;;
esac
