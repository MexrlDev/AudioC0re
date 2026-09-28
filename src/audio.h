/* SPDX-License-Identifier: MIT */
#ifndef AUDIO_H
#define AUDIO_H
#include "app.h"

int  audio_open        (struct ctx *c, const char *path);
void audio_close       (struct ctx *c);
void audio_play        (struct ctx *c);
void audio_pause       (struct ctx *c);
void audio_resume      (struct ctx *c);
int  audio_is_playing  (struct ctx *c);
int  audio_seek        (struct ctx *c, u32 ms);
u32  audio_position_ms (struct ctx *c);
u32  audio_duration_ms (struct ctx *c);
void audio_tick        (struct ctx *c);

#endif
