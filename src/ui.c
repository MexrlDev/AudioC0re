/* SPDX-License-Identifier: MIT */
#include "ui.h"
#include "font_aa.h"
#include "ps_libc.h"

void ui_clear(u32 *fb, u32 color) {
    for (int i = 0; i < SCR_W * SCR_H; i++) fb[i] = color;
}

void ui_fill(u32 *fb, int x, int y, int w, int h, u32 color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        u32 *row = fb + (y + j) * SCR_W + x;
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

void ui_frame(u32 *fb, int x, int y, int w, int h, u32 color, int t) {
    ui_fill(fb, x, y, w, t, color);
    ui_fill(fb, x, y + h - t, w, t, color);
    ui_fill(fb, x, y, t, h, color);
    ui_fill(fb, x + w - t, y, t, h, color);
}

static inline int scale_to_h(int scale) {
    if (scale < 1) scale = 1;
    int h = scale * 8;
    if (h < 4) h = 4;
    return h;
}

void ui_str(u32 *fb, int x, int y, const char *s, u32 color, int scale) {
    font_aa_draw(fb, x, y, s, color, scale_to_h(scale));
}

int ui_str_w(const char *s, int scale) {
    return font_aa_width(s, scale_to_h(scale));
}

void ui_str_center(u32 *fb, int y, const char *s, u32 color, int scale) {
    int w = ui_str_w(s, scale);
    ui_str(fb, (SCR_W - w) / 2, y, s, color, scale);
}

void ui_str_right(u32 *fb, int xr, int y, const char *s, u32 color, int scale) {
    ui_str(fb, xr - ui_str_w(s, scale), y, s, color, scale);
}

void ui_blit_bgra(u32 *fb, int x, int y, const u32 *src, int sw, int sh) {
    for (int j = 0; j < sh; j++) {
        int py = y + j;
        if (py < 0 || py >= SCR_H) continue;
        const u32 *srow = src + j * sw;
        u32 *drow = fb + py * SCR_W;
        for (int i = 0; i < sw; i++) {
            int px = x + i;
            if (px < 0 || px >= SCR_W) continue;
            drow[px] = srow[i] | 0xFF000000u;
        }
    }
}

void ui_blit_bgra_scaled(u32 *fb, int x, int y,
                         const u32 *src, int sw, int sh,
                         int dw, int dh)
{
    for (int dy = 0; dy < dh; dy++) {
        int py = y + dy;
        if (py < 0 || py >= SCR_H) continue;
        int sy = dy * sh / dh;
        if (sy >= sh) sy = sh - 1;
        const u32 *srow = src + sy * sw;
        u32 *drow = fb + py * SCR_W;
        for (int dx = 0; dx < dw; dx++) {
            int px = x + dx;
            if (px < 0 || px >= SCR_W) continue;
            int sx = dx * sw / dw;
            if (sx >= sw) sx = sw - 1;
            drow[px] = srow[sx] | 0xFF000000u;
        }
    }
}

void ui_blit_bgra_alpha(u32 *fb, int x, int y,
                        const u32 *src, int sw, int sh, u8 alpha)
{
    if (alpha == 0) return;

    if (alpha == 255) {
        for (int j = 0; j < sh; j++) {
            int py = y + j;
            if (py < 0 || py >= SCR_H) continue;
            const u32 *srow = src + j * sw;
            u32 *drow = fb + py * SCR_W;
            for (int i = 0; i < sw; i++) {
                int px = x + i;
                if (px < 0 || px >= SCR_W) continue;
                u32 s = srow[i];
                u32 sa = (s >> 24) & 0xFF;
                if (sa == 0) continue;
                if (sa == 255) { drow[px] = s | 0xFF000000u; continue; }
                u32 inv = 255 - sa;
                u32 sr = (s >> 16) & 0xFF;
                u32 sg = (s >>  8) & 0xFF;
                u32 sb =  s        & 0xFF;
                u32 d  = drow[px];
                u32 dr = (d >> 16) & 0xFF;
                u32 dg = (d >>  8) & 0xFF;
                u32 db =  d        & 0xFF;
                u32 r  = (sr * sa + dr * inv) / 255;
                u32 g  = (sg * sa + dg * inv) / 255;
                u32 b  = (sb * sa + db * inv) / 255;
                drow[px] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
        return;
    }

    for (int j = 0; j < sh; j++) {
        int py = y + j;
        if (py < 0 || py >= SCR_H) continue;
        const u32 *srow = src + j * sw;
        u32 *drow = fb + py * SCR_W;
        for (int i = 0; i < sw; i++) {
            int px = x + i;
            if (px < 0 || px >= SCR_W) continue;
            u32 s = srow[i];
            u32 sa = (s >> 24) & 0xFF;
            if (sa == 0) continue;
            u32 eff = (sa * alpha) / 255;
            if (eff == 0) continue;
            u32 inv = 255 - eff;
            u32 sr = (s >> 16) & 0xFF;
            u32 sg = (s >>  8) & 0xFF;
            u32 sb =  s        & 0xFF;
            u32 d  = drow[px];
            u32 dr = (d >> 16) & 0xFF;
            u32 dg = (d >>  8) & 0xFF;
            u32 db =  d        & 0xFF;
            u32 r  = (sr * eff + dr * inv) / 255;
            u32 g  = (sg * eff + dg * inv) / 255;
            u32 b  = (sb * eff + db * inv) / 255;
            drow[px] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}
