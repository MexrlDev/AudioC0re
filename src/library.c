/* SPDX-License-Identifier: MIT */
#include "library.h"
#include "ps_libc.h"

void grid_init(grid_state *g, ac_library *lib) {
    memset(g, 0, sizeof(*g));
    g->lib = lib;
    g->view_first_row = 0;
    g->cache_first_row = -1;
    g->cache_valid = 0;
}

void grid_free(grid_state *g) {
    if (g->cover_cache) free(g->cover_cache);
    g->cover_cache = 0;
    g->cache_valid = 0;
    g->cache_first_row = -1;
}

static void load_one_tile(grid_state *g, int idx, u32 *slot) {
    ac_entry *e = &g->lib->entries[idx];
    char path[240];
    ac_full_path(g->lib, e->cover, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        char dpath[240];
        ac_full_path(g->lib, "cover/_default.bin", dpath, sizeof(dpath));
        f = fopen(dpath, "r");
    }
    if (!f) return;

    u32 src_w = g->lib->cover_w ? g->lib->cover_w : 500;
    u32 src_h = g->lib->cover_h ? g->lib->cover_h : 500;
    u32 src_bytes = src_w * src_h * 4;

    u32 *src = (u32 *)malloc(src_bytes);
    if (!src) { fclose(f); return; }
    size_t got = fread(src, 1, src_bytes, f);
    fclose(f);
    if (got < src_bytes) { free(src); return; }

    for (int dy = 0; dy < TILE_H; dy++) {
        int sy = dy * (int)src_h / TILE_H;
        u32 *srow = src + (u64)sy * src_w;
        u32 *drow = slot + (u64)dy * TILE_W;
        for (int dx = 0; dx < TILE_W; dx++) {
            int sx = dx * (int)src_w / TILE_W;
            drow[dx] = srow[sx];
        }
    }
    free(src);
}

static void load_cache_row(grid_state *g, int row, int slot) {
    if (slot < 0 || slot >= CACHE_ROWS) return;

    int count = (int)g->lib->count;
    for (int col = 0; col < GRID_COLS; col++) {
        int ci = slot * GRID_COLS + col;
        g->cache_ok[ci] = 0;

        int idx = row * GRID_COLS + col;
        if (idx < 0 || idx >= count) continue;

        u32 *dest = g->cover_cache +
                    ((u64)ci * TILE_W * TILE_H);
        load_one_tile(g, idx, dest);
        g->cache_ok[ci] = 1;
    }
}

int grid_ensure_cache(grid_state *g) {
    if (!g->lib || !g->lib->entries) return -1;

    int count = (int)g->lib->count;
    int total_rows = (count + GRID_COLS - 1) / GRID_COLS;

    if (total_rows < 1) total_rows = 1;

    int first = g->view_first_row - 1;
    if (first < 0) first = 0;
    if (first + CACHE_ROWS > total_rows) {
        first = total_rows - CACHE_ROWS;
        if (first < 0) first = 0;
    }

    if (!g->cover_cache) {
        u64 bytes = (u64)CACHE_TILES * TILE_W * TILE_H * 4;
        g->cover_cache = (u32 *)malloc((size_t)bytes);
        if (!g->cover_cache) {
            printf("ac: cover cache alloc failed (%u bytes)\n",
                   (unsigned)bytes);
            return -1;
        }
        g->cache_valid = 0;
    }

    if (g->cache_valid && first == g->cache_first_row) return 0;

    if (g->cache_valid && first == g->cache_first_row + 1) {
        u64 row_bytes = (u64)GRID_COLS * TILE_W * TILE_H * 4;

        memmove(g->cover_cache,
                g->cover_cache + (u64)GRID_COLS * TILE_W * TILE_H,
                (u64)(CACHE_ROWS - 1) * row_bytes);

        for (int r = 0; r < CACHE_ROWS - 1; r++) {
            for (int c = 0; c < GRID_COLS; c++) {
                g->cache_ok[r * GRID_COLS + c] =
                    g->cache_ok[(r + 1) * GRID_COLS + c];
            }
        }
        load_cache_row(g, first + CACHE_ROWS - 1, CACHE_ROWS - 1);
        g->cache_first_row = first;
        return 0;
    }

    if (g->cache_valid && first == g->cache_first_row - 1) {
        u64 row_bytes = (u64)GRID_COLS * TILE_W * TILE_H * 4;
        memmove(g->cover_cache + (u64)GRID_COLS * TILE_W * TILE_H,
                g->cover_cache,
                (u64)(CACHE_ROWS - 1) * row_bytes);

        for (int r = CACHE_ROWS - 1; r > 0; r--) {
            for (int c = 0; c < GRID_COLS; c++) {
                g->cache_ok[r * GRID_COLS + c] =
                    g->cache_ok[(r - 1) * GRID_COLS + c];
            }
        }
        load_cache_row(g, first, 0);
        g->cache_first_row = first;
        return 0;
    }

    for (int r = 0; r < CACHE_ROWS; r++) {
        load_cache_row(g, first + r, r);
    }
    g->cache_first_row = first;
    g->cache_valid = 1;
    printf("ac: grid cache rows %d..%d\n", first, first + CACHE_ROWS - 1);
    return 0;
}
