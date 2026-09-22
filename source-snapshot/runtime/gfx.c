#include "gfx.h"
#include "tables.h"

/* Adapted from LuaPSX/src/ui.c lines 1-75. The bodies below are byte-identical
   to that file; only the emulator-specific functions were left behind (see
   gfx.h for the list and the reasons). */

int str_len(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

void ui_fill(u32 *scr, u32 color) {
    for (int i = 0; i < UI_W * UI_H; i++) scr[i] = color;
}

void clear_fb(u32 *fb) {
    for (int i = 0; i < SCR_W * SCR_H; i++) fb[i] = 0xFF000000u;
}

/* The font covers ASCII 32..90 -- space through 'Z'. Lowercase is folded to
   uppercase before the lookup, so lowercase input is SAFE and simply renders
   as uppercase. Anything still outside the range falls back to index 0, which
   is a space, so no input can index past the table. */
void draw_char(u32 *scr, int x, int y, char ch, u32 color) {
    int idx = 0;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch >= 32 && ch <= 90) idx = ch - 32;
    const u8 *glyph = font_data[idx];

    for (int r = 0; r < 8; r++) {
        u8 bits = glyph[r];
        for (int c = 0; c < 8; c++) {
            if (bits & (0x80 >> c)) {
                int px = x + c, py = y + r;
                if (px >= 0 && px < UI_W && py >= 0 && py < UI_H)
                    scr[py * UI_W + px] = color;
            }
        }
    }
}

void draw_str(u32 *scr, int x, int y, const char *s, u32 color) {
    while (*s) {
        draw_char(scr, x, y, *s, color);
        x += 8;
        s++;
    }
}

void draw_centered(u32 *scr, int y, const char *s, u32 color) {
    int x = (UI_W - str_len(s) * 8) / 2;
    if (x < 0) x = 0;
    draw_str(scr, x, y, s, color);
}

void draw_hline(u32 *scr, int y, int x1, int x2, u32 color) {
    if (y < 0 || y >= UI_H) return;
    for (int x = x1; x < x2 && x < UI_W; x++) {
        if (x >= 0) scr[y * UI_W + x] = color;
    }
}

/* ---- added at Stage 3 ---------------------------------------------------
   Clipping is done ONCE against the surface bounds rather than per pixel: the
   inner loops then run without a branch, which matters because fill_rect is on
   the per-frame path. Every edge is clamped, including negative origins, so a
   caller cannot address outside the UI_W x UI_H surface. */
void fill_rect(u32 *scr, int x, int y, int w, int h, u32 color) {
    if (w <= 0 || h <= 0) return;

    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > UI_W) x1 = UI_W;
    if (y1 > UI_H) y1 = UI_H;
    if (x0 >= x1 || y0 >= y1) return;

    for (int py = y0; py < y1; py++) {
        u32 *row = &scr[py * UI_W];
        for (int px = x0; px < x1; px++) row[px] = color;
    }
}

void draw_border(u32 *scr, int thickness, u32 color) {
    if (thickness <= 0) return;
    fill_rect(scr, 0, 0, UI_W, thickness, color);                    /* top    */
    fill_rect(scr, 0, UI_H - thickness, UI_W, thickness, color);     /* bottom */
    fill_rect(scr, 0, 0, thickness, UI_H, color);                    /* left   */
    fill_rect(scr, UI_W - thickness, 0, thickness, UI_H, color);     /* right  */
}

/* UI_W * UI_SCALE == SCR_W and UI_H * UI_SCALE == SCR_H exactly, so this writes
   every pixel of the frame: no centring arithmetic, no pillarbox, and no need
   to clear_fb() first. */
void blit_ui(u32 *fb, const u32 *src) {
    for (int uy = 0; uy < UI_H; uy++) {
        const u32 *srow = &src[uy * UI_W];
        for (int dy = 0; dy < UI_SCALE; dy++) {
            u32 *row = &fb[(uy * UI_SCALE + dy) * SCR_W];
            for (int ux = 0; ux < UI_W; ux++) {
                u32 c = srow[ux];
                u32 *out = &row[ux * UI_SCALE];
                for (int dx = 0; dx < UI_SCALE; dx++) out[dx] = c;
            }
        }
    }
}
