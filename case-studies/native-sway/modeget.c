#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_ioctl 54
#define SYS_exit_group 248
#define O_RDWR 2
static long sys1(long n,long a){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;__asm__ volatile("svc 0":"+r"(r0):"r"(r7):"memory");return r0;}
static long sys3(long n,long a,long b,long c){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;register long r1 __asm__("r1")=b;register long r2 __asm__("r2")=c;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory");return r0;}
static long sys6(long n,long a,long b,long c,long d,long e,long f){register long r7 __asm__("r7")=n;register long r0 __asm__("r0")=a;register long r1 __asm__("r1")=b;register long r2 __asm__("r2")=c;register long r3 __asm__("r3")=d;register long r4 __asm__("r4")=e;register long r5 __asm__("r5")=f;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r3),"r"(r4),"r"(r5),"r"(r7):"memory");return r0;}
static int slen(const char*s){int n=0;while(s[n])n++;return n;}
static void puts_(const char*s){sys3(SYS_write,1,(long)s,slen(s));}
static void putdec(long v){char b[12];int i=0;int neg=v<0;unsigned long u=neg?(unsigned long)(-v):(unsigned long)v;if(neg)puts_("-");if(!u){puts_("0");return;}while(u){b[i++]=(char)('0'+(u%10));u/=10;}while(i--){char c=b[i];sys3(SYS_write,1,(long)&c,1);}}
struct modeinfo{unsigned int clock;unsigned short hdisplay,hsync_start,hsync_end,htotal,hskew;unsigned short vdisplay,vsync_start,vsync_end,vtotal,vscan;unsigned int vrefresh,flags,type;char name[32];};
struct conn{unsigned long long enc_ptr,modes_ptr,props_ptr,vals_ptr;unsigned int count_modes,count_props,count_encoders;unsigned int encoder_id,connector_id,connector_type,connector_type_id,connection;unsigned int mm_width,mm_height,subpixel,pad;};
void _start(void)
{
	long fd=sys3(SYS_open,(long)"/dev/dri/card0",O_RDWR,0);
	puts_("open ");putdec(fd);puts_("\n");
	static struct modeinfo modes[32];
	struct conn c;unsigned int *p=(unsigned int*)&c;for(unsigned k=0;k<sizeof(c)/4;k++)p[k]=0;
	c.modes_ptr=(unsigned long long)(unsigned long)modes;c.count_modes=32;c.connector_id=36;
	long r=sys6(SYS_ioctl,fd,0xC05064A7UL,(long)&c,0,0,0);
	puts_("GETCONNECTOR ");putdec(r);puts_(" conn=");putdec(c.connection);puts_(" modes=");putdec(c.count_modes);puts_(" encoder=");putdec(c.encoder_id);puts_(" type=");putdec(c.connector_type);puts_("\n");
	for(unsigned i=0;i<c.count_modes&&i<10;i++){
		puts_("  ");putdec(modes[i].hdisplay);puts_("x");putdec(modes[i].vdisplay);
		puts_(" @");putdec(modes[i].vrefresh);puts_(" clk=");putdec(modes[i].clock);
		puts_(" h=");putdec(modes[i].hsync_start);puts_("/");putdec(modes[i].hsync_end);puts_("/");putdec(modes[i].htotal);
		puts_(" v=");putdec(modes[i].vsync_start);puts_("/");putdec(modes[i].vsync_end);puts_("/");putdec(modes[i].vtotal);
		puts_(" flags=");putdec(modes[i].flags);puts_(" type=");putdec(modes[i].type);
		puts_(" name=");puts_(modes[i].name);puts_("\n");
	}
	sys1(SYS_close,fd);sys1(SYS_exit_group,0);
}
