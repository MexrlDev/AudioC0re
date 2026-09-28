/* SPDX-License-Identifier: MIT */
#include "app.h"
#include "ui.h"
#include "ps_libc.h"

struct ctx G_CTX;

void ctx_init(struct ctx *c, u64 eboot_base, u64 dlsym_addr, struct ext_args *ext) {
    memset(c, 0, sizeof(*c));
    c->eboot_base = eboot_base;
    c->G = (void *)(eboot_base + GADGET_OFFSET);
    c->D = (void *)dlsym_addr;
    c->video_h = -1;
    c->audio_h = -1;
    c->pad_h   = -1;
    c->accepted_fd = -1;
    c->ext     = ext;
    c->log_fd  = ext->log_fd;
    for (int i = 0; i < 16; i++) c->log_sa[i] = ext->log_addr[i];

    c->volume           = 100;
    c->volume_saved     = 100;
    c->volume_slide_q16 = 0;
    c->volume_until_ms  = 0;

    void *G = c->G, *D = c->D;
    c->usleep    = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelUsleep");
    c->cancel    = SYM(G, D, LIBKERNEL_HANDLE, "scePthreadCancel");
    c->load_mod  = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelLoadStartModule");
    c->alloc_dm  = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelAllocateDirectMemory");
    c->map_dm    = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelMapDirectMemory");
    c->dm_size   = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelGetDirectMemorySize");
    c->create_eq = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelCreateEqueue");
    c->wait_eq   = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelWaitEqueue");
    c->delete_eq = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelDeleteEqueue");

    c->mmap_fn   = SYM(G, D, LIBKERNEL_HANDLE, "mmap");
    if (!c->mmap_fn)
        c->mmap_fn = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelMmap");
    c->munmap    = SYM(G, D, LIBKERNEL_HANDLE, "munmap");
    if (!c->munmap)
        c->munmap = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelMunmap");

    c->kopen     = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelOpen");
    c->kread     = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelRead");
    c->kwrite    = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelWrite");
    c->kclose    = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelClose");
    c->klseek    = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelLseek");
    c->kmkdir    = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelMkdir");
    c->kunlink   = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelUnlink");
    c->krmdir    = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelRmdir");
    c->clock_gettime = SYM(G, D, LIBKERNEL_HANDLE, "clock_gettime");
    c->socket_fn = SYM(G, D, LIBKERNEL_HANDLE, "socket");
    c->bind_fn   = SYM(G, D, LIBKERNEL_HANDLE, "bind");
    c->listen_fn = SYM(G, D, LIBKERNEL_HANDLE, "listen");
    c->accept_fn = SYM(G, D, LIBKERNEL_HANDLE, "accept");
    c->send_fn   = SYM(G, D, LIBKERNEL_HANDLE, "send");
    c->recv_fn   = SYM(G, D, LIBKERNEL_HANDLE, "recv");
    c->sendto_fn = SYM(G, D, LIBKERNEL_HANDLE, "sendto");
    c->recvfrom_fn = SYM(G, D, LIBKERNEL_HANDLE, "recvfrom");
    c->close_fn  = SYM(G, D, LIBKERNEL_HANDLE, "close");
    c->setsockopt_fn = SYM(G, D, LIBKERNEL_HANDLE, "setsockopt");
    c->poll_fn   = SYM(G, D, LIBKERNEL_HANDLE, "poll");
    c->select_fn = SYM(G, D, LIBKERNEL_HANDLE, "select");
    c->module_info_from_addr = SYM(G, D, LIBKERNEL_HANDLE,
                                   "sceKernelGetModuleInfoFromAddr");
    c->sys_sw_version        = SYM(G, D, LIBKERNEL_HANDLE,
                                   "sceKernelGetSystemSwVersion");
}

__attribute__((noinline))
void ulog(struct ctx *c, const char *msg) {
    if (c->log_fd < 0 || !c->sendto_fn) return;
    NC(c->G, c->sendto_fn, (u64)c->log_fd, (u64)msg,
       (u64)strlen(msg), 0, (u64)c->log_sa, 16);
}

__attribute__((noinline))
void ulog_num(struct ctx *c, const char *prefix, u64 v) {
    char b[96]; int p = 0;
    while (*prefix && p < 70) b[p++] = *prefix++;
    p += s_itoa(b + p, (int)v);
    b[p++] = '\n'; b[p] = 0;
    ulog(c, b);
}

__attribute__((noinline))
void ulog_hex(struct ctx *c, const char *prefix, u64 v) {
    char b[96]; int p = 0;
    while (*prefix && p < 70) b[p++] = *prefix++;
    p += s_hex64(b + p, v);
    b[p++] = '\n'; b[p] = 0;
    ulog(c, b);
}

int ctx_video_up(struct ctx *c, u64 eboot_base) {
    void *G = c->G, *D = c->D;
    if (!c->usleep || !c->load_mod || !c->alloc_dm || !c->map_dm) return -1;

    if (c->cancel) {
        u64 gs = *(u64 *)(eboot_base + EBOOT_GS_THREAD);
        if (gs) NC(G, c->cancel, gs, 0,0,0,0,0);
    }
    NC(G, c->usleep, 200000, 0,0,0,0,0);

    s32 vid = (s32)NC(G, c->load_mod, (u64)"libSceVideoOut.sprx",0,0,0,0,0);
    if (vid < 0) return -2;
    c->vid_open  = SYM(G, D, vid, "sceVideoOutOpen");
    c->vid_close = SYM(G, D, vid, "sceVideoOutClose");
    c->vid_reg   = SYM(G, D, vid, "sceVideoOutRegisterBuffers");
    c->vid_flip  = SYM(G, D, vid, "sceVideoOutSubmitFlip");
    c->vid_rate  = SYM(G, D, vid, "sceVideoOutSetFlipRate");
    c->vid_evt   = SYM(G, D, vid, "sceVideoOutAddFlipEvent");
    if (!c->vid_open || !c->vid_close || !c->vid_reg || !c->vid_flip) return -3;

    s32 emu = *(s32 *)(eboot_base + EBOOT_VIDOUT);
    if (emu >= 0) NC(G, c->vid_close, (u64)emu, 0,0,0,0,0);
    NC(G, c->usleep, 80000, 0,0,0,0,0);

    c->video_h = (s32)NC(G, c->vid_open, 0xFF, 0, 0, 0, 0, 0);
    if (c->video_h < 0) return -4;

    if (c->create_eq) NC(G, c->create_eq, (u64)&c->eq, (u64)"acq",0,0,0,0);
    if (c->vid_evt && c->eq) NC(G, c->vid_evt, c->eq, (u64)c->video_h,0,0,0,0);

    u64 total = c->dm_size ? NC(G, c->dm_size,0,0,0,0,0,0) : 0x300000000ULL;
    u64 phys = 0;
    NC(G, c->alloc_dm, 0, total, FB_TOTAL, 0x200000, 3, (u64)&phys);
    c->vmem = 0;
    NC(G, c->map_dm, (u64)&c->vmem, FB_TOTAL, 0x33, 0, phys, 0x200000);
    if (!c->vmem) return -5;

    c->fbs[0] = (u32*)c->vmem;
    c->fbs[1] = (u32*)((u8*)c->vmem + FB_ALIGNED);
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        c->fbs[0][i] = RGB(10,10,12);
        c->fbs[1][i] = RGB(10,10,12);
    }

    u8 attr[64]; memset(attr, 0, 64);
    *(u32*)(attr+0)  = 0x80000000;
    *(u32*)(attr+4)  = 1;
    *(u32*)(attr+12) = SCR_W;
    *(u32*)(attr+16) = SCR_H;
    *(u32*)(attr+20) = SCR_W;

    if (NC(G, c->vid_reg, (u64)c->video_h, 0, (u64)c->fbs, 2, (u64)attr, 0) != 0)
        return -6;
    if (c->vid_rate) NC(G, c->vid_rate, (u64)c->video_h, 0,0,0,0,0);
    c->active = 0;
    return 0;
}

void ctx_audio_up(struct ctx *c) {
    void *G = c->G, *D = c->D;
    s32 aud = (s32)NC(G, c->load_mod, (u64)"libSceAudioOut.sprx",0,0,0,0,0);
    if (aud < 0) { ulog(c, "[ac] libSceAudioOut load FAILED\n"); return; }
    c->aud_open  = SYM(G, D, aud, "sceAudioOutOpen");
    c->aud_out   = SYM(G, D, aud, "sceAudioOutOutput");
    c->aud_close = SYM(G, D, aud, "sceAudioOutClose");
    if (c->aud_open)
        c->audio_h = (s32)NC(G, c->aud_open, 0xFF, 0, 0,
                             SAMPLES_PER_BUF, SAMPLE_RATE, AUDIO_S16_STEREO);
}

void ctx_pad_up(struct ctx *c) {
    void *G = c->G, *D = c->D;
    s32 pad = (s32)NC(G, c->load_mod, (u64)"libScePad.sprx", 0,0,0,0,0);
    if (pad < 0) { ulog(c, "[ac] libScePad load FAILED\n"); return; }

    u32 real_user_id = 0;
    s32 usr = (s32)NC(G, c->load_mod, (u64)"libSceUserService.sprx", 0,0,0,0,0);
    if (usr > 0) {
        void *get_user = SYM(G, D, usr, "sceUserServiceGetInitialUser");
        if (get_user) {
            u32 uid = 0;
            s32 rc = (s32)NC(G, get_user, (u64)&uid, 0,0,0,0,0);
            if (rc == 0 && uid != 0) real_user_id = uid;
        }
    }
    if (real_user_id == 0) real_user_id = 1;
    c->user_id = (s32)real_user_id;

    c->pad_init         = SYM(G, D, pad, "scePadInit");
    c->pad_geth         = SYM(G, D, pad, "scePadGetHandle");
    c->pad_read         = SYM(G, D, pad, "scePadRead");
    c->pad_set_lightbar = SYM(G, D, pad, "scePadSetLightBar");
    c->pad_set_vib      = SYM(G, D, pad, "scePadSetVibration");

    if (c->pad_init) (void)NC(G, c->pad_init, 0,0,0,0,0,0);
    if (c->usleep) NC(G, c->usleep, 50000, 0,0,0,0,0);
    if (c->pad_geth)
        c->pad_h = (s32)NC(G, c->pad_geth, (u64)c->user_id, 0,0,0,0,0);
}

u32 pad_raw(struct ctx *c) {
    if (c->pad_h <= 0 || !c->pad_read) return 0;
    u8 buf[128];
    memset(buf, 0, sizeof(buf));
    s32 n = (s32)NC(c->G, c->pad_read, (u64)c->pad_h, (u64)buf, 1, 0, 0, 0);
    if (n <= 0 || (u32)n >= 0x80000000) return 0;
    u32 r = *(u32*)buf;
    if (r & 0x80000000) return 0;
    return r & DS_PAD_MASK;
}

int pad_set_lightbar(struct ctx *c, u8 r, u8 g, u8 b) {
    if (c->pad_h <= 0 || !c->pad_set_lightbar) return -1;
    struct { u8 r, g, b, x; } col = { r, g, b, 0 };
    return (s32)NC(c->G, c->pad_set_lightbar, (u64)c->pad_h, (u64)&col, 0,0,0,0);
}

int pad_set_vibration(struct ctx *c, u8 large, u8 small) {
    if (c->pad_h <= 0 || !c->pad_set_vib) return -1;
    struct { u8 l, s, r[6]; } v = { large, small, {0,0,0,0,0,0} };
    return (s32)NC(c->G, c->pad_set_vib, (u64)c->pad_h, (u64)&v, 0,0,0,0);
}

void video_flip(struct ctx *c, int wait_vsync) {
    if (c->video_h < 0 || !c->vid_flip) return;
    NC(c->G, c->vid_flip, (u64)c->video_h, (u64)c->active, 1,
       (u64)c->total_frames, 0, 0);
    if (wait_vsync && c->eq && c->wait_eq) {
        u8 evt[64]; s32 cnt = 0;
        NC(c->G, c->wait_eq, c->eq, (u64)evt, 1, (u64)&cnt, 0, 0);
    }
    c->active ^= 1;
    c->total_frames++;
}

u64 get_uptime_ms(struct ctx *c) {
    if (!c->clock_gettime) return 0;
    u64 ts[2] = {0,0};
    if (NC(c->G, c->clock_gettime, 4, (u64)ts, 0,0,0,0) != 0) return 0;
    return ts[0] * 1000ULL + ts[1] / 1000000ULL;
}

void ctx_cleanup(struct ctx *c) {
    pad_set_vibration(c, 0, 0);
    pad_set_lightbar(c, 0, 0, 200);
    if (c->usleep) NC(c->G, c->usleep, 100000, 0,0,0,0,0);
    if (c->aud_close && c->audio_h >= 0)
        NC(c->G, c->aud_close, (u64)c->audio_h, 0,0,0,0,0);
    if (c->fbs[0]) ui_clear(c->fbs[0], 0xFF000000);
    if (c->fbs[1]) ui_clear(c->fbs[1], 0xFF000000);
    if (c->vid_flip && c->video_h >= 0)
        NC(c->G, c->vid_flip, (u64)c->video_h, (u64)c->active, 1, 0,0,0);
    if (c->usleep) NC(c->G, c->usleep, 50000, 0,0,0,0,0);
    if (c->vid_close && c->video_h >= 0)
        NC(c->G, c->vid_close, (u64)c->video_h, 0,0,0,0,0);
    if (c->delete_eq && c->eq)
        NC(c->G, c->delete_eq, c->eq, 0,0,0,0,0);
}

void ctx_error_screen(struct ctx *c,
                      const char *title,
                      const char *detail,
                      const char *hint)
{
    pad_set_lightbar(c, 255, 30, 30);

    c->pad_prev = pad_raw(c);
    if (c->usleep) NC(c->G, c->usleep, 200000, 0,0,0,0,0);
    c->pad_prev = pad_raw(c);

    while (1) {
        u32 *fb = c->fbs[c->active];
        ui_clear(fb, RGB(12, 12, 16));

        ui_fill(fb, 0, 0, SCR_W, 8, RGB(200, 30, 30));
        ui_fill(fb, 0, SCR_H - 8, SCR_W, 8, RGB(200, 30, 30));

        ui_str_center(fb, 220, "AudioC0re", RGB(200, 30, 30), 8);

        ui_str_center(fb, 440, title, RGB(255, 255, 255), 6);

        if (detail && detail[0])
            ui_str_center(fb, 560, detail, RGB(200, 200, 220), 4);

        if (hint && hint[0]) {
            ui_str_center(fb, 760, hint, RGB(160, 160, 180), 3);
            ui_str_center(fb, 810,
                          "then send the payload again.",
                          RGB(160, 160, 180), 3);
        }

        ui_str_center(fb, 990, "Press X or O to exit",
                      RGB(120, 120, 140), 3);

        video_flip(c, 1);

        u32 raw = pad_raw(c);
        u32 pressed = raw & ~c->pad_prev;
        c->pad_prev = raw;
        if (pressed & (DS_CROSS | DS_CIRCLE)) break;

        if (c->usleep) NC(c->G, c->usleep, 16667, 0,0,0,0,0);
    }
}
