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
static long sys1(long n,long a){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;__asm__ volatile("svc 0":"+r"(r0):"r"(r7):"memory");return r0;}
static long sys2(long n,long a,long b){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;register long r1 __asm__("r1")=b;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r7):"memory");return r0;}
static long sys3(long n,long a,long b,long c){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;register long r1 __asm__("r1")=b;register long r2 __asm__("r2")=c;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory");return r0;}
static long sys6(long n,long a,long b,long c,long d,long e,long f){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;register long r1 __asm__("r1")=b;register long r2 __asm__("r2")=c;register long r3 __asm__("r3")=d;register long r4 __asm__("r4")=e;register long r5 __asm__("r5")=f;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r3),"r"(r4),"r"(r5),"r"(r7):"memory");return r0;}
static int slen(const char*s){int n=0;while(s[n])n++;return n;}
static void puts_(const char*s){sys3(SYS_write,1,(long)s,slen(s));}
static void putdec(long v){char b[12];int i=0;int neg=v<0;unsigned long u=neg?(unsigned long)(-v):(unsigned long)v;if(neg)puts_("-");if(!u){puts_("0");return;}while(u){b[i++]=(char)('0'+(u%10));u/=10;}while(i--){char c=b[i];sys3(SYS_write,1,(long)&c,1);}}
void *memcpy(void*d,const void*src,unsigned long n){unsigned char*a=d;const unsigned char*b=src;while(n--)*a++=*b++;return d;}
struct modeinfo{unsigned int clock;unsigned short hdisplay,hsync_start,hsync_end,htotal,hskew;unsigned short vdisplay,vsync_start,vsync_end,vtotal,vscan;unsigned int vrefresh,flags,type;char name[32];};
struct conn{unsigned long long enc_ptr,modes_ptr,props_ptr,vals_ptr;unsigned int count_modes,count_props,count_encoders;unsigned int encoder_id,connector_id,connector_type,connector_type_id,connection;unsigned int mm_width,mm_height,subpixel,pad;};
struct crtc{unsigned long long set_connectors_ptr;unsigned int count_connectors,crtc_id,fb_id,x,y,gamma_size,mode_valid;struct modeinfo mode;};
static struct modeinfo modes[32];
static long open_card(void){return sys3(SYS_open,(long)"/dev/dri/card0",O_RDWR,0);}
static int get_mode(long fd,struct modeinfo *m)
{
	struct conn c;unsigned int *p=(unsigned int*)&c;for(unsigned k=0;k<sizeof(c)/4;k++)p[k]=0;
	c.modes_ptr=(unsigned long long)(unsigned long)modes;c.count_modes=32;c.connector_id=36;
	long r=sys6(SYS_ioctl,fd,0xC05064A7UL,(long)&c,0,0,0);
	puts_("GETCONNECTOR ");putdec(r);puts_(" modes=");putdec(c.count_modes);puts_("\n");
	if(r||!c.count_modes)return -1;
	*m=modes[0];return 0;
}
static long setcrtc(long fd,unsigned int fb,struct modeinfo *m)
{
	struct crtc s;unsigned int *p=(unsigned int*)&s;for(unsigned k=0;k<sizeof(s)/4;k++)p[k]=0;
	unsigned int conn=36;s.set_connectors_ptr=(unsigned long long)(unsigned long)&conn;s.count_connectors=1;s.crtc_id=20;s.fb_id=fb;s.mode=*m;s.mode_valid=1;
	return sys6(SYS_ioctl,fd,0xC06864A2UL,(long)&s,0,0,0);
}
void _start(void)
{
	long fd=open_card();
	puts_("open ");putdec(fd);puts_("\n");
	struct modeinfo m;if(get_mode(fd,&m)){puts_("no mode\n");sys1(SYS_exit_group,1);}
	puts_("mode ");putdec(m.hdisplay);puts_("x");putdec(m.vdisplay);puts_(" clock=");putdec(m.clock);puts_("\n");
	struct {unsigned int height,width,bpp,flags,handle,pitch;unsigned int size;} d;
	d.height=m.vdisplay;d.width=m.hdisplay;d.bpp=32;d.flags=0;d.handle=0;d.pitch=0;d.size=0;
	long cr=sys6(SYS_ioctl,fd,0xC02064B2UL,(long)&d,0,0,0);
	puts_("CREATE_DUMB ");putdec(cr);puts_(" pitch=");putdec(d.pitch);puts_(" size=");putdec(d.size);puts_("\n");
	if(cr)sys1(SYS_exit_group,2);
	struct {unsigned int handle,pad;unsigned long long offset;} mp;mp.handle=d.handle;mp.pad=0;mp.offset=0;
	long mr=sys6(SYS_ioctl,fd,0xC01064B3UL,(long)&mp,0,0,0);puts_("MAP_DUMB ");putdec(mr);puts_("\n");
	unsigned long buf=(unsigned long)sys6(SYS_mmap2,0,d.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(long)(mp.offset>>12));
	puts_("buffer ");putdec((long)buf);puts_("\n");
	if(buf>=0xFFFFF000UL)sys1(SYS_exit_group,3);
	unsigned int *px=(unsigned int*)buf;unsigned int st=d.pitch>>2;unsigned int W=d.width,H=d.height;
	unsigned int bars[8]={0xFFFFFFFF,0xFFFFFF00,0xFF00FFFF,0xFF00FF00,0xFFFF00FF,0xFFFF0000,0xFF0000FF,0xFF000000};
	unsigned int bw=W>>3;
	for(unsigned b=0;b<8;b++)for(unsigned y=0;y<H;y++)for(unsigned x=b*bw;x<(b+1)*bw;x++)px[y*st+x]=bars[b];
	for(unsigned y=0;y<H/8;y++)for(unsigned x=0;x<W;x++)px[y*st+x]=0xFF101010;
	struct {unsigned int fb_id,width,height,pitch,bpp,depth,handle;} f;
	f.fb_id=0;f.width=W;f.height=H;f.pitch=d.pitch;f.bpp=32;f.depth=24;f.handle=d.handle;
	long ar=sys6(SYS_ioctl,fd,0xC01C64AEUL,(long)&f,0,0,0);
	puts_("ADDFB ");putdec(ar);puts_(" fb=");putdec(f.fb_id);puts_("\n");
	if(ar)sys1(SYS_exit_group,4);
	long sr=setcrtc(fd,f.fb_id,&m);
	puts_("SETCRTC(ours) ");putdec(sr);if(sr<0){puts_(" errno=");putdec(-sr);}puts_("\n");
	{struct {long s,n;} ts;ts.s=3;ts.n=0;sys2(SYS_nanosleep,(long)&ts,0);}
	unsigned int cand[3]={68,69,70};int done=0;
	for(int i=0;i<3;i++){
		long r=setcrtc(fd,cand[i],&m);
		puts_("SETCRTC(fb=");putdec(cand[i]);puts_(") ");putdec(r);puts_("\n");
		if(r==0){done=1;break;}
	}
	if(!done){puts_("no vendor fb accepted; leaving our fb on screen\n");}
	puts_("done\n");
	sys1(SYS_close,fd);sys1(SYS_exit_group,0);
}
