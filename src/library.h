/* SPDX-License-Identifier: MIT */
#ifndef LIBRARY_H
#define LIBRARY_H
#include "core.h"
#include "app.h"
#include "manifest.h"

#define GRID_COLS     4
#define TILE_W        270
#define TILE_H        270
#define TILE_PAD      16
#define ROW_H         (TILE_H + TILE_PAD)

#define GRID_TOP      150
#define GRID_BOTTOM   (SCR_H - 70)
#define GRID_VIEW_H   (GRID_BOTTOM - GRID_TOP)

#define VISIBLE_ROWS  (GRID_VIEW_H / ROW_H)
#define CACHE_ROWS    (VISIBLE_ROWS + 2)
#define CACHE_TILES   (CACHE_ROWS * GRID_COLS)

typedef struct {
    ac_library *lib;
    int         cursor;
    int         view_first_row;
    int         show_credits;
    u64         marquee_epoch_ms;

    u32        *cover_cache;
    int         cache_first_row;
    u8          cache_ok[CACHE_TILES];
    int         cache_valid;
} grid_state;

void grid_init(grid_state *g, ac_library *lib);
void grid_free(grid_state *g);
int  grid_ensure_cache(grid_state *g);
void grid_draw(grid_state *g, u32 *fb);
int  grid_input(grid_state *g, u32 raw, u32 pressed, int *launch_idx);

#endif
