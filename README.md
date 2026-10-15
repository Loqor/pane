# Pane

A paint.net-style image editor for Linux (C++20 / Qt 6). Built for tiling
Wayland compositors such as niri: the Tools, History, Layers and Colors
panels float *inside* the main window.

## Build and run

```sh
nix run                      # build and launch
nix develop                  # dev shell (Qt 6, libpng, libjpeg-turbo, lz4, Catch2)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # ASan + UBSan
ninja -C build && (cd build && ctest --output-on-failure)
./build/src/pane image.png
```

## Themes

Use View → Theme: Light, Dark, Catppuccin Latte / Frappé / Macchiato / Mocha,
plus accent colors. For a custom theme, drop a JSON file into
`~/.config/pane/themes/`. It needs the keys `base mantle crust surface0
surface1 surface2 overlay0 overlay1 overlay2 text subtext0 subtext1 accent`,
plus optional `name` and `dark`.

## Status

All six milestones are done: foundation, tools, adjustments, effects, file formats + performance, and polish (settings, shortcut remapping, autosave/crash recovery, printing, tablet pressure, palettes). See `docs/superpowers/specs/` for the
roadmap. Icons are text glyphs from the bundled Symbols Nerd Font.
