/* SPDX-License-Identifier: MIT */
#ifndef WAITING_H
#define WAITING_H
#include "app.h"

void waiting_draw    (struct ctx *c, const char *msg, int dots);
void waiting_progress(struct ctx *c, const char *msg,
                      int idx, const char *file);
void waiting_receive (struct ctx *c, const char *file, int idx,
                      u64 received, u64 total);

#endif
