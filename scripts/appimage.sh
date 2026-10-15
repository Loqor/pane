#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-3.0-only
# Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).
#
# Build dist/Pane-x86_64.AppImage. Needs Docker or Podman, nothing else.
#
# The build runs inside Debian 12, so the AppImage links against glibc 2.36 and
# runs on that or anything newer (Ubuntu 22.10+, Debian 12+, Fedora 37+, Arch, ...).
# Qt (with the Wayland and X11 platform plugins) and every codec library are
# bundled by linuxdeploy.
set -euo pipefail

BASE_IMAGE="${BASE_IMAGE:-debian:12}"
LINUXDEPLOY=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
LINUXDEPLOY_QT=https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage

if [ "${1:-}" != "--inside" ]; then
    cd "$(dirname "$0")/.."
    engine=$(command -v docker || command -v podman || true)
    [ -n "$engine" ] || { echo "Docker or Podman is required." >&2; exit 1; }
    mkdir -p dist
    "$engine" run --rm -v "$PWD":/src:ro -v "$PWD/dist":/dist \
        -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
        "$BASE_IMAGE" bash /src/scripts/appimage.sh --inside
    echo "dist/Pane-x86_64.AppImage ($(du -h dist/Pane-x86_64.AppImage | cut -f1))"
    exit 0
fi

# ---- inside the container ----
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends ca-certificates curl file >/dev/null

mkdir /pane
(cd /src && tar cf - --exclude='./build*' --exclude=./dist --exclude=./.git .) | (cd /pane && tar xf -)
cd /pane
scripts/build.sh --test   # installs build deps, builds, runs the test suite

DESTDIR=/AppDir cmake --install build-release --prefix /usr

cd /tmp
curl -fsSLo linuxdeploy "$LINUXDEPLOY"
curl -fsSLo linuxdeploy-plugin-qt "$LINUXDEPLOY_QT"
chmod +x linuxdeploy linuxdeploy-plugin-qt

export APPIMAGE_EXTRACT_AND_RUN=1          # no FUSE inside containers
export QMAKE=/usr/lib/qt6/bin/qmake
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so"
export EXTRA_QT_MODULES="waylandcompositor"
export LDAI_OUTPUT=Pane-x86_64.AppImage
# libOpenGL is a tiny glvnd stub that Debian's Qt links but minimal installs lack;
# linuxdeploy skips GL libraries by default, so bundle it explicitly.
./linuxdeploy --appdir /AppDir \
    --desktop-file /AppDir/usr/share/applications/pane.desktop \
    --icon-file /AppDir/usr/share/icons/hicolor/scalable/apps/pane.svg \
    --library /usr/lib/x86_64-linux-gnu/libOpenGL.so.0 \
    --plugin qt --output appimage

install -m 755 -o "$HOST_UID" -g "$HOST_GID" Pane-x86_64.AppImage /dist/
