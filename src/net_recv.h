/* SPDX-License-Identifier: MIT */
#ifndef NET_RECV_H
#define NET_RECV_H
#include "app.h"

typedef void (*ac_progress_fn)(const char *name, int idx, void *user);
typedef void (*ac_chunk_fn)(const char *name, int idx,
                            u64 received, u64 total, void *user);

int ac_recv_library(struct ctx *c, s32 conn_fd, const char *dest_root,
                    ac_progress_fn on_progress,
                    ac_chunk_fn on_chunk,
                    void *user);

#endif
