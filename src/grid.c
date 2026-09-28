/* SPDX-License-Identifier: MIT */
#include "app.h"
#include "library.h"
#include "ui.h"
#include "volume.h"
#include "marquee.h"
#include "ps_libc.h"

#define COL_BG        RGB(15,15,15)
#define COL_PANEL     RGB(24,24,28)
#define COL_TITLE     RGB(240,240,240)
#define COL_TITLE_DIM RGB(160,160,170)
#define COL_ACCENT    RGB(200,30,30)
#define COL_RING      RGB(255,255,255)
#define COL_RING_DIM  RGB(90,90,100)
#define COL_OVERLAY   0xB0000000u
#define COL_DIM_BG    0xCC000000u

static void draw_tile_overlay(u32 *fb, int tx, int ty,
                              const char *title, const char *artist,
                              int focused, u64 elapsed_ms)
{
    ui_fill(fb, tx, ty + TILE_H - 66, TILE_W, 66, COL_OVERLAY);

    int inner_x = tx + 10;
    int inner_w = TILE_W - 20;

    int title_y = ty + TILE_H - 58;
    int title_h = 30;

    if (title && title[0]) {
        int tw = ui_str_w(title, 3);
        if (tw > inner_w) {
            if (focused) {
                int off = marquee_offset(tw, inner_w, elapsed_ms);
                font_aa_set_clip(inner_x, title_y - 2,
                                 inner_w, title_h);
                ui_str(fb, inner_x - off, title_y,
                       title, COL_TITLE, 3);
                font_aa_clear_clip();
            } else {
                char t1[24]; int n1 = 0;
                for (int i = 0; title[i] && n1 < 22; i++)
                    t1[n1++] = title[i];
                t1[n1++] = '.'; t1[n1++] = '.';
                t1[n1] = 0;
                ui_str(fb, inner_x, title_y, t1, COL_TITLE, 3);
            }
        } else {
            ui_str(fb, inner_x, title_y, title, COL_TITLE, 3);
        }
    }

    if (artist && artist[0]) {
        int artist_y = ty + TILE_H - 30;
        int artist_h = 24;
        int aw = ui_str_w(artist, 2);
        if (aw > inner_w) {
            if (focused) {
                int off = marquee_offset(aw, inner_w, elapsed_ms);
                font_aa_set_clip(inner_x, artist_y - 2,
                                 inner_w, artist_h);
                ui_str(fb, inner_x - off, artist_y,
                       artist, COL_TITLE_DIM, 2);
                font_aa_clear_clip();
            } else {
                char a1[28]; int m1 = 0;
                for (int i = 0; artist[i] && m1 < 26; i++)
                    a1[m1++] = artist[i];
                a1[m1++] = '.'; a1[m1++] = '.';
                a1[m1] = 0;
                ui_str(fb, inner_x, artist_y, a1, COL_TITLE_DIM, 2);
            }
        } else {
            ui_str(fb, inner_x, artist_y, artist, COL_TITLE_DIM, 2);
        }
    }
}

static void draw_tile(u32 *fb, int tx, int ty,
                      const u32 *cover, int focused)
{
    if (cover) {
        ui_blit_bgra(fb, tx, ty, cover, TILE_W, TILE_H);
    } else {
        ui_fill(fb, tx, ty, TILE_W, TILE_H, RGB(24,24,32));
    }

    if (focused) {
        ui_frame(fb, tx - 5, ty - 5, TILE_W + 10, TILE_H + 10,
                 COL_RING, 4);
        ui_frame(fb, tx - 1, ty - 1, TILE_W + 2, TILE_H + 2,
                 0xFFDDDDDDu, 1);
    } else {
        ui_frame(fb, tx - 1, ty - 1, TILE_W + 2, TILE_H + 2,
                 COL_RING_DIM, 1);
    }
}

static void draw_credits_popup(u32 *fb) {
    const int box_w = 1000;
    const int box_h = 760;
    const int x0 = (SCR_W - box_w) / 2;
    const int y0 = (SCR_H - box_h) / 2;

    ui_fill(fb, 0, 0, SCR_W, SCR_H, COL_DIM_BG);

    ui_fill(fb, x0, y0, box_w, box_h, RGB(20, 20, 24));
    ui_frame(fb, x0, y0, box_w, box_h, COL_ACCENT, 6);
    ui_frame(fb, x0 + 12, y0 + 12, box_w - 24, box_h - 24,
             RGB(80, 80, 90), 2);

    ui_str_center(fb, y0 + 70, "CREDITS", RGB(255, 255, 255), 8);
    ui_fill(fb, x0 + 120, y0 + 190, box_w - 240, 2, COL_ACCENT);

    ui_str_center(fb, y0 + 240, "Programming / Coding",
                  RGB(160, 160, 180), 4);
    ui_str_center(fb, y0 + 300, "MexrlDev", RGB(255, 255, 255), 6);

    ui_fill(fb, x0 + 120, y0 + 400, box_w - 240, 2, COL_ACCENT);

    ui_str_center(fb, y0 + 440, "Special Thanks",
                  RGB(160, 160, 180), 4);
    ui_str_center(fb, y0 + 500, "Egycnq", RGB(240, 240, 240), 5);
    ui_str_center(fb, y0 + 570, "Gezine", RGB(240, 240, 240), 5);

    ui_str_center(fb, y0 + box_h - 60,
                  "Press O to close",
                  RGB(140, 140, 160), 3);
}

static void draw_header(u32 *fb, grid_state *g) {
    ui_fill(fb, 0, 0, SCR_W, 96, COL_PANEL);
    ui_fill(fb, 0, 96, SCR_W, 4, COL_ACCENT);
    ui_str(fb, 60, 26, "AudioC0re", RGB(255,255,255), 5);
    ui_str(fb, 60 + ui_str_w("AudioC0re ", 5), 26,
           "Library", COL_ACCENT, 5);

    char cnt[32]; int p = 0;
    p += s_itoa(cnt + p, (int)g->lib->count);
    const char *suffix = " tracks";
    for (int i = 0; suffix[i]; i++) cnt[p++] = suffix[i];
    cnt[p] = 0;
    ui_str_right(fb, SCR_W - 60, 40, cnt, COL_TITLE_DIM, 3);
}

static void draw_footer(u32 *fb) {
    ui_fill(fb, 0, GRID_BOTTOM, SCR_W, SCR_H - GRID_BOTTOM, COL_PANEL);
    ui_fill(fb, 0, GRID_BOTTOM, SCR_W, 4, COL_ACCENT);
    ui_str(fb, 60, GRID_BOTTOM + 22,
           "D-Pad: Navigate   L1/R1: Page   L2/R2: Volume   "
           "Square: Mute   X: Play   Touchpad: Credits   Options: Exit",
           COL_TITLE_DIM, 3);
}

void grid_draw(grid_state *g, u32 *fb) {
    ui_clear(fb, COL_BG);

    grid_ensure_cache(g);

    u64 now = get_uptime_ms(&G_CTX);
    if (g->marquee_epoch_ms == 0) g->marquee_epoch_ms = now;
    u64 elapsed = now - g->marquee_epoch_ms;

    int count = (int)g->lib->count;
    int total_rows = (count + GRID_COLS - 1) / GRID_COLS;

    int grid_w = GRID_COLS * TILE_W + (GRID_COLS - 1) * TILE_PAD;
    int gx0 = (SCR_W - grid_w) / 2;

    int first_row = g->view_first_row;
    if (first_row < 0) first_row = 0;

    int draw_rows = VISIBLE_ROWS;

    for (int ri = 0; ri < draw_rows; ri++) {
        int row = first_row + ri;
        if (row >= total_rows) break;

        int ty = GRID_TOP + ri * ROW_H;
        if (ty + TILE_H > GRID_BOTTOM) break;

        for (int col = 0; col < GRID_COLS; col++) {
            int idx = row * GRID_COLS + col;
            if (idx >= count) continue;

            int tx = gx0 + col * (TILE_W + TILE_PAD);

            const u32 *cover = 0;
            if (g->cache_valid) {
                int ci = (row - g->cache_first_row) * GRID_COLS + col;
                if (ci >= 0 && ci < CACHE_TILES && g->cache_ok[ci]) {
                    cover = g->cover_cache +
                            (u64)ci * TILE_W * TILE_H;
                }
            }

            int focused = (idx == g->cursor) && !g->show_credits;

            draw_tile(fb, tx, ty, cover, focused);

            ac_entry *e = &g->lib->entries[idx];
            draw_tile_overlay(fb, tx, ty,
                              e->title, e->artist,
                              focused, elapsed);
        }
    }

    draw_header(fb, g);
    draw_footer(fb);

    if (g->show_credits)
        draw_credits_popup(fb);
}

int grid_input(grid_state *g, u32 raw, u32 pressed, int *launch_idx) {
    *launch_idx = -2;

    if (g->show_credits) {
        if (pressed & (DS_CIRCLE | DS_CROSS | DS_TOUCHPAD))
            g->show_credits = 0;
        return 1;
    }

    if (pressed & DS_TOUCHPAD) {
        g->show_credits = 1;
        return 1;
    }

    volume_handle_hold(&G_CTX, raw, pressed, DS_R2, DS_L2, 5);
    if (pressed & DS_SQUARE) volume_mute_toggle(&G_CTX);

    int count = (int)g->lib->count;
    if (count == 0) return 1;

    int total_rows = (count + GRID_COLS - 1) / GRID_COLS;
    int cursor_row = g->cursor / GRID_COLS;
    int cursor_col = g->cursor % GRID_COLS;

    int row = cursor_row;
    int col = cursor_col;
    int cursor_changed = 0;

    if (pressed & DS_LEFT)  { col--; cursor_changed = 1; }
    if (pressed & DS_RIGHT) { col++; cursor_changed = 1; }
    if (pressed & DS_UP)    { row--; cursor_changed = 1; }
    if (pressed & DS_DOWN)  { row++; cursor_changed = 1; }

    if (cursor_changed) {
        if (col < 0) col = 0;
        if (col >= GRID_COLS) col = GRID_COLS - 1;
        if (row < 0) row = 0;
        if (row >= total_rows) row = total_rows - 1;

        int new_idx = row * GRID_COLS + col;
        if (new_idx >= count) new_idx = count - 1;
        if (new_idx != g->cursor) {
            g->cursor = new_idx;
            g->marquee_epoch_ms = get_uptime_ms(&G_CTX);
        }
    }

    if (pressed & DS_L1) {
        g->view_first_row -= VISIBLE_ROWS;
        if (g->view_first_row < 0) g->view_first_row = 0;
        g->marquee_epoch_ms = get_uptime_ms(&G_CTX);
    }
    if (pressed & DS_R1) {
        int max_first = total_rows - VISIBLE_ROWS;
        if (max_first < 0) max_first = 0;
        g->view_first_row += VISIBLE_ROWS;
        if (g->view_first_row > max_first)
            g->view_first_row = max_first;
        g->marquee_epoch_ms = get_uptime_ms(&G_CTX);
    }

    cursor_row = g->cursor / GRID_COLS;
    if (cursor_row < g->view_first_row)
        g->view_first_row = cursor_row;
    if (cursor_row >= g->view_first_row + VISIBLE_ROWS)
        g->view_first_row = cursor_row - VISIBLE_ROWS + 1;

    int max_first = total_rows - VISIBLE_ROWS;
    if (max_first < 0) max_first = 0;
    if (g->view_first_row > max_first)
        g->view_first_row = max_first;
    if (g->view_first_row < 0)
        g->view_first_row = 0;

    if (pressed & DS_CROSS) { *launch_idx = g->cursor; return 1; }
    return 1;
}
