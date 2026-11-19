# File: Setup/rhel_wayland.sh
# Purpose: libwayland on Red Hat Enterprise Linux. Each release has its own way, chosen by its major version, and a
#          release without one stops with a message:
#            RHEL 10  the system's wayland-devel, as on Fedora
#            RHEL 9   libwayland 1.24.0, built from its release tarball into wayland_install beside the repository.
#                     RHEL 9's own 1.21 has no wl_pointer axis_relative_direction, and cuts the connection on any event
#                     of a later wl_pointer. The tarball must have the SHA-256 below (Fedora's package has the same
#                     file). 1.24.0 is the newest that RHEL 9's meson (0.63) can build: 1.25 needs 0.64. CMakeLists.txt
#                     builds with this one on RHEL 9 alone, and refuses any other.
#
#   bash Setup/rhel_wayland.sh --check      succeed when this release's libwayland is here
#   bash Setup/rhel_wayland.sh --install    install it
#
# Setup/recipes.conf runs it for wayland on rhel.
set -u

readonly RHEL_MAJOR="$(rpm --eval '%{?rhel}')"
readonly WAYLAND_VERSION=1.24.0
readonly WAYLAND_SHA256=82892487a01ad67b334eca83b54317a7c86a03a89cfadacfef5211f11a5d0536
readonly WAYLAND_URL="https://gitlab.freedesktop.org/wayland/wayland/-/releases/$WAYLAND_VERSION/downloads/wayland-$WAYLAND_VERSION.tar.xz"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Beside the repository, as Compiler_Pins is: this file is <base>/jCppGUI/Lin/v001/Setup/rhel_wayland.sh.
PREFIX="$(realpath -m "$ROOT/../../../wayland_install")"

no_way() {
  echo "Setup/rhel_wayland.sh knows no way to get libwayland on Red Hat Enterprise Linux ${RHEL_MAJOR:-(unknown)}." >&2
  exit 1
}

check_rhel9() {
  pc="$PREFIX/lib/pkgconfig/wayland-client.pc"
  [ -f "$pc" ] && grep -qx "Version: $WAYLAND_VERSION" "$pc" && [ -x "$PREFIX/bin/wayland-scanner" ]
}

install_rhel9() {
  # meson is in CodeReady Linux Builder. expat is for wayland-scanner, libffi for the libraries.
  sudo dnf install -y meson expat-devel libffi-devel || return 1
  work="$(mktemp -d)" || return 1
  trap 'rm -rf "$work"' EXIT
  tarball="$work/wayland-$WAYLAND_VERSION.tar.xz"
  curl --proto '=https' --tlsv1.2 -sSfL --retry 3 -o "$tarball" "$WAYLAND_URL" || { echo "Could not download $WAYLAND_URL" >&2; return 1; }
  got="$(sha256sum "$tarball" | cut -d' ' -f1)"
  if [ "$got" != "$WAYLAND_SHA256" ]; then
    echo "wayland-$WAYLAND_VERSION.tar.xz has SHA-256 $got, and must have $WAYLAND_SHA256. Nothing was installed." >&2
    return 1
  fi
  tar -xJf "$tarball" -C "$work" || return 1
  meson setup "$work/build" "$work/wayland-$WAYLAND_VERSION" --prefix="$PREFIX" --libdir=lib --buildtype=release \
    -Ddocumentation=false -Dtests=false -Ddtd_validation=false || return 1
  meson compile -C "$work/build" || return 1
  # What was there is replaced whole, so nothing of an older build is left beside the new one.
  rm -rf "$PREFIX"
  meson install -C "$work/build" || return 1
  check_rhel9 || { echo "libwayland $WAYLAND_VERSION was built, and is still not found in $PREFIX." >&2; return 1; }
  echo "libwayland $WAYLAND_VERSION is in $PREFIX."
}

case "${1:-}" in
  --check)
    case "$RHEL_MAJOR" in
      10) pkg-config --exists wayland-client && [ -n "$(command -v wayland-scanner)" ] ;;
      9) check_rhel9 ;;
      *) no_way ;;
    esac ;;
  --install)
    case "$RHEL_MAJOR" in
      10) sudo dnf install -y wayland-devel ;;
      9) install_rhel9 ;;
      *) no_way ;;
    esac ;;
  *)
    echo "usage: bash Setup/rhel_wayland.sh --check|--install" >&2
    exit 2 ;;
esac
