#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <poll.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "presentation-time-client-protocol.h"

#define W 1920
#define H 1080
#define NBUFFERS 3
#define NFRAMES 600
#define BAR_W 128
#define BAR_H 120
#define BAR_Y ((H - BAR_H) / 2)
#define BG 0x00000000U

struct test_buffer {
    struct wl_buffer *wl_buffer;
    uint32_t *pixels;
    int busy;
    int bar_x;
};

struct feedback_data {
    int done;
};

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wp_presentation *presentation;
static struct wl_surface *surface;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *toplevel;
static struct test_buffer buffers[NBUFFERS];
static struct feedback_data feedbacks[NFRAMES];
static int configured;
static int configured_width;
static int configured_height;
static int frame_ready = 1;
static int should_close;
static unsigned int frame_callbacks;
static unsigned int submitted_count;
static unsigned int presented_count;
static unsigned int discarded_count;
static unsigned int missed_vblanks;
static uint64_t first_sec;
static uint64_t last_sec;
static uint32_t first_nsec;
static uint32_t last_nsec;
static uint32_t first_refresh_ns;
static uint64_t previous_seq;
static uint32_t presentation_clock_id = 0xffffffffU;

static uint32_t udiv32(uint32_t n, uint32_t d)
{
    if (!d) return 0;
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; --i) {
        uint32_t carry = r >> 31;
        r = (r << 1) | ((n >> i) & 1U);
        if (carry || r >= d) { r -= d; q |= 1U << i; }
    }
    return q;
}

static int make_shm(int size)
{
    char path[] = "/tmp/m5presentXXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    unlink(path);
    if (ftruncate(fd, size) < 0) { close(fd); return -1; }
    return fd;
}

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    (void)data;
    if (!strcmp(interface, wl_compositor_interface.name))
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface,
                                      version > 4 ? 4 : version);
    else if (!strcmp(interface, wl_shm_interface.name))
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, xdg_wm_base_interface.name))
        wm = wl_registry_bind(registry, name, &xdg_wm_base_interface,
                              version > 1 ? 1 : version);
    else if (!strcmp(interface, wp_presentation_interface.name))
        presentation = wl_registry_bind(registry, name,
                                        &wp_presentation_interface,
                                        version > 1 ? 1 : version);
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = 0,
};

static void wm_ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };

static void surface_configure(void *data, struct xdg_surface *xdg,
                              uint32_t serial)
{
    (void)data;
    xdg_surface_ack_configure(xdg, serial);
    configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *top,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
    (void)data; (void)top; (void)states;
    if (width > 0) configured_width = width;
    if (height > 0) configured_height = height;
}

static void toplevel_close(void *data, struct xdg_toplevel *top)
{
    (void)data; (void)top;
    should_close = 1;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

static void frame_done(void *data, struct wl_callback *callback,
                       uint32_t time)
{
    (void)data; (void)time;
    frame_ready = 1;
    frame_callbacks++;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener frame_listener = { .done = frame_done };

static void buffer_release(void *data, struct wl_buffer *buffer)
{
    (void)buffer;
    ((struct test_buffer *)data)->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static void presentation_clock(void *data, struct wp_presentation *object,
                               uint32_t clock_id)
{
    (void)data; (void)object;
    presentation_clock_id = clock_id;
}

static const struct wp_presentation_listener presentation_listener = {
    .clock_id = presentation_clock,
};

static void feedback_sync_output(void *data,
                                 struct wp_presentation_feedback *feedback,
                                 struct wl_output *output)
{
    (void)data; (void)feedback; (void)output;
}

static void feedback_presented(void *data,
                               struct wp_presentation_feedback *feedback,
                               uint32_t sec_hi, uint32_t sec_lo,
                               uint32_t nsec, uint32_t refresh,
                               uint32_t seq_hi, uint32_t seq_lo,
                               uint32_t flags)
{
    (void)feedback; (void)flags;
    struct feedback_data *entry = (struct feedback_data *)data;
    entry->done = 1;
    uint64_t sec = ((uint64_t)sec_hi << 32) | sec_lo;
    if (presented_count == 0) {
        first_sec = sec;
        first_nsec = nsec;
        first_refresh_ns = refresh;
    } else {
        uint64_t seq = ((uint64_t)seq_hi << 32) | seq_lo;
        if (seq > previous_seq + 1)
            missed_vblanks += (uint32_t)(seq - previous_seq - 1);
    }
    last_sec = sec;
    last_nsec = nsec;
    previous_seq = ((uint64_t)seq_hi << 32) | seq_lo;
    presented_count++;
}

static void feedback_discarded(void *data,
                               struct wp_presentation_feedback *feedback)
{
    (void)feedback;
    ((struct feedback_data *)data)->done = 1;
    discarded_count++;
}

static const struct wp_presentation_feedback_listener feedback_listener = {
    .sync_output = feedback_sync_output,
    .presented = feedback_presented,
    .discarded = feedback_discarded,
};

static int free_buffer(void)
{
    for (int i = 0; i < NBUFFERS; ++i)
        if (!buffers[i].busy) return i;
    return -1;
}

static int dispatch_until_ready(int timeout_ms)
{
    int waited = 0;
    int fd = wl_display_get_fd(display);
    while (!should_close && (!frame_ready || free_buffer() < 0)) {
        int slice = timeout_ms - waited;
        if (slice > 100) slice = 100;
        if (slice <= 0) return -1;
        struct pollfd pfd = { fd, POLLIN, 0 };
        int r = poll(&pfd, 1, slice);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r > 0 && wl_display_dispatch(display) < 0) return -1;
        waited += slice;
    }
    return should_close ? -1 : free_buffer();
}

static void paint_rect(uint32_t *pixels, int x, uint32_t color)
{
    if (x < 0) return;
    for (int y = BAR_Y; y < BAR_Y + BAR_H; ++y) {
        uint32_t *row = pixels + y * W + x;
        for (int i = 0; i < BAR_W; ++i) row[i] = color;
    }
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    display = wl_display_connect(0);
    if (!display) { puts("wl_display_connect failed"); return 1; }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, 0);
    if (wl_display_roundtrip(display) < 0 || !compositor || !shm || !wm ||
        !presentation) {
        puts("required Wayland globals missing");
        return 2;
    }
    xdg_wm_base_add_listener(wm, &wm_listener, 0);
    wp_presentation_add_listener(presentation, &presentation_listener, 0);

    surface = wl_compositor_create_surface(compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(wm, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, 0);
    toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, 0);
    xdg_toplevel_set_title(toplevel, "M5 GPU composition 60fps probe");
    xdg_toplevel_set_app_id(toplevel, "m5-gpu-60fps-probe");
    xdg_toplevel_set_fullscreen(toplevel, 0);
    wl_surface_set_opaque_region(surface, 0);
    wl_surface_set_input_region(surface, 0);
    wl_surface_commit(surface);

    int guard = 0;
    while (!configured && guard++ < 100)
        if (wl_display_dispatch(display) < 0) return 3;
    if (!configured) { puts("xdg configure timeout"); return 4; }

    const int frame_bytes = W * H * 4;
    const int shm_bytes = frame_bytes * NBUFFERS;
    int fd = make_shm(shm_bytes);
    if (fd < 0) { puts("shm create failed"); return 5; }
    uint32_t *map = mmap(0, shm_bytes, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { puts("mmap failed"); return 6; }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, shm_bytes);
    for (int i = 0; i < NBUFFERS; ++i) {
        buffers[i].pixels = map + (frame_bytes / 4) * i;
        buffers[i].busy = 0;
        buffers[i].bar_x = -1;
        memset(buffers[i].pixels, 0, frame_bytes);
        buffers[i].wl_buffer = wl_shm_pool_create_buffer(
            pool, frame_bytes * i, W, H, W * 4, WL_SHM_FORMAT_ARGB8888);
        if (!buffers[i].wl_buffer ||
            wl_buffer_add_listener(buffers[i].wl_buffer, &buffer_listener,
                                   &buffers[i]) < 0) {
            puts("wl_buffer setup failed");
            return 7;
        }
    }
    wl_shm_pool_destroy(pool);
    close(fd);

    printf("configured=%dx%d presentation_clock=%u buffers=%d frames=%d\n",
           configured_width, configured_height, presentation_clock_id,
           NBUFFERS, NFRAMES);

    int previous_screen_x = -1;
    for (int frame = 0; frame < NFRAMES; ++frame) {
        int bi = dispatch_until_ready(frame == 0 ? 3000 : 1500);
        if (bi < 0) {
            printf("render wait failed frame=%d presented=%u callback=%u\n",
                   frame, presented_count, frame_callbacks);
            should_close = 1;
            break;
        }
        int x = (frame * 31) % (W - BAR_W);
        uint32_t color = 0xff000000U | ((uint32_t)(frame * 7 & 255) << 16) |
                         ((uint32_t)(255 - (frame * 3 & 255)) << 8) | 0x40U;
        if (buffers[bi].bar_x >= 0)
            paint_rect(buffers[bi].pixels, buffers[bi].bar_x, BG);
        paint_rect(buffers[bi].pixels, x, color);
        buffers[bi].bar_x = x;

        struct wl_callback *cb = wl_surface_frame(surface);
        if (!cb || wl_callback_add_listener(cb, &frame_listener, 0) < 0) {
            puts("frame callback setup failed");
            return 8;
        }
        feedbacks[frame].done = 0;
        struct wp_presentation_feedback *feedback =
            wp_presentation_feedback(presentation, surface);
        if (!feedback || wp_presentation_feedback_add_listener(
                feedback, &feedback_listener, &feedbacks[frame]) < 0) {
            puts("presentation feedback setup failed");
            return 9;
        }
        buffers[bi].busy = 1;
        wl_surface_attach(surface, buffers[bi].wl_buffer, 0, 0);
        int left = x;
        int right = x + BAR_W;
        if (previous_screen_x >= 0) {
            if (previous_screen_x < left) left = previous_screen_x;
            if (previous_screen_x + BAR_W > right)
                right = previous_screen_x + BAR_W;
        }
        wl_surface_damage(surface, left, BAR_Y, right - left, BAR_H);
        wl_surface_commit(surface);
        if (wl_display_flush(display) < 0) {
            puts("wl_display_flush failed");
            return 10;
        }
        submitted_count++;
        previous_screen_x = x;
        frame_ready = 0;
    }

    int wait_ms = 0;
    while (!should_close && presented_count + discarded_count < NFRAMES &&
           wait_ms < 3000) {
        struct pollfd pfd = { wl_display_get_fd(display), POLLIN, 0 };
        int r = poll(&pfd, 1, 100);
        if (r > 0 && wl_display_dispatch(display) < 0) break;
        wait_ms += 100;
    }

    uint32_t elapsed_ms = 0;
    if (presented_count > 1) {
        uint64_t sec_delta = last_sec - first_sec;
        int32_t ns_delta = (int32_t)last_nsec - (int32_t)first_nsec;
        if (ns_delta < 0) { sec_delta--; ns_delta += 1000000000; }
        elapsed_ms = (uint32_t)(sec_delta * 1000U) +
                     udiv32((uint32_t)ns_delta, 1000000U);
    }
    uint32_t fps_x100 = elapsed_ms ?
        udiv32((presented_count - 1U) * 100000U, elapsed_ms) : 0;
    printf("submitted=%u presented=%u discarded=%u frame_callbacks=%u "
           "elapsed_ms=%u presented_fps=%u.%02u refresh_ns=%u "
           "missed_vblanks=%u\n",
           submitted_count,
           presented_count, discarded_count, frame_callbacks, elapsed_ms,
           fps_x100 / 100U, fps_x100 % 100U, first_refresh_ns,
           missed_vblanks);
    return 0;
}
