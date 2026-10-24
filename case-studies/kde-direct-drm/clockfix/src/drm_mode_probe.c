typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

struct drm_mode_modeinfo {
    u32 clock;
    u16 hdisplay, hsync_start, hsync_end, htotal, hskew;
    u16 vdisplay, vsync_start, vsync_end, vtotal, vscan;
    u32 vrefresh;
    u32 flags, type;
    char name[32];
};

struct drm_mode_card_res {
    u64 fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
    u32 count_fbs, count_crtcs, count_connectors, count_encoders;
    u32 min_width, max_width, min_height, max_height;
};

struct drm_mode_get_connector {
    u64 encoders_ptr, modes_ptr, props_ptr, prop_values_ptr;
    u32 count_modes, count_props, count_encoders;
    u32 encoder_id, connector_id, connector_type, connector_type_id;
    u32 connection, mm_width, mm_height, subpixel, pad;
};

struct drm_mode_crtc {
    u64 set_connectors_ptr;
    u32 count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid;
    struct drm_mode_modeinfo mode;
};

static inline long sc3(long nr, long a, long b, long c)
{
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r7 __asm__("r7") = nr;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory", "cc");
    return r0;
}

static inline long sc1(long nr, long a)
{
    register long r0 __asm__("r0") = a;
    register long r7 __asm__("r7") = nr;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory", "cc");
    return r0;
}

void *memset(void *dst, int value, unsigned long count)
{
    u8 *p = (u8 *)dst;
    while (count--) *p++ = (u8)value;
    return dst;
}

static long sys_open(const char *path, long flags)
{
    return sc3(5, (long)path, flags, 0);
}

static long sys_ioctl(long fd, long request, void *arg)
{
    return sc3(54, fd, request, (long)arg);
}

static void sys_write(const char *p, unsigned int n)
{
    sc3(4, 1, (long)p, n);
}

static void sys_exit(int status)
{
    sc1(1, status);
    for (;;) { }
}

static char line[512];
static unsigned int used;

static void putc1(char c) { if (used < sizeof(line)) line[used++] = c; }
static void puts1(const char *s) { while (*s) putc1(*s++); }
static unsigned long div10(unsigned long v, unsigned long *remainder)
{
    unsigned long q = 0, r = 0;
    for (int i = 31; i >= 0; --i) {
        r = (r << 1) | ((v >> i) & 1u);
        if (r >= 10) { r -= 10; q |= (1u << i); }
    }
    *remainder = r;
    return q;
}
static void dec1(unsigned long v)
{
    char b[24]; unsigned int n = 0;
    do { unsigned long r; v = div10(v, &r); b[n++] = (char)('0' + r); } while (v && n < sizeof(b));
    while (n) putc1(b[--n]);
}

static void flush(void)
{
    if (used) sys_write(line, used);
    used = 0;
}

static void report_mode(const char *label, u32 id, const struct drm_mode_modeinfo *m)
{
    puts1(label); puts1(" connector="); dec1(id); puts1(" name="); puts1(m->name);
    puts1(" size="); dec1(m->hdisplay); putc1('x'); dec1(m->vdisplay);
    puts1(" vrefresh="); dec1(m->vrefresh);
    puts1(" clock_khz="); dec1(m->clock);
    puts1(" totals="); dec1(m->htotal); putc1('x'); dec1(m->vtotal);
    putc1('\n'); flush();
}

int main(void)
{
    static u32 fbs[32], crtcs[32], connectors[32], encoders[32];
    static struct drm_mode_modeinfo modes[32];
    struct drm_mode_card_res res = {0};
    const long GETRESOURCES = 0xC04064A0;
    const long GETCONNECTOR = 0xC05064A7;
    const long GETCRTC = 0xC06864A1;
    long fd = sys_open("/dev/dri/card0", 2);
    if (fd < 0) { puts1("open card0 failed rc="); dec1((unsigned long)-fd); putc1('\n'); flush(); return 1; }

    res.fb_id_ptr = (u64)(u32)(unsigned long)fbs;
    res.crtc_id_ptr = (u64)(u32)(unsigned long)crtcs;
    res.connector_id_ptr = (u64)(u32)(unsigned long)connectors;
    res.encoder_id_ptr = (u64)(u32)(unsigned long)encoders;
    res.count_fbs = 32; res.count_crtcs = 32; res.count_connectors = 32; res.count_encoders = 32;
    long rc = sys_ioctl(fd, GETRESOURCES, &res);
    if (rc < 0) { puts1("GETRESOURCES failed rc="); dec1((unsigned long)-rc); putc1('\n'); flush(); return 2; }

    puts1("resources connectors="); dec1(res.count_connectors); puts1(" crtcs="); dec1(res.count_crtcs); putc1('\n'); flush();
    for (u32 i = 0; i < res.count_connectors && i < 32; ++i) {
        struct drm_mode_get_connector c = {0};
        c.connector_id = connectors[i];
        c.modes_ptr = (u64)(u32)(unsigned long)modes;
        c.count_modes = 32;
        rc = sys_ioctl(fd, GETCONNECTOR, &c);
        if (rc < 0) { puts1("GETCONNECTOR failed id="); dec1(connectors[i]); puts1(" rc="); dec1((unsigned long)-rc); putc1('\n'); flush(); continue; }
        puts1("connector id="); dec1(c.connector_id); puts1(" type="); dec1(c.connector_type);
        puts1(" connection="); dec1(c.connection); puts1(" modes="); dec1(c.count_modes); putc1('\n'); flush();
        for (u32 j = 0; j < c.count_modes && j < 32; ++j) report_mode("available", c.connector_id, &modes[j]);
    }
    for (u32 i = 0; i < res.count_crtcs && i < 32; ++i) {
        struct drm_mode_crtc c = {0};
        c.crtc_id = crtcs[i];
        rc = sys_ioctl(fd, GETCRTC, &c);
        if (rc == 0 && c.mode_valid) report_mode("active crtc", c.crtc_id, &c.mode);
    }
    return 0;
}

void _start(void)
{
    int rc = main();
    sys_exit(rc);
}
