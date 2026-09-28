/* SPDX-License-Identifier: MIT */
#include "ps_libc.h"

#define VA __builtin_va_list
#define VA_START(ap,last) __builtin_va_start(ap,last)
#define VA_END(ap)        __builtin_va_end(ap)
#define VA_ARG(ap,t)      __builtin_va_arg(ap,t)

PERSIST static void *__G, *__D;
PERSIST static void *fn_mmap;
PERSIST static void *fn_kopen, *fn_kread, *fn_kwrite, *fn_kclose;
PERSIST static void *fn_klseek, *fn_kmkdir;
PERSIST static void *fn_sendto;
PERSIST static s32   __log_fd = -1;
PERSIST static u8    __log_sa[16];
PERSIST static u8   *__pool;
PERSIST static size_t __pool_sz, __pool_used;

typedef struct blk {
    u32 size;
    u32 magic;
    struct blk *next;
    u32 _pad;
} blk_t;

#define BLK_MAGIC  0x414C4C4Fu
#define FREE_MAGIC 0x46524545u
#define BLK_SIZE   ((u32)sizeof(blk_t))

PERSIST static blk_t *__free_list;

#define FB_MAX 64
PERSIST static void *fb_ptr[FB_MAX];
PERSIST static size_t fb_sz[FB_MAX];
PERSIST static int    fb_n;

static FILE _null = { -1, 0, 0, NULL, 0, 0 };
FILE *stdin  = &_null;
FILE *stdout = &_null;
FILE *stderr = &_null;

static void log_str(const char *s) {
    if (__log_fd < 0 || !fn_sendto) return;
    int n = 0; while (s[n]) n++;
    NC(__G, fn_sendto, (u64)__log_fd, (u64)s, (u64)n, 0,
       (u64)__log_sa, 16);
}

static void log_num_u64(const char *prefix, u64 v) {
    char b[96]; int p = 0;
    while (*prefix && p < 70) b[p++] = *prefix++;
    char tmp[24]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) { tmp[t++] = '0' + (v % 10); v /= 10; }
    while (t) b[p++] = tmp[--t];
    b[p++] = '\n'; b[p] = 0;
    log_str(b);
}

void ps_libc_init(void *G, void *D, void *mmap_fn,
                  void *kopen, void *kread, void *kwrite,
                  void *kclose, void *klseek, void *kmkdir,
                  void *sendto, s32 log_fd, u8 *log_sa)
{
    /* Free the previous session's memory before we overwrite anything.
       Both __pool and the fallback mmap regions in fb_ptr[] are PERSIST,
       so they point at the *last* session's mappings.  Without this
       munmap, every shellcode launch leaks ~32 MB and after a handful
       of relaunches the console is out of pool memory. */
    if (__pool || fb_n > 0) {
        void *munmap_fn = SYM(G, D, LIBKERNEL_HANDLE, "munmap");
        if (!munmap_fn)
            munmap_fn = SYM(G, D, LIBKERNEL_HANDLE, "sceKernelMunmap");
        if (munmap_fn) {
            if (__pool) {
                NC(G, munmap_fn, (u64)__pool, (u64)__pool_sz, 0,0,0,0);
                log_num_u64("ps_libc: freed prior pool bytes = ",
                            (u64)__pool_sz);
            }
            for (int i = 0; i < fb_n; i++) {
                if (fb_ptr[i]) {
                    NC(G, munmap_fn, (u64)fb_ptr[i],
                       (u64)fb_sz[i], 0,0,0,0);
                }
            }
        }
    }

    __G = G; __D = D;
    fn_mmap = mmap_fn;
    fn_kopen = kopen; fn_kread = kread; fn_kwrite = kwrite;
    fn_kclose = kclose; fn_klseek = klseek; fn_kmkdir = kmkdir;
    fn_sendto = sendto;
    __log_fd = log_fd;
    if (log_sa) for (int i = 0; i < 16; i++) __log_sa[i] = log_sa[i];
    __pool = 0; __pool_sz = 0; __pool_used = 0;
    __free_list = 0;
    fb_n = 0;
}

static void pool_init(void) {
    if (__pool) return;
    const u64 sizes[] = { 32*1024*1024, 16*1024*1024, 8*1024*1024,
                          4*1024*1024,  2*1024*1024,  1*1024*1024,
                          512*1024, 256*1024 };
    const u32 PROT = 3;
    const u32 MAPA = 0x1002;
    for (int i = 0; i < 8; i++) {
        void *p = (void*)NC(__G, fn_mmap, 0, sizes[i], PROT, MAPA, (u64)-1, 0);
        if ((s64)p != -1 && p) {
            __pool = p;
            __pool_sz = sizes[i];
            log_num_u64("ps_libc: pool = ", (u64)sizes[i]);
            return;
        }
    }
    log_str("ps_libc: pool allocation failed entirely\n");
}

void ps_libc_reset_pool(void) {
    __pool_used = 0;
    __free_list = 0;
}

/* Returns the size in bytes of the memory pool that was actually
   granted.  Triggers pool_init if it hasn't run yet.  Returns 0 if
   even the smallest attempt failed. */
u64 ps_libc_pool_size(void) {
    if (!__pool) pool_init();
    return (u64)__pool_sz;
}

void *malloc(size_t n) {
    if (n == 0) n = 16;
    n = (n + 15) & ~(size_t)15;

    blk_t *b = __free_list;
    blk_t *prev = 0;
    while (b) {
        if (b->magic == FREE_MAGIC && b->size >= n) {
            if (prev) prev->next = b->next;
            else      __free_list = b->next;

            if (b->size >= n + BLK_SIZE + 16) {
                blk_t *rest = (blk_t*)((u8*)b + BLK_SIZE + n);
                rest->size  = b->size - (u32)n - BLK_SIZE;
                rest->magic = FREE_MAGIC;
                rest->next  = __free_list;
                rest->_pad  = 0;
                __free_list = rest;
                b->size = (u32)n;
            }
            b->magic = BLK_MAGIC;
            b->next  = 0;
            return (void*)((u8*)b + BLK_SIZE);
        }
        prev = b;
        b = b->next;
    }

    if (!__pool) pool_init();
    if (__pool && __pool_used + BLK_SIZE + n <= __pool_sz) {
        blk_t *nb = (blk_t*)(__pool + __pool_used);
        __pool_used += BLK_SIZE + n;
        nb->size  = (u32)n;
        nb->magic = BLK_MAGIC;
        nb->next  = 0;
        nb->_pad  = 0;
        return (void*)((u8*)nb + BLK_SIZE);
    }

    if (fn_mmap) {
        u64 total = BLK_SIZE + n;
        void *p = (void*)NC(__G, fn_mmap, 0, total, 3, 0x1002, (u64)-1, 0);
        if ((s64)p != -1 && p) {
            blk_t *nb = (blk_t*)p;
            nb->size  = (u32)n;
            nb->magic = BLK_MAGIC;
            nb->next  = 0;
            nb->_pad  = 0;
            if (fb_n < FB_MAX) {
                fb_ptr[fb_n] = p;
                fb_sz [fb_n] = total;
                fb_n++;
            }
            return (void*)((u8*)nb + BLK_SIZE);
        }
    }

    log_str("ps_libc: OOM\n");
    return 0;
}

void free(void *p) {
    if (!p) return;
    blk_t *b = (blk_t*)((u8*)p - BLK_SIZE);
    if (b->magic != BLK_MAGIC) return;
    b->magic = FREE_MAGIC;
    b->next  = __free_list;
    __free_list = b;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (n == 0) { free(p); return 0; }
    blk_t *b = (blk_t*)((u8*)p - BLK_SIZE);
    size_t old = (b->magic == BLK_MAGIC) ? b->size : 0;
    void *q = malloc(n);
    if (q) {
        size_t copy = old < n ? old : n;
        if (copy) memcpy(q, p, copy);
        free(p);
    }
    return q;
}

void *calloc(size_t n, size_t sz) {
    void *p = malloc(n * sz);
    if (p) memset(p, 0, n * sz);
    return p;
}

void *memcpy(void *d, const void *s, size_t n) {
    __asm__ volatile ("rep movsb" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
    return d;
}
void *memset(void *s, int c, size_t n) {
    void *r = s;
    __asm__ volatile ("rep stosb" : "+D"(s), "+c"(n) : "a"(c) : "memory");
    return r;
}
void *memmove(void *d, const void *s, size_t n) {
    if (d < s) return memcpy(d, s, n);
    u8 *dd = d; const u8 *ss = s;
    for (size_t i = n; i; i--) dd[i-1] = ss[i-1];
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const u8 *x = a, *y = b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (u8)a[i] - (u8)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (u8)*a - (u8)*b;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)); return r; }
char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
char *strcat(char *d, const char *s) { char *r = d; while (*d) d++; while ((*d++ = *s++)); return r; }
char *strchr(const char *s, int c) { for (; *s; s++) if (*s == (char)c) return (char*)s; return 0; }
char *strrchr(const char *s, int c) {
    const char *l = 0;
    for (; *s; s++) if (*s == (char)c) l = s;
    return (char*)l;
}
char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
int atoi(const char *s) {
    int v = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v*10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

static int fmt_int(char *b, int max, long v, int base, int upper,
                   int width, int zero)
{
    char tmp[24]; int n = 0, neg = 0;
    unsigned long m;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v < 0 && base == 10) { neg = 1; m = (unsigned long)-v; }
    else m = (unsigned long)v;
    if (m == 0) tmp[n++] = '0';
    while (m) { tmp[n++] = dig[m % base]; m /= base; }
    int total = n + neg;
    int pad = width > total ? width - total : 0;
    int out = 0;
    char pc = zero ? '0' : ' ';
    if (!zero) for (int i = 0; i < pad && out < max; i++) b[out++] = pc;
    if (neg && out < max) b[out++] = '-';
    if (zero)  for (int i = 0; i < pad && out < max; i++) b[out++] = pc;
    for (int i = n - 1; i >= 0 && out < max; i--) b[out++] = tmp[i];
    return out;
}

int vsnprintf(char *b, size_t n, const char *fmt, VA ap) {
    int out = 0, max = (int)n - 1;
    if (max < 0) max = 0;
    while (*fmt && out < max) {
        if (*fmt != '%') { b[out++] = *fmt++; continue; }
        fmt++;
        int zero = 0;
        while (*fmt == '0') { zero = 1; fmt++; }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width*10 + (*fmt++ - '0');
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; if (*fmt == 'l') fmt++; }
        else if (*fmt == 'z') { is_long = 1; fmt++; }
        switch (*fmt) {
        case 'd': case 'i': {
            long v = is_long ? VA_ARG(ap, long) : (long)VA_ARG(ap, int);
            out += fmt_int(b+out, max-out, v, 10, 0, width, zero);
            break; }
        case 'u': {
            unsigned long v = is_long ? VA_ARG(ap, unsigned long)
                                      : (unsigned long)VA_ARG(ap, unsigned);
            out += fmt_int(b+out, max-out, (long)v, 10, 0, width, zero);
            break; }
        case 'x': {
            unsigned long v = is_long ? VA_ARG(ap, unsigned long)
                                      : (unsigned long)VA_ARG(ap, unsigned);
            out += fmt_int(b+out, max-out, (long)v, 16, 0, width, zero);
            break; }
        case 'X': {
            unsigned long v = is_long ? VA_ARG(ap, unsigned long)
                                      : (unsigned long)VA_ARG(ap, unsigned);
            out += fmt_int(b+out, max-out, (long)v, 16, 1, width, zero);
            break; }
        case 'p': {
            void *v = VA_ARG(ap, void*);
            if (out < max) b[out++] = '0';
            if (out < max) b[out++] = 'x';
            out += fmt_int(b+out, max-out, (long)(u64)v, 16, 0, 0, 0);
            break; }
        case 's': {
            const char *s = VA_ARG(ap, const char*);
            if (!s) s = "(null)";
            while (*s && out < max) b[out++] = *s++;
            break; }
        case 'c': {
            int c = VA_ARG(ap, int);
            if (out < max) b[out++] = (char)c;
            break; }
        case '%': if (out < max) b[out++] = '%'; break;
        default:
            if (out < max) b[out++] = '%';
            if (out < max) b[out++] = *fmt;
            break;
        }
        fmt++;
    }
    if (n) b[out < (int)n ? out : (int)n-1] = 0;
    return out;
}

int snprintf(char *b, size_t n, const char *fmt, ...) {
    VA ap; VA_START(ap, fmt);
    int r = vsnprintf(b, n, fmt, ap);
    VA_END(ap);
    return r;
}

int printf(const char *fmt, ...) {
    char buf[256];
    VA ap; VA_START(ap, fmt);
    int r = vsnprintf(buf, sizeof(buf), fmt, ap);
    VA_END(ap);
    log_str(buf);
    return r;
}

#define MAX_FILES 16
PERSIST static FILE __files[MAX_FILES];
PERSIST static int  __files_inited;

static FILE *alloc_file(void) {
    if (!__files_inited) {
        for (int i = 0; i < MAX_FILES; i++) __files[i].fd = -1;
        __files_inited = 1;
    }
    for (int i = 0; i < MAX_FILES; i++) if (__files[i].fd < 0) return &__files[i];
    return 0;
}

FILE *fopen(const char *path, const char *mode) {
    int flags = 0;
    if (mode[0] == 'r')      flags = 0;
    else if (mode[0] == 'w') flags = 0x0001 | 0x0200 | 0x0400;
    else if (mode[0] == 'a') flags = 0x0001 | 0x0200 | 0x0008;

    s32 fd = (s32)NC(__G, fn_kopen, (u64)path, (u64)flags, 0x1FF, 0, 0, 0);
    if (fd < 0) return 0;
    FILE *f = alloc_file();
    if (!f) { NC(__G, fn_kclose, (u64)fd, 0,0,0,0,0); return 0; }
    f->fd = fd; f->eof = 0; f->err = 0; f->buf = 0; f->buf_len = f->buf_pos = 0;
    return f;
}

size_t fread(void *p, size_t sz, size_t n, FILE *f) {
    if (!f || f->fd < 0) return 0;
    size_t total = sz * n;
    u8 *dst = (u8*)p;
    size_t done = 0;
    while (done < total) {
        s32 got = (s32)NC(__G, fn_kread, (u64)f->fd, (u64)(dst+done),
                          (u64)(total-done), 0,0,0);
        if (got <= 0) { f->eof = 1; break; }
        done += got;
    }
    return done / sz;
}

size_t fwrite(const void *p, size_t sz, size_t n, FILE *f) {
    if (!f || f->fd < 0) return 0;
    size_t total = sz * n;
    const u8 *src = p;
    size_t done = 0;
    while (done < total) {
        s32 w = (s32)NC(__G, fn_kwrite, (u64)f->fd, (u64)(src+done),
                        (u64)(total-done), 0,0,0);
        if (w <= 0) { f->err = 1; break; }
        done += w;
    }
    return done / sz;
}

int fclose(FILE *f) {
    if (!f || f->fd < 0) return EOF;
    int r = (s32)NC(__G, fn_kclose, (u64)f->fd, 0,0,0,0,0);
    f->fd = -1;
    return r == 0 ? 0 : EOF;
}

int fseek(FILE *f, long off, int whence) {
    if (!f || f->fd < 0) return -1;
    s64 r = (s64)NC(__G, fn_klseek, (u64)f->fd, (u64)off, (u64)whence, 0,0,0);
    if (r < 0) return -1;
    f->eof = 0;
    return 0;
}

long ftell(FILE *f) {
    if (!f || f->fd < 0) return -1;
    s64 r = (s64)NC(__G, fn_klseek, (u64)f->fd, 0, 1, 0,0,0);
    return r < 0 ? -1 : (long)r;
}

int feof(FILE *f) { return f ? f->eof : 1; }

int mkdir(const char *path, unsigned int mode) {
    return (s32)NC(__G, fn_kmkdir, (u64)path, (u64)mode, 0,0,0,0);
}

int remove(const char *path) { (void)path; return -1; }

void exit(int code) { (void)code; for (;;) {} }
void abort(void)    { for (;;) {} }
