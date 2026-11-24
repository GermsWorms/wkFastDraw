/* fb_check.h: for the check build only (build.ps1 makes tests\fastdraw-check.dll). Every 16th blit is also done with plain
 * loops on a copy of the frame's pixels and the two are compared. WKFASTDRAW_CHECK names the report file. */
static FILE *fb_check_file;
static unsigned fb_calls, fb_checked, fb_bad, fb_keyed_n, fb_overlaps, fb_widest;
static uint8_t *fb_tmp;
static size_t fb_tmp_n;

static void fb_check_open(void) {
    char p[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("WKFASTDRAW_CHECK", p, sizeof(p));
    if (n && n < sizeof(p)) fb_check_file = fopen(p, "w");
}

static void fb_check_run(uint8_t *d, const uint8_t *s, int w, int h, int ds, int ss, uint32_t key, int keyed) {
    int y;
    size_t n;
    if (w <= 0 || h <= 0) return;
    fb_calls++;
    if ((unsigned)w > fb_widest) fb_widest = (unsigned)w;
    if (fb_overlap(d, s, w, h, ds, ss)) { fb_overlaps++; fb_plain(d, s, w, h, ds, ss, key, keyed); return; }
    n = (size_t)w * (size_t)h;
    if (!fb_check_file || fb_calls % 16 || n > (64u << 20)) { fb_fast(d, s, w, h, ds, ss, key, keyed); return; }
    if (n > fb_tmp_n) { free(fb_tmp); fb_tmp = (uint8_t *)malloc(n); fb_tmp_n = fb_tmp ? n : 0; }
    if (!fb_tmp) { fb_fast(d, s, w, h, ds, ss, key, keyed); return; }
    for (y = 0; y < h; y++) memcpy(fb_tmp + (size_t)y * w, d + (ptrdiff_t)y * ds, (size_t)w);
    fb_plain(fb_tmp, s, w, h, w, ss, key, keyed);
    fb_fast(d, s, w, h, ds, ss, key, keyed);
    fb_checked++;
    fb_keyed_n += keyed != 0;
    for (y = 0; y < h; y++)
        if (memcmp(fb_tmp + (size_t)y * w, d + (ptrdiff_t)y * ds, (size_t)w)) {
            if (fb_bad++ < 20) fprintf(fb_check_file, "DIFFERENT: %dx%d key %lu keyed %d row %d\n", w, h, (unsigned long)key, keyed, y);
            break;
        }
    if (fb_checked % 4096 == 0) {
        fprintf(fb_check_file, "blits %u, checked %u (%u keyed), different %u, overlapping %u, widest %u\n", fb_calls, fb_checked,
                fb_keyed_n, fb_bad, fb_overlaps, fb_widest);
        fflush(fb_check_file);
    }
}
