#ifndef LUAPORT_GFX_H
#define LUAPORT_GFX_H

#include "core.h"

/* Primitive software rendering: an 8x8 bitmap font and an integer-scale blit.
 *
 * Adapted from LuaPSX/src/ui.h + ui.c. Only the emulator-independent half was
 * taken. Specifically REMOVED:
 *   psx_blit_vram()             PlayStation VRAM conversion -- emulator specific
 *   blit_scale()                Mega Drive scaler; needs MD_FB_W/H and SCALE,
 *                               which core.h no longer defines. The GBA adapter
 *                               will want its own 240x160 equivalent at M6, and
 *                               it belongs in adapters/gba/, not here.
 *   is_rom_file(), extract_rom_name()
 *                               ROM picker helpers -- arrive at M13
 *
 * There is no font library and no UI framework here by design: the whole text
 * renderer is ~50 lines and the font table is 472 bytes. */

#define COL_BG     0xFF0F1410u
#define COL_BRAND  0xFF9BBC0Fu
#define COL_HEAD   0xFF8BAC0Fu
#define COL_LINE   0xFF306230u
#define COL_SEL    0xFFE0F8D0u
#define COL_NORM   0xFF9BBC0Fu
#define COL_DIM    0xFF306230u
#define COL_WARN   0xFFE8B040u

/* GLYPH COVERAGE -- read this before writing any on-screen string.
 *
 * font_data in tables.h is indexed [ch - 32] and covers ASCII 32..90. Several
 * entries inside that range are `{0}` placeholders and therefore render as a
 * BLANK CELL, not as the character asked for:
 *
 *     "  #  $  %  &  *  ;  @        <-- all blank, all silently
 *
 * '%' is the trap worth naming: a "FPS 59.9 %" line would lose its sign and
 * nothing would report an error. What IS available, and all Stage 3 uses:
 *
 *     A-Z   0-9   space   .   :   -   /   +   ,   (   )   <   =   >   ?   !
 *
 * Lowercase is folded to uppercase by draw_char, so lowercase input is safe and
 * simply renders uppercase. Anything outside 32..90 falls back to index 0 (a
 * space), so no string can index past the table -- an unsupported character
 * costs a blank cell, never a fault. */

int  str_len(const char *s);

/* All of these operate on a UI_W x UI_H ARGB surface. */
void ui_fill(u32 *scr, u32 color);
void draw_char(u32 *scr, int x, int y, char ch, u32 color);
void draw_str(u32 *scr, int x, int y, const char *s, u32 color);
void draw_centered(u32 *scr, int y, const char *s, u32 color);
void draw_hline(u32 *scr, int y, int x1, int x2, u32 color);

/* Added at Stage 3 for the liveness indicator. Neither exists in LuaPSX/ui.c:
   its menu never needed a filled block, because selection is shown by colouring
   text. Both clip against the surface on every edge, so no caller can walk off
   the buffer with a negative origin or an oversized extent. */
void fill_rect(u32 *scr, int x, int y, int w, int h, u32 color);

/* A `thickness`-pixel frame around the whole UI surface, drawn as four clipped
   rects. Used as the slow half of the liveness indicator. */
void draw_border(u32 *scr, int thickness, u32 color);

/* UI_W x UI_H scaled by UI_SCALE covers 1920x1080 exactly, so this writes every
   pixel of the frame: no centring arithmetic, no pillarbox, and no need to
   clear_fb() first. */
void blit_ui(u32 *fb, const u32 *src);
void clear_fb(u32 *fb);

#endif
