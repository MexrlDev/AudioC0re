/* SPDX-License-Identifier: MIT */
#include "volume.h"
#include "ui.h"
#include "ps_libc.h"

#define VOL_BAR_W        26
#define VOL_BAR_H        420
#define VOL_RIGHT_MARGIN 70
#define VOL_HIDE_MS      3000
#define VOL_SLIDE_MS     150
#define VOL_Q16_ONE      65536u

#define VOL_REPEAT_DELAY_MS  450
#define VOL_REPEAT_PERIOD_MS  50

void volume_init(struct ctx *c) {
    c->volume           = 100;
    c->volume_saved     = 100;
    c->volume_slide_q16 = 0;
    c->volume_until_ms  = 0;
    c->vol_hold_dir     = 0;
    c->vol_hold_next_ms = 0;
}

int volume_adjust(struct ctx *c, int delta) {
    int base = (int)c->volume;
    if (base == 0 && c->volume_saved > 0) {
        base = (int)c->volume_saved;
    }

    int nv = base + delta;
    if (nv < 0)   nv = 0;
    if (nv > 100) nv = 100;
    if (nv == (int)c->volume) return 0;

    c->volume = (u8)nv;
    if (c->volume > 0) c->volume_saved = c->volume;
    c->volume_until_ms = get_uptime_ms(c) + VOL_HIDE_MS;
    return 1;
}

int volume_mute_toggle(struct ctx *c) {
    if (c->volume > 0) {
        c->volume_saved = c->volume;
        c->volume = 0;
    } else {
        c->volume = (c->volume_saved > 0) ? c->volume_saved : 100;
        c->volume_saved = c->volume;
    }
    c->volume_until_ms = get_uptime_ms(c) + VOL_HIDE_MS;
    return 1;
}

void volume_handle_hold(struct ctx *c, u32 raw, u32 pressed,
                        u32 up_mask, u32 down_mask, int step)
{
    u64 now = get_uptime_ms(c);

    int dir = 0;
    if (raw & up_mask)        dir = +1;
    else if (raw & down_mask) dir = -1;

    if (dir == 0) {
        c->vol_hold_dir = 0;
        return;
    }

    int this_edge = (dir > 0)
                  ? ((pressed & up_mask)   != 0)
                  : ((pressed & down_mask) != 0);

    if (this_edge || dir != (int)c->vol_hold_dir) {
        volume_adjust(c, dir * step);
        c->vol_hold_dir     = (s8)dir;
        c->vol_hold_next_ms = now + VOL_REPEAT_DELAY_MS;
        return;
    }

    if (now >= c->vol_hold_next_ms) {
        volume_adjust(c, dir * step);
        c->vol_hold_next_ms = now + VOL_REPEAT_PERIOD_MS;
    }
}

void volume_tick(struct ctx *c, u32 dt_ms) {
    u64 now = get_uptime_ms(c);
    int want = (now < c->volume_until_ms);

    u32 step = (u32)(((u64)dt_ms * VOL_Q16_ONE) / VOL_SLIDE_MS);
    if (step > VOL_Q16_ONE) step = VOL_Q16_ONE;

    if (want) {
        u32 q = c->volume_slide_q16 + step;
        c->volume_slide_q16 = (q > VOL_Q16_ONE) ? VOL_Q16_ONE : q;
    } else {
        u32 q = c->volume_slide_q16;
        c->volume_slide_q16 = (q > step) ? (q - step) : 0;
    }
}

void volume_draw(struct ctx *c, u32 *fb) {
    u32 q16 = c->volume_slide_q16;
    if (q16 == 0) return;

    int bar_x = SCR_W - VOL_RIGHT_MARGIN - VOL_BAR_W;
    int bar_y = (SCR_H - VOL_BAR_H) / 2;

    int span = SCR_W - bar_x;
    int offset = (int)(((u64)span * (VOL_Q16_ONE - q16)) / VOL_Q16_ONE);
    int x = bar_x + offset;

    ui_fill(fb, x - 6, bar_y - 6,
            VOL_BAR_W + 12, VOL_BAR_H + 12, RGB(10, 10, 12));
    ui_fill(fb, x, bar_y, VOL_BAR_W, VOL_BAR_H, RGB(40, 40, 48));

    int fill_h = (VOL_BAR_H * (int)c->volume) / 100;
    if (fill_h > 0) {
        int fill_y = bar_y + VOL_BAR_H - fill_h;
        ui_fill(fb, x, fill_y, VOL_BAR_W, fill_h, RGB(255, 255, 255));
    }

    ui_frame(fb, x - 6, bar_y - 6,
             VOL_BAR_W + 12, VOL_BAR_H + 12, RGB(200, 200, 210), 3);

    char pct[8];
    int p = s_itoa(pct, (int)c->volume);
    pct[p] = 0;
    int tw = ui_str_w(pct, 5);
    ui_str(fb, x + (VOL_BAR_W - tw) / 2, bar_y - 90,
           pct, RGB(255, 255, 255), 5);

    tw = ui_str_w("VOL", 3);
    ui_str(fb, x + (VOL_BAR_W - tw) / 2, bar_y - 145,
           "VOL", RGB(180, 180, 190), 3);
}
