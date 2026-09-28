/* SPDX-License-Identifier: MIT */
#include "marquee.h"

int marquee_offset(int text_w, int view_w, u64 elapsed_ms) {
    if (text_w <= view_w) return 0;

    int dist = text_w - view_w + 40;
    if (dist < 1) return 0;

    const u64 HOLD_MS   = 1200;
    const int SPEED_PPS = 80;

    u64 scroll_ms = (u64)dist * 1000 / SPEED_PPS;
    if (scroll_ms < 300) scroll_ms = 300;

    u64 cycle = (HOLD_MS * 2) + (scroll_ms * 2);
    u64 phase = elapsed_ms % cycle;

    if (phase < HOLD_MS) return 0;
    phase -= HOLD_MS;

    if (phase < scroll_ms)
        return (int)((u64)dist * phase / scroll_ms);
    phase -= scroll_ms;

    if (phase < HOLD_MS) return dist;
    phase -= HOLD_MS;

    return (int)((u64)dist * (scroll_ms - phase) / scroll_ms);
}
