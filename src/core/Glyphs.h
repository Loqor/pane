// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include <QString>

// Every icon in Pane is a text glyph from the bundled Symbols Nerd Font
// (Material Design Icons range). Names follow the "md-" glyph names.
namespace pane::glyphs {

inline QString g(char32_t cp) { return QString::fromUcs4(&cp, 1); }

// Tools
inline const QString RectSelect = g(0xf0489);      // md-selection
inline const QString MoveSelected = g(0xf01be);    // md-cursor_move
inline const QString MoveSelection = g(0xf0a6d);   // md-selection_drag
inline const QString Lasso = g(0xf0f03);           // md-lasso
inline const QString EllipseSelect = g(0xf0d32);   // md-selection_ellipse
inline const QString MagicWand = g(0xf1844);       // md-magic_staff
inline const QString Zoom = g(0xf0349);            // md-magnify
inline const QString Pan = g(0xf0e47);             // md-hand_back_right
inline const QString Bucket = g(0xf0266);          // md-format_color_fill
inline const QString Gradient = g(0xf174a);        // md-gradient_horizontal
inline const QString Brush = g(0xf00e3);           // md-brush
inline const QString Eraser = g(0xf01fe);          // md-eraser
inline const QString Pencil = g(0xf03eb);          // md-pencil
inline const QString ColorPicker = g(0xf020a);     // md-eyedropper
inline const QString CloneStamp = g(0xf0d39);      // md-stamper
inline const QString Recolor = g(0xf027c);         // md-format_paint
inline const QString Text = g(0xf0284);            // md-format_text
inline const QString Line = g(0xf055e);            // md-vector_line
inline const QString Shapes = g(0xf0832);          // md-shape_outline

// Actions
inline const QString NewImage = g(0xf0eed);        // md-file_plus_outline
inline const QString Open = g(0xf0770);            // md-folder_open
inline const QString Save = g(0xf0193);            // md-content_save
inline const QString SaveAll = g(0xf0194);         // md-content_save_all
inline const QString Print = g(0xf042a);           // md-printer
inline const QString Cut = g(0xf0190);             // md-content_cut
inline const QString Copy = g(0xf018f);            // md-content_copy
inline const QString Paste = g(0xf0192);           // md-content_paste
inline const QString Crop = g(0xf019e);            // md-crop
inline const QString Deselect = g(0xf0777);        // md-selection_off
inline const QString SelectAll = g(0xf0486);       // md-select_all
inline const QString InvertSelection = g(0xf0487); // md-select_inverse
inline const QString Undo = g(0xf054c);            // md-undo
inline const QString Redo = g(0xf044e);            // md-redo
inline const QString Grid = g(0xf02c1);            // md-grid
inline const QString Ruler = g(0xf046d);           // md-ruler
inline const QString Settings = g(0xf0493);        // md-cog
inline const QString Help = g(0xf0625);            // md-help_circle_outline
inline const QString Fill = g(0xf0266);            // md-format_color_fill
inline const QString Resize = g(0xf0a68);          // md-resize
inline const QString CanvasSize = g(0xf0c8e);      // md-image_size_select_large
inline const QString FlipH = g(0xf10e7);           // md-flip_horizontal
inline const QString FlipV = g(0xf10e8);           // md-flip_vertical
inline const QString RotateRight = g(0xf0467);     // md-rotate_right
inline const QString RotateLeft = g(0xf0465);      // md-rotate_left
inline const QString Flatten = g(0xf0792);         // md-arrow_collapse_down
inline const QString Image = g(0xf0976);           // md-image_outline
inline const QString ZoomIn = g(0xf06ed);          // md-magnify_plus_outline
inline const QString ZoomOut = g(0xf06ec);         // md-magnify_minus_outline
inline const QString ZoomFit = g(0xf18f5);         // md-fit_to_screen_outline

// Layers / panels
inline const QString Layers = g(0xf0328);          // md-layers
inline const QString LayerAdd = g(0xf0e4d);        // md-layers_plus
inline const QString Delete = g(0xf01b4);          // md-delete
inline const QString Duplicate = g(0xf0191);       // md-content_duplicate
inline const QString Merge = g(0xf0f5c);           // md-merge
inline const QString ArrowUp = g(0xf005d);         // md-arrow_up
inline const QString ArrowDown = g(0xf0045);       // md-arrow_down
inline const QString Properties = g(0xf0493);      // md-cog
inline const QString History = g(0xf02da);         // md-history
inline const QString Palette = g(0xf03d8);         // md-palette
inline const QString Eye = g(0xf0208);             // md-eye
inline const QString EyeOff = g(0xf0209);          // md-eye_off
inline const QString Close = g(0xf0156);           // md-close
inline const QString Swap = g(0xf04e1);            // md-swap_horizontal
inline const QString Reset = g(0xf006f);           // md-backup_restore
inline const QString First = g(0xf0600);           // md-page_first
inline const QString Last = g(0xf0601);            // md-page_last
inline const QString ChevronDown = g(0xf0140);     // md-chevron_down
inline const QString ChevronRight = g(0xf0142);    // md-chevron_right
inline const QString Plus = g(0xf0415);            // md-plus
inline const QString Minus = g(0xf0374);           // md-minus

// Adjustments / effects
inline const QString Adjust = g(0xf062e);         // md-tune
inline const QString AutoLevel = g(0xf0068);      // md-auto_fix
inline const QString BlackWhite = g(0xf02f0);     // md-image_filter_black_white
inline const QString BrightContrast = g(0xf0196); // md-contrast_box
inline const QString Curves = g(0xf0c50);         // md-chart_bell_curve
inline const QString HueSat = g(0xf0e0c);         // md-palette_outline
inline const QString Invert = g(0xf0301);         // md-invert_colors
inline const QString Levels = g(0xf0129);         // md-chart_histogram
inline const QString Posterize = g(0xf1396);      // md-circle_half_full
inline const QString Sepia = g(0xf02f8);          // md-image_filter_vintage
inline const QString Effect = g(0xf0674);         // md-creation
inline const QString FxArtistic = g(0xf03d9);      // md-palette_advanced
inline const QString FxBlur = g(0xf00b5);          // md-blur
inline const QString FxDistort = g(0xf078d);       // md-waves
inline const QString FxNoise = g(0xf0d7c);         // md-grain
inline const QString FxPhoto = g(0xf0104);         // md-camera_iris
inline const QString FxRender = g(0xf015f);        // md-cloud
inline const QString FxStylize = g(0xf02f3);       // md-image_filter_drama
inline const QString FxObject = g(0xf0495);        // md-shape_plus
inline const QString FxColor = g(0xf08b5);         // md-palette_swatch
inline const QString Reseed = g(0xf076e);          // md-dice_multiple
inline const QString RepeatFx = g(0xf0456);        // md-repeat

// Text formatting
inline const QString Bold = g(0xf0264);            // md-format_bold
inline const QString Italic = g(0xf0277);          // md-format_italic
inline const QString Underline = g(0xf0287);       // md-format_underline
inline const QString Strikeout = g(0xf0280);       // md-format_strikethrough
inline const QString AlignLeft = g(0xf0262);       // md-format_align_left
inline const QString AlignCenter = g(0xf0260);     // md-format_align_center
inline const QString AlignRight = g(0xf0263);      // md-format_align_right
inline const QString ArrowStart = g(0xf19b1);      // md-arrow_left_thin
inline const QString ArrowEnd = g(0xf19b0);        // md-arrow_right_thin

} // namespace pane::glyphs
