/* SPDX-License-Identifier: MIT */
#include "splash.h"
#include "ui.h"
#include "ps_libc.h"

#define LOGO_W 500
#define LOGO_H 500

#define FADE_IN_MS    2000
#define HOLD_MS        400
#define FADE_OUT_MS   2000
#define LOGO_TOTAL_MS (FADE_IN_MS + HOLD_MS + FADE_OUT_MS)

#define BG_FADE_MS_NORMAL 1000
#define BG_FADE_MS_FAST    150

/* Target background — matches grid.c's COL_BG exactly so the
   hand-off from splash to menu has no visible colour jump. */
#define BG_R  15
#define BG_G  15
#define BG_B  15

#define COL_MEMORIAL RGB(120, 140, 200)

static const char MEMORIAL_TEXT[] = "In memory of MexrlDev PsVue-Mod Project";

static u32 *load_bin(const char *path, u32 bytes) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    u32 *buf = (u32*)malloc(bytes);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, bytes, f);
    fclose(f);
    if (got < bytes) { free(buf); return 0; }
    return buf;
}

void splash_show(struct ctx *c, const char *lib_root) {
    ulog(c, "[ac] splash: enter\n");

    if (!lib_root || !lib_root[0] || (u64)lib_root < 0x10000) {
        lib_root = "/av_contents/content_tmp/audioC0re";
    }

    static char path[256];
    int p = 0;
    for (int i = 0; lib_root[i] && p < 230; i++) path[p++] = lib_root[i];
    const char *tail = "/image/logo.bin";
    for (int i = 0; tail[i] && p < 254; i++) path[p++] = tail[i];
    path[p] = 0;

    u32 *logo = load_bin(path, LOGO_W * LOGO_H * 4);
    if (!logo) {
        ulog(c, "[ac] splash: no logo.bin, skipping\n");
        return;
    }

    int lx = (SCR_W - LOGO_W) / 2;
    int ly = (SCR_H - LOGO_H) / 2 - 30;

    const int caption_scale = 3;
    int caption_w = ui_str_w(MEMORIAL_TEXT, caption_scale);
    int caption_x = (SCR_W - caption_w) / 2;
    int caption_y = ly + LOGO_H + 50;

    /* Absorb any button still held from the previous screen so a
       held Cross doesn't trigger an immediate skip.  The user has
       to release and re-press. */
    c->pad_prev = pad_raw(c);

    u64 t0 = get_uptime_ms(c);
    int skipped = 0;

    while (1) {
        u64 now = get_uptime_ms(c);
        u64 elapsed = now - t0;

        /* Skip input: Cross jumps the timeline to the start of the
           background fade, so the menu hand-off still happens — just
           with a 150 ms fade instead of a 1 s one. */
        if (!skipped) {
            u32 raw = pad_raw(c);
            u32 pressed = raw & ~c->pad_prev;
            c->pad_prev = raw;
            if (pressed & DS_CROSS) {
                skipped = 1;
                t0 = now - LOGO_TOTAL_MS;
                elapsed = LOGO_TOTAL_MS;
                ulog(c, "[ac] splash: skipped\n");
            }
        }

        /* Background colour.  Before LOGO_TOTAL_MS: pure black.
           After: lerp black -> menu bg over fade_ms. */
        u32 fade_ms = skipped ? BG_FADE_MS_FAST : BG_FADE_MS_NORMAL;
        u32 bg;
        if (elapsed < LOGO_TOTAL_MS) {
            bg = RGB(0, 0, 0);
        } else {
            u64 bt = elapsed - LOGO_TOTAL_MS;
            if (bt > fade_ms) bt = fade_ms;
            u32 r = (u32)((u64)BG_R * bt / fade_ms);
            u32 g = (u32)((u64)BG_G * bt / fade_ms);
            u32 b = (u32)((u64)BG_B * bt / fade_ms);
            bg = RGB(r, g, b);
        }

        /* Logo alpha.  When skipped we never draw the logo again. */
        u8 alpha = 0;
        if (!skipped) {
            if (elapsed < FADE_IN_MS) {
                alpha = (u8)(elapsed * 255 / FADE_IN_MS);
            } else if (elapsed < (u64)(FADE_IN_MS + HOLD_MS)) {
                alpha = 255;
            } else if (elapsed < (u64)LOGO_TOTAL_MS) {
                u64 fo = elapsed - FADE_IN_MS - HOLD_MS;
                if (fo >= FADE_OUT_MS) alpha = 0;
                else alpha = (u8)(255 - (fo * 255 / FADE_OUT_MS));
            }
        }

        u32 *fb = c->fbs[c->active];
        ui_clear(fb, bg);

        if (alpha > 0) {
            ui_blit_bgra_alpha(fb, lx, ly, logo, LOGO_W, LOGO_H, alpha);

            u32 base = COL_MEMORIAL;
            u32 r = ((base >> 16) & 0xFF) * alpha / 255;
            u32 g = ((base >>  8) & 0xFF) * alpha / 255;
            u32 b = ( base        & 0xFF) * alpha / 255;
            u32 col = 0xFF000000u | (r << 16) | (g << 8) | b;
            ui_str(fb, caption_x, caption_y, MEMORIAL_TEXT,
                   col, caption_scale);
        }

        video_flip(c, 1);

        u64 total_ms = LOGO_TOTAL_MS + fade_ms;
        if (elapsed >= total_ms) break;

        if (c->usleep) NC(c->G, c->usleep, 16667, 0,0,0,0,0);
    }

    free(logo);
    ulog(c, "[ac] splash: done\n");
}
