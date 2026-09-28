/* SPDX-License-Identifier: MIT */
#include "audio.h"
#include "ps_libc.h"

#define RING_FRAMES   16384
#define RING_MASK     (RING_FRAMES - 1)
#define BYTES_PER_FRAME (2 * 2)

static s16     g_ring[RING_FRAMES * 2];
static volatile u32 g_ring_w;
static volatile u32 g_ring_r;
static volatile int g_ring_eof;

static FILE   *g_audio_file;
static u64     g_file_bytes;
static u32     g_duration_ms;
static u32     g_position_ms;

static int     g_playing;
static int     g_thread_running;
static u64     g_audio_thread_tid;

static u32 ring_fill(void) { return g_ring_w - g_ring_r; }

static void ring_push(const s16 *src, u32 frames) {
    u32 w = g_ring_w;
    for (u32 i = 0; i < frames; i++) {
        u32 idx = (w + i) & RING_MASK;
        g_ring[idx * 2]     = src[i * 2];
        g_ring[idx * 2 + 1] = src[i * 2 + 1];
    }
    __asm__ volatile ("" ::: "memory");
    g_ring_w = w + frames;
}

static int ring_pop(s16 *dst, u32 frames) {
    u32 r = g_ring_r;
    if (ring_fill() < frames) return 0;
    for (u32 i = 0; i < frames; i++) {
        u32 idx = (r + i) & RING_MASK;
        dst[i * 2]     = g_ring[idx * 2];
        dst[i * 2 + 1] = g_ring[idx * 2 + 1];
    }
    __asm__ volatile ("" ::: "memory");
    g_ring_r = r + frames;
    return 1;
}

static void *audio_decode_thread(void *arg) {
    struct ctx *c = (struct ctx *)arg;
    static s16 chunk[SAMPLES_PER_BUF * 2];

    while (g_thread_running) {
        if (!g_playing) {
            if (c->usleep) NC(c->G, c->usleep, 5000, 0,0,0,0,0);
            continue;
        }
        if (g_ring_eof) {
            if (c->usleep) NC(c->G, c->usleep, 10000, 0,0,0,0,0);
            continue;
        }
        if (ring_fill() + SAMPLES_PER_BUF > RING_FRAMES) {
            if (c->usleep) NC(c->G, c->usleep, 3000, 0,0,0,0,0);
            continue;
        }

        size_t got = fread(chunk, 1, sizeof(chunk), g_audio_file);
        if (got == 0) {
            g_ring_eof = 1;
            continue;
        }
        u32 frames = (u32)(got / BYTES_PER_FRAME);
        if (frames == 0) continue;
        ring_push(chunk, frames);
    }
    return 0;
}

int audio_open(struct ctx *c, const char *path) {
    if (g_audio_file) audio_close(c);

    g_audio_file = fopen(path, "r");
    if (!g_audio_file) {
        printf("audio: cannot open %s\n", path);
        return -1;
    }

    fseek(g_audio_file, 0, SEEK_END);
    long sz = ftell(g_audio_file);
    fseek(g_audio_file, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(g_audio_file); g_audio_file = 0;
        return -1;
    }
    g_file_bytes  = (u64)sz;
    g_duration_ms = (u32)(g_file_bytes / BYTES_PER_FRAME * 1000ULL
                          / SAMPLE_RATE);

    g_ring_w = g_ring_r = 0;
    g_ring_eof = 0;
    g_position_ms = 0;
    g_playing = 1;

    if (!g_thread_running) {
        void *pc = SYM(c->G, c->D, LIBKERNEL_HANDLE, "scePthreadCreate");
        if (pc) {
            g_thread_running = 1;
            NC(c->G, pc, (u64)&g_audio_thread_tid, 0,
               (u64)audio_decode_thread, (u64)c, (u64)"ac0re_dec", 0);
        }
    }
    printf("audio: opened %s, dur=%u ms\n", path, g_duration_ms);
    return 0;
}

void audio_close(struct ctx *c) {
    g_playing = 0;
    if (g_audio_file) { fclose(g_audio_file); g_audio_file = 0; }
    g_ring_w = g_ring_r = 0;
    (void)c;
}

void audio_play  (struct ctx *c) { if (g_audio_file) g_playing = 1; (void)c; }
void audio_pause (struct ctx *c) { g_playing = 0; (void)c; }
void audio_resume(struct ctx *c) { if (g_audio_file) g_playing = 1; (void)c; }

int audio_is_playing(struct ctx *c) { (void)c; return g_playing; }

int audio_seek(struct ctx *c, u32 ms) {
    if (!g_audio_file) return -1;
    if (ms > g_duration_ms) ms = g_duration_ms;
    u64 byte_off = (u64)ms * SAMPLE_RATE / 1000ULL * BYTES_PER_FRAME;
    if (byte_off > g_file_bytes) byte_off = g_file_bytes;
    if (fseek(g_audio_file, (long)byte_off, SEEK_SET) != 0) return -1;

    g_ring_w = g_ring_r = 0;
    g_ring_eof = 0;
    g_position_ms = ms;
    (void)c;
    return 0;
}

u32 audio_position_ms(struct ctx *c) { (void)c; return g_position_ms; }
u32 audio_duration_ms(struct ctx *c) { (void)c; return g_duration_ms; }

void audio_tick(struct ctx *c) {
    if (!g_audio_file || c->audio_h < 0 || !c->aud_out) return;
    if (!g_playing) {
        if (c->usleep) NC(c->G, c->usleep, 5000, 0,0,0,0,0);
        return;
    }

    static s16 submit[SAMPLES_PER_BUF * 2];
    if (ring_pop(submit, SAMPLES_PER_BUF)) {
        if (c->volume < 100) {
            int v = (int)c->volume;
            for (int i = 0; i < SAMPLES_PER_BUF * 2; i++) {
                submit[i] = (s16)(((int)submit[i] * v) / 100);
            }
        }
        NC(c->G, c->aud_out, (u64)c->audio_h, (u64)submit, 0,0,0,0);
        g_position_ms += (u32)((u64)SAMPLES_PER_BUF * 1000ULL / SAMPLE_RATE);
    } else if (g_ring_eof) {
        g_position_ms = g_duration_ms;
    } else {
        if (c->usleep) NC(c->G, c->usleep, 2000, 0,0,0,0,0);
    }
}
