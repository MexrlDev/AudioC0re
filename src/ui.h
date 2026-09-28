/* SPDX-License-Identifier: MIT */
#ifndef UI_H
#define UI_H
#include "core.h"

void ui_clear(u32 *fb, u32 color);
void ui_fill(u32 *fb, int x, int y, int w, int h, u32 color);
void ui_frame(u32 *fb, int x, int y, int w, int h, u32 color, int t);

void ui_str(u32 *fb, int x, int y, const char *s, u32 color, int scale);
void ui_str_center(u32 *fb, int y, const char *s, u32 color, int scale);
void ui_str_right(u32 *fb, int xr, int y, const char *s, u32 color, int scale);
int  ui_str_w(const char *s, int scale);

void ui_blit_bgra(u32 *fb, int x, int y, const u32 *src, int sw, int sh);
void ui_blit_bgra_scaled(u32 *fb, int x, int y,
                         const u32 *src, int sw, int sh,
                         int dw, int dh);
void ui_blit_bgra_alpha(u32 *fb, int x, int y,
                        const u32 *src, int sw, int sh, u8 alpha);

#endif
