/* drmcap.c - LD_PRELOAD shim for wlroots 0.18 on the Tizen ndp DRM driver (4.1 kernel).
 *  - DRM_CAP_CRTC_IN_VBLANK_EVENT (0x12) is reported as supported
 *  - hardware cursor calls are accepted and ignored (ndp has no cursor plane)
 *  - drmHandleEvent injects the single CRTC id (20) into page-flip events:
 *    this kernel's vblank event has crtc_id=0, which wlroots 0.18 cannot match.
 */
#define SYS_read 3
#define SYS_ioctl 54
static long sys1(long n,long a){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;__asm__ volatile("svc 0":"+r"(r0):"r"(r7):"memory");return r0;}
static long sys3(long n,long a,long b,long c){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;register long r1 __asm__("r1")=b;register long r2 __asm__("r2")=c;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory");return r0;}
struct getcap { unsigned long long capability, value; };
int drmGetCap(int fd, unsigned long long capability, unsigned long long *value)
{
	if (capability == 0x12ULL) { if (value) *value = 1; return 0; }
	struct getcap c; c.capability = capability; c.value = 0;
	long r = sys3(SYS_ioctl, fd, 0xC010640CUL, (long)&c);
	if (r == 0 && value) *value = c.value;
	return (int)r;
}
int drmModeSetCursor(int fd, unsigned int crtc, unsigned int handle, unsigned int w, unsigned int h) { return 0; }
int drmModeMoveCursor(int fd, unsigned int crtc, int x, int y) { return 0; }
int drmModeSetCursor2(int fd, unsigned int crtc, unsigned int handle, unsigned int w, unsigned int h, int hx, int hy) { return 0; }

struct drm_event { unsigned int type, length; };
struct drm_event_vblank { unsigned int type, length; unsigned long long user_data; unsigned int tv_sec, tv_usec, sequence, crtc_id; };
typedef struct {
	int version;
	void (*vblank_handler)(int, unsigned int, unsigned int, unsigned int, void *);
	void (*page_flip_handler)(int, unsigned int, unsigned int, unsigned int, void *);
	void (*page_flip_handler2)(int, unsigned int, unsigned int, unsigned int, unsigned int, void *);
	void (*sequence_handler)(int, unsigned long long, unsigned long long, unsigned long long);
} drmEventContext;
int drmHandleEvent(int fd, drmEventContext *ctx)
{
	unsigned char buf[1024];
	long n = sys3(SYS_read, fd, (long)buf, sizeof(buf));
	if (n <= 0) return n == 0 ? 0 : -1;
	unsigned int off = 0;
	while (off + 8 <= (unsigned int)n) {
		struct drm_event *e = (struct drm_event *)(buf + off);
		if (e->length < 8 || off + e->length > (unsigned int)n) break;
		if (e->type == 2 && e->length >= sizeof(struct drm_event_vblank)) {
			struct drm_event_vblank *v = (struct drm_event_vblank *)e;
			unsigned int crtc = 20;
			if (ctx->version >= 3 && ctx->page_flip_handler2)
				ctx->page_flip_handler2(fd, v->sequence, v->tv_sec, v->tv_usec, crtc, (void *)(unsigned long)v->user_data);
			else if (ctx->page_flip_handler)
				ctx->page_flip_handler(fd, v->sequence, v->tv_sec, v->tv_usec, (void *)(unsigned long)v->user_data);
		} else if (e->type == 1 && e->length >= sizeof(struct drm_event_vblank)) {
			struct drm_event_vblank *v = (struct drm_event_vblank *)e;
			if (ctx->vblank_handler)
				ctx->vblank_handler(fd, v->sequence, v->tv_sec, v->tv_usec, (void *)(unsigned long)v->user_data);
		}
		off += e->length;
	}
	return 0;
}
