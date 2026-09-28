/* SPDX-License-Identifier: MIT */
#include "loading.h"
#include "ui.h"
#include "ps_libc.h"

#define LOADING_IMG_W 200
#define LOADING_IMG_H 200

static const s16 spin_cos[16] = {
    256,  237,  181,   98,    0,  -98, -181, -237,
   -256, -237, -181,  -98,    0,   98,  181,  237
};
static const s16 spin_sin[16] = {
      0,   98,  181,  237,  256,  237,  181,   98,
      0,  -98, -181, -237, -256, -237, -181,  -98
};

static u32 *g_loading_img    = 0;
static int  g_loading_tried  = 0;

static u32 *try_load_loading(struct ctx *c, const char *lib_root) {
    char path[256];
    int p = 0;
    for (int i = 0; lib_root[i] && p < 230; i++) path[p++] = lib_root[i];
    const char *tail = "/image/loading.bin";
    for (int i = 0; tail[i] && p < 254; i++) path[p++] = tail[i];
    path[p] = 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0;
    u32 bytes = LOADING_IMG_W * LOADING_IMG_H * 4;
    u32 *img = (u32*)malloc(bytes);
    if (!img) { fclose(f); return 0; }
    size_t got = fread(img, 1, bytes, f);
    fclose(f);
    if (got < bytes) { free(img); return 0; }
    return img;
}

static void blit_rotated(u32 *fb, int cx, int cy,
                         const u32 *src, int w, int h, int step)
{
    int c  = spin_cos[step & 15];
    int s  = spin_sin[step & 15];
    int hw = w / 2;
    int hh = h / 2;
    int r  = (hw > hh ? hw : hh) + 4;

    for (int dy = -r; dy <= r; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= SCR_H) continue;
        u32 *drow = fb + py * SCR_W;
        for (int dx = -r; dx <= r; dx++) {
            int px = cx + dx;
            if (px < 0 || px >= SCR_W) continue;
            int sx256 =  c * dx + s * dy;
            int sy256 = -s * dx + c * dy;
            int sx = hw + (sx256 >> 8);
            int sy = hh + (sy256 >> 8);
            if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
            u32 v = src[sy * w + sx];
            u32 sa = (v >> 24) & 0xFF;
            if (sa == 0) continue;
            drow[px] = v | 0xFF000000u;
        }
    }
}

static void draw_dot_spinner(u32 *fb, int cx, int cy, int frame) {
    static const s16 dot_x[12] = { 60,  52,  30,   0, -30, -52,
                                  -60, -52, -30,   0,  30,  52 };
    static const s16 dot_y[12] = {  0,  30,  52,  60,  52,  30,
                                    0, -30, -52, -60, -52, -30 };
    int head = frame & 15;

    for (int i = 0; i < 12; i++) {
        int idx = (i + head) % 12;
        int px = cx + dot_x[i];
        int py = cy + dot_y[i];
        int bright = 255 - idx * 18;
        if (bright < 40) bright = 40;
        u32 col = 0xFF000000u
                | ((u32)(bright * 200 / 255) << 16)
                | ((u32)(bright *  40 / 255) << 8)
                |  (u32)(bright *  40 / 255);
        for (int dy = -6; dy <= 6; dy++) {
            int yy = py + dy;
            if (yy < 0 || yy >= SCR_H) continue;
            u32 *row = fb + yy * SCR_W;
            for (int dx = -6; dx <= 6; dx++) {
                if (dx * dx + dy * dy > 36) continue;
                int xx = px + dx;
                if (xx < 0 || xx >= SCR_W) continue;
                row[xx] = col;
            }
        }
    }
}

void loading_show(struct ctx *c, const char *lib_root, int frame) {
    if (!g_loading_tried) {
        g_loading_tried = 1;
        g_loading_img = try_load_loading(c, lib_root);
        if (g_loading_img) {
            ulog(c, "[ac] loading: image cached\n");
        } else {
            ulog(c, "[ac] loading: using procedural spinner\n");
        }
    }

    u32 *fb = c->fbs[c->active];
    ui_clear(fb, RGB(10, 10, 12));

    if (g_loading_img) {
        int lx = (SCR_W - LOADING_IMG_W) / 2;
        int ly = (SCR_H - LOADING_IMG_H) / 2 - 40;
        blit_rotated(fb, lx + LOADING_IMG_W / 2, ly + LOADING_IMG_H / 2,
                     g_loading_img, LOADING_IMG_W, LOADING_IMG_H, frame);
    } else {
        draw_dot_spinner(fb, SCR_W / 2, SCR_H / 2 - 40, frame);
    }

    ui_str_center(fb, SCR_H / 2 + 160, "Loading...",
                  RGB(210, 210, 220), 5);
}
