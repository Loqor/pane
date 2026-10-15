# Pane

> **This entire project was written by Claude**, Anthropic's AI model, working
> through [Claude Code](https://claude.com/claude-code). That covers every line of C++, every
> test, the build files, the packaging and the design docs. [Loqor](https://github.com/Loqor)
> directed it: they chose what to build, made the product calls and tested the
> results. See [AUTHORS](AUTHORS).

A paint.net-style image editor for Linux (C++20 / Qt 6). It is built for tiling
Wayland compositors such as niri, so the Tools, History, Layers and Colors
panels float *inside* the main window.

## Install

### AppImage (any distro)

Download `Pane-x86_64.AppImage` from the
[Releases](https://github.com/Loqor/pane/releases) page, then run:

```sh
chmod +x Pane-x86_64.AppImage
./Pane-x86_64.AppImage
```

Qt and every library are bundled. It runs on any x86-64 distro from Debian 12 /
Ubuntu 22.10 / Fedora 37 onward (glibc ≥ 2.36). Without FUSE, run
`./Pane-x86_64.AppImage --appimage-extract-and-run`.

### Build from source (Debian/Ubuntu, Fedora, Arch, openSUSE)

```sh
git clone https://github.com/Loqor/pane && cd pane
scripts/build.sh --install     # installs deps (sudo), builds, installs to ~/.local
```

You need Qt ≥ 6.4 and CMake ≥ 3.25. The script is tested on Debian 12, Ubuntu
24.04, Fedora, Arch and openSUSE Tumbleweed. Run `scripts/build.sh --help` for options (`--no-deps`, `--test`,
`PREFIX=/usr/local`).

### Nix

```sh
nix run github:Loqor/pane
```

## Development

```sh
nix develop                  # dev shell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # ASan + UBSan
ninja -C build && (cd build && ctest --output-on-failure)
./build/src/pane image.png
scripts/appimage.sh          # build dist/Pane-x86_64.AppImage (needs Docker or Podman)
```

## Themes

Use View → Theme to pick Light, Dark, or Catppuccin Latte / Frappé / Macchiato /
Mocha, plus accent colors. For a custom theme, drop a JSON file into
`~/.config/pane/themes/`. It needs the keys `base mantle crust surface0
surface1 surface2 overlay0 overlay1 overlay2 text subtext0 subtext1 accent`,
plus optional `name` and `dark`.

## Status

All six milestones are done: foundation, tools, adjustments, effects, file
formats + performance, and polish (settings, shortcut remapping,
autosave/crash recovery, printing, tablet pressure, palettes). See
`docs/superpowers/specs/` for the roadmap. Icons are text glyphs from the
bundled Symbols Nerd Font.

## License

Pane is free software under the **GNU Lesser General Public License v3.0**
([COPYING.LESSER](COPYING.LESSER), which builds on the GPL v3 in
[COPYING](COPYING)). Source files carry `SPDX-License-Identifier: LGPL-3.0-only`.
The bundled Symbols Nerd Font has its own licenses; see
`third_party/fonts/README.md`.

Pane is not affiliated with paint.net or dotPDN LLC.
