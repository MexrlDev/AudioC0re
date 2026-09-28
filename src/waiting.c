/* SPDX-License-Identifier: MIT */
#include "waiting.h"
#include "ui.h"
#include "ps_libc.h"

#define RECV_BAR_W 1000
#define RECV_BAR_H 16

void waiting_draw(struct ctx *c, const char *msg, int dots) {
    u32 *fb = c->fbs[c->active];
    ui_clear(fb, RGB(10, 10, 14));

    ui_str_center(fb, 380, "AudioC0re", RGB(230, 230, 240), 8);
    ui_str_center(fb, 500, msg,        RGB(180, 180, 200), 4);

    int n = dots % 4;
    int dot_w = ui_str_w(".", 6);
    if (dot_w < 1) dot_w = 1;
    int total_w = dot_w * 3;
    int start_x = (SCR_W - total_w) / 2;

    for (int i = 0; i < 3; i++) {
        if (i < n)
            ui_str(fb, start_x + i * dot_w, 590, ".", RGB(200, 30, 30), 6);
    }
}

void waiting_progress(struct ctx *c, const char *msg,
                      int idx, const char *file)
{
    u32 *fb = c->fbs[c->active];
    ui_clear(fb, RGB(10, 10, 14));

    ui_str_center(fb, 340, "AudioC0re", RGB(230, 230, 240), 8);
    ui_str_center(fb, 460, msg, RGB(180, 180, 200), 4);

    char buf[32];
    int p = s_itoa(buf, idx);
    buf[p] = 0;
    ui_str_center(fb, 570, buf, RGB(200, 30, 30), 6);

    if (file) ui_str_center(fb, 680, file, RGB(150, 150, 170), 3);
}

void waiting_receive(struct ctx *c, const char *file, int idx,
                     u64 received, u64 total)
{
    u32 *fb = c->fbs[c->active];
    ui_clear(fb, RGB(10, 10, 14));

    ui_str_center(fb, 340, "AudioC0re", RGB(230, 230, 240), 8);
    ui_str_center(fb, 460, "Receiving...", RGB(180, 180, 200), 4);

    if (idx > 0) {
        char buf[32];
        int p = s_itoa(buf, idx);
        buf[p] = 0;
        ui_str_center(fb, 560, buf, RGB(200, 30, 30), 5);
    }

    if (file) ui_str_center(fb, 640, file, RGB(150, 150, 170), 3);

    int bar_x = (SCR_W - RECV_BAR_W) / 2;
    int bar_y = 720;

    ui_fill(fb, bar_x, bar_y, RECV_BAR_W, RECV_BAR_H, RGB(40, 40, 48));

    if (total > 0) {
        int fill = (int)((u64)RECV_BAR_W * received / total);
        if (fill > RECV_BAR_W) fill = RECV_BAR_W;
        if (fill > 0)
            ui_fill(fb, bar_x, bar_y, fill, RECV_BAR_H, RGB(200, 30, 30));
    }

    char pct[8];
    int p = 0;
    u64 pv = 0;
    if (total > 0) pv = received * 100 / total;
    if (pv > 100) pv = 100;
    p += s_itoa(pct + p, (int)pv);
    pct[p++] = '%'; pct[p] = 0;
    ui_str_center(fb, 770, pct, RGB(200, 200, 215), 3);
}
