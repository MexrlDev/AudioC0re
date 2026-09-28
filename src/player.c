/* SPDX-License-Identifier: MIT */
#include "player.h"
#include "audio.h"
#include "loading.h"
#include "ui.h"
#include "volume.h"
#include "marquee.h"
#include "ps_libc.h"

#define COVER_W 500
#define COVER_H 500

static void blit_cover(u32 *fb, u32 *cover, int cx, int cy) {
    if (!cover) return;
    int x0 = cx - COVER_W / 2;
    int y0 = cy - COVER_H / 2;
    for (int j = 0; j < COVER_H; j++) {
        int py = y0 + j;
        if (py < 0 || py >= SCR_H) continue;
        const u32 *srow = cover + j * COVER_W;
        u32 *drow = fb + py * SCR_W;
        for (int i = 0; i < COVER_W; i++) {
            int px = x0 + i;
            if (px < 0 || px >= SCR_W) continue;
            u32 v = srow[i];
            u32 sa = (v >> 24) & 0xFF;
            if (sa == 0) continue;
            if (sa == 255) { drow[px] = v | 0xFF000000u; continue; }
            u32 inv = 255 - sa;
            u32 sr = (v >> 16) & 0xFF;
            u32 sg = (v >>  8) & 0xFF;
            u32 sb =  v        & 0xFF;
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
}

static u32 *load_cover(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    u32 bytes = COVER_W * COVER_H * 4;
    u32 *buf = (u32*)malloc(bytes);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, bytes, f);
    fclose(f);
    if (got < bytes) { free(buf); return 0; }
    return buf;
}

static void format_time(char *out, u32 ms) {
    u32 s = ms / 1000;
    u32 m = s / 60;
    s %= 60;
    int p = 0;
    p += s_itoa(out + p, (int)m);
    out[p++] = ':';
    if (s < 10) out[p++] = '0';
    p += s_itoa(out + p, (int)s);
    out[p] = 0;
}

static void draw_scrolling_line(u32 *fb, int x, int y,
                                const char *text, u32 color, int scale,
                                int view_w, u64 elapsed_ms)
{
    if (!text || !text[0]) return;
    int tw = ui_str_w(text, scale);
    if (tw <= view_w) {
        ui_str(fb, x, y, text, color, scale);
        return;
    }
    int off = marquee_offset(tw, view_w, elapsed_ms);
    int th = scale * 8 + 12;
    font_aa_set_clip(x, y - 2, view_w, th);
    ui_str(fb, x - off, y, text, color, scale);
    font_aa_clear_clip();
}

static void draw_player(u32 *fb, const ac_entry *entry,
                        u32 *cover, int playing, int scrubbing,
                        u32 pos_ms, u32 dur_ms, u64 elapsed_ms)
{
    ui_clear(fb, RGB(12,12,16));

    int cx = SCR_W * 3 / 4 - 60;
    int cy = SCR_H / 2 - 140;
    if (cover) blit_cover(fb, cover, cx, cy);
    else ui_fill(fb, cx - COVER_W/2, cy - COVER_H/2,
                 COVER_W, COVER_H, RGB(28,28,36));

    int tx = 80;
    int view_w = cx - COVER_W / 2 - 60 - tx;

    draw_scrolling_line(fb, tx, 180,
                        entry->title[0] ? entry->title : "Unknown Track",
                        RGB(255,255,255), 6, view_w, elapsed_ms);

    if (entry->artist[0])
        draw_scrolling_line(fb, tx, 280,
                            entry->artist,
                            RGB(180,180,200), 4, view_w, elapsed_ms);

    if (entry->album[0])
        draw_scrolling_line(fb, tx, 340,
                            entry->album,
                            RGB(130,130,150), 3, view_w, elapsed_ms);

    ui_str(fb, tx, 440, playing ? "PLAYING" : "PAUSED",
           playing ? RGB(80,200,80) : RGB(240,180,60), 3);

    int bar_x = tx;
    int bar_y = SCR_H - 320;
    int bar_w = SCR_W * 3 / 4 - 160;
    int bar_h = 14;
    ui_fill(fb, bar_x, bar_y, bar_w, bar_h, RGB(60,60,60));
    u32 dur = dur_ms ? dur_ms : 1;
    u32 pp  = scrubbing ? pos_ms : pos_ms;
    if (pp > dur) pp = dur;
    int fill = (int)((u64)bar_w * pp / dur);
    ui_fill(fb, bar_x, bar_y, fill, bar_h,
            scrubbing ? RGB(255,200,80) : RGB(230,30,30));

    char t1[16], t2[16];
    format_time(t1, pp);
    format_time(t2, dur_ms);
    ui_str(fb, bar_x, bar_y - 44, t1, RGB(220,220,220), 3);
    ui_str_right(fb, bar_x + bar_w, bar_y - 44, t2, RGB(220,220,220), 3);

    ui_str(fb, tx, SCR_H - 220,
           "X Play/Pause   D-Pad L/R Seek +/-3s   "
           "L1/R1 Prev/Next   Up/Down Volume   Square Mute   O Back",
           RGB(140,140,160), 3);
}

int player_run(struct ctx *c, ac_library *lib, int start_idx) {
    if (!lib || !lib->entries) return -1;
    if (start_idx < 0) start_idx = 0;
    if (start_idx >= (int)lib->count) start_idx = (int)lib->count - 1;

    const ac_entry *entry = &lib->entries[start_idx];

    char apath[256];
    ac_full_path(lib, entry->audio, apath, sizeof(apath));
    printf("player: opening %s\n", apath);

    loading_show(c, lib->lib_root, 0);
    video_flip(c, 1);

    if (audio_open(c, apath) != 0) {
        c->pad_prev = pad_raw(c);
        while (1) {
            u32 *fb = c->fbs[c->active];
            ui_clear(fb, RGB(15,15,15));
            ui_str_center(fb, SCR_H/2 - 60, "CANNOT PLAY TRACK",
                          RGB(255,80,80), 6);
            ui_str_center(fb, SCR_H/2 + 40, entry->title,
                          RGB(200,200,200), 4);
            ui_str_center(fb, SCR_H/2 + 140, "Press O to return.",
                          RGB(160,160,160), 4);
            video_flip(c, 1);
            u32 raw = pad_raw(c);
            u32 pressed = raw & ~c->pad_prev;
            c->pad_prev = raw;
            if (pressed & (DS_CIRCLE | DS_CROSS)) break;
            if (c->usleep) NC(c->G, c->usleep, 16667, 0,0,0,0,0);
        }
        return -1;
    }

    char cpath[256];
    ac_full_path(lib, entry->cover, cpath, sizeof(cpath));
    u32 *cover = load_cover(cpath);

    u32 dur_ms = audio_duration_ms(c);
    int playing = 1;
    int exit_req = 0;
    int next_idx = -1;
    int scrubbing = 0;
    u32 scrub_ms = 0;

    u64 marquee_epoch = get_uptime_ms(c);

    for (int i = 0; i < 5; i++) {
        pad_raw(c);
        if (c->usleep) NC(c->G, c->usleep, 16667, 0,0,0,0,0);
    }
    c->pad_prev = pad_raw(c);

    while (!exit_req) {
        u32 raw     = pad_raw(c);
        u32 pressed = raw & ~c->pad_prev;
        c->pad_prev = raw;

        u64 elapsed = get_uptime_ms(c) - marquee_epoch;

        if (pressed & DS_CIRCLE) { exit_req = 1; next_idx = -1; }

        if (pressed & DS_CROSS) {
            if (scrubbing) {
                audio_seek(c, scrub_ms);
                scrubbing = 0;
                audio_play(c);
                playing = 1;
            } else {
                if (playing) { audio_pause(c); playing = 0; }
                else         { audio_resume(c); playing = 1; }
            }
        }

        volume_handle_hold(c, raw, pressed, DS_UP, DS_DOWN, 5);
        if (pressed & DS_SQUARE) volume_mute_toggle(c);

        if (pressed & DS_L1) {
            next_idx = (start_idx - 1 + (int)lib->count) % (int)lib->count;
            exit_req = 1;
        }
        if (pressed & DS_R1) {
            next_idx = (start_idx + 1) % (int)lib->count;
            exit_req = 1;
        }

        if ((raw & DS_LEFT) || (raw & DS_RIGHT)) {
            if (!scrubbing) { scrubbing = 1; scrub_ms = audio_position_ms(c); }
            if (raw & DS_LEFT) {
                if (scrub_ms > 3000) scrub_ms -= 3000;
                else scrub_ms = 0;
            }
            if (raw & DS_RIGHT) {
                scrub_ms += 3000;
                if (scrub_ms > dur_ms) scrub_ms = dur_ms;
            }
        }

        u32 pos = audio_position_ms(c);
        if (playing && pos >= dur_ms && dur_ms > 0) {
            for (int i = 0; i < 60; i++) {
                draw_player(c->fbs[c->active], entry, cover, playing,
                            0, dur_ms, dur_ms, elapsed);
                volume_tick(c, 16);
                volume_draw(c, c->fbs[c->active]);
                video_flip(c, 1);
                if (c->usleep) NC(c->G, c->usleep, 16667, 0,0,0,0,0);
                elapsed = get_uptime_ms(c) - marquee_epoch;
            }
            exit_req = 1;
            next_idx = -1;
            break;
        }

        audio_tick(c);

        draw_player(c->fbs[c->active], entry, cover, playing,
                    scrubbing, scrubbing ? scrub_ms : audio_position_ms(c),
                    dur_ms, elapsed);

        volume_tick(c, 16);
        volume_draw(c, c->fbs[c->active]);

        video_flip(c, 1);
    }

    audio_close(c);
    if (cover) free(cover);
    return next_idx;
}
