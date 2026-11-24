/* blit_test: the module's blit against (1) the plain loops and (2) WA's own machine code, run here.
 *
 * (2) takes the loops' bytes (005A28C2..005A2976) out of WA.exe (WA_EXE, else Steam's usual folder; skipped where there
 * is none), puts two copies in executable memory -- one as it is, one with the module's two jumps written into it -- and
 * calls both with the stack frame and registers WA has there, on the same made-up pictures. Compared after each call:
 * every byte of the frame and of the guard areas around it, the source (must be untouched), the stack pointer, ebp, and
 * WA's own stack variables. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/fastblit.h"

static uint32_t seed = 12345;
static uint32_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static int range(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

#define AREA (3 << 20)
static uint8_t *a0, *a1, *src0, *src1;      /* the two frames, the two sources */

/* source pixels: single pixels, runs, mostly key, no key */
static void fill_source(uint8_t *p, size_t n, uint32_t key, int how) {
    size_t i = 0;
    const uint8_t k = (uint8_t)key;
    while (i < n) {
        size_t run = how == 0 ? 1 : (size_t)range(1, how == 1 ? 40 : 200);
        const int is_key = how == 3 ? 0 : how == 2 ? range(0, 9) < 8 : range(0, 1);
        if (run > n - i) run = n - i;
        if (is_key) memset(p + i, k, run);
        else { size_t j; for (j = 0; j < run; j++) { p[i + j] = (uint8_t)rnd(); if (how == 3 && p[i + j] == k) p[i + j] ^= 1; } }
        i += run;
    }
}

typedef struct { int w, h, ds, ss, keyed, how; uint32_t key; size_t doff, soff; int overlap; } call_t;

static void make_call(call_t *c) {
    static const uint32_t keys[] = {0, 1, 127, 128, 254, 255, 256, 0x1ff, 0xffffffffu, 0x80000000u};
    int pad;
    c->w = range(0, 9) ? range(1, 320) : range(1, 2600);
    c->h = range(0, 30) ? range(1, 24) : range(-3, 1);
    c->keyed = range(0, 2) != 0;
    c->key = range(0, 1) ? keys[range(0, 9)] : (rnd() & 255u);
    c->how = range(0, 3);
    pad = range(0, 70);
    c->ds = c->w + pad;
    c->ss = c->w + range(0, 70);
    c->doff = 70000 + (size_t)range(0, 63);
    c->soff = 70000 + (size_t)range(0, 63);
    c->overlap = range(0, 19) == 0;         /* (source inside the frame's memory) */
    if (c->h > 0 && range(0, 7) == 0) { c->doff += (size_t)c->ds * (size_t)(c->h - 1); c->ds = -c->ds; }
    if (c->h > 0 && range(0, 7) == 0) { c->soff += (size_t)c->ss * (size_t)(c->h - 1); c->ss = -c->ss; }
    if (c->overlap) c->soff = c->doff + (size_t)range(0, 400) - 200;
}

static int plain_tests(int n) {
    int i;
    for (i = 0; i < n; i++) {
        call_t c;
        size_t j;
        make_call(&c);
        c.overlap = 0;
        for (j = 60000; j < 200000; j++) a0[j] = (uint8_t)rnd();
        memcpy(a1 + 60000, a0 + 60000, 140000);
        fill_source(src0 + 60000, 140000, c.key, c.how);
        if (c.h > 0) fb_plain(a0 + c.doff, src0 + c.soff, c.w, c.h, c.ds, c.ss, c.key, c.keyed);
        fb_run(a1 + c.doff, src0 + c.soff, c.w, c.h, c.ds, c.ss, c.key, c.keyed);
        if (memcmp(a0 + 60000, a1 + 60000, 140000)) {
            printf("blit_test: plain and fast differ: %dx%d strides %d %d key %lu keyed %d source %d\n", c.w, c.h, c.ds, c.ss,
                   (unsigned long)c.key, c.keyed, c.how);
            return 0;
        }
    }
    printf("blit_test: %d blits, the same as the plain loops\n", n);
    return 1;
}

/* every width and alignment, edges against unreadable pages: nothing past the row is read or written */
static int edge_tests(void) {
    SYSTEM_INFO si;
    uint8_t *d, *s, *dl, *sl;
    DWORD old;
    size_t page, len;
    int w, keyed, k = 0;
    GetSystemInfo(&si);
    page = si.dwPageSize;
    len = page * 4;
    d = (uint8_t *)VirtualAlloc(NULL, len + 2 * page, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    s = (uint8_t *)VirtualAlloc(NULL, len + 2 * page, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!d || !s) return 0;
    VirtualProtect(d, page, PAGE_NOACCESS, &old); VirtualProtect(d + page + len, page, PAGE_NOACCESS, &old);
    VirtualProtect(s, page, PAGE_NOACCESS, &old); VirtualProtect(s + page + len, page, PAGE_NOACCESS, &old);
    dl = d + page; sl = s + page;
    for (w = 1; w <= 2200; w += w < 140 ? 1 : 37)
        for (keyed = 0; keyed < 2; keyed++) {
            int at;
            for (at = 0; at < 4; at++) {    /* both at the start, both at the end, and crossed */
                uint8_t *dp = at & 1 ? dl + len - w : dl;
                const uint8_t *sp = at & 2 ? sl + len - w : sl;
                int x, ok = 1;
                for (x = 0; x < w; x++) { dp[x] = (uint8_t)rnd(); ((uint8_t *)sp)[x] = range(0, 1) ? 7 : (uint8_t)rnd(); a0[x] = dp[x]; }
                fb_plain(a0, sp, w, 1, w, w, 7, keyed);
                fb_run(dp, sp, w, 1, w, w, 7, keyed);
                for (x = 0; x < w; x++) if (a0[x] != dp[x]) ok = 0;
                if (!ok) { printf("blit_test: edge row of %d differs\n", w); return 0; }
                k++;
            }
        }
    VirtualFree(d, 0, MEM_RELEASE); VirtualFree(s, 0, MEM_RELEASE);
    printf("blit_test: %d rows against unreadable pages, nothing past a row touched\n", k);
    return 1;
}

/* ---- WA's own code ---- */
#define C0 0x5a28c2u
#define C1 0x5a2976u
#define ENTRY 0x5a28dau                     /* test byte [ebp+24h],2: eax = frame pixel, esi = source pixel, ebx = w, ecx = h */
typedef struct { void *surface; int32_t w, h, flags, lock_stride, src_stride; void *dst; const void *src; } ctx_t;
void *entry_ptr, *run_saved_esp, *run_entry_esp, *run_entry_ebp, *run_exit_esp, *run_exit_ebp;
uint32_t test_key;
int32_t frame_w;
uint32_t run_locals[20];                    /* WA's stack variables [esp+10h..] as they are at 005A2960 */
extern void run_code(void *entry, const ctx_t *c), exit_stub(void), key_stub(void);
__asm__(
    ".text\n.globl _run_code\n_run_code:\n"
    " pushl %ebp\n pushl %ebx\n pushl %esi\n pushl %edi\n"
    " movl 20(%esp),%eax\n movl %eax,_entry_ptr\n movl 24(%esp),%edx\n movl %esp,_run_saved_esp\n"
    " subl $0x200,%esp\n andl $-8,%esp\n leal 0x100(%esp),%ebp\n"
    " xorl %ecx,%ecx\n1: movl $0x5a5a0000,%eax\n addl %ecx,%eax\n movl %eax,0x10(%esp,%ecx,4)\n incl %ecx\n cmpl $20,%ecx\n jb 1b\n"
    " movl 0(%edx),%ecx\n movl %ecx,0x8(%ebp)\n movl 4(%edx),%ecx\n movl %ecx,0x1c(%ebp)\n"
    " movl 8(%edx),%ecx\n movl %ecx,0x20(%ebp)\n movl 12(%edx),%ecx\n movl %ecx,0x24(%ebp)\n"
    " movl 16(%edx),%ecx\n movl %ecx,0x1c(%esp)\n movl 20(%edx),%ecx\n movl %ecx,0x30(%esp)\n"
    " movl %esp,_run_entry_esp\n movl %ebp,_run_entry_ebp\n"
    " movl 4(%edx),%ebx\n movl 28(%edx),%esi\n movl 8(%edx),%ecx\n movl 24(%edx),%eax\n xorl %edx,%edx\n jmp *_entry_ptr\n"
    ".globl _exit_stub\n_exit_stub:\n"
    " movl %esp,_run_exit_esp\n movl %ebp,_run_exit_ebp\n cld\n"
    " movl _run_entry_esp,%edx\n xorl %ecx,%ecx\n2: movl 0x10(%edx,%ecx,4),%eax\n movl %eax,_run_locals(,%ecx,4)\n incl %ecx\n cmpl $20,%ecx\n jb 2b\n"
    " movl _run_saved_esp,%esp\n popl %edi\n popl %esi\n popl %ebx\n popl %ebp\n ret\n"
    ".globl _key_stub\n_key_stub:\n movl 4(%esp),%eax\n movl _test_key,%ecx\n movl %ecx,(%eax)\n movl $0xcccccccc,%ecx\n movl $0xdddddddd,%edx\n ret $8\n");
static void *__cdecl wa_memcpy(void *d, const void *s, size_t n) { return memmove(d, s, n); }   /* 005DC530 copes with overlap */

static unsigned char *load_wa(DWORD *n) {
    char path[MAX_PATH];
    unsigned char *b = NULL;
    LARGE_INTEGER z;
    HANDLE f;
    if (!GetEnvironmentVariableA("WA_EXE", path, sizeof(path)))
        strcpy(path, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Worms Armageddon\\WA.exe");
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    if (GetFileSizeEx(f, &z) && (b = (unsigned char *)malloc((size_t)z.QuadPart)) && !ReadFile(f, b, (DWORD)z.QuadPart, n, NULL)) { free(b); b = NULL; }
    CloseHandle(f);
    return b;
}

static void put_jump(uint8_t *at, const void *to) { fb_jump(at, (uintptr_t)at, (uintptr_t)to); }

static int wa_tests(int n) {
    DWORD size = 0;
    unsigned char *f = load_wa(&size);
    uint8_t *code, *orig, *mod;
    void *vt[9], *surface = vt;
    uint32_t pe, o, rva, rp, rs, a;
    int i, overlaps = 0, keyed = 0;
    if (!f) { printf("blit_test: no WA.exe here: the test against WA's own code was skipped\n"); return 1; }
    pe = *(uint32_t *)(f + 0x3c);
    o = pe + 24 + *(uint16_t *)(f + pe + 20);
    rva = *(uint32_t *)(f + o + 12); rs = *(uint32_t *)(f + o + 16); rp = *(uint32_t *)(f + o + 20);
    if (C1 - 0x400000u > rva + rs) { printf("blit_test: not WA 3.8.1: skipped\n"); return 1; }
    code = (uint8_t *)VirtualAlloc(NULL, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    orig = code; mod = code + 4096;
    memcpy(orig, f + rp + (C0 - 0x400000u - rva), C1 - C0);
    free(f);
    if (memcmp(orig + (FB_KEYED_VA - C0), fb_keyed_was, FB_PATCH) || memcmp(orig + (FB_OPAQUE_VA - C0), fb_opaque_was, FB_PATCH)) {
        printf("blit_test: WA.exe's bytes are not 3.8.1's: skipped\n");
        return 1;
    }
    a = (uint32_t)(uintptr_t)&frame_w;
    memcpy(orig + (0x5a292du - C0), &a, 4);
    memcpy(mod, orig, C1 - C0);
    {   /* WA's call of memcpy */
        uint8_t *c = orig + (0x5a2949u - C0);
        const uint32_t rel = (uint32_t)((uintptr_t)wa_memcpy - ((uintptr_t)c + 5));
        memcpy(c + 1, &rel, 4);
    }
    put_jump(orig + (FB_BACK_VA - C0), (void *)exit_stub);
    put_jump(mod + (FB_BACK_VA - C0), (void *)exit_stub);
    put_jump(mod + (FB_KEYED_VA - C0), (void *)fb_thunk_keyed);
    put_jump(mod + (FB_OPAQUE_VA - C0), (void *)fb_thunk_opaque);
    memset(mod + (0x5a28feu - C0), 0xcc, 0x5a293eu - 0x5a28feu);     /* (WA's old loops must not run any more) */
    memset(mod + (0x5a2943u - C0), 0xcc, 0x5a2960u - 0x5a2943u);
    fb_frame_stride = &frame_w;
    fb_back = mod + (FB_BACK_VA - C0);
    FlushInstructionCache(GetCurrentProcess(), code, 8192);
    for (i = 0; i < 9; i++) vt[i] = NULL;
    vt[8] = (void *)key_stub;
    {
        static void *vtp;
        static void **obj[1];
        vtp = vt;
        obj[0] = (void **)vtp;
        surface = obj;
    }
    for (i = 0; i < n; i++) {
        call_t c;
        ctx_t x;
        uint32_t l0[20];
        size_t j;
        int pass, k;
        make_call(&c);
        if (c.overlap && !c.keyed && range(0, 1)) c.keyed = 1;
        for (j = 60000; j < 200000; j++) a0[j] = (uint8_t)rnd();
        if (c.overlap) fill_source(a0 + 60000, 140000, c.key, c.how);
        memcpy(a1 + 60000, a0 + 60000, 140000);
        fill_source(src0 + 60000, 140000, c.key, c.how);
        memcpy(src1 + 60000, src0 + 60000, 140000);
        test_key = c.key;
        for (pass = 0; pass < 2; pass++) {
            uint8_t *frame = pass ? a1 : a0;
            x.surface = surface; x.w = c.w; x.h = c.h; x.flags = c.keyed ? 2 : 0;
            x.lock_stride = c.ds; frame_w = c.keyed ? c.ds : 0x7fffffff;     /* (each loop has its own frame stride) */
            if (c.keyed) x.lock_stride = 0x7fffffff;
            x.src_stride = c.ss;
            x.dst = frame + c.doff;
            x.src = c.overlap ? frame + c.soff : src0 + c.soff;
            run_code((pass ? mod : orig) + (ENTRY - C0), &x);
            if (run_exit_esp != run_entry_esp || run_exit_ebp != run_entry_ebp) {
                printf("blit_test: %s leaves the stack changed\n", pass ? "the module" : "WA (the test is wrong)");
                return 0;
            }
            if (!pass) memcpy(l0, run_locals, sizeof(l0));
        }
        for (k = 0; k < 20; k++)            /* [esp+10h] is WA's row counter: nobody reads it after the loops */
            if (k != 0 && l0[k] != run_locals[k]) { printf("blit_test: WA's stack variable [esp+%xh] differs\n", 0x10 + 4 * k); return 0; }
        if (memcmp(a0 + 60000, a1 + 60000, 140000) || memcmp(src0 + 60000, src1 + 60000, 140000)) {
            printf("blit_test: WA's code and the module differ: %dx%d strides %d %d key %lu keyed %d source %d overlap %d\n", c.w, c.h,
                   c.ds, c.ss, (unsigned long)c.key, c.keyed, c.how, c.overlap);
            return 0;
        }
        overlaps += c.overlap; keyed += c.keyed;
    }
    VirtualFree(code, 0, MEM_RELEASE);
    printf("blit_test: %d blits (%d keyed, %d overlapping), the same as WA's own code\n", n, keyed, overlaps);
    return 1;
}

int main(int argc, char **argv) {
    const int n = argc > 1 ? atoi(argv[1]) : 60000;
    a0 = (uint8_t *)calloc(1, AREA); a1 = (uint8_t *)calloc(1, AREA);
    src0 = (uint8_t *)calloc(1, AREA); src1 = (uint8_t *)calloc(1, AREA);
    if (!a0 || !a1 || !src0 || !src1) return 1;
    if (argc > 2) seed = (uint32_t)strtoul(argv[2], NULL, 0) | 1u;
    return plain_tests(n) && edge_tests() && wa_tests(n) ? 0 : 1;
}
