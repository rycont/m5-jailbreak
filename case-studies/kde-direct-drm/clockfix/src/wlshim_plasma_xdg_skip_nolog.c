#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
typedef struct _IO_FILE FILE;
extern FILE *stderr;
extern int fprintf(FILE *, const char *, ...);
#define fprintf(...) ((void)0)
extern void *memset(void *, int, size_t);
extern int strcmp(const char *, const char *);
extern char *strchr(const char *, int);
struct wl_proxy;
struct wl_array;
typedef int32_t wl_fixed_t;
struct wl_message { const char *name; const char *signature; const struct wl_interface **types; };
struct wl_interface { const char *name; int version; int method_count; const struct wl_message *methods; int event_count; const struct wl_message *events; };
union wl_argument { int32_t i; uint32_t u; void *o; uint32_t n; const char *s; struct wl_array *a; int32_t h; wl_fixed_t f; };
extern struct wl_proxy *wl_proxy_marshal_array_constructor_versioned(struct wl_proxy *, uint32_t, union wl_argument *, const struct wl_interface *, uint32_t);
extern void wl_proxy_marshal_array(struct wl_proxy *, uint32_t, union wl_argument *);
extern struct wl_proxy *wl_proxy_create(struct wl_proxy *, const struct wl_interface *);
extern void wl_proxy_destroy(struct wl_proxy *);
struct wl_object_local { const struct wl_interface *interface; void *implementation; uint32_t id; };
struct wl_proxy_local { struct wl_object_local object; };

union wl_arg_local { int32_t i; uint32_t u; void *o; uint32_t n; const char *s; struct wl_array *a; int32_t h; int32_t f; };
extern void wl_proxy_destroy(struct wl_proxy*);

#define WL_MARSHAL_FLAG_DESTROY 1
static struct wl_proxy *g_last_surface;

struct wl_proxy *wl_proxy_marshal_flags(struct wl_proxy *proxy, uint32_t opcode,
    const struct wl_interface *interface, uint32_t version, uint32_t flags, ...)
{
  const struct wl_interface *iface = ((struct wl_proxy_local*)proxy)->object.interface;
  const struct wl_message *msg = &iface->methods[opcode];
  const char *sig = msg->signature ? msg->signature : "";
  union wl_arg_local args[24];
  memset(args, 0, sizeof(args));
  int is_create_surface = (iface && strcmp(iface->name,"wl_compositor")==0 && opcode==0);
  fprintf(stderr,"[WLFLAGS] opcode=%u sig=%s iface=%s\n", opcode, sig, interface?interface->name:"(null)");
  va_list ap; va_start(ap, flags);
  int ai = 0;
  for (const char *p = sig; *p && ai < 24; p++) {
    if (*p >= '0' && *p <= '9') continue;
    if (*p == '?') continue;
    switch (*p) {
      case 'u': args[ai].u = va_arg(ap, uint32_t); break;
      case 'i': args[ai].i = va_arg(ap, int32_t); break;
      case 'o': { void *ov = va_arg(ap, void*); args[ai].o = (struct wl_object*)ov; fprintf(stderr,"[WLFLAGS] o[%d]=%p\n", ai, ov); break; }
      case 'n': (void)va_arg(ap, void *); args[ai].n = 0; break;
      case 's': args[ai].s = va_arg(ap, const char*); break;
      case 'a': args[ai].a = va_arg(ap, struct wl_array*); break;
      case 'h': args[ai].h = va_arg(ap, int32_t); break;
      case 'f': args[ai].f = va_arg(ap, wl_fixed_t); break;
      default: break;
    }
    ai++;
  }
  va_end(ap);
  if (iface && strcmp(iface->name,"xdg_wm_base")==0 && opcode==2 && args[1].o == NULL) {
    args[1].o = (struct wl_object*)g_last_surface;
    fprintf(stderr,"[WLFLAGS] xdg null surface -> last_surface=%p\n",(void*)g_last_surface);
  }
  if (iface && strcmp(iface->name,"zwp_linux_buffer_params_v1")==0 && opcode==3) {
    fprintf(stderr,"[WLFLAGS] CHEM args n=%u i1=%d i2=%d u3=%u u4=%u\n", args[0].u, args[1].i, args[2].i, args[3].u, args[4].u);
    if (args[1].i == 0 && args[2].i > 0 && args[3].u > 0) {
      args[1] = args[2]; args[2] = args[3]; args[3] = args[4]; args[4].u = 0;
      fprintf(stderr,"[WLFLAGS] CHEM normalized -> %d x %d fmt=0x%x\n", args[1].i, args[2].i, args[3].u);
    }
  }
  if (interface && args[1].o == NULL) {
    struct wl_proxy *replacement = NULL;
    if (((strcmp(interface->name,"zwp_tablet_seat_v2")==0 && opcode==0) ||
         (strcmp(interface->name,"wl_data_device")==0 && opcode==1) ||
         (strcmp(interface->name,"zwp_primary_selection_device_v1")==0 && opcode==1) ||
         (strcmp(interface->name,"zwp_text_input_v2")==0 && opcode==1)))
      replacement = g_wl_seat;
    else if (strcmp(interface->name,"zxdg_output_v1")==0 && opcode==1)
      replacement = g_wl_output;
    else if (((strcmp(interface->name,"wp_fractional_scale_v1")==0) ||
              (strcmp(interface->name,"wp_viewport")==0)) && opcode==1)
      replacement = g_last_surface;
    if (replacement) {
      args[1].o = (struct wl_object *)replacement;
      fprintf(stderr,"[WLFLAGS] substitute null object %s op=%u with=%p\n",
              interface->name, opcode, (void *)replacement);
    } else if ((strcmp(interface->name,"zwp_tablet_seat_v2")==0 && opcode==0) ||
               (strcmp(interface->name,"wl_data_device")==0 && opcode==1) ||
               (strcmp(interface->name,"zwp_primary_selection_device_v1")==0 && opcode==1) ||
               (strcmp(interface->name,"zxdg_output_v1")==0 && opcode==1) ||
               (strcmp(interface->name,"zwp_text_input_v2")==0 && opcode==1)) {
      fprintf(stderr,"[WLFLAGS] no proxy for null object factory %s op=%u\n",
              interface->name, opcode);
      return (struct wl_proxy *)0;
    }
  }
  if (iface && strcmp(iface->name,"zwp_tablet_seat_v2")==0 && opcode==0 && interface == NULL) {
    fprintf(stderr,"[WLFLAGS] SKIP tablet seat destroy\n");
    return NULL;
  }
  struct wl_proxy *ret = 0;
  int nidx = -1; { const char *q = strchr(sig, 'n'); if (q) nidx = (int)(q - sig); }
  if (interface) { ret = wl_proxy_marshal_array_constructor_versioned(proxy, opcode, (union wl_argument*)args, interface, version);
    if (!ret && nidx >= 0) ret = (struct wl_proxy*)args[nidx].o;
    fprintf(stderr,"[WLFLAGS] ctor ret=%p nidx=%d nval=%p\n", ret, nidx, nidx>=0?(void*)args[nidx].o:0); }
  else wl_proxy_marshal_array(proxy, opcode, (union wl_argument*)args);
  if (is_create_surface && ret) g_last_surface = ret;
  if ((flags & WL_MARSHAL_FLAG_DESTROY) && proxy) wl_proxy_destroy(proxy);
  return ret;
}
