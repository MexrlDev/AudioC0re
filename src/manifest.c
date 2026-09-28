/* SPDX-License-Identifier: MIT */
#include "manifest.h"
#include "ps_libc.h"

static void copy_str(char *dst, const char *src, int max) {
    int i = 0;
    for (; src[i] && i < max - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

int ac_load(ac_library *lib, const char *lib_root) {
    if (!lib || !lib_root) return -1;
    memset(lib, 0, sizeof(*lib));
    copy_str(lib->lib_root, lib_root, 160);

    char path[200];
    int p = 0;
    for (int i = 0; lib_root[i] && p < 185; i++) path[p++] = lib_root[i];
    const char *tail = "/manifest.bin";
    for (int i = 0; tail[i] && p < 198; i++) path[p++] = tail[i];
    path[p] = 0;

    FILE *f = fopen(path, "r");
    if (!f) { printf("ac: no manifest at %s\n", path); return -1; }

    u8 hdr[AC_HDR_SIZE];
    if (fread(hdr, 1, AC_HDR_SIZE, f) != AC_HDR_SIZE) {
        printf("ac: manifest too short\n");
        fclose(f); return -1;
    }

    u32 magic   = *(u32*)(hdr + 0);
    u32 version = *(u32*)(hdr + 4);
    u32 count   = *(u32*)(hdr + 8);
    u32 cov_w   = *(u32*)(hdr + 12);
    u32 cov_h   = *(u32*)(hdr + 16);
    u32 str_off = *(u32*)(hdr + 20);
    u32 str_sz  = *(u32*)(hdr + 24);

    if ((magic != AC_MAGIC && magic != AC_MAGIC_ALT)
        || version != AC_VERSION || count == 0) {
        printf("ac: manifest bad magic/version/count "
               "(magic=%08x version=%u count=%u)\n",
               (unsigned)magic, (unsigned)version, (unsigned)count);
        fclose(f); return -1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if ((u32)file_size < str_off + str_sz) {
        printf("ac: manifest truncated\n");
        fclose(f); return -1;
    }

    u8 *buf = (u8*)malloc((size_t)file_size);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)file_size, f) != (size_t)file_size) {
        free(buf); fclose(f); return -1;
    }
    fclose(f);

    ac_entry *ents = (ac_entry*)malloc(sizeof(ac_entry) * count);
    if (!ents) { free(buf); return -1; }
    memset(ents, 0, sizeof(ac_entry) * count);

    const char *strtab = (const char*)(buf + str_off);

    for (u32 i = 0; i < count; i++) {
        const u8 *raw = buf + AC_HDR_SIZE + i * AC_ENTRY_SIZE;

        u32 toff  = *(u32*)(raw + 0x00);
        u32 aoff  = *(u32*)(raw + 0x04);
        u32 aloff = *(u32*)(raw + 0x08);
        u32 coff  = *(u32*)(raw + 0x0C);
        u32 auoff = *(u32*)(raw + 0x10);

        if (toff  >= str_sz || aoff  >= str_sz || aloff >= str_sz ||
            coff  >= str_sz || auoff >= str_sz) {
            printf("ac: manifest entry %u has bad offset\n", (unsigned)i);
            free(ents); free(buf); return -1;
        }

        copy_str(ents[i].title,  strtab + toff,  AC_TITLE_MAX);
        copy_str(ents[i].artist, strtab + aoff,  AC_ARTIST_MAX);
        copy_str(ents[i].album,  strtab + aloff, AC_ALBUM_MAX);
        copy_str(ents[i].cover,  strtab + coff,  AC_PATH_MAX);
        copy_str(ents[i].audio,  strtab + auoff, AC_PATH_MAX);

        ents[i].duration_ms = *(u32*)(raw + 0x14);
        ents[i].added_ts    = *(u32*)(raw + 0x18);
        ents[i].sort_key    = *(u32*)(raw + 0x1C);
        ents[i].flags       = *(u32*)(raw + 0x20);
    }

    lib->map_base = buf;
    lib->map_size = (u64)file_size;
    lib->count    = count;
    lib->cover_w  = cov_w;
    lib->cover_h  = cov_h;
    lib->entries  = ents;

    printf("ac: manifest loaded count=%u cover=%ux%u\n",
           (unsigned)count, (unsigned)cov_w, (unsigned)cov_h);
    return 0;
}

void ac_unload(ac_library *lib) {
    if (!lib) return;
    if (lib->entries)  free(lib->entries);
    if (lib->map_base) free(lib->map_base);
    lib->entries  = 0;
    lib->map_base = 0;
    lib->count    = 0;
}

void ac_full_path(const ac_library *lib, const char *rel,
                  char *out, int out_max) {
    int p = 0;
    for (int i = 0; lib->lib_root[i] && p < out_max - 2; i++)
        out[p++] = lib->lib_root[i];
    if (p < out_max - 2 && rel[0] != '/') out[p++] = '/';
    for (int i = 0; rel[i] && p < out_max - 1; i++)
        out[p++] = rel[i];
    out[p] = 0;
}
