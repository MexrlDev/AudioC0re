/* SPDX-License-Identifier: MIT */
#include "net_recv.h"
#include "ps_libc.h"

#define RECV_BUF 65536

static void mkdir_parents(struct ctx *c, const char *file_path) {
    if (!c->kmkdir) return;

    char buf[256];
    int n = 0;
    int last_slash = -1;
    for (int i = 0; file_path[i] && n < 254; i++) {
        buf[n] = file_path[i];
        if (buf[n] == '/') last_slash = n;
        n++;
    }
    buf[n] = 0;
    if (last_slash <= 0) return;

    buf[last_slash] = 0;

    for (int i = 1; i < last_slash; i++) {
        if (buf[i] == '/') {
            char save = buf[i];
            buf[i] = 0;
            NC(c->G, c->kmkdir, (u64)buf, 0777, 0, 0, 0, 0);
            buf[i] = save;
        }
    }
    NC(c->G, c->kmkdir, (u64)buf, 0777, 0, 0, 0, 0);
}

static void mkdir_p(struct ctx *c, const char *path) {
    if (!c->kmkdir) return;
    char buf[256];
    int n = 0;
    for (int i = 0; path[i] && n < 254; i++) buf[n++] = path[i];
    buf[n] = 0;
    for (int i = 1; i < n; i++) {
        if (buf[i] == '/') {
            char save = buf[i];
            buf[i] = 0;
            NC(c->G, c->kmkdir, (u64)buf, 0777, 0, 0, 0, 0);
            buf[i] = save;
        }
    }
    NC(c->G, c->kmkdir, (u64)buf, 0777, 0, 0, 0, 0);
}

static int read_all(struct ctx *c, s32 fd, u8 *buf, u32 n) {
    u32 got = 0;
    while (got < n) {
        s32 r = (s32)NC(c->G, c->recv_fn, (u64)fd, (u64)(buf + got),
                        (u64)(n - got), 0, 0, 0);
        if (r <= 0) return -1;
        got += r;
    }
    return 0;
}

int ac_recv_library(struct ctx *c, s32 conn_fd, const char *dest_root,
                    ac_progress_fn on_progress,
                    ac_chunk_fn on_chunk,
                    void *user)
{
    if (conn_fd < 0 || !c->recv_fn) return -1;

    mkdir_p(c, dest_root);

    u8 *buf = (u8*)malloc(RECV_BUF);
    if (!buf) { NC(c->G, c->close_fn, (u64)conn_fd, 0,0,0,0,0); return -3; }

    int file_idx = 0;
    int rc = 0;

    while (1) {
        u8 hdr[10];
        if (read_all(c, conn_fd, hdr, 10) != 0) { rc = -4; break; }

        u64 size = 0;
        for (int i = 0; i < 8; i++) size |= ((u64)hdr[i]) << (i * 8);
        u16 name_len = (u16)hdr[8] | ((u16)hdr[9] << 8);

        if (size == 0 && name_len == 0) break;
        if (name_len == 0 || name_len > 200) { rc = -5; break; }

        char name[208];
        if (read_all(c, conn_fd, (u8*)name, name_len) != 0) { rc = -6; break; }
        name[name_len] = 0;

        char path[256]; int p = 0;
        for (int i = 0; dest_root[i] && p < 240; i++) path[p++] = dest_root[i];
        path[p++] = '/';
        for (int i = 0; name[i] && p < 254; i++) path[p++] = name[i];
        path[p] = 0;

        mkdir_parents(c, path);
        printf("ac: recv %s (%u bytes)\n", path, (unsigned)size);

        if (on_progress) on_progress(name, file_idx + 1, user);

        s32 out = (s32)NC(c->G, c->kopen, (u64)path,
                          (u64)(0x0001 | 0x0200 | 0x0400), 0x1FF, 0, 0, 0);
        if (out < 0) {
            if (c->kunlink) NC(c->G, c->kunlink, (u64)path, 0,0,0,0,0);
            out = (s32)NC(c->G, c->kopen, (u64)path,
                          (u64)(0x0001 | 0x0200 | 0x0400), 0x1FF, 0, 0, 0);
        }
        if (out < 0) {
            printf("ac: kopen failed %d on %s\n", (int)out, path);
            rc = -7;
            break;
        }

        u64 remaining = size;
        u64 received  = 0;
        while (remaining > 0) {
            u32 chunk = (remaining > RECV_BUF) ? RECV_BUF : (u32)remaining;
            if (read_all(c, conn_fd, buf, chunk) != 0) { rc = -8; break; }
            u32 written = 0;
            while (written < chunk) {
                s32 w = (s32)NC(c->G, c->kwrite, (u64)out,
                                (u64)(buf + written),
                                (u64)(chunk - written), 0, 0, 0);
                if (w <= 0) { rc = -9; break; }
                written += w;
            }
            if (rc != 0) break;
            remaining -= chunk;
            received  += chunk;

            if (on_chunk) on_chunk(name, file_idx + 1, received, size, user);
        }

        NC(c->G, c->kclose, (u64)out, 0,0,0,0,0);
        if (rc != 0) break;

        file_idx++;
    }

    free(buf);
    NC(c->G, c->close_fn, (u64)conn_fd, 0,0,0,0,0);
    return rc == 0 ? file_idx : rc;
}
