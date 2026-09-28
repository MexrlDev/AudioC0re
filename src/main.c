/* SPDX-License-Identifier: MIT */
#include "core.h"
#include "app.h"
#include "ui.h"
#include "manifest.h"
#include "library.h"
#include "player.h"
#include "audio.h"
#include "splash.h"
#include "waiting.h"
#include "volume.h"
#include "net_recv.h"
#include "ps_libc.h"

extern char __bss_start[];
extern char __bss_end[];
extern char __rela_start[];
extern char __rela_end[];

typedef struct { u64 r_offset; u64 r_info; s64 r_addend; } Elf64_Rela;
#define ELF64_R_TYPE(i) ((u32)((i) & 0xffffffffU))
#define R_X86_64_RELATIVE 8
#define R_X86_64_64       1

static int do_relocations(u64 load_base) {
    Elf64_Rela *r = (Elf64_Rela *)__rela_start;
    Elf64_Rela *e = (Elf64_Rela *)__rela_end;
    int n = 0;
    while (r < e) {
        u32 type = ELF64_R_TYPE(r->r_info);
        u64 *slot = (u64 *)(load_base + r->r_offset);
        if (type == R_X86_64_RELATIVE) {
            *slot = load_base + (u64)r->r_addend;
            n++;
        } else if (type == R_X86_64_64) {
            *slot += load_base;
            n++;
        }
        r++;
    }
    return n;
}

static const char LIB_ROOT[] = "/av_contents/content_tmp/audioC0re";

const char *ac_get_lib_root(void) { return LIB_ROOT; }

static void on_recv_progress(const char *name, int idx, void *user) {
    struct ctx *c = user;
    waiting_receive(c, name, idx, 0, 0);
    video_flip(c, 1);
}

static void on_recv_chunk(const char *name, int idx,
                          u64 received, u64 total, void *user)
{
    struct ctx *c = user;
    waiting_receive(c, name, idx, received, total);
    video_flip(c, 1);
}

static int try_receive_library(struct ctx *c, s32 listen_fd) {
    if (listen_fd < 0 || !c->accept_fn) {
        ulog(c, "[ac] no library listener\n");
        return 0;
    }

    u64 t0 = get_uptime_ms(c);
    int dots = 0;
    while (get_uptime_ms(c) - t0 < 3000) {
        u32 raw = pad_raw(c);
        u32 pressed = raw & ~c->pad_prev;
        c->pad_prev = raw;
        if (pressed & DS_CIRCLE) {
            ulog(c, "[ac] user skipped library receive\n");
            return 0;
        }
        waiting_draw(c, "Waiting for library", dots++);
        video_flip(c, 1);
        if (c->usleep) NC(c->G, c->usleep, 150000, 0,0,0,0,0);
    }

    ulog(c, "[ac] blocking on accept\n");
    waiting_draw(c, "Waiting for library", 0);
    video_flip(c, 1);

    u8 sa[16]; s32 alen = 16;
    s32 cfd = (s32)NC(c->G, c->accept_fn, (u64)listen_fd, (u64)sa,
                      (u64)&alen, 0, 0, 0);
    if (cfd < 0) {
        ulog_hex(c, "[ac] accept failed, ret = ", (u64)(s64)cfd);
        return 0;
    }
    ulog_hex(c, "[ac] accepted conn fd = ", (u64)(s64)cfd);
    c->accepted_fd = cfd;
    return 1;
}

static void wipe_library(struct ctx *c, ac_library *lib) {
    void *unlink_fn = c->kunlink;
    void *rmdir_fn  = SYM(c->G, c->D, LIBKERNEL_HANDLE, "sceKernelRmdir");
    if (!unlink_fn) return;

    char fp[256];

    if (lib && lib->entries) {
        for (u32 i = 0; i < lib->count; i++) {
            ac_full_path(lib, lib->entries[i].cover, fp, sizeof(fp));
            NC(c->G, unlink_fn, (u64)fp, 0,0,0,0,0);
            ac_full_path(lib, lib->entries[i].audio, fp, sizeof(fp));
            NC(c->G, unlink_fn, (u64)fp, 0,0,0,0,0);
        }
    }

    const char *fixed[] = {
        "image/loading.bin", "image/logo.bin",
        "manifest.bin", "manifest.json",
        "cover/_default.bin",
    };
    for (unsigned i = 0; i < sizeof(fixed)/sizeof(fixed[0]); i++) {
        int p = 0;
        for (int k = 0; LIB_ROOT[k] && p < 240; k++) fp[p++] = LIB_ROOT[k];
        fp[p++] = '/';
        for (int k = 0; fixed[i][k] && p < 254; k++) fp[p++] = fixed[i][k];
        fp[p] = 0;
        NC(c->G, unlink_fn, (u64)fp, 0,0,0,0,0);
    }

    if (rmdir_fn) {
        const char *subdirs[] = { "image", "cover", "audio" };
        for (unsigned i = 0; i < 3; i++) {
            int p = 0;
            for (int k = 0; LIB_ROOT[k] && p < 240; k++) fp[p++] = LIB_ROOT[k];
            fp[p++] = '/';
            for (int k = 0; subdirs[i][k] && p < 254; k++) fp[p++] = subdirs[i][k];
            fp[p] = 0;
            NC(c->G, rmdir_fn, (u64)fp, 0,0,0,0,0);
        }
        NC(c->G, rmdir_fn, (u64)LIB_ROOT, 0,0,0,0,0);
    }

    ulog(c, "[ac] library wiped\n");
}

__attribute__((section(".text._start")))
void _start(u64 eboot_base, u64 dlsym_addr, struct ext_args *ext) {
    u64 load_base = (u64)&_start;
    int nreloc = do_relocations(load_base);
    for (volatile char *p = __bss_start; p < __bss_end; p++) *p = 0;

    ext->step = 1;
    ctx_init(&G_CTX, eboot_base, dlsym_addr, ext);
    volume_init(&G_CTX);

    {
        char b[64]; int p = 0;
        const char *pre = "[ac] relocs = ";
        while (*pre) b[p++] = *pre++;
        p += s_itoa(b + p, nreloc);
        b[p++] = '\n'; b[p] = 0;
        ulog(&G_CTX, b);
    }

    ext->step = 2;
    if (ctx_video_up(&G_CTX, eboot_base) != 0) {
        ext->status = -100; ext->step = 3; return;
    }
    ulog(&G_CTX, "[ac] video up\n");

    ext->step = 4;
    ps_libc_init(G_CTX.G, G_CTX.D,
        G_CTX.mmap_fn, G_CTX.kopen, G_CTX.kread, G_CTX.kwrite,
        G_CTX.kclose, G_CTX.klseek, G_CTX.kmkdir,
        G_CTX.sendto_fn, ext->log_fd, ext->log_addr);
    ulog(&G_CTX, "[ac] libc up\n");

    ext->step = 5;
    ctx_pad_up(&G_CTX);
    ulog(&G_CTX, "[ac] pad: done\n");

    pad_set_lightbar(&G_CTX, 255, 105, 180);

    ext->step = 6;
    {
        u64 pool_sz = ps_libc_pool_size();
        ulog_num(&G_CTX, "[ac] libc pool (bytes) = ", pool_sz);

        if (pool_sz < AC_MIN_POOL_BYTES) {
            char detail[64];
            int p = 0;
            p += s_itoa(detail + p, (int)(pool_sz / (1024 * 1024)));
            const char *suffix = " MB free, need 16 MB";
            while (*suffix && p < 60) detail[p++] = *suffix++;
            detail[p] = 0;

            ulog(&G_CTX,
                 "[ac] INSUFFICIENT MEMORY - showing error screen\n");
            ctx_error_screen(&G_CTX,
                             "NOT ENOUGH MEMORY",
                             detail,
                             "Close and reopen Star Wars Racer Revenge,");
            ctx_cleanup(&G_CTX);
            ext->status = -200;
            ext->step = 99;
            return;
        }
    }

    ext->step = 7;
    ctx_audio_up(&G_CTX);
    ulog(&G_CTX, "[ac] audio: done\n");

    pad_set_vibration(&G_CTX, 0, 0);

    ext->step = 8;
    s32 lib_fd = (s32)ext->dbg[1];
    ulog_hex(&G_CTX, "[ac] library listener fd = ", (u64)(s64)lib_fd);

    pad_set_lightbar(&G_CTX, 255, 100, 100);

    if (try_receive_library(&G_CTX, lib_fd)) {
        ulog(&G_CTX, "[ac] receiving library\n");
        waiting_receive(&G_CTX, "Receiving...", 0, 0, 0);
        video_flip(&G_CTX, 1);

        int n = ac_recv_library(&G_CTX, G_CTX.accepted_fd, LIB_ROOT,
                                on_recv_progress,
                                on_recv_chunk,
                                &G_CTX);
        ulog_num(&G_CTX, "[ac] received files = ", (u64)n);

        waiting_progress(&G_CTX, "Done", n > 0 ? n : 0, "");
        video_flip(&G_CTX, 1);
        if (G_CTX.usleep) NC(G_CTX.G, G_CTX.usleep, 500000, 0,0,0,0,0);
    } else {
        ulog(&G_CTX, "[ac] no library transfer, using existing files\n");
    }

    pad_set_lightbar(&G_CTX, 255, 105, 180);

    ext->step = 9;
    splash_show(&G_CTX, LIB_ROOT);

    ps_libc_reset_pool();

    pad_set_lightbar(&G_CTX, 220, 20, 20);

    ext->step = 10;
    ac_library lib;
    if (ac_load(&lib, LIB_ROOT) != 0) {
        u32 *fb = G_CTX.fbs[G_CTX.active];
        ui_clear(fb, RGB(15,15,15));
        ui_str_center(fb, SCR_H/2 - 100, "AudioC0re", RGB(255,255,255), 10);
        ui_str_center(fb, SCR_H/2 + 20, "No library found",
                      RGB(255,80,80), 6);
        ui_str_center(fb, SCR_H/2 + 120,
                      "Send music from tools/build_library.py",
                      RGB(180,180,180), 4);
        ui_str_center(fb, SCR_H/2 + 200, "Press O to exit.",
                      RGB(150,150,150), 4);
        video_flip(&G_CTX, 1); video_flip(&G_CTX, 1);
        while (1) {
            u32 raw = pad_raw(&G_CTX);
            u32 pressed = raw & ~G_CTX.pad_prev;
            G_CTX.pad_prev = raw;
            if (pressed & (DS_CIRCLE | DS_CROSS)) break;
            if (G_CTX.usleep) NC(G_CTX.G, G_CTX.usleep, 16667, 0,0,0,0,0);
        }
        wipe_library(&G_CTX, 0);
        goto done;
    }

    grid_state grid;
    grid_init(&grid, &lib);

    G_CTX.pad_prev = pad_raw(&G_CTX);
    ulog(&G_CTX, "[ac] entering main loop\n");

    while (1) {
        u32 raw     = pad_raw(&G_CTX);
        u32 pressed = raw & ~G_CTX.pad_prev;
        G_CTX.pad_prev = raw;

        if (pressed & DS_OPTIONS) {
            ulog(&G_CTX, "[ac] options in grid: exit + wipe\n");
            break;
        }

        int launch = -2;
        grid_input(&grid, raw, pressed, &launch);
        if (launch == -1) break;
        if (launch >= 0 && launch < (int)lib.count) {
            grid_free(&grid);

            int next = launch;
            while (next >= 0) {
                int r = player_run(&G_CTX, &lib, next);
                G_CTX.pad_prev = pad_raw(&G_CTX);
                if (r < 0) break;
                next = r;
            }
            grid_free(&grid);
            G_CTX.pad_prev = pad_raw(&G_CTX);
        }

        volume_tick(&G_CTX, 16);

        grid_draw(&grid, G_CTX.fbs[G_CTX.active]);
        volume_draw(&G_CTX, G_CTX.fbs[G_CTX.active]);
        video_flip(&G_CTX, 1);

        if (G_CTX.usleep) NC(G_CTX.G, G_CTX.usleep, 16667, 0,0,0,0,0);
    }

    grid_free(&grid);
    wipe_library(&G_CTX, &lib);
    ac_unload(&lib);

done:
    ctx_cleanup(&G_CTX);
    ulog(&G_CTX, "[ac] clean exit\n");
    ext->status = 0;
    ext->step = 99;
    ext->frame_count = G_CTX.total_frames;
}
