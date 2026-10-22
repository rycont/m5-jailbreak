/* kmsown.c - steal DRM master, scan out our own dumb buffer, watch for vendor flips,
 * then restore the vendor fb/master.  Physical-memory primitive: /dev/ntkgpu.
 */
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_ioctl 54
#define SYS_mmap2 192
#define SYS_nanosleep 162
#define SYS_exit_group 248
#define O_RDWR 2
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1

static long sys1(long n, long a){register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a; __asm__ volatile("svc 0":"+r"(r0):"r"(r7):"memory"); return r0;}
static long sys2(long n,long a,long b){register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a; register long r1 __asm__("r1")=b; __asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r7):"memory"); return r0;}
static long sys3(long n,long a,long b,long c){register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a; register long r1 __asm__("r1")=b; register long r2 __asm__("r2")=c; __asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory"); return r0;}
static long sys6(long n,long a,long b,long c,long d,long e,long f){register long r7 __asm__("r7")=n; register long r0 __asm__("r0")=a; register long r1 __asm__("r1")=b; register long r2 __asm__("r2")=c; register long r3 __asm__("r3")=d; register long r4 __asm__("r4")=e; register long r5 __asm__("r5")=f; __asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r3),"r"(r4),"r"(r5),"r"(r7):"memory"); return r0;}
void *memcpy(void *d, const void *src, unsigned long n){unsigned char *a=d; const unsigned char *b=src; while(n--)*a++=*b++; return d;}
static int slen(const char*s){int n=0;while(s[n])n++;return n;}
static void puts_(const char*s){sys3(SYS_write,1,(long)s,slen(s));}
static const char HEX[]="0123456789abcdef";
static void puthex(unsigned long v){char b[9];int i;for(i=7;i>=0;i--){b[i]=HEX[v&15];v>>=4;}b[8]=' ';sys3(SYS_write,1,(long)b,9);}
static void putdec(long v){char b[12];int i=0;int neg=v<0;unsigned long u=neg?(unsigned long)(-v):(unsigned long)v;if(neg)puts_("-");if(!u){puts_("0");return;}while(u){b[i++]=(char)('0'+(u%10));u/=10;}while(i--){char c=b[i];sys3(SYS_write,1,(long)&c,1);}}

struct modeinfo { unsigned int clock; unsigned short hdisplay,hsync_start,hsync_end,htotal,hskew; unsigned short vdisplay,vsync_start,vsync_end,vtotal,vscan; unsigned int vrefresh,flags,type; char name[32]; };
struct crtc { unsigned long long set_connectors_ptr; unsigned int count_connectors,crtc_id,fb_id,x,y,gamma_size,mode_valid; struct modeinfo mode; };

static long gfd;
static unsigned int rd32(unsigned long phys){unsigned long page=phys&~0xFFFUL;unsigned long p=(unsigned long)sys6(SYS_mmap2,0,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,gfd,(long)(page>>12));if(p>=0xFFFFF000UL)return 0;return *(volatile unsigned int*)(p+(phys-page));}
static void wr32(unsigned long phys,unsigned int v){unsigned long page=phys&~0xFFFUL;unsigned long p=(unsigned long)sys6(SYS_mmap2,0,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,gfd,(long)(page>>12));if(p>=0xFFFFF000UL)return;*(volatile unsigned int*)(p+(phys-page))=v;}

#define MINOR 0x27B3E280UL
#define MFIELD (MINOR+0x34)
#define PATCHBASE 0x022BA000UL
static void flush(void){unsigned long m=(unsigned long)sys6(SYS_mmap2,0,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,gfd,(long)(PATCHBASE>>12));if(m<0xFFFFF000UL)sys3(0x0F0002UL,(long)(m+0x700),(long)(m+0x900),0);}

void _start(void)
{
	gfd=sys3(SYS_open,(long)"/dev/ntkgpu",O_RDWR,0);
	if(gfd<0){puts_("ntkgpu open fail\n");sys1(SYS_exit_group,1);}
	puts_("master field before open: ");puthex(rd32(MFIELD));puts_("\n");
	long fd=sys3(SYS_open,(long)"/dev/dri/card0",O_RDWR,0);
	if(fd<0){puts_("card0 open fail\n");sys1(SYS_exit_group,4);}
	puts_("card0 opened (auto-master if minor->master was NULL)\n");
	struct crtc c; unsigned int *q=(unsigned int*)&c; for(unsigned k=0;k<sizeof(c)/4;k++)q[k]=0;
	c.crtc_id=20;
	long g=sys6(SYS_ioctl,fd,0xC06864A1UL,(long)&c,0,0,0);
	puts_("GETCRTC ");putdec(g);puts_(" fb=");putdec((long)c.fb_id);puts_(" ");putdec(c.mode.hdisplay);puts_("x");putdec(c.mode.vdisplay);puts_(" valid=");putdec(c.mode_valid);puts_("\n");

	if(g==0){
		struct {unsigned int height,width,bpp,flags,handle,pitch; unsigned long long size;} d;
		d.height=c.mode.vdisplay; d.width=c.mode.hdisplay; d.bpp=32; d.flags=0; d.handle=0; d.pitch=0; d.size=0;
		long cr=sys6(SYS_ioctl,fd,0xC02064B2UL,(long)&d,0,0,0);
		puts_("CREATE_DUMB ");putdec(cr);puts_(" handle=");putdec((long)d.handle);puts_(" pitch=");putdec((long)d.pitch);puts_(" size=");putdec((long)d.size);puts_("\n");
		if(cr==0){
			struct {unsigned int handle,pad; unsigned long long offset;} mp; mp.handle=d.handle;mp.pad=0;mp.offset=0;
			long mr=sys6(SYS_ioctl,fd,0xC01064B3UL,(long)&mp,0,0,0);
			puts_("MAP_DUMB ");putdec(mr);puts_(" off=");putdec((long)mp.offset);puts_("\n");
			if(mr==0){
				unsigned long buf=(unsigned long)sys6(SYS_mmap2,0,d.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(long)(mp.offset>>12));
				puts_("buffer va=");puthex(buf);puts_("\n");
				if(buf<0xFFFFF000UL){
					unsigned int bars[8]={0xFFFFFFFF,0xFFFFFF00,0xFF00FFFF,0xFF00FF00,0xFFFF00FF,0xFFFF0000,0xFF0000FF,0xFF000000};
					unsigned int *px=(unsigned int*)buf; unsigned int W=d.width,H=d.height; unsigned int st=d.pitch>>2; unsigned int bw=W>>3;
					for(unsigned int b=0;b<8;b++)for(unsigned int y=0;y<H;y++)for(unsigned int x=b*bw;x<(b+1)*bw;x++)px[y*st+x]=bars[b];
					for(unsigned int y=0;y<H/8;y++)for(unsigned int x=0;x<W;x++)px[y*st+x]=0xFF101010;
					struct {unsigned int fb_id,width,height,pitch,bpp,depth,handle;} f;
					f.fb_id=0;f.width=W;f.height=H;f.pitch=d.pitch;f.bpp=32;f.depth=24;f.handle=d.handle;
					long ar=sys6(SYS_ioctl,fd,0xC01C64AEUL,(long)&f,0,0,0);
					puts_("ADDFB ");putdec(ar);puts_(" fb=");putdec((long)f.fb_id);puts_("\n");
					if(ar==0){
						struct crtc s; unsigned int *qq=(unsigned int*)&s; for(unsigned k=0;k<sizeof(s)/4;k++)qq[k]=0;
						unsigned int conn=36;
						s.set_connectors_ptr=(unsigned long long)(unsigned long)&conn;
						s.count_connectors=1;s.crtc_id=20;s.fb_id=f.fb_id;s.x=0;s.y=0;s.mode=c.mode;s.mode_valid=1;
						long sr=sys6(SYS_ioctl,fd,0xC06864A2UL,(long)&s,0,0,0);
						puts_("SETCRTC(ours) ");putdec(sr);if(sr<0){puts_(" errno=");putdec(-sr);}puts_("\n");
						if(sr==0){
							{
								struct {long s,n;} ts; ts.s=0; ts.n=100000000; 
								int ours=0,other=0;
								for(int i=0;i<40;i++){
									struct crtc cc; unsigned int *rr=(unsigned int*)&cc; for(unsigned k=0;k<sizeof(cc)/4;k++)rr[k]=0;cc.crtc_id=20;
									long z=sys6(SYS_ioctl,fd,0xC06864A1UL,(long)&cc,0,0,0);
									if(cc.fb_id==f.fb_id)ours++;else other++;
									if(i<10){puts_("t");putdec(i);puts_(" ");putdec(z);puts_(" fb=");putdec((long)cc.fb_id);puts_(cc.fb_id==f.fb_id?" OURS\n":" other\n");}
									sys2(SYS_nanosleep,(long)&ts,0);
								}
								puts_("samples ours=");putdec(ours);puts_(" other=");putdec(other);puts_("\n");
							}
						}
						puts_("restoring vendor fb\n");
						struct crtc s2; unsigned int *uu=(unsigned int*)&s2; for(unsigned k=0;k<sizeof(s2)/4;k++)uu[k]=0;
						unsigned int conn2=36;
						s2.set_connectors_ptr=(unsigned long long)(unsigned long)&conn2;
						s2.count_connectors=1;s2.crtc_id=20;s2.fb_id=c.fb_id;s2.x=0;s2.y=0;s2.mode=c.mode;s2.mode_valid=1;
						long sr2=sys6(SYS_ioctl,fd,0xC06864A2UL,(long)&s2,0,0,0);
						puts_("SETCRTC(vendor) ");putdec(sr2);puts_("\n");
					}
				}
			}
		}
	}
	sys1(SYS_close,fd);
	puts_("master field after close: ");puthex(rd32(MFIELD));puts_("\n");
	sys1(SYS_exit_group,0);
}
