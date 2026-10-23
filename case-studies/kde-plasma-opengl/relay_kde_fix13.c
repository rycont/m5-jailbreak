#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <dlfcn.h>
#include <wayland-server.h>
#include <wayland-server-protocol.h>
#include "xdg-shell-server-protocol.h"
#include "linux-dmabuf-server.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#ifndef SYS_memfd_create
#define SYS_memfd_create 385
#endif
static int relay_memfd_create(const char *name, unsigned int flags){ return (int)syscall(SYS_memfd_create, name, flags); }
#define memfd_create relay_memfd_create

typedef void* tbm_surface_h; typedef void* tbm_srv_t; typedef void* EGLDisplay; typedef void* EGLContext; typedef void* EGLSurface; typedef void* EGLImage; typedef void* EGLConfig;
struct tbm_plane { unsigned char *ptr; uint32_t size, offset, stride; void *r1,*r2,*r3; };
struct tbm_surface_info { uint32_t width,height,format,bpp,size,num_planes; struct tbm_plane planes[4]; void *r4,*r5,*r6; };
struct fb_var { uint32_t xres,yres,xres_virtual,yres_virtual,xoffset,yoffset,bits_per_pixel; uint32_t grayscale,red,green,blue,transp,nonstd,activate,height,width,accel_flags,pixclock,left,right,upper,lower,hync_len,vsync_len,sync,vmode,rotate,colorspace; uint32_t reserved[32]; };
struct fb_fix { char id[16]; unsigned long smem_start; uint32_t smem_len,type,type_aux,visual; uint16_t xpanstep,ypanstep,ywrapstep; uint32_t line_length; unsigned long mmio_start; uint32_t mmio_len,accel; uint16_t capabilities; uint16_t reserved[2]; };
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOGET_DMABUF 0x80044621u
#define KER_OSD_IOCTLID 200
#define IO_PLANE_SHOW_ENABLE _IOW(KER_OSD_IOCTLID, 0x0E, unsigned char)
#define IO_PM_RESUME _IO(KER_OSD_IOCTLID, 0x03)
#define IO_IS_PLANE_SHOW _IOR(KER_OSD_IOCTLID, 0x0F, unsigned char *)
#define EGL_NATIVE_SURFACE_TIZEN 0x32A1
#define EGL_LINUX_DMA_BUF_EXT 0x3270

static tbm_srv_t (*tbm_srv_init)(struct wl_display*, const char*, int, uint32_t);
static tbm_surface_h (*tbm_srv_get_surface)(tbm_srv_t, struct wl_resource*);
static uint32_t (*tbm_get_w)(tbm_surface_h), (*tbm_get_h)(tbm_surface_h), (*tbm_get_fmt)(tbm_surface_h);
static void* (*tbm_bm_init)(int); static void* (*tbm_import)(void*,int); static void* (*tbm_make)(struct tbm_surface_info*,void**,int);
static tbm_srv_t g_srv;

static EGLDisplay edpy; static EGLContext ectx; static EGLSurface epbuf; static EGLConfig ecfg;
static EGLImage (*eglCreateImageKHR_)(EGLDisplay,EGLContext,unsigned,void*,const int32_t*);
static void (*glEGLImageTargetTexture2DOES_)(unsigned,EGLImage);
static unsigned tex_dst, fbo_dst, tex_src, prog, tex_uniform; static int fbw,fbh,fbfd,fbstride,old_show=-1; static unsigned char *fbmem;
static void (*glClearColor_)(float,float,float,float); static void (*glClear_)(unsigned); static void (*glFinish_)(void);
static void (*glViewport_)(int,int,int,int); static void (*glBindAttribLocation_)(unsigned,unsigned,const char*); static void (*glGenTextures_)(int,unsigned*); static void (*glBindTexture_)(unsigned,unsigned);
static void (*glTexParameteri_)(unsigned,unsigned,int); static void (*glGenFramebuffers_)(int,unsigned*); static void (*glBindFramebuffer_)(unsigned,unsigned);
static void (*glFramebufferTexture2D_)(unsigned,unsigned,unsigned,unsigned,int); static unsigned (*glCheckFramebufferStatus_)(unsigned);
static void (*glUseProgram_)(unsigned); static unsigned (*glCreateShader_)(unsigned); static void (*glShaderSource_)(unsigned,int,const char**,const int*);
static void (*glCompileShader_)(unsigned); static unsigned (*glCreateProgram_)(void); static void (*glAttachShader_)(unsigned,unsigned); static void (*glLinkProgram_)(unsigned);
static int (*glGetUniformLocation_)(unsigned,const char*); static void (*glUniform1i_)(int,int); static void (*glVertexAttribPointer_)(unsigned,int,unsigned,unsigned char,int,const void*);
static void (*glEnableVertexAttribArray_)(unsigned); static void (*glDrawArrays_)(unsigned,int,int); static void (*glGetShaderiv_)(unsigned,unsigned,int*); static void (*glGetProgramiv_)(unsigned,unsigned,int*);
static void (*glReadPixels_)(int,int,int,int,unsigned,unsigned,void*);

static void restore_plane(void){ if(fbfd>=0 && old_show>=0){ int r=ioctl(fbfd,IO_PLANE_SHOW_ENABLE,(unsigned char)old_show); fprintf(stderr,"[OSD] restore plane=%d r=%d\n",old_show,r); } }
static void on_sig(int s){ restore_plane(); _exit(0); }

static int init_egl(void *twl){
  void *egl=dlopen("/usr/lib/libEGL.so.1",RTLD_NOW|RTLD_GLOBAL); if(!egl) egl=dlopen("libEGL.so.1",RTLD_NOW|RTLD_GLOBAL);
  void *gl=dlopen("/usr/lib/libGLESv2.so.2",RTLD_NOW|RTLD_GLOBAL); if(!gl) gl=dlopen("libGLESv2.so.2",RTLD_NOW|RTLD_GLOBAL);
  if(!egl||!gl){ fprintf(stderr,"EGL/GLES dlopen fail\n"); return -1; }
  EGLDisplay (*gd)(void*)=dlsym(egl,"eglGetDisplay"); int (*ei)(EGLDisplay,int*,int*)=dlsym(egl,"eglInitialize");
  int (*ch)(EGLDisplay,const int*,void**,int,int*)=dlsym(egl,"eglChooseConfig"); EGLSurface (*pb)(EGLDisplay,void*,const int*)=dlsym(egl,"eglCreatePbufferSurface");
  EGLContext (*cc)(EGLDisplay,void*,EGLContext,const int*)=dlsym(egl,"eglCreateContext"); int (*mc)(EGLDisplay,EGLSurface,EGLSurface,EGLContext)=dlsym(egl,"eglMakeCurrent");
  void *(*gp)(const char*)=dlsym(egl,"eglGetProcAddress");
  if(!gd||!ei||!ch||!pb||!cc||!mc){ fprintf(stderr,"EGL syms missing\n"); return -1; }
  edpy=gd(twl); int ma=0,mi=0; if(!ei(edpy,&ma,&mi)){ fprintf(stderr,"eglInitialize fail 0x%x\n",((int(*)(void))dlsym(egl,"eglGetError"))()); return -1; }
  int attr[]={0x3033,0x0001,0x3040,0x0004,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038}; int n=0;
  if(!ch(edpy,attr,(void**)&ecfg,1,&n)||!n){ fprintf(stderr,"choose config fail\n"); return -1; }
  epbuf=pb(edpy,ecfg,(int[]){0x3057,16,0x3056,16,0x3038}); ectx=cc(edpy,ecfg,0,(int[]){0x3098,2,0x3038});
  if(!mc(edpy,epbuf,epbuf,ectx)){ fprintf(stderr,"makeCurrent fail\n"); return -1; }
  eglCreateImageKHR_=dlsym(egl,"eglCreateImageKHR"); if(!eglCreateImageKHR_ && gp) eglCreateImageKHR_=gp("eglCreateImageKHR");
  glEGLImageTargetTexture2DOES_=gp?gp("glEGLImageTargetTexture2DOES"):dlsym(gl,"glEGLImageTargetTexture2DOES");
  #define S(x) x##_=dlsym(gl,#x)
  S(glClearColor); S(glClear); S(glFinish); S(glViewport); S(glBindAttribLocation); S(glGenTextures); S(glBindTexture); S(glTexParameteri); S(glGenFramebuffers); S(glBindFramebuffer); S(glFramebufferTexture2D); S(glCheckFramebufferStatus); S(glUseProgram); S(glCreateShader); S(glShaderSource); S(glCompileShader); S(glCreateProgram); S(glAttachShader); S(glLinkProgram); S(glGetUniformLocation); S(glUniform1i); S(glVertexAttribPointer); S(glEnableVertexAttribArray); S(glDrawArrays); S(glGetShaderiv); S(glGetProgramiv); S(glReadPixels);
  fprintf(stderr,"EGL init ok dpy=%p ctx=%p img=%p targettex=%p\n",edpy,ectx,(void*)eglCreateImageKHR_,(void*)glEGLImageTargetTexture2DOES_);
  return 0;
}

struct tex_cache_ent { tbm_surface_h s; uint32_t w,h; unsigned t; };
static struct tex_cache_ent tex_cache[8]; static int tex_cache_n;

static int dumped_src_tex=0; static int dump_frames=0;
static void dump_src_tex(unsigned t,int w,int h){
  if(dumped_src_tex || w!=1920 || h!=1080) return;
  if(++dump_frames < 200) return;
  dumped_src_tex=1;
  unsigned fbo=0; glGenFramebuffers_(1,&fbo); glBindFramebuffer_(0x8D40,fbo);
  glFramebufferTexture2D_(0x8D40,0x8CE0,0x0DE1,t,0);
  unsigned char *buf=(unsigned char*)malloc((size_t)w*h*4);
  if(buf){
    glReadPixels_(0,0,w,h,0x1908,0x1401,buf);
    FILE *f=fopen("/tmp/kwin_tex.raw","wb");
    if(f){ fwrite(buf,1,(size_t)w*h*4,f); fclose(f); }
    free(buf);
  }
  glBindFramebuffer_(0x8D40,fbo_dst);
  fprintf(stderr,"[DUMP] kwin texture dumped %dx%d\n",w,h);
}

static unsigned make_tex_from_surface(tbm_surface_h s, uint32_t w, uint32_t h){
  for(int i=0;i<tex_cache_n;i++){
    if(tex_cache[i].s==s && tex_cache[i].w==w && tex_cache[i].h==h){
      return tex_cache[i].t;
    }
  }
  int32_t at[5]={0x3057,(int32_t)w,0x3056,(int32_t)h,0x3038};
  EGLImage img=eglCreateImageKHR_?eglCreateImageKHR_(edpy,ectx,EGL_NATIVE_SURFACE_TIZEN,s,at):0;
  if(!img){ fprintf(stderr,"[OSD] image fail surf=%p %ux%u\n",s,w,h); return 0; }
  unsigned t=0; glGenTextures_(1,&t); glBindTexture_(0x0DE1,t); glTexParameteri_(0x0DE1,0x2801,0x2601); glTexParameteri_(0x0DE1,0x2800,0x2601);
  glEGLImageTargetTexture2DOES_(0x0DE1,img);
  if(tex_cache_n<8){ tex_cache[tex_cache_n].s=s; tex_cache[tex_cache_n].w=w; tex_cache[tex_cache_n].h=h; tex_cache[tex_cache_n].t=t; tex_cache_n++; fprintf(stderr,"[TEX] new %d surf=%p\n",tex_cache_n,s); }
  return t;
}

static int init_fb1(void){
  const char *fbe=getenv("FB_FD"); fbfd = fbe?atoi(fbe):-1;
  if(fbfd<0){ fbfd=open("/dev/fb1",O_RDWR); }
  fprintf(stderr,"[OSD] fb1 fd=%d errno=%d\n",fbfd,errno); if(fbfd<0){ return -1; }
  struct fb_var v; struct fb_fix f; memset(&v,0,sizeof v); memset(&f,0,sizeof f);
  if(ioctl(fbfd,FBIOGET_VSCREENINFO,&v)<0||ioctl(fbfd,FBIOGET_FSCREENINFO,&f)<0){ fprintf(stderr,"fb ioctl fail\n"); return -1; }
  fbw=v.xres; fbh=v.yres; uint32_t stride=f.line_length, size=stride*v.yres; fbstride=(int)stride;
  if(fcntl(0,F_GETFD)==-1){ int z=open("/dev/null",O_RDONLY); fprintf(stderr,"[OSD] reopened fd0=%d\n",z); }
  errno=0; int pr=ioctl(fbfd,IO_PM_RESUME,0); fprintf(stderr,"[OSD] PM_RESUME r=%d errno=%d\n",pr,errno);
  unsigned char pre[512]; memset(pre,0,sizeof pre);
  for(int i=0;i<512;i++) pre[i]=(fcntl(i,F_GETFD)>=0);
  struct { uint32_t fd,flags; } ex; memset(&ex,0,sizeof ex);
  errno=0; int dr=ioctl(fbfd,FBIOGET_DMABUF,&ex); fprintf(stderr,"[OSD] FBIOGET_DMABUF r=%d fd=%u flags=0x%x errno=%d\n",dr,ex.fd,ex.flags,errno);
  if(dr<0){ return -1; }
  int dmfd=(int)ex.fd;
  if(dmfd<=0){
    for(int i=3;i<512;i++){
      if(pre[i]) continue;
      if(fcntl(i,F_GETFD)<0) continue;
      char lp[64],tg[160]; snprintf(lp,sizeof lp,"/proc/self/fd/%d",i);
      int n=readlink(lp,tg,sizeof tg-1); if(n<0) continue; tg[n]=0;
      fprintf(stderr,"[OSD] newfd %d -> %s\n",i,tg);
      if(strstr(tg,"dmabuf")){ dmfd=i; break; }
    }
  }
  fprintf(stderr,"[OSD] fb geom ok %dx%d stride=%u dmabuf=%d\n",fbw,fbh,stride,dmfd);
  void *bm=tbm_bm_init?tbm_bm_init(-1):0; fprintf(stderr,"[OSD] bm=%p\n",bm);
  void *bo=tbm_import?tbm_import(bm,dmfd):0; fprintf(stderr,"[OSD] bo=%p\n",bo);
  struct tbm_surface_info inf; memset(&inf,0,sizeof inf); inf.width=fbw; inf.height=fbh; inf.format=0x34325241; inf.bpp=32; inf.size=size; inf.num_planes=1; inf.planes[0].size=size; inf.planes[0].stride=stride;
  tbm_surface_h surf=tbm_make?tbm_make(&inf,&bo,1):0;
  fprintf(stderr,"[OSD] fb1 %dx%d stride=%u size=%u dmabuf=%d bo=%p surf=%p\n",fbw,fbh,stride,size,dmfd,bo,surf);
  fbmem=(unsigned char*)mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED,fbfd,0); if(fbmem==MAP_FAILED){ fbmem=0; fprintf(stderr,"[OSD] fb mmap failed errno=%d\n",errno); } else fprintf(stderr,"[OSD] fb mmap=%p\n",fbmem);
  if(!surf) return -1;
  tex_dst=make_tex_from_surface(surf,fbw,fbh); if(!tex_dst) return -1;
  glGenFramebuffers_(1,&fbo_dst); glBindFramebuffer_(0x8D40,fbo_dst); glFramebufferTexture2D_(0x8D40,0x8CE0,0x0DE1,tex_dst,0);
  unsigned st=glCheckFramebufferStatus_(0x8D40); fprintf(stderr,"[OSD] FBO dst status=0x%x\n",st);
  unsigned char show=0; ioctl(fbfd,IO_IS_PLANE_SHOW,&show); old_show=show;
  return st==0x8CD5?0:-1;
}

static unsigned compile(unsigned type,const char *src){
  unsigned s=glCreateShader_(type); glShaderSource_(s,1,&src,0); glCompileShader_(s); int ok=0; glGetShaderiv_(s,0x8B81,&ok);
  if(!ok){ char log[512]; int n=0; ((void(*)(unsigned,int,int*,char*))dlsym(RTLD_DEFAULT,"glGetShaderInfoLog"))(s,512,&n,log); fprintf(stderr,"shader fail: %s\n",log); }
  return s;
}
static int init_prog(void){
  const char *vs="attribute vec2 p; attribute vec2 t; varying vec2 v; void main(){ v=t; gl_Position=vec4(p,0.0,1.0); }";
  const char *fs="precision mediump float; varying vec2 v; uniform sampler2D s; void main(){ gl_FragColor=texture2D(s,v); }";
  unsigned v=compile(0x8B31,vs), f=compile(0x8B30,fs); prog=glCreateProgram_(); glAttachShader_(prog,v); glAttachShader_(prog,f); glBindAttribLocation_(prog,0,"p"); glBindAttribLocation_(prog,1,"t"); glLinkProgram_(prog);
  int ok=0; glGetProgramiv_(prog,0x8B82,&ok); fprintf(stderr,"[OSD] program=%u link=%d\n",prog,ok);
  tex_uniform=glGetUniformLocation_(prog,"s"); return ok?0:-1;
}
static void show_plane_once(void){ static int shown=0; if(!shown){ unsigned char old=0; ioctl(fbfd,IO_IS_PLANE_SHOW,&old); int r=ioctl(fbfd,IO_PLANE_SHOW_ENABLE,(unsigned char)1); fprintf(stderr,"[OSD] plane old=%u enable r=%d errno=%d\n",old,r,errno); shown=1; } }
static int draw_shm(struct wl_resource *buf){
  struct wl_shm_buffer *sb=wl_shm_buffer_get(buf); if(!sb) return 0;
  int w=wl_shm_buffer_get_width(sb), h=wl_shm_buffer_get_height(sb), st=wl_shm_buffer_get_stride(sb);
  wl_shm_buffer_begin_access(sb); unsigned char *src=(unsigned char*)wl_shm_buffer_get_data(sb);
  if(src && fbmem){ int n=h<fbh?h:fbh; for(int y=0;y<n;y++) memcpy(fbmem+(size_t)y*fbstride, src+(size_t)y*st, (size_t)((w<fbw?w:fbw)*4)); }
  wl_shm_buffer_end_access(sb);
  fprintf(stderr,"[OSD] shm %dx%d stride=%d fmt=0x%x\n",w,h,st,wl_shm_buffer_get_format(sb));
  show_plane_once(); return 1;
}
static void draw_frame(tbm_surface_h s,uint32_t w,uint32_t h){
  if(!s) return; tex_src=make_tex_from_surface(s,w,h); if(!tex_src) return; dump_src_tex(tex_src,w,h);
  glBindFramebuffer_(0x8D40,fbo_dst); glViewport_(0,0,fbw,fbh); glUseProgram_(prog); glUniform1i_(tex_uniform,0);
  glBindTexture_(0x0DE1,tex_src);
  static const float verts[]={-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,1,1,1};
  unsigned loc_p=0, loc_t=1;
  glEnableVertexAttribArray_(loc_p); glVertexAttribPointer_(loc_p,2,0x1406,0,16,verts);
  glEnableVertexAttribArray_(loc_t); glVertexAttribPointer_(loc_t,2,0x1406,0,16,verts+2);
  glDrawArrays_(0x0005,0,4); glFinish_();
  unsigned char px[4]={0,0,0,0}; if(glReadPixels_){ glReadPixels_(fbw/2,fbh/2,1,1,0x1908,0x1401,px); fprintf(stderr,"[OSD] readback=%02x%02x%02x%02x\n",px[0],px[1],px[2],px[3]); }
  static int shown=0; if(!shown){ unsigned char old=0; ioctl(fbfd,IO_IS_PLANE_SHOW,&old); int r=ioctl(fbfd,IO_PLANE_SHOW_ENABLE,(unsigned char)1); fprintf(stderr,"[OSD] plane old=%u enable r=%d errno=%d\n",old,r,errno); shown=1; }
}

struct surf_state { struct wl_resource *res; struct wl_resource *pending; struct wl_resource *frame; struct wl_resource *xdg_surface; struct wl_resource *xdg_toplevel; int configured; };
static struct wl_display *g_dpy;
extern const struct xdg_surface_interface kde_xs_impl;
extern const struct xdg_toplevel_interface kde_xt_impl;
static void surf_destroy(struct wl_client *c, struct wl_resource *r){ wl_resource_destroy(r); }
static void surf_attach(struct wl_client *c, struct wl_resource *r, struct wl_resource *buffer, int32_t x, int32_t y){ struct surf_state *st=wl_resource_get_user_data(r); fprintf(stderr,"[ATTACH] buffer=%p id=%u st=%p\n",(void*)buffer,buffer?wl_resource_get_id(buffer):0,(void*)st); st->pending=buffer; }
static void surf_damage(struct wl_client *c, struct wl_resource *r, int32_t x,int32_t y,int32_t w,int32_t h){}
static void surf_frame(struct wl_client *c, struct wl_resource *r, uint32_t id){ struct surf_state *st=wl_resource_get_user_data(r); st->frame=wl_resource_create(c,&wl_callback_interface,1,id); }
static void surf_set_opaque(struct wl_client *c, struct wl_resource *r, struct wl_resource *reg){}
static void surf_set_input(struct wl_client *c, struct wl_resource *r, struct wl_resource *reg){}
static void surf_commit(struct wl_client *c, struct wl_resource *r){
  struct surf_state *st=wl_resource_get_user_data(r); fprintf(stderr,"[XDG] commit st=%p pending=%p\n", (void*)st, st?(void*)st->pending:0);
  if(st->pending){
    if(draw_shm(st->pending)){ wl_buffer_send_release(st->pending); st->pending=NULL; if(st->frame){ wl_callback_send_done(st->frame,0); wl_resource_destroy(st->frame); st->frame=NULL; } return; }
    tbm_surface_h s=tbm_srv_get_surface(g_srv,st->pending);
    if(!s) s=(tbm_surface_h)wl_resource_get_user_data(st->pending);
    uint32_t w=s&&tbm_get_w?tbm_get_w(s):0,h=s&&tbm_get_h?tbm_get_h(s):0;
    fprintf(stderr,"[OSD] commit surf=%p %ux%u\n",s,w,h);
    if(s&&w&&h) draw_frame(s,w,h);
    wl_buffer_send_release(st->pending); st->pending=NULL;
  }
  if(st->frame){ wl_callback_send_done(st->frame,0); wl_resource_destroy(st->frame); st->frame=NULL; }
}
static void surf_transform(struct wl_client *c, struct wl_resource *r, int32_t t){}
static void surf_scale(struct wl_client *c, struct wl_resource *r, int32_t s){}
static void surf_damage_buffer(struct wl_client *c, struct wl_resource *r, int32_t x,int32_t y,int32_t w,int32_t h){}
static const struct wl_surface_interface surface_impl={.destroy=surf_destroy,.attach=surf_attach,.damage=surf_damage,.frame=surf_frame,.set_opaque_region=surf_set_opaque,.set_input_region=surf_set_input,.commit=surf_commit,.set_buffer_transform=surf_transform,.set_buffer_scale=surf_scale,.damage_buffer=surf_damage_buffer};

static void kde_configure(struct surf_state *st){
  if(!st->xdg_surface || !st->xdg_toplevel) return;
  uint32_t serial = wl_display_next_serial(g_dpy);
  struct wl_array states; wl_array_init(&states);
  xdg_toplevel_send_configure(st->xdg_toplevel, 1920, 1080, &states);
  xdg_surface_send_configure(st->xdg_surface, serial);
  fprintf(stderr,"[XDG] configure sent serial=%u %dx%d xs=%p xt=%p\n", serial,1920,1080,(void*)st->xdg_surface,(void*)st->xdg_toplevel);
  fflush(stderr);
  wl_array_release(&states);
  st->configured = 1;
}

static void xdg_wm_destroy(struct wl_client *c, struct wl_resource *r){ wl_resource_destroy(r); }
static void xdg_get_surface(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface_res){
  fprintf(stderr,"[XDG] get_surface id=%u surf_res=%p st=%p\n", id, (void*)surface_res, wl_resource_get_user_data(surface_res));
  struct surf_state *st = wl_resource_get_user_data(surface_res);
  if(!st) return;
  struct wl_resource *xs = wl_resource_create(c, &xdg_surface_interface, wl_resource_get_version(r), id);
  if(!xs){ wl_client_post_no_memory(c); return; }
  st->xdg_surface = xs;
  wl_resource_set_implementation(xs, &kde_xs_impl, st, 0);
}
static void xdg_pong(struct wl_client *c, struct wl_resource *r, uint32_t serial){}
static const struct xdg_wm_base_interface kde_wm_impl = { .destroy=xdg_wm_destroy, .get_xdg_surface=xdg_get_surface, .pong=xdg_pong };

static void xs_destroy(struct wl_client *c, struct wl_resource *r){ wl_resource_destroy(r); }
static void xs_get_toplevel(struct wl_client *c, struct wl_resource *r, uint32_t id){
  fprintf(stderr,"[XDG] get_toplevel id=%u st=%p\n", id, wl_resource_get_user_data(r));
  struct surf_state *st = wl_resource_get_user_data(r);
  if(!st) return;
  struct wl_resource *xt = wl_resource_create(c, &xdg_toplevel_interface, wl_resource_get_version(r), id);
  if(!xt){ wl_client_post_no_memory(c); return; }
  st->xdg_toplevel = xt;
  wl_resource_set_implementation(xt, &kde_xt_impl, st, 0);
  kde_configure(st);
}
static void xs_get_popup(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *parent, struct wl_resource *pos){}
static void xs_set_geometry(struct wl_client *c, struct wl_resource *r, int32_t x,int32_t y,int32_t w,int32_t h){}
static void xs_ack(struct wl_client *c, struct wl_resource *r, uint32_t serial){ struct surf_state *st=wl_resource_get_user_data(r); fprintf(stderr,"[XDG] ack serial=%u st=%p\n", serial,(void*)st); if(st) st->configured=2; }
const struct xdg_surface_interface kde_xs_impl = { .destroy=xs_destroy, .get_toplevel=xs_get_toplevel, .get_popup=xs_get_popup, .set_window_geometry=xs_set_geometry, .ack_configure=xs_ack };

static void xt_destroy(struct wl_client *c, struct wl_resource *r){}
static void xt_set_parent(struct wl_client *c, struct wl_resource *r, struct wl_resource *p){}
static void xt_set_title(struct wl_client *c, struct wl_resource *r, const char *t){}
static void xt_set_app_id(struct wl_client *c, struct wl_resource *r, const char *a){}
static void xt_show_menu(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y){}
static void xt_move(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial){}
static void xt_resize(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial, uint32_t edges){}
static void xt_set_max_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h){}
static void xt_set_min_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h){}
static void xt_set_maximized(struct wl_client *c, struct wl_resource *r){}
static void xt_unset_maximized(struct wl_client *c, struct wl_resource *r){}
static void xt_set_fullscreen(struct wl_client *c, struct wl_resource *r, struct wl_resource *o){}
static void xt_unset_fullscreen(struct wl_client *c, struct wl_resource *r){}
static void xt_set_minimized(struct wl_client *c, struct wl_resource *r){}
const struct xdg_toplevel_interface kde_xt_impl = { .destroy=xt_destroy, .set_parent=xt_set_parent, .set_title=xt_set_title, .set_app_id=xt_set_app_id, .show_window_menu=xt_show_menu, .move=xt_move, .resize=xt_resize, .set_max_size=xt_set_max_size, .set_min_size=xt_set_min_size, .set_maximized=xt_set_maximized, .unset_maximized=xt_unset_maximized, .set_fullscreen=xt_set_fullscreen, .unset_fullscreen=xt_unset_fullscreen, .set_minimized=xt_set_minimized };


static void dm_fb_destroy(struct wl_client *c, struct wl_resource *r){ wl_resource_destroy(r); }
static const struct zwp_linux_dmabuf_feedback_v1_interface dm_fb_impl = { .destroy=dm_fb_destroy };
static void dm_feedback_send(struct wl_resource *fb){
  struct stat st; dev_t dev=0; if(stat("/dev/dri/card0",&st)==0) dev=st.st_rdev;
  int fd=memfd_create("dmfmt",MFD_CLOEXEC);
  if(fd>=0){ unsigned char buf[32]; memset(buf,0,32); uint32_t fmt0=0x34325258, fmt1=0x34325241; memcpy(buf,&fmt0,4); memcpy(buf+8,&fmt1,4); if(write(fd,buf,32)==32){ lseek(fd,0,SEEK_SET); zwp_linux_dmabuf_feedback_v1_send_format_table(fb,fd,32);} close(fd); }
  struct wl_array arr; wl_array_init(&arr); void *p=wl_array_add(&arr,sizeof(dev)); if(p){ memcpy(p,&dev,sizeof(dev)); zwp_linux_dmabuf_feedback_v1_send_main_device(fb,&arr); zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(fb,&arr); }
  struct wl_array idx; wl_array_init(&idx); uint16_t *ip=wl_array_add(&idx,4); if(ip){ ip[0]=0; ip[1]=1; }
  zwp_linux_dmabuf_feedback_v1_send_tranche_formats(fb,&idx);
  zwp_linux_dmabuf_feedback_v1_send_tranche_flags(fb,0);
  zwp_linux_dmabuf_feedback_v1_send_tranche_done(fb);
  zwp_linux_dmabuf_feedback_v1_send_done(fb);
  wl_array_release(&arr); wl_array_release(&idx);
  fprintf(stderr,"[DMABUF] feedback sent dev=%llu\n",(unsigned long long)dev);
}
static void dm_get_default_feedback(struct wl_client *c, struct wl_resource *r, uint32_t id){
  struct wl_resource *fb=wl_resource_create(c,&zwp_linux_dmabuf_feedback_v1_interface,wl_resource_get_version(r)>4?4:wl_resource_get_version(r),id);
  if(!fb){ wl_client_post_no_memory(c); return; }
  wl_resource_set_implementation(fb,&dm_fb_impl,0,0);
  dm_feedback_send(fb);
}
static void dm_get_surface_feedback(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface){
  dm_get_default_feedback(c,r,id);
}
static void dm_params_destroy(struct wl_client *c, struct wl_resource *r){ wl_resource_destroy(r); }
struct dm_params_state { int fd; uint32_t off, stride; };
static void dm_params_add(struct wl_client *c, struct wl_resource *r, int32_t fd, uint32_t plane, uint32_t off, uint32_t stride, uint32_t mh, uint32_t ml){ fprintf(stderr,"[DMABUF] params_add fd=%d plane=%u off=%u stride=%u\n",fd,plane,off,stride);
  struct dm_params_state *ps=wl_resource_get_user_data(r);
  if(ps && plane==0){ if(ps->fd>=0) close(ps->fd); ps->fd=dup(fd); ps->off=off; ps->stride=stride; }
}
static void dm_params_create(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h, uint32_t fmt, uint32_t flags){ fprintf(stderr,"[DMABUF] params_create(v1) %dx%d fmt=0x%x\n",w,h,fmt); zwp_linux_buffer_params_v1_send_failed(r); }
static void dm_buf_destroy_req(struct wl_client *c, struct wl_resource *r){ wl_resource_destroy(r); }
static const struct wl_buffer_interface dm_buf_impl = { .destroy=dm_buf_destroy_req };
static void dm_params_create_immed(struct wl_client *c, struct wl_resource *r, uint32_t id, int32_t w, int32_t h, uint32_t fmt, uint32_t flags){
  struct dm_params_state *ps=wl_resource_get_user_data(r);
  fprintf(stderr,"[DMABUF] create_immed enter ps=%p fd=%d w=%d h=%d fmt=0x%x id=%u\n",(void*)ps,ps?ps->fd:-999,w,h,fmt,id);
  if(w==0 && h==1920 && fmt==1080){ w=1920; h=1080; fmt=0x34325258; fprintf(stderr,"[DMABUF] corrected shifted args -> 1920x1080 XRGB\n"); }
  if(!ps || ps->fd<0 || w<=0 || h<=0){ zwp_linux_buffer_params_v1_send_failed(r); return; }
  void *bm=tbm_bm_init?tbm_bm_init(-1):0; void *bo=tbm_import?tbm_import(bm,dup(ps->fd)):0;
  struct tbm_surface_info inf; memset(&inf,0,sizeof inf);
  inf.width=w; inf.height=h; inf.format=fmt; inf.bpp=32; inf.size=ps->stride*h; inf.num_planes=1; inf.planes[0].size=inf.size; inf.planes[0].offset=0; inf.planes[0].stride=ps->stride;
  void *surf=tbm_make?tbm_make(&inf,&bo,1):0;
  fprintf(stderr,"[DMABUF] create_immed %dx%d fmt=0x%x fd=%d surf=%p\n",w,h,fmt,ps->fd,surf);
  if(!surf){ zwp_linux_buffer_params_v1_send_failed(r); return; }
  struct wl_resource *buf=wl_resource_create(c,&wl_buffer_interface,1,id);
  if(!buf){ zwp_linux_buffer_params_v1_send_failed(r); return; }
  wl_resource_set_implementation(buf,&dm_buf_impl,surf,NULL);
}
static const struct zwp_linux_buffer_params_v1_interface dm_params_impl = { .destroy=dm_params_destroy, .add=dm_params_add, .create=dm_params_create, .create_immed=dm_params_create_immed };
static void dm_create_params(struct wl_client *c, struct wl_resource *r, uint32_t id){
  struct wl_resource *p=wl_resource_create(c,&zwp_linux_buffer_params_v1_interface,wl_resource_get_version(r),id);
  if(!p){ wl_client_post_no_memory(c); return; }
  struct dm_params_state *ps=calloc(1,sizeof *ps); ps->fd=-1;
  wl_resource_set_implementation(p,&dm_params_impl,ps,NULL);
}
static void dm_destroy(struct wl_client *c, struct wl_resource *r){ wl_resource_destroy(r); }
static const struct zwp_linux_dmabuf_v1_interface dm_impl = { .destroy=dm_destroy, .create_params=dm_create_params, .get_default_feedback=dm_get_default_feedback, .get_surface_feedback=dm_get_surface_feedback };
static void output_bind(struct wl_client *c, void *data, uint32_t ver, uint32_t id){
  fprintf(stderr,"[OUTPUT] bind ver=%u\n",ver);
  struct wl_resource *r=wl_resource_create(c,&wl_output_interface,ver,id);
  if(!r){ wl_client_post_no_memory(c); return; }
  wl_output_send_geometry(r,0,0,600,340,WL_OUTPUT_SUBPIXEL_UNKNOWN,"M5","panel",WL_OUTPUT_TRANSFORM_NORMAL);
  wl_output_send_mode(r,WL_OUTPUT_MODE_CURRENT|WL_OUTPUT_MODE_PREFERRED,1920,1080,60000);
  if(ver>=2){ wl_output_send_scale(r,1); }
  if(ver>=4){ wl_resource_post_event(r,4,"WL-1"); wl_resource_post_event(r,5,"M5 panel"); }
  if(ver>=2) wl_output_send_done(r);
}
static void dm_bind(struct wl_client *c, void *data, uint32_t ver, uint32_t id){
  uint32_t v=ver>4?4:ver;
  struct wl_resource *r=wl_resource_create(c,&zwp_linux_dmabuf_v1_interface,v,id);
  if(!r){ wl_client_post_no_memory(c); return; }
  wl_resource_set_implementation(r,&dm_impl,0,0);
  fprintf(stderr,"[DMABUF] bind v%u\n",v);
}

static void xdg_bind(struct wl_client *c, void *data, uint32_t ver, uint32_t id){
  uint32_t v = ver>4?4:ver;
  struct wl_resource *res = wl_resource_create(c, &xdg_wm_base_interface, v, id);
  if(!res){ wl_client_post_no_memory(c); return; }
  wl_resource_set_implementation(res, &kde_wm_impl, 0, 0);
}
static void seat_get_pointer(struct wl_client *c, struct wl_resource *r, uint32_t id){ wl_resource_create(c,&wl_pointer_interface,wl_resource_get_version(r),id); }
static void seat_get_keyboard(struct wl_client *c, struct wl_resource *r, uint32_t id){ wl_resource_create(c,&wl_keyboard_interface,wl_resource_get_version(r),id); }
static void seat_get_touch(struct wl_client *c, struct wl_resource *r, uint32_t id){ wl_resource_create(c,&wl_touch_interface,wl_resource_get_version(r),id); }
static void seat_release(struct wl_client *c, struct wl_resource *r){ wl_resource_destroy(r); }
static const struct wl_seat_interface kde_seat_impl = { .get_pointer=seat_get_pointer, .get_keyboard=seat_get_keyboard, .get_touch=seat_get_touch, .release=seat_release };
static void seat_bind(struct wl_client *c, void *data, uint32_t ver, uint32_t id){
  uint32_t v=ver>5?5:ver;
  struct wl_resource *res=wl_resource_create(c,&wl_seat_interface,v,id);
  if(!res){ wl_client_post_no_memory(c); return; }
  wl_resource_set_implementation(res,&kde_seat_impl,0,0);
  wl_seat_send_capabilities(res,0);
}

static void comp_create_surface(struct wl_client *c, struct wl_resource *r, uint32_t id){
  struct wl_resource *surf=wl_resource_create(c,&wl_surface_interface,wl_resource_get_version(r),id); if(!surf){wl_client_post_no_memory(c);return;}
  struct surf_state *st=calloc(1,sizeof *st); st->res=surf; wl_resource_set_implementation(surf,&surface_impl,st,NULL);
}
static void comp_create_region(struct wl_client *c, struct wl_resource *r, uint32_t id){ wl_resource_create(c,&wl_region_interface,wl_resource_get_version(r),id); }
static const struct wl_compositor_interface comp_impl={.create_surface=comp_create_surface,.create_region=comp_create_region};
static void comp_bind(struct wl_client *c, void *data, uint32_t ver, uint32_t id){ struct wl_resource *res=wl_resource_create(c,&wl_compositor_interface,ver<4?ver:4,id); wl_resource_set_implementation(res,&comp_impl,NULL,NULL); }

int main(int argc,char**argv){
  const char *sock=argc>1?argv[1]:"wayland-9";
  signal(SIGINT,on_sig); signal(SIGTERM,on_sig);
  void *h=dlopen("/usr/lib/libwayland-tbm-server.so.0",RTLD_NOW|RTLD_GLOBAL); if(!h) h=dlopen("libwayland-tbm-server.so.0",RTLD_NOW|RTLD_GLOBAL);
  void *t=dlopen("libtbm.so.1",RTLD_NOW|RTLD_GLOBAL); if(!t) t=dlopen("/usr/lib/libtbm.so.1",RTLD_NOW|RTLD_GLOBAL);
  if(h){ tbm_srv_init=dlsym(h,"wayland_tbm_server_init"); tbm_srv_get_surface=dlsym(h,"wayland_tbm_server_get_surface"); }
  if(t){ tbm_get_w=dlsym(t,"tbm_surface_get_width"); tbm_get_h=dlsym(t,"tbm_surface_get_height"); tbm_get_fmt=dlsym(t,"tbm_surface_get_format"); tbm_bm_init=dlsym(t,"tbm_bufmgr_init"); tbm_import=dlsym(t,"tbm_bo_import_fd"); tbm_make=dlsym(t,"tbm_surface_internal_create_with_bos"); }
  void *wc=dlopen("libwayland-client.so.0",RTLD_NOW|RTLD_GLOBAL); void *ec=dlopen("/usr/lib/libEGL.so.1",RTLD_NOW|RTLD_GLOBAL);
  void* (*wlc)(const char*)=wc?dlsym(wc,"wl_display_connect"):0; void* (*gd)(void*)=ec?dlsym(ec,"eglGetDisplay"):0;
  const char *tv=getenv("TV_WAYLAND_DISPLAY"); if(!tv) tv="wayland-0"; void *twl=wlc?wlc(tv):0;
  fprintf(stderr,"[OSD] TV wl=%p\n",twl); if(!twl) return 2;
  if(init_egl(twl)<0) return 3;
  if(init_fb1()<0) return 4;
  if(init_prog()<0) return 5;
  struct wl_display *dpy=wl_display_create(); if(!dpy) return 6;
  wl_display_init_shm(dpy);
  if(wl_display_add_socket(dpy,sock)<0){ fprintf(stderr,"add socket fail\n"); return 7; }
  g_dpy=dpy;
  g_srv=tbm_srv_init?tbm_srv_init(dpy,NULL,-1,0):0; fprintf(stderr,"[OSD] tbm_srv=%p\n",g_srv); if(!g_srv) return 8;
  wl_global_create(dpy,&wl_compositor_interface,4,NULL,comp_bind);
  wl_global_create(dpy,&xdg_wm_base_interface,4,NULL,xdg_bind);
  wl_global_create(dpy,&wl_seat_interface,5,NULL,seat_bind);
  wl_global_create(dpy,&zwp_linux_dmabuf_v1_interface,4,NULL,dm_bind);
  wl_global_create(dpy,&wl_output_interface,3,NULL,output_bind);
  fprintf(stderr,"[OSD] listening %s\n",sock);
  wl_display_run(dpy);
  restore_plane();
  return 0;
}
