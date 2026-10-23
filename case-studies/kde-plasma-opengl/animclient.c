#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <poll.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

static struct wl_compositor *comp; static struct wl_shm *shm; static struct xdg_wm_base *wm;
static int configured=0, released=1;
static void reg_global(void*d,struct wl_registry*r,uint32_t n,const char*iface,uint32_t v){
  if(!strcmp(iface,wl_compositor_interface.name)) comp=wl_registry_bind(r,n,&wl_compositor_interface,v>4?4:v);
  else if(!strcmp(iface,wl_shm_interface.name)) shm=wl_registry_bind(r,n,&wl_shm_interface,1);
  else if(!strcmp(iface,xdg_wm_base_interface.name)) wm=wl_registry_bind(r,n,&xdg_wm_base_interface,1);
}
static const struct wl_registry_listener rl={reg_global,0};
static void wm_ping(void*d,struct xdg_wm_base*b,uint32_t s){ xdg_wm_base_pong(b,s); }
static const struct xdg_wm_base_listener wml={wm_ping};
static void xs_conf(void*d,struct xdg_surface*s,uint32_t serial){ xdg_surface_ack_configure(s,serial); configured=1; }
static const struct xdg_surface_listener xsl={xs_conf};
static void buf_release(void*d,struct wl_buffer*b){ released=1; }
static const struct wl_buffer_listener bl={buf_release};

static int make_shm(int size){ char t[]="/tmp/animXXXXXX"; int fd=mkstemp(t); if(fd<0) return -1; unlink(t); if(ftruncate(fd,size)<0) return -1; return fd; }

int main(){
  setvbuf(stdout,0,_IONBF,0);
  struct wl_display*dpy=wl_display_connect(NULL); if(!dpy){printf("no display\n");return 1;}
  struct wl_registry*reg=wl_display_get_registry(dpy); wl_registry_add_listener(reg,&rl,0); wl_display_roundtrip(dpy);
  printf("comp=%p shm=%p wm=%p\n",comp,shm,wm); if(!comp||!shm||!wm) return 2;
  xdg_wm_base_add_listener(wm,&wml,0);
  const int W=480,H=270,BPR=W*4,SZ=W*H*4;
  int fd=make_shm(SZ); if(fd<0){printf("shm fail\n");return 3;}
  void*map=mmap(NULL,SZ,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); if(map==MAP_FAILED){printf("mmap fail\n");return 3;}
  struct wl_shm_pool*pool=wl_shm_create_pool(shm,fd,SZ);
  struct wl_buffer*buf=wl_shm_pool_create_buffer(pool,0,W,H,BPR,WL_SHM_FORMAT_XRGB8888);
  wl_buffer_add_listener(buf,&bl,0);
  struct wl_surface*surf=wl_compositor_create_surface(comp);
  struct xdg_surface*xs=xdg_wm_base_get_xdg_surface(wm,surf);
  xdg_surface_add_listener(xs,&xsl,0);
  struct xdg_toplevel*top=xdg_surface_get_toplevel(xs);
  xdg_toplevel_set_title(top,"animclient"); xdg_toplevel_set_app_id(top,"animclient");
  wl_surface_commit(surf);
  int guard=0; while(!configured && guard++<200){ wl_display_dispatch(dpy); }
  printf("configured=%d\n",configured);
  struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0);
  int frames=0; uint32_t*c=(uint32_t*)map;
  for(int i=0;i<900;i++){
    for(int j=0;j<W*H;j++) c[j]=0xff000000u | ((uint32_t)(i*5)&0xff)<<16 | ((uint32_t)(j)&0xff);
    wl_surface_attach(surf,buf,0,0); wl_surface_damage(surf,0,0,W,H); wl_surface_commit(surf);
    wl_display_flush(dpy);
    struct pollfd pfd={wl_display_get_fd(dpy),POLLIN,0};
    int g=0; while(!released && g++<10){ if(poll(&pfd,1,5)>0) wl_display_dispatch(dpy); }
    released=0; frames++;
    usleep(16000);
  }
  clock_gettime(CLOCK_MONOTONIC,&t1);
  double dt=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
  printf("frames=%d elapsed=%.3f client_fps=%.2f\n",frames,dt,frames/dt);
  return 0;
}
