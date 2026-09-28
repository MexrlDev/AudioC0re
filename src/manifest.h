/* SPDX-License-Identifier: MIT */
#ifndef MANIFEST_H
#define MANIFEST_H
#include "core.h"

#define AC_MAGIC      0x52304341u
#define AC_MAGIC_ALT  0x30524341u
#define AC_VERSION    2
#define AC_ENTRY_SIZE 40
#define AC_HDR_SIZE   32

#define AC_TITLE_MAX  64
#define AC_ARTIST_MAX 64
#define AC_ALBUM_MAX  64
#define AC_PATH_MAX   96

typedef struct {
    char title[AC_TITLE_MAX];
    char artist[AC_ARTIST_MAX];
    char album[AC_ALBUM_MAX];
    char cover[AC_PATH_MAX];
    char audio[AC_PATH_MAX];
    u32  duration_ms;
    u32  added_ts;
    u32  sort_key;
    u32  flags;
} ac_entry;

typedef struct {
    void      *map_base;
    u64        map_size;
    u32        count;
    u32        cover_w, cover_h;
    ac_entry  *entries;
    char       lib_root[160];
} ac_library;

int  ac_load(ac_library *lib, const char *lib_root);
void ac_unload(ac_library *lib);
void ac_full_path(const ac_library *lib, const char *rel,
                  char *out, int out_max);

#endif
