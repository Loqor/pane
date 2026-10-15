#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-3.0-only
# Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).
#
# Build (and optionally install) Pane on a regular Linux distro, no Nix needed.
#
#   scripts/build.sh               install dependencies, then build into ./build-release
#   scripts/build.sh --no-deps     skip the package-manager step
#   scripts/build.sh --install     also install to ~/.local (or PREFIX=/usr/local)
#   scripts/build.sh --test        also build and run the test suite
#
# Needs Qt >= 6.4, CMake >= 3.25 and a C++20 compiler (GCC 12+ / Clang 16+).
# Tested on: Debian 12, Ubuntu 24.04, Fedora, Arch, openSUSE Tumbleweed.
set -euo pipefail

cd "$(dirname "$0")/.."

deps=1 install=0 test=0
for arg in "$@"; do
    case "$arg" in
        --no-deps) deps=0 ;;
        --install) install=1 ;;
        --test)    test=1 ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

PREFIX="${PREFIX:-$HOME/.local}"
BUILD_DIR="${BUILD_DIR:-build-release}"

sudo_cmd() { if [ "$(id -u)" -eq 0 ]; then "$@"; else sudo "$@"; fi; }

install_deps() {
    local id like
    id=$( . /etc/os-release && echo "${ID:-}")
    like=$( . /etc/os-release && echo "${ID_LIKE:-}")
    case " $id $like " in
        *" debian "*|*" ubuntu "*)
            sudo_cmd apt-get update
            sudo_cmd apt-get install -y --no-install-recommends \
                build-essential ca-certificates git cmake ninja-build pkg-config \
                qt6-base-dev qt6-base-dev-tools qt6-wayland libgl-dev \
                libpng-dev libjpeg-dev liblz4-dev zlib1g-dev libwebp-dev \
                libavif-dev libheif-dev libtiff-dev libgif-dev catch2
            ;;
        *" fedora "*|*" rhel "*|*" centos "*)
            sudo_cmd dnf install -y \
                gcc-c++ git cmake ninja-build pkgconf-pkg-config \
                qt6-qtbase-devel qt6-qtwayland \
                libpng-devel libjpeg-turbo-devel lz4-devel zlib-devel libwebp-devel \
                libavif-devel libheif-devel libtiff-devel giflib-devel catch2-devel
            ;;
        *" arch "*)
            sudo_cmd pacman -Syu --needed --noconfirm \
                base-devel cmake ninja pkgconf qt6-base qt6-wayland \
                libpng libjpeg-turbo lz4 zlib libwebp libavif libheif libtiff giflib catch2
            ;;
        *" suse "*|*" opensuse "*)
            sudo_cmd zypper --non-interactive install \
                gcc-c++ git cmake ninja pkg-config qt6-base-devel qt6-wayland \
                libpng16-devel libpng16-compat-devel libjpeg8-devel liblz4-devel zlib-devel libwebp-devel \
                libavif-devel libheif-devel libtiff-devel giflib-devel Catch2-devel
            ;;
        *)
            echo "Unknown distro '$id'. Install Qt 6 (base + wayland), CMake, Ninja, pkg-config and the"
            echo "dev packages for libpng, libjpeg, lz4, zlib, libwebp, libavif, libheif, libtiff, giflib,"
            echo "then re-run with --no-deps."
            exit 1
            ;;
    esac
}

[ "$deps" -eq 1 ] && install_deps

testing=OFF
[ "$test" -eq 1 ] && testing=ON

cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_TESTING="$testing"
cmake --build "$BUILD_DIR"

if [ "$test" -eq 1 ]; then
    (cd "$BUILD_DIR" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure)
fi

if [ "$install" -eq 1 ]; then
    if [ -w "$PREFIX" ] || [ ! -e "$PREFIX" ]; then
        cmake --install "$BUILD_DIR"
    else
        sudo_cmd cmake --install "$BUILD_DIR"
    fi
    echo
    echo "Installed to $PREFIX/bin/pane"
    case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) echo "Note: $PREFIX/bin is not on your PATH." ;; esac
else
    echo
    echo "Built: $BUILD_DIR/src/pane   (run scripts/build.sh --install to install it)"
fi
