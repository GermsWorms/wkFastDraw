/* wkFastDraw: Worms Armageddon 3.8.1 draws its map faster (a WormKit module). The picture is the same, pixel for pixel.
 *
 * WA draws everything on one CPU thread, and most of that thread's time goes into one function: the software "surface
 * blit" 005A2790, which copies the land tiles, the sky gradient and the back layers onto WA's 8-bit frame one byte at a
 * time. This module replaces that function's two inner loops with SSE2 ones (fastblit.h) and changes nothing else: no
 * game state, no file, no setting, no Direct3D. Other players don't need it; online games and replays stay in step.
 *
 * Two jumps of 5 bytes are written into WA's code when the module is loaded, only if
 *   - WA.exe is 3.8.1 (Steam's file, or another edition with the same code: GOG's),
 *   - the function in memory is byte for byte WA's (nothing else has changed it), and
 *   - the module could be pinned in memory (so WA's code never jumps into a module that is gone).
 * Otherwise WA is left alone. Either way one line or two say so in %LOCALAPPDATA%\wkFastDraw\wkFastDraw.log. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image_guard.h"
#ifdef FB_CHECK                             /* (tests only: every n-th blit is done by plain loops too and compared) */
#define FB_RUN fb_check_run
static void fb_check_run(uint8_t *, const uint8_t *, int, int, int, int, uint32_t, int);
#endif
#include "fastblit.h"
#ifdef FB_CHECK
#include "../tests/fb_check.h"
#endif

#define VERSION "1.0"
#define WA_BASE 0x400000u
#define VA(a) ((uintptr_t)(base + ((uintptr_t)(a) - WA_BASE)))
#ifndef PF_XMMI64_INSTRUCTIONS_AVAILABLE
#define PF_XMMI64_INSTRUCTIONS_AVAILABLE 10
#endif

static uintptr_t base;
static HANDLE log_file = INVALID_HANDLE_VALUE;

static void open_log(void) {                /* added to, so a second copy's refusal doesn't hide the first one's line */
    char p[MAX_PATH + 32];
    DWORD high = 0, size;
    const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", p, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    strcat(p, "\\wkFastDraw");
    CreateDirectoryA(p, NULL);
    strcat(p, "\\wkFastDraw.log");
    log_file = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_file == INVALID_HANDLE_VALUE) return;
    size = GetFileSize(log_file, &high);
    if (high || size > 256u * 1024u) SetEndOfFile(log_file);          /* (at its start: emptied) */
    else SetFilePointer(log_file, 0, NULL, FILE_END);
}

static void say(const char *fmt, ...) {
    char b[1024];
    SYSTEMTIME t;
    va_list v;
    DWORD n;
    int k, m;
    if (log_file == INVALID_HANDLE_VALUE) return;
    GetLocalTime(&t);
    k = sprintf(b, "%04u-%02u-%02u %02u:%02u:%02u [%lu] ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                (unsigned long)GetCurrentProcessId());
    va_start(v, fmt);
    m = _vsnprintf(b + k, sizeof(b) - k - 3, fmt, v);
    va_end(v);
    k = m < 0 ? (int)sizeof(b) - 3 : k + m;
    b[k++] = '\r';
    b[k++] = '\n';
    WriteFile(log_file, b, (DWORD)k, &n, NULL);
}

/* WA's code from 005A28C2 (where the addresses are worked out) up to 005A2976, as in 3.8.1: its SHA-256 with the one absolute
 * address in it (moved with WA.exe) as zeros */
#define CHECK_VA 0x5a28c2u
#define CHECK_SIZE 180u
#define CHECK_RELOC (0x5a292du - CHECK_VA)  /* add edi,[007A0EF0] */
static const uint8_t check_sha[32] = {
    0x35, 0x27, 0xaf, 0x66, 0xc5, 0xa7, 0x47, 0xc7, 0x2f, 0x68, 0x35, 0xbf, 0x62, 0xb7, 0xfe, 0x6d,
    0xe3, 0xd7, 0x62, 0x54, 0x0f, 0x88, 0xe7, 0x3e, 0xef, 0x28, 0x1b, 0x22, 0x50, 0x44, 0xe9, 0xa9};

static int code_is_wa(void) {
    uint8_t code[CHECK_SIZE], digest[32];
    const uint32_t frame_w = (uint32_t)VA(FB_FRAME_W_VA);
    wa_sha256 h;
    memcpy(code, (const void *)VA(CHECK_VA), CHECK_SIZE);
    if (memcmp(code + CHECK_RELOC, &frame_w, 4)) return 0;
    memset(code + CHECK_RELOC, 0, 4);
    wa_sha_init(&h);
    wa_sha_update(&h, code, CHECK_SIZE);
    wa_sha_final(&h, digest);
    return !memcmp(digest, check_sha, 32);
}

typedef BOOL(WINAPI *handle_ex_fn)(DWORD, LPCSTR, HMODULE *);
static handle_ex_fn handle_ex(void) {       /* (looked up: MinGW's headers hide it) */
    return (handle_ex_fn)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetModuleHandleExA");
}
static int pin(void) {                      /* PIN | FROM_ADDRESS */
    const handle_ex_fn ex = handle_ex();
    HMODULE me;
    return ex && ex(0x1 | 0x4, (LPCSTR)(void *)&pin, &me);
}

/* whose jump is in WA's loops already: the module's path ("" if there is no jump, or no module there) */
static void jump_owner(char *out, DWORD size) {
    const uint32_t at[2] = {FB_KEYED_VA, FB_OPAQUE_VA};
    const handle_ex_fn ex = handle_ex();
    int k;
    out[0] = 0;
    for (k = 0; k < 2 && ex && !out[0]; k++) {
        const uint8_t *p = (const uint8_t *)VA(at[k]);
        uint32_t rel;
        HMODULE m;
        if (*p != 0xe9) continue;
        memcpy(&rel, p + 1, 4);
        if (ex(0x2 | 0x4, (LPCSTR)(uintptr_t)(uint32_t)((uintptr_t)p + FB_PATCH + rel), &m) &&     /* UNCHANGED_REFCOUNT | FROM_ADDRESS */
            !GetModuleFileNameA(m, out, size)) out[0] = 0;
        out[size - 1] = 0;
    }
}

/* both jumps, or none */
static int write_jumps(void) {
    const uintptr_t first = VA(FB_KEYED_VA), size = VA(FB_OPAQUE_VA) + FB_PATCH - first;
    uint8_t keyed[FB_PATCH], opaque[FB_PATCH];
    DWORD old, ignored;
    fb_jump(keyed, VA(FB_KEYED_VA), (uintptr_t)fb_thunk_keyed);
    fb_jump(opaque, VA(FB_OPAQUE_VA), (uintptr_t)fb_thunk_opaque);
    if (!VirtualProtect((void *)first, size, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy((void *)VA(FB_KEYED_VA), keyed, FB_PATCH);
    memcpy((void *)VA(FB_OPAQUE_VA), opaque, FB_PATCH);
    VirtualProtect((void *)first, size, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), (void *)first, size);
    return 1;
}

static void install(HINSTANCE self) {
    char exe[MAX_PATH], me[MAX_PATH], why[256] = "its path is unknown";
    DWORD n;
    open_log();
    base = (uintptr_t)GetModuleHandleA(NULL);
    n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (!n || n >= sizeof(exe) || !wa_supported_image(exe, why, sizeof(why))) {
        say("wkFastDraw " VERSION ": not Worms Armageddon 3.8.1 (%s): not installed", why);
        return;
    }
    if (!IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE)) {
        say("wkFastDraw " VERSION ": this CPU has no SSE2: not installed");
        return;
    }
    if (!code_is_wa()) {
        jump_owner(me, sizeof(me));
        if (me[0]) say("wkFastDraw " VERSION ": %s already changes WA's drawing function (another copy of this module?): this copy does nothing", me);
        else say("wkFastDraw " VERSION ": WA's drawing function has been changed by something else: not installed");
        return;
    }
    if (!pin()) {
        say("wkFastDraw " VERSION ": could not pin the module in memory (error %lu): not installed", GetLastError());
        return;
    }
    fb_frame_stride = (const int32_t *)VA(FB_FRAME_W_VA);
    fb_back = (void *)VA(FB_BACK_VA);
#ifdef FB_CHECK
    fb_check_open();
#endif
    if (!write_jumps()) {
        say("wkFastDraw " VERSION ": WA's code can't be written (error %lu): not installed", GetLastError());
        return;
    }
    n = GetModuleFileNameA(self, me, sizeof(me));
    if (!n || n >= sizeof(me)) strcpy(me, "?");
    say("wkFastDraw " VERSION " ready: WA's map drawing uses SSE2 (%s; WA at %08lx, this module %s)", why, (unsigned long)base, me);
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        install(self);
        if (log_file != INVALID_HANDLE_VALUE) { CloseHandle(log_file); log_file = INVALID_HANDLE_VALUE; }
    }
    return TRUE;
}
