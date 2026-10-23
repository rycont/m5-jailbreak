#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell-client-protocol.h"

typedef void* EGLDisplay; typedef void* EGLConfig; typedef void* EGLContext; typedef void* EGLSurface;
static EGLDisplay edpy; static EGLConfig ecfg; static EGLContext ectx; static EGLSurface esurf;
static int (*p_eglInitialize)(EGLDisplay,int*,int*);
static int (*p_eglChooseConfig)(EGLDisplay,const int*,EGLConfig*,int,int*);
static EGLContext (*p_eglCreateContext)(EGLDisplay,EGLConfig,EGLContext,const int*);
static EGLSurface (*p_eglCreateWindowSurface)(EGLDisplay,EGLConfig,void*,const int*);
static int (*p_eglMakeCurrent)(EGLDisplay,EGLSurface,EGLSurface,EGLContext);
static int (*p_eglSwapBuffers)(EGLDisplay,EGLSurface);
static int (*p_eglSwapInterval)(EGLDisplay,int);
static void (*p_glClearColor)(float,float,float,float);
static void (*p_glClear)(unsigned);
static const unsigned char* (*p_glGetString)(unsigned);

static struct wl_compositor *comp; static struct xdg_wm_base *wm;
static int configured=0, framedone=0;
static void reg_global(void*d,struct wl_registry*r,uint32_t n,const char*iface,uint32_t v){
  if(!strcmp(iface,wl_compositor_interface.name)) comp=wl_registry_bind(r,n,&wl_compositor_interface,v>4?4:v);
  else if(!strcmp(iface,xdg_wm_base_interface.name)) wm=wl_registry_bind(r,n,&xdg_wm_base_interface,1);
}
static const struct wl_registry_listener rl={reg_global,0};
static void wm_ping(void*d,struct xdg_wm_base*b,uint32_t s){ xdg_wm_base_pong(b,s); }
static const struct xdg_wm_base_listener wml={wm_ping};
static void xs_conf(void*d,struct xdg_surface*s,uint32_t serial){ xdg_surface_ack_configure(s,serial); configured=1; }
static const struct xdg_surface_listener xsl={xs_conf};
static void frame_done(void*d,struct wl_callback*cb,uint32_t t){ framedone=1; wl_callback_destroy(cb); }
static const struct wl_callback_listener fl={frame_done};

int main(int argc,char**argv){
  setvbuf(stdout,0,_IONBF,0);
  double secs = argc>1?atof(argv[1]):10.0;
  void *wl=dlopen("libwayland-client.so.0",RTLD_NOW|RTLD_GLOBAL);
  void *wegl=dlopen("libwayland-egl.so.1",RTLD_NOW|RTLD_GLOBAL);
  void *egl=dlopen("/usr/lib/libEGL.so.1",RTLD_NOW|RTLD_GLOBAL); if(!egl) egl=dlopen("libEGL.so.1",RTLD_NOW|RTLD_GLOBAL);
  void *gles=dlopen("/usr/lib/libGLESv2.so.2",RTLD_NOW|RTLD_GLOBAL); if(!gles) gles=dlopen("libGLESv2.so.2",RTLD_NOW|RTLD_GLOBAL);
  printf("libs wl=%p wegl=%p egl=%p gles=%p\n",wl,wegl,egl,gles); if(!wl||!wegl||!egl||!gles) return 2;
  EGLDisplay (*p_eglGetDisplay)(void*)=dlsym(egl,"eglGetDisplay");
  p_eglInitialize=dlsym(egl,"eglInitialize"); p_eglChooseConfig=dlsym(egl,"eglChooseConfig");
  p_eglCreateContext=dlsym(egl,"eglCreateContext"); p_eglCreateWindowSurface=dlsym(egl,"eglCreateWindowSurface");
  p_eglMakeCurrent=dlsym(egl,"eglMakeCurrent"); p_eglSwapBuffers=dlsym(egl,"eglSwapBuffers"); p_eglSwapInterval=dlsym(egl,"eglSwapInterval");
  p_glClearColor=dlsym(gles,"glClearColor"); p_glClear=dlsym(gles,"glClear"); p_glGetString=dlsym(gles,"glGetString");
  void* (*p_wlcreate)(struct wl_surface*,int,int)=dlsym(wegl,"wl_egl_window_create");
  if(!p_eglGetDisplay||!p_eglInitialize||!p_eglChooseConfig||!p_eglCreateContext||!p_eglCreateWindowSurface||!p_eglMakeCurrent||!p_wlcreate) { printf("missing syms\n"); return 2; }
  struct wl_display*dpy=wl_display_connect(NULL); if(!dpy){printf("no display\n");return 3;}
  struct wl_registry*reg=wl_display_get_registry(dpy); wl_registry_add_listener(reg,&rl,0); wl_display_roundtrip(dpy);
  printf("comp=%p wm=%p\n",comp,wm); if(!comp||!wm) return 3;
  xdg_wm_base_add_listener(wm,&wml,0);
  struct wl_surface*surf=wl_compositor_create_surface(comp);
  struct xdg_surface*xs=xdg_wm_base_get_xdg_surface(wm,surf);
  xdg_surface_add_listener(xs,&xsl,0);
  struct xdg_toplevel*top=xdg_surface_get_toplevel(xs);
  xdg_toplevel_set_title(top,"egl60"); xdg_toplevel_set_app_id(top,"egl60");
  xdg_toplevel_set_fullscreen(top,NULL);
  wl_surface_commit(surf);
  struct timespec a,b; clock_gettime(CLOCK_MONOTONIC,&a);
  while(!configured){ wl_display_dispatch(dpy); clock_gettime(CLOCK_MONOTONIC,&b); if((b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9>5.0) break; }
  printf("configured=%d\n",configured); if(!configured) return 4;
  edpy=p_eglGetDisplay(dpy); int maj=0,min=0;
  printf("eglInitialize=%d\n",p_eglInitialize(edpy,&maj,&min));
  int ca[]={0x3033,0x0004,0x3040,0x0004,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038}; int n=0;
  if(!p_eglChooseConfig(edpy,ca,&ecfg,1,&n)||!n){printf("chooseconfig fail\n");return 5;}
  ectx=p_eglCreateContext(edpy,ecfg,0,(int[]){0x3098,2,0x3038});
  struct wl_egl_window*win=p_wlcreate(surf,1920,1080);
  esurf=p_eglCreateWindowSurface(edpy,ecfg,win,(int[]){0x3038});
  printf("ctx=%p win=%p surf=%p\n",ectx,win,esurf); if(!ectx||!esurf) return 6;
  if(!p_eglMakeCurrent(edpy,esurf,esurf,ectx)){printf("makecurrent fail\n");return 7;}
  if(p_eglSwapInterval) p_eglSwapInterval(edpy,0);
  printf("GL_RENDERER=%s GL_VERSION=%s\n",(const char*)(p_glGetString?p_glGetString(0x1F01):(const unsigned char*)"?"),(const char*)(p_glGetString?p_glGetString(0x1F02):(const unsigned char*)"?"));
  clock_gettime(CLOCK_MONOTONIC,&a);
  int frames=0;
  while(1){
    struct wl_callback*cb=wl_surface_frame(surf); wl_callback_add_listener(cb,&fl,0); framedone=0;
    float v=(frames%120)/120.0f;
    p_glClearColor(v,0.2f,1.0f-v,1.0f); p_glClear(0x4000);
    p_eglSwapBuffers(edpy,esurf);
    wl_display_flush(dpy);
    while(!framedone){ if(wl_display_dispatch(dpy)<0) break; clock_gettime(CLOCK_MONOTONIC,&b); if((b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9>secs+2.0) break; }
    frames++;
    clock_gettime(CLOCK_MONOTONIC,&b);
    if((b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9>=secs) break;
  }
  clock_gettime(CLOCK_MONOTONIC,&b);
  double dt=(b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9;
  printf("frames=%d elapsed=%.3f client_fps=%.2f\n",frames,dt,frames/dt);
  return 0;
}
