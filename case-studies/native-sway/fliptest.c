#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_ioctl 54
#define SYS_mmap2 192
#define SYS_poll 168
#define SYS_read 3
#define SYS_nanosleep 162
#define SYS_exit_group 248
#define O_RDWR 2
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
static long sys1(long n,long a){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;__asm__ volatile("svc 0":"+r"(r0):"r"(r7):"memory");return r0;}
static long sys2(long n,long a,long b){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;register long r1 __asm__("r1")=b;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r7):"memory");return r0;}
static long sys3(long n,long a,long b,long c){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;register long r1 __asm__("r1")=b;register long r2 __asm__("r2")=c;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory");return r0;}
static long sys6(long n,long a,long b,long c,long d,long e,long f){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;register long r1 __asm__("r1")=b;register long r2 __asm__("r2")=c;register long r3 __asm__("r3")=d;register long r4 __asm__("r4")=e;register long r5 __asm__("r5")=f;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r3),"r"(r4),"r"(r5),"r"(r7):"memory");return r0;}
static int slen(const char*s){int n=0;while(s[n])n++;return n;}
static void puts_(const char*s){sys3(SYS_write,1,(long)s,slen(s));}
static void putdec(long v){char b[12];int i=0;int neg=v<0;unsigned long u=neg?(unsigned long)(-v):(unsigned long)v;if(neg)puts_("-");if(!u){puts_("0");return;}while(u){b[i++]=(char)('0'+(u%10));u/=10;}while(i--){char c=b[i];sys3(SYS_write,1,(long)&c,1);}}
void *memcpy(void*d,const void*src,unsigned long n){unsigned char*a=d;const unsigned char*b=src;while(n--)*a++=*b++;return d;}
struct modeinfo{unsigned int clock;unsigned short hdisplay,hsync_start,hsync_end,htotal,hskew;unsigned short vdisplay,vsync_start,vsync_end,vtotal,vscan;unsigned int vrefresh,flags,type;char name[32];};
struct crtc{unsigned long long set_connectors_ptr;unsigned int count_connectors,crtc_id,fb_id,x,y,gamma_size,mode_valid;struct modeinfo mode;};
struct dumb{unsigned int height,width,bpp,flags,handle,pitch;unsigned int size;};
static unsigned int mkfb(long fd,unsigned int W,unsigned int H,unsigned int color,unsigned int *outfb)
{
	struct dumb d;d.height=H;d.width=W;d.bpp=32;d.flags=0;d.handle=0;d.pitch=0;d.size=0;
	long cr=sys6(SYS_ioctl,fd,0xC02064B2UL,(long)&d,0,0,0);
	if(cr){puts_("CREATE_DUMB fail ");putdec(cr);puts_("\n");return 0;}
	struct {unsigned int handle,pad; unsigned long long offset;} mp;mp.handle=d.handle;mp.pad=0;mp.offset=0;
	long mr=sys6(SYS_ioctl,fd,0xC01064B3UL,(long)&mp,0,0,0);
	if(mr){puts_("MAP_DUMB fail ");putdec(mr);puts_("\n");return 0;}
	unsigned long buf=(unsigned long)sys6(SYS_mmap2,0,d.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(long)((mp.offset>>12)));
	if(buf>=0xFFFFF000UL){puts_("mmap fail\n");return 0;}
	unsigned int *px=(unsigned int*)buf;unsigned int st=d.pitch>>2;
	for(unsigned int y=0;y<H;y++)for(unsigned int x=0;x<W;x++)px[y*st+x]=color;
	struct {unsigned int fb_id,width,height,pitch,bpp,depth,handle;} f;
	f.fb_id=0;f.width=W;f.height=H;f.pitch=d.pitch;f.bpp=32;f.depth=24;f.handle=d.handle;
	long ar=sys6(SYS_ioctl,fd,0xC01C64AEUL,(long)&f,0,0,0);
	if(ar){puts_("ADDFB fail ");putdec(ar);puts_("\n");return 0;}
	*outfb=f.fb_id;return d.handle;
}
void _start(void)
{
	long fd=sys3(SYS_open,(long)"/dev/dri/card0",O_RDWR,0);
	puts_("open ");putdec(fd);puts_("\n");
	struct crtc c;unsigned int *q=(unsigned int*)&c;for(unsigned k=0;k<sizeof(c)/4;k++)q[k]=0;c.crtc_id=20;
	long g=sys6(SYS_ioctl,fd,0xC06864A1UL,(long)&c,0,0,0);
	puts_("GETCRTC ");putdec(g);puts_(" fb=");putdec((long)c.fb_id);puts_("\n");
	if(g)sys1(SYS_exit_group,1);
	unsigned int fa,fb;
	mkfb(fd,c.mode.hdisplay,c.mode.vdisplay,0xFFFF0000,&fa);
	mkfb(fd,c.mode.hdisplay,c.mode.vdisplay,0xFF00FF00,&fb);
	puts_("fbA=");putdec(fa);puts_(" fbB=");putdec(fb);puts_("\n");
	struct crtc s;unsigned int *p=(unsigned int*)&s;for(unsigned k=0;k<sizeof(s)/4;k++)p[k]=0;
	unsigned int conn=36;s.set_connectors_ptr=(unsigned long long)(unsigned long)&conn;s.count_connectors=1;s.crtc_id=20;s.fb_id=fa;s.mode=c.mode;s.mode_valid=1;
	long sr=sys6(SYS_ioctl,fd,0xC06864A2UL,(long)&s,0,0,0);
	puts_("SETCRTC(A) ");putdec(sr);puts_("\n");
	struct {unsigned int crtc_id,fb_id,flags,reserved;unsigned long long user_data;} fl;
	fl.crtc_id=20;fl.fb_id=fb;fl.flags=1;fl.reserved=0;fl.user_data=0xdeadbeef;
	long fr=sys6(SYS_ioctl,fd,0xC01864B0UL,(long)&fl,0,0,0);
	puts_("PAGE_FLIP(B,EVENT) ");putdec(fr);if(fr<0){puts_(" errno=");putdec(-fr);}puts_("\n");
	struct {int fd;short events,revents;} pfd;pfd.fd=(int)fd;pfd.events=1;pfd.revents=0;
	long pr=sys6(SYS_poll,(long)&pfd,1,500,0,0,0);
	puts_("poll ");putdec(pr);puts_(" revents=");putdec(pfd.revents);puts_("\n");
	if(pr>0){unsigned int ev[16];long rd=sys3(SYS_read,fd,(long)ev,64);puts_("read ");putdec(rd);if(rd>=8){puts_(" type=");putdec(ev[0]);puts_(" len=");putdec(ev[1]);puts_(" seq=");putdec(ev[5]);puts_(" crtc=");putdec(ev[7]);}puts_("\n");}
	struct crtc c2;unsigned int *r=(unsigned int*)&c2;for(unsigned k=0;k<sizeof(c2)/4;k++)r[k]=0;c2.crtc_id=20;
	long g2=sys6(SYS_ioctl,fd,0xC06864A1UL,(long)&c2,0,0,0);
	puts_("GETCRTC after ");putdec(g2);puts_(" fb=");putdec((long)c2.fb_id);puts_(c2.fb_id==fb?" (B)":" (not B)");puts_("\n");
	struct crtc s0;unsigned int *u0=(unsigned int*)&s0;for(unsigned k=0;k<sizeof(s0)/4;k++)u0[k]=0;s0.crtc_id=20;
	long r0=sys6(SYS_ioctl,fd,0xC06864A2UL,(long)&s0,0,0,0);
	puts_("SETCRTC(off) ");putdec(r0);puts_("\n");
	unsigned int cand[3]={c.fb_id,68,69};int ok=0;
	for(int i=0;i<3;i++){
		struct crtc s2;unsigned int *u=(unsigned int*)&s2;for(unsigned k=0;k<sizeof(s2)/4;k++)u[k]=0;
		unsigned int conn2=36;s2.set_connectors_ptr=(unsigned long long)(unsigned long)&conn2;s2.count_connectors=1;s2.crtc_id=20;s2.fb_id=cand[i];s2.mode=c.mode;s2.mode_valid=1;
		long rr=sys6(SYS_ioctl,fd,0xC06864A2UL,(long)&s2,0,0,0);
		puts_("SETCRTC(fb=");putdec(cand[i]);puts_(") ");putdec(rr);puts_("\n");
		if(rr==0){ok=1;break;}
	}
	puts_(ok?"restored vendor\n":"restore FAILED\n");
	sys1(SYS_close,fd);sys1(SYS_exit_group,0);
}
