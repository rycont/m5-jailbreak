struct wl_proxy;
struct wl_proxy *g_wl_seat;
struct wl_proxy *g_wl_output;
#include "wlshim_plasma_xdg_skip_nolog.c"

typedef struct _wl_display wl_display;
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
typedef struct {
  const char *dli_fname;
  void *dli_fbase;
  const char *dli_sname;
  void *dli_saddr;
} Dl_info_local;
extern int dladdr(const void *, Dl_info_local *);

typedef wl_display *(*wl_connect_fn)(const char *);
wl_display *wl_display_connect(const char *name);
static void *real_wayland_handle;
static wl_connect_fn real_connect;

struct wl_proxy *wl_proxy_marshal_constructor_versioned(
    struct wl_proxy *proxy, uint32_t opcode,
    const struct wl_interface *interface, uint32_t version, ...)
{
  const struct wl_interface *iface =
      ((struct wl_proxy_local *)proxy)->object.interface;
  const struct wl_message *msg = &iface->methods[opcode];
  const char *sig = msg->signature ? msg->signature : "";
  union wl_arg_local args[24];
  memset(args, 0, sizeof(args));
  va_list ap;
  va_start(ap, version);
  int ai = 0;
  for (const char *p = sig; *p && ai < 24; ++p) {
    if ((*p >= '0' && *p <= '9') || *p == '?') continue;
    switch (*p) {
      case 'u': args[ai].u = va_arg(ap, uint32_t); break;
      case 'i': args[ai].i = va_arg(ap, int32_t); break;
      case 'o': args[ai].o = va_arg(ap, void *); break;
      case 'n': (void)va_arg(ap, void *); args[ai].n = 0; break;
      case 's': args[ai].s = va_arg(ap, const char *); break;
      case 'a': args[ai].a = va_arg(ap, struct wl_array *); break;
      case 'h': args[ai].h = va_arg(ap, int32_t); break;
      case 'f': args[ai].f = va_arg(ap, wl_fixed_t); break;
      default: break;
    }
    ++ai;
  }
  va_end(ap);
  if (iface && strcmp(iface->name, "wl_registry") == 0 && opcode == 0)
    fprintf(stderr, "[REGISTRY_BIND] name=%u interface=%s version=%u new=%s/%u\n",
            args[0].u, args[1].s ? args[1].s : "(null)", args[2].u,
            interface ? interface->name : "(null)", version);
  struct wl_proxy *ret = wl_proxy_marshal_array_constructor_versioned(
      proxy, opcode, (union wl_argument *)args, interface, version);
  if (ret && iface && strcmp(iface->name, "wl_registry") == 0 && opcode == 0 && interface) {
    if (strcmp(interface->name, "wl_seat") == 0) {
      g_wl_seat = ret;
      fprintf(stderr, "[WAYLAND_PROXY] saved wl_seat=%p\n", (void *)ret);
    } else if (strcmp(interface->name, "wl_output") == 0 && !g_wl_output) {
      g_wl_output = ret;
      fprintf(stderr, "[WAYLAND_PROXY] saved wl_output=%p\n", (void *)ret);
    }
  }
  if (iface && strcmp(iface->name, "wl_registry") == 0 && opcode == 0)
    fprintf(stderr, "[REGISTRY_BIND] ret=%p\n", (void *)ret);
  return ret;
}

static wl_display *connect_redirect(const char *name)
{
  if (!real_wayland_handle)
    real_wayland_handle = dlopen(
        "/tmp/wltest4/libwayland_client_real.so.1", 0x102);
  if (real_wayland_handle && !real_connect)
    real_connect = (wl_connect_fn)dlsym(
        real_wayland_handle, "wl_display_connect");
  Dl_info_local fn_info = {0};
  if (real_connect)
    dladdr((const void *)real_connect, &fn_info);

  const char *actual = name;
  if (!name || strcmp(name, "wayland-0") == 0)
    actual = "wayland-4";

  wl_display *display = real_connect ? real_connect(actual) : (wl_display *)0;
  fprintf(stderr, "[WLCONNECT_REDIRECT] requested=%s actual=%s display=%p fn=%p module=%s base=%p self=%p\n",
          name ? name : "(null)", actual, (void *)display,
          (void *)real_connect, fn_info.dli_fname ? fn_info.dli_fname : "(null)",
          fn_info.dli_fbase, (void *)wl_display_connect);
  return display;
}

/* The active eglw2 wrapper looks up the regular symbol by name. */
wl_display *wl_display_connect(const char *name)
{
  return connect_redirect(name);
}

/* Keep compatibility with the separate EGL wrapper that looks up this alias. */
wl_display *wl_display_connect_real(const char *name)
    __asm__("wl_display_connect.real");

wl_display *wl_display_connect_real(const char *name)
{
  return connect_redirect(name);
}
