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
static long sc(long fd,unsigned int fb,unsigned int nconn,struct modeinfo*m,int valid)
{
	struct crtc s;unsigned int *p=(unsigned int*)&s;for(unsigned k=0;k<sizeof(s)/4;k++)p[k]=0;
	unsigned int conn=36;
	s.set_connectors_ptr=(unsigned long long)(unsigned long)&conn;s.count_connectors=nconn;s.crtc_id=20;s.fb_id=fb;
	if(m){s.mode=*m;}s.mode_valid=valid;
	return sys6(SYS_ioctl,fd,0xC06864A2UL,(long)&s,0,0,0);
}
void _start(void)
{
	long fd=sys3(SYS_open,(long)"/dev/dri/card0",O_RDWR,0);
	puts_("open ");putdec(fd);puts_("\n");
	long r0=sc(fd,0,0,0,0);
	puts_("SETCRTC(off,0conn,nomode) ");putdec(r0);if(r0<0){puts_(" errno=");putdec(-r0);}puts_("\n");
	struct conn c;unsigned int *q=(unsigned int*)&c;for(unsigned k=0;k<sizeof(c)/4;k++)q[k]=0;
	c.modes_ptr=(unsigned long long)(unsigned long)modes;c.count_modes=32;c.connector_id=36;
	long gr=sys6(SYS_ioctl,fd,0xC05064A7UL,(long)&c,0,0,0);
	puts_("GETCONNECTOR ");putdec(gr);puts_(" modes=");putdec(c.count_modes);puts_(" enc=");putdec(c.encoder_id);puts_("\n");
	struct modeinfo m=modes[0];
	long sr=sc(fd,0xFFFFFFFFU,1,&m,1);
	puts_("SETCRTC(-1,conn,mode) ");putdec(sr);if(sr<0){puts_(" errno=");putdec(-sr);}puts_("\n");
	for(unsigned i=0;i<3;i++){unsigned int cand[3]={68,69,70};long r=sc(fd,cand[i],1,&m,1);puts_("SETCRTC(fb=");putdec(cand[i]);puts_(") ");putdec(r);puts_("\n");if(r==0)break;}
	sys1(SYS_close,fd);sys1(SYS_exit_group,0);
}
