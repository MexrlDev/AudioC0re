/* SPDX-License-Identifier: MIT */
#ifndef VOLUME_H
#define VOLUME_H
#include "app.h"

void volume_init  (struct ctx *c);
int  volume_adjust(struct ctx *c, int delta);
int  volume_mute_toggle(struct ctx *c);
void volume_tick  (struct ctx *c, u32 dt_ms);
void volume_draw  (struct ctx *c, u32 *fb);

void volume_handle_hold(struct ctx *c, u32 raw, u32 pressed,
                        u32 up_mask, u32 down_mask, int step);

#endif
