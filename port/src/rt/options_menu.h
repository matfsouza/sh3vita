// Options menu pages. Pure: tested by tests/test_options_menu.py.
// The options menu is one list of rows, [0xbcc178] the selected one and [0xbcc182 + row] "row shown": rows 1..10 are
// the Options page, 11..21 Extra Options, 22..28 Configuration (resolution, rendering resolution, gamma, lens flare,
// shadows: settings the port fixes, which could break the picture on the Vita). 0x5e3560 / 0x5e35e0 turn the page
// (game_fixes.c turns once more to skip Configuration). In Extra Options, Display Mode (a rendering setting) is
// turned off; the gameplay rows stay (blood colour and amount, walk / run, map zoom, noise effect, low health
// indicator, and the ammunition rows unlocked after the first ending).
#pragma once

#define OPT_ROW_DISPLAY_MODE 16

static inline int opt_page(int row) { return row <= 10 ? 0 : row <= 21 ? 1 : 2; }
static inline int opt_hidden_page(int row) { return opt_page(row) == 2; }
static inline int opt_blocked_row(int row) { return row == OPT_ROW_DISPLAY_MODE; }
static inline void opt_block_rows(unsigned char *shown) { shown[OPT_ROW_DISPLAY_MODE] = 0; }
