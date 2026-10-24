/* vsyncprobe - measure OSD vsync interval via FBIO_WAITFORVSYNC (arg must be 0) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>

#define FBIO_WAITFORVSYNC_LOCAL 0x40044620u

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
	const char *node = argc > 1 ? argv[1] : "/dev/fb0";
	int n = argc > 2 ? atoi(argv[2]) : 120;
	int fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		printf("open %s: %s\n", node, strerror(errno));
		return 1;
	}
	double *d = calloc(n, sizeof(double));
	int ok = 0;
	double t0 = now_ms(), prev = t0;
	for (int i = 0; i < n; i++) {
		double a = now_ms();
		int r = ioctl(fd, FBIO_WAITFORVSYNC_LOCAL, 0);
		double b = now_ms();
		if (r < 0) {
			printf("WAITFORVSYNC failed at %d: %s\n", i, strerror(errno));
			break;
		}
		if (i > 2)
			d[ok++] = b - a;
		prev = b;
	}
	double t1 = now_ms();
	printf("node=%s waits=%d elapsed=%.3f ms avg=%.3f ms -> %.3f Hz\n",
	       node, ok + 3, t1 - t0, (t1 - t0) / (ok + 3), 1000.0 / ((t1 - t0) / (ok + 3)));
	if (ok > 0) {
		qsort(d, ok, sizeof(double), cmp_double);
		double sum = 0;
		for (int i = 0; i < ok; i++)
			sum += d[i];
		printf("interval(ms): min=%.3f p10=%.3f median=%.3f p90=%.3f max=%.3f mean=%.3f\n",
		       d[0], d[ok / 10], d[ok / 2], d[(ok * 9) / 10], d[ok - 1], sum / ok);
	}
	close(fd);
	return 0;
}
