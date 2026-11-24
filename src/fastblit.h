/* fastblit.h: the two inner loops of WA 3.8.1's software "surface blit" (005A2790), 16 pixels at a time.
 *
 * WA copies every land tile, sky strip and back layer onto its 8-bit frame with that function:
 *   keyed  (005A28F7..005A293C): a byte loop that copies each pixel unless it is the colour key. The key (a dword from
 *          the surface) is compared with the pixel widened to a dword, so a key over 255 means "copy everything".
 *   opaque (005A293E..005A295E): one call of the C library's memcpy (005DC530, which copes with overlap as memmove
 *          does) a row -- the sky gradient is thousands of rows of 64 pixels a frame.
 * Both come here with edi = the first pixel on the frame, esi = the first source pixel, ebx = the width,
 * [ebp+20h] = the height, [esp+30h] = the source's row stride; the frame's row stride is [007A0EF0] (its width) for the
 * keyed loop and [esp+1Ch] (from the lock) for the opaque one; the key is at [esp+18h]. Both go on at 005A2960, which
 * reloads eax, ecx and edx before it uses them, and doesn't read ebx, esi, edi or the flags again: nothing has to be
 * handed back.
 *
 * The result is WA's, byte for byte. Where the source and the frame overlap (WA never does that) the order of the plain
 * loops would show, so such a call gets the plain loops. */
#ifndef FASTBLIT_H
#define FASTBLIT_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <emmintrin.h>

#define FB_KEYED_VA 0x5a28f7u               /* mov eax,[ebp+20h]; test eax,eax */
#define FB_OPAQUE_VA 0x5a293eu              /* test ecx,ecx; jle 005A2960; mov [esp+10h],ecx */
#define FB_BACK_VA 0x5a2960u                /* after the loops */
#define FB_FRAME_W_VA 0x7a0ef0u             /* the frame's width = its row stride */
#define FB_PATCH 5                          /* bytes: jmp rel32 */

static const int32_t *fb_frame_stride;      /* -> [007A0EF0] */
void *fb_back;                              /* -> 005A2960 (read by the thunks) */

/* WA's loops as they are, in C (the overlap case and the tests' reference) */
static void fb_plain(uint8_t *d, const uint8_t *s, int w, int h, int ds, int ss, uint32_t key, int keyed) {
    int x, y;
    if (w <= 0) return;
    for (y = 0; y < h; y++, d += ds, s += ss) {
        if (!keyed) { memmove(d, s, (size_t)w); continue; }
        for (x = 0; x < w; x++) if (s[x] != key) d[x] = s[x];
    }
}

/* the bytes from the first row to the last, each way (strides may be negative) */
static int fb_overlap(const uint8_t *d, const uint8_t *s, int w, int h, int ds, int ss) {
    const intptr_t dn = (intptr_t)ds * (h - 1), sn = (intptr_t)ss * (h - 1);
    const uintptr_t d0 = (uintptr_t)d + (dn < 0 ? dn : 0), d1 = (uintptr_t)d + (dn > 0 ? dn : 0) + w;
    const uintptr_t s0 = (uintptr_t)s + (sn < 0 ? sn : 0), s1 = (uintptr_t)s + (sn > 0 ? sn : 0) + w;
    return d0 < s1 && s0 < d1;
}

static inline __m128i fb_load(const uint8_t *p) { return _mm_loadu_si128((const __m128i *)p); }

/* 16 pixels: those that are not the key. (a = 1: d is 16-byte aligned) */
static inline void fb_key16(uint8_t *d, const uint8_t *s, __m128i k, int a) {
    const __m128i p = fb_load(s), m = _mm_cmpeq_epi8(p, k);
    const int mm = _mm_movemask_epi8(m);
    __m128i o = p;
    if (mm == 0xffff) return;
    if (mm) o = _mm_or_si128(_mm_and_si128(m, fb_load(d)), _mm_andnot_si128(m, p));
    if (a) _mm_store_si128((__m128i *)d, o); else _mm_storeu_si128((__m128i *)d, o);
}

/* The first 16 pixels, then aligned 16s, then (if some are left) the last 16: pixels two of them cover are written twice
 * with the same result (source and frame don't overlap here). */
static void fb_fast(uint8_t *d, const uint8_t *s, int w, int h, int ds, int ss, uint32_t key, int keyed) {
    const __m128i k = _mm_set1_epi8((char)key);
    int x, y;
    if (w <= 0) return;
    if (keyed && key > 255u) keyed = 0;
    if (w < 16) {
        for (y = 0; y < h; y++, d += ds, s += ss) {
            if (keyed) { for (x = 0; x < w; x++) if (s[x] != key) d[x] = s[x]; }
            else for (x = 0; x < w; x++) d[x] = s[x];
        }
        return;
    }
    for (y = 0; y < h; y++, d += ds, s += ss) {
        const int first = 16 - (int)((uintptr_t)d & 15u);     /* (1..16: where the frame is aligned) */
        if (keyed) {
            fb_key16(d, s, k, 0);
            for (x = first; x + 16 <= w; x += 16) fb_key16(d + x, s + x, k, 1);
            if (x < w) fb_key16(d + w - 16, s + w - 16, k, 0);
        } else if (w >= 1024) {
            memcpy(d, s, (size_t)w);                          /* (long rows: the library's copy, as WA's own) */
        } else {
            _mm_storeu_si128((__m128i *)d, fb_load(s));
            for (x = first; x + 16 <= w; x += 16) _mm_store_si128((__m128i *)(d + x), fb_load(s + x));
            if (x < w) _mm_storeu_si128((__m128i *)(d + w - 16), fb_load(s + w - 16));
        }
    }
}

#ifndef FB_RUN                              /* (a test build puts its own checks around it) */
static void fb_run(uint8_t *d, const uint8_t *s, int w, int h, int ds, int ss, uint32_t key, int keyed) {
    if (w <= 0 || h <= 0) return;
    if (fb_overlap(d, s, w, h, ds, ss)) fb_plain(d, s, w, h, ds, ss, key, keyed);
    else fb_fast(d, s, w, h, ds, ss, key, keyed);
}
#define FB_RUN fb_run
#endif

void __cdecl fb_keyed(uint8_t *d, const uint8_t *s, int w, int h, int ss, uint32_t key) {
    FB_RUN(d, s, w, h, *fb_frame_stride, ss, key, 1);
}
void __cdecl fb_opaque(uint8_t *d, const uint8_t *s, int w, int h, int ss, int ds) {
    FB_RUN(d, s, w, h, ds, ss, 0, 0);
}

/* jumped to from WA's code (the stack is WA's, as above) */
__asm__(
    ".text\n"
    ".globl _fb_thunk_keyed\n_fb_thunk_keyed:\n"
    "    movl 0x18(%esp), %eax\n    movl 0x30(%esp), %ecx\n    movl 0x20(%ebp), %edx\n"
    "    pushl %eax\n    pushl %ecx\n    pushl %edx\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    call _fb_keyed\n    addl $24, %esp\n    jmp *_fb_back\n"
    ".globl _fb_thunk_opaque\n_fb_thunk_opaque:\n"
    "    movl 0x1c(%esp), %eax\n    movl 0x30(%esp), %ecx\n    movl 0x20(%ebp), %edx\n"
    "    pushl %eax\n    pushl %ecx\n    pushl %edx\n    pushl %ebx\n    pushl %esi\n    pushl %edi\n"
    "    call _fb_opaque\n    addl $24, %esp\n    jmp *_fb_back\n");
extern void fb_thunk_keyed(void), fb_thunk_opaque(void);

static const uint8_t fb_keyed_was[FB_PATCH] = {0x8b, 0x45, 0x20, 0x85, 0xc0};
static const uint8_t fb_opaque_was[FB_PATCH] = {0x85, 0xc9, 0x7e, 0x1e, 0x89};

/* the jump from `at` to `to` */
static void fb_jump(uint8_t out[FB_PATCH], uintptr_t at, uintptr_t to) {
    const uint32_t rel = (uint32_t)(to - (at + FB_PATCH));
    out[0] = 0xe9;
    memcpy(out + 1, &rel, 4);
}
#endif
