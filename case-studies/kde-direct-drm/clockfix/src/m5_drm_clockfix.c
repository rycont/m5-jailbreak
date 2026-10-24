/*
 * Normalize the M5 LVDS mode clock for KWin's refresh-rate calculation.
 * The DRM driver reports 148 kHz together with a 60 Hz 1920x1080 timing,
 * while KWin 6.3.6 derives mHz from clock/htotal/vtotal and consequently
 * schedules frames roughly every 16 seconds.  Present a standard 148500 kHz
 * clock to KWin, but pass the driver's original 148 value back to the kernel
 * when drmModeSetCrtc programs the mode.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

typedef struct {
    u32 clock;
    u16 hdisplay, hsync_start, hsync_end, htotal, hskew;
    u16 vdisplay, vsync_start, vsync_end, vtotal, vscan;
    u32 vrefresh;
    u32 flags, type;
    char name[32];
} drmModeModeInfo;

typedef struct {
    u32 connector_id, encoder_id, connector_type, connector_type_id;
    u32 connection, mmWidth, mmHeight, subpixel;
    int count_modes;
    drmModeModeInfo *modes;
} drmModeConnector;

typedef struct {
    u32 crtc_id, buffer_id;
    u32 x, y, width, height;
    int mode_valid;
    drmModeModeInfo mode;
    int gamma_size;
} drmModeCrtc;

_Static_assert(sizeof(drmModeModeInfo) == 68, "drmModeModeInfo ABI mismatch");
_Static_assert(__builtin_offsetof(drmModeConnector, modes) == 36, "drmModeConnector ABI mismatch");
_Static_assert(__builtin_offsetof(drmModeCrtc, mode) == 28, "drmModeCrtc ABI mismatch");

typedef drmModeConnector *(*get_connector_fn)(int, u32);
typedef drmModeCrtc *(*get_crtc_fn)(int, u32);
typedef int (*set_crtc_fn)(int, u32, u32, u32, u32, u32 *, int, drmModeModeInfo *);

extern void *dlsym(void *, const char *);
extern int *__errno_location(void);
extern int open(const char *, int, ...);
extern long write(int, const void *, unsigned long);
extern int close(int);

static int connector_logged;
static int crtc_logged;
static int set_logged;

static void log_once(int *flag, const char *message, unsigned int length)
{
    if (*flag) return;
    *flag = 1;
    int fd = open("/tmp/m5_drm_clockfix.log", 1 | 64 | 1024, 0666);
    if (fd >= 0) {
        write(fd, message, length);
        close(fd);
    }
}

static int is_m5_1080p60(const drmModeModeInfo *mode)
{
    return mode
        && mode->hdisplay == 1920 && mode->vdisplay == 1080
        && mode->htotal == 2200 && mode->vtotal == 1125
        && mode->vrefresh == 60;
}

static void normalize(drmModeModeInfo *mode, int *did_change)
{
    if (is_m5_1080p60(mode) && mode->clock == 148) {
        mode->clock = 148500;
        *did_change = 1;
    }
}

drmModeConnector *drmModeGetConnector(int fd, u32 connector_id)
{
    static get_connector_fn real_fn;
    if (!real_fn) real_fn = (get_connector_fn)dlsym((void *)-1, "drmModeGetConnector");
    if (!real_fn) return 0;

    drmModeConnector *connector = real_fn(fd, connector_id);
    int saved_errno = *__errno_location();
    int changed = 0;
    if (connector && connector->connector_type == 7 && connector->modes) {
        for (int i = 0; i < connector->count_modes; ++i) {
            normalize(&connector->modes[i], &changed);
        }
    }
    if (changed) {
        static const char msg[] = "GETCONNECTOR LVDS 1920x1080: clock 148 -> 148500 kHz (KWin view)\n";
        log_once(&connector_logged, msg, sizeof(msg) - 1);
    }
    *__errno_location() = saved_errno;
    return connector;
}

drmModeCrtc *drmModeGetCrtc(int fd, u32 crtc_id)
{
    static get_crtc_fn real_fn;
    if (!real_fn) real_fn = (get_crtc_fn)dlsym((void *)-1, "drmModeGetCrtc");
    if (!real_fn) return 0;

    drmModeCrtc *crtc = real_fn(fd, crtc_id);
    int saved_errno = *__errno_location();
    int changed = 0;
    if (crtc && crtc->mode_valid) normalize(&crtc->mode, &changed);
    if (changed) {
        static const char msg[] = "GETCRTC LVDS 1920x1080: clock 148 -> 148500 kHz (KWin view)\n";
        log_once(&crtc_logged, msg, sizeof(msg) - 1);
    }
    *__errno_location() = saved_errno;
    return crtc;
}

int drmModeSetCrtc(int fd, u32 crtc_id, u32 buffer_id, u32 x, u32 y,
                   u32 *connectors, int count, drmModeModeInfo *mode)
{
    static set_crtc_fn real_fn;
    if (!real_fn) real_fn = (set_crtc_fn)dlsym((void *)-1, "drmModeSetCrtc");
    if (!real_fn) return -1;

    int changed = 0;
    u32 original_clock = 0;
    if (is_m5_1080p60(mode) && mode->clock == 148500) {
        original_clock = mode->clock;
        mode->clock = 148;
        changed = 1;
    }
    int result = real_fn(fd, crtc_id, buffer_id, x, y, connectors, count, mode);
    int saved_errno = *__errno_location();
    if (changed) {
        mode->clock = original_clock;
        static const char msg[] = "SETCRTC LVDS 1920x1080: passed driver's native clock 148 kHz\n";
        log_once(&set_logged, msg, sizeof(msg) - 1);
    }
    *__errno_location() = saved_errno;
    return result;
}
