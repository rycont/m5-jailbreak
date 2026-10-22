/* Freestanding ARM test: no libc, raw syscalls only. Proves unsigned native ELF exec. */
static long sys3(long n, long a, long b, long c)
{
	register long r7 __asm__("r7") = n;
	register long r0 __asm__("r0") = a;
	register long r1 __asm__("r1") = b;
	register long r2 __asm__("r2") = c;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
	return r0;
}

static long sys1(long n, long a)
{
	register long r7 __asm__("r7") = n;
	register long r0 __asm__("r0") = a;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory");
	return r0;
}

#define SYS_write 4
#define SYS_openat 322
#define SYS_exit_group 248
#define SYS_getuid 24
#define SYS_geteuid 25
#define SYS_getgid 47

static char buf[128];

static int put_hex(char *p, unsigned long v)
{
	char *d = p;
	int n = 0;
	char tmp[16];
	do { tmp[n++] = "0123456789abcdef"[v & 0xf]; v >>= 4; } while (v);
	while (n) *p++ = tmp[--n];
	return (int)(p - d);
}

void _start(void)
{
	long uid = sys1(SYS_getuid, 0);
	long euid = sys1(SYS_geteuid, 0);
	long gid = sys1(SYS_getgid, 0);
	long pid = sys1(20, 0);
	char *p = buf;
	*p++ = 'N'; *p++ = 'A'; *p++ = 'T'; *p++ = 'I'; *p++ = 'V'; *p++ = 'E';
	*p++ = ' '; *p++ = 'E'; *p++ = 'L'; *p++ = 'F'; *p++ = ' '; *p++ = 'R'; *p++ = 'A'; *p++ = 'N';
	*p++ = ' '; *p++ = 'u'; *p++ = 'i'; *p++ = 'd'; *p++ = '=';
	p += put_hex(p, (unsigned long)uid);
	*p++ = ' '; *p++ = 'e'; *p++ = 'u'; *p++ = 'i'; *p++ = 'd'; *p++ = '=';
	p += put_hex(p, (unsigned long)euid);
	*p++ = ' '; *p++ = 'g'; *p++ = 'i'; *p++ = 'd'; *p++ = '=';
	p += put_hex(p, (unsigned long)gid);
	*p++ = ' '; *p++ = 'p'; *p++ = 'i'; *p++ = 'd'; *p++ = '=';
	p += put_hex(p, (unsigned long)pid);
	*p++ = '\n';

	long fd = sys3(SYS_openat, -100, (long)"/tmp/native_ran.txt", 1 | 64 | 512);
	if (fd >= 0) sys3(15, (long)"/tmp/native_ran.txt", 0644, 0);
	if (fd >= 0) {
		sys3(SYS_write, fd, (long)buf, (long)(p - buf));
		sys1(6, fd);
	}
	sys1(SYS_exit_group, 0);
	for (;;) { }
}
