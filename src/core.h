/* SPDX-License-Identifier: MIT */

#ifndef AUDIOC0RE_CORE_H
#define AUDIOC0RE_CORE_H

typedef __UINT8_TYPE__   u8;
typedef __UINT16_TYPE__  u16;
typedef __UINT32_TYPE__  u32;
typedef __UINT64_TYPE__  u64;

typedef __INT8_TYPE__    s8;
typedef __INT16_TYPE__   s16;
typedef __INT32_TYPE__   s32;
typedef __INT64_TYPE__   s64;

typedef __INTPTR_TYPE__  sptr;
typedef __UINTPTR_TYPE__ uptr;

typedef __SIZE_TYPE__    size_t;
typedef __PTRDIFF_TYPE__ ssize_t;
typedef __SIZE_TYPE__    usize;
typedef __PTRDIFF_TYPE__ isize;

#ifndef NULL
#  define NULL ((void *)0)
#endif

#define ARRAY_LEN(a)      (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)         ((a) < (b) ? (a) : (b))
#define MAX(a, b)         ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi)  ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define UNUSED(x)         ((void)(x))

#define BIT(n)            (1ULL << (n))
#define BIT_MASK(n)       ((n) >= 64 ? ~0ULL : (BIT(n) - 1ULL))

#define ALIGN_UP(x, a)    (((x) + ((u64)(a) - 1)) & ~((u64)(a) - 1))
#define ALIGN_DOWN(x, a)  ((x) & ~((u64)(a) - 1))
#define IS_ALIGNED(x, a)  (((x) & ((a) - 1)) == 0)

#define STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)

#define HOST_GADGET_OFFSET      0x31AA9
#define HOST_GS_THREAD_OFFSET   0x057F89B0
#define HOST_VIDOUT_OFFSET      0x02d695d0
#define HOST_LIBKERNEL_HANDLE   0x2001

#define GADGET_OFFSET           HOST_GADGET_OFFSET
#define EBOOT_GS_THREAD         HOST_GS_THREAD_OFFSET
#define EBOOT_VIDOUT            HOST_VIDOUT_OFFSET
#define LIBKERNEL_HANDLE        HOST_LIBKERNEL_HANDLE

#define SCR_W        1920
#define SCR_H        1080
#define SCR_PIXELS   (SCR_W * SCR_H)

#define FB_BYTES     (SCR_PIXELS * 4)
#define FB_STRIDE    ALIGN_UP(FB_BYTES, 0x200000)
#define FB_TOTAL     (FB_STRIDE * 2)

#define FB_SIZE      FB_BYTES
#define FB_ALIGNED   FB_STRIDE

#define ARGB(a, r, g, b) \
    (((u32)(a) << 24) | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

#define RGB(r, g, b) ARGB(0xFF, (r), (g), (b))

#define AUDIO_RATE       48000
#define AUDIO_FRAMES     2048
#define AUDIO_FORMAT     1              

#define SAMPLE_RATE      AUDIO_RATE
#define SAMPLES_PER_BUF  AUDIO_FRAMES
#define AUDIO_S16_STEREO AUDIO_FORMAT

__attribute__((naked, noinline, unused))
static u64 native_call(void *gadget, void *fn,
                       u64 a1, u64 a2, u64 a3,
                       u64 a4, u64 a5, u64 a6)
{
    __asm__ volatile (
        "pushq %%rbx\n\t"
        "movq %%rsi, %%rbx\n\t"      
        "movq %%rdi, %%rax\n\t"      
        "movq %%rdx, %%rdi\n\t"      
        "movq %%rcx, %%rsi\n\t"      
        "movq %%r8,  %%rdx\n\t"      
        "movq %%r9,  %%rcx\n\t"      
        "movq 16(%%rsp), %%r8\n\t"   
        "movq 24(%%rsp), %%r9\n\t"   
        "callq *%%rax\n\t"           
        "popq %%rbx\n\t"
        "retq"
        ::: "memory"
    );
}

__attribute__((unused))
static void *resolve_sym(void *gadget, void *dlsym_fn,
                         s32 handle, const char *name)
{
    void *addr = 0;
    native_call(gadget, dlsym_fn,
                (u64)(s64)handle,
                (u64)name,
                (u64)&addr,
                0, 0, 0);
    return addr;
}

#define NC   native_call
#define SYM  resolve_sym

#define PERSIST  __attribute__((section(".ps_persist")))

static inline int s_len(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static inline void s_cpy(char *dst, const char *src) {
    while ((*dst++ = *src++)) { }
}

static inline int s_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}

static inline int s_eq(const char *a, const char *b) {
    return s_cmp(a, b) == 0;
}

static inline void m_set(void *dst, u8 val, u64 n) {
    u8 *p = (u8 *)dst;
    while (n--) *p++ = val;
}

static inline void m_cpy(void *dst, const void *src, u64 n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--) *d++ = *s++;
}

static inline u32 rd_u32le(const void *p) {
    const u8 *b = (const u8 *)p;
    return (u32)b[0] | ((u32)b[1] << 8)
         | ((u32)b[2] << 16) | ((u32)b[3] << 24);
}

static inline u64 rd_u64le(const void *p) {
    const u8 *b = (const u8 *)p;
    return (u64)rd_u32le(b) | ((u64)rd_u32le(b + 4) << 32);
}

static inline int s_itoa(char *out, int v) {
    char tmp[12];
    int n = 0, p = 0;

    if (v == 0) {
        out[p++] = '0';
        out[p] = 0;
        return p;
    }
    if (v < 0) {
        out[p++] = '-';
        u32 u = (u32)(-(v + 1)) + 1u;
        while (u) { tmp[n++] = '0' + (u % 10); u /= 10; }
    } else {
        u32 u = (u32)v;
        while (u) { tmp[n++] = '0' + (u % 10); u /= 10; }
    }
    while (n) out[p++] = tmp[--n];
    out[p] = 0;
    return p;
}

static inline int s_hex64(char *out, u64 v) {
    static const char hexdigits[] = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; i++) {
        out[2 + i] = hexdigits[(v >> ((15 - i) * 4)) & 0xF];
    }
    out[18] = 0;
    return 18;
}

struct ext_args {
    s64 status;
    s64 step;
    u32 frame_count;
    u32 _pad;
    s32 log_fd;
    s32 pad_fd;
    u8  log_addr[16];
    u64 dbg[8];
};

STATIC_ASSERT(sizeof(u8)     == 1, "u8 must be exactly 1 byte");
STATIC_ASSERT(sizeof(u16)    == 2, "u16 must be exactly 2 bytes");
STATIC_ASSERT(sizeof(u32)    == 4, "u32 must be exactly 4 bytes");
STATIC_ASSERT(sizeof(u64)    == 8, "u64 must be exactly 8 bytes");
STATIC_ASSERT(sizeof(s8)     == 1, "s8 must be exactly 1 byte");
STATIC_ASSERT(sizeof(s16)    == 2, "s16 must be exactly 2 bytes");
STATIC_ASSERT(sizeof(s32)    == 4, "s32 must be exactly 4 bytes");
STATIC_ASSERT(sizeof(s64)    == 8, "s64 must be exactly 8 bytes");
STATIC_ASSERT(sizeof(void *) == 8, "pointers must be 8 bytes (LP64)");
STATIC_ASSERT(sizeof(size_t) == 8, "size_t must be 8 bytes on x86_64");

STATIC_ASSERT(sizeof(struct ext_args) == 0x70,
              "ext_args must be exactly 0x70 bytes");
STATIC_ASSERT(__builtin_offsetof(struct ext_args, log_fd)   == 0x18,
              "ext_args.log_fd must be at 0x18");
STATIC_ASSERT(__builtin_offsetof(struct ext_args, log_addr) == 0x20,
              "ext_args.log_addr must be at 0x20");
STATIC_ASSERT(__builtin_offsetof(struct ext_args, dbg)      == 0x30,
              "ext_args.dbg must be at 0x30");

#endif /* AUDIOC0RE_CORE_H */
