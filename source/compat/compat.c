#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <poll.h>

#include "vita_compat.h"

int g_utc_offset_min;
static volatile int utc_known;

int compat_utc_known(void)
{
	return utc_known;
}

static void fill(SceDateTime *t, time_t secs, unsigned int usec)
{
	struct tm tm;
	gmtime_r(&secs, &tm);
	t->year = tm.tm_year + 1900;
	t->month = tm.tm_mon + 1;
	t->day = tm.tm_mday;
	t->hour = tm.tm_hour;
	t->minute = tm.tm_min;
	t->second = tm.tm_sec;
	t->microsecond = usec;
}

/* osGetTime(): milliseconds since 1900-01-01 on the console clock (local time) */
static uint64_t local_ms_1970(void)
{
	return osGetTime() - 2208988800ULL * 1000;
}

int sceRtcGetCurrentClockLocalTime(SceDateTime *t)
{
	uint64_t ms = local_ms_1970();
	fill(t, (time_t)(ms / 1000), (ms % 1000) * 1000);
	return 0;
}

int sceRtcGetCurrentClock(SceDateTime *t, int tz)
{
	(void)tz;
	int64_t ms = (int64_t)local_ms_1970() - (int64_t)g_utc_offset_min * 60000;
	fill(t, (time_t)(ms / 1000), (ms % 1000) * 1000);
	return 0;
}

SceUInt64 sceKernelGetProcessTimeWide(void)
{
	return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000000);
}

int sceKernelDelayThread(unsigned int usec)
{
	svcSleepThread((int64_t)usec * 1000);
	return 0;
}

int sceKernelGetRandomNumber(void *buf, size_t len)
{
	if (R_SUCCEEDED(PS_GenerateRandomBytes(buf, len)))
		return 0;
	/* ps:ps unavailable: mix the tick counter (only used for nicks and nonces) */
	unsigned char *b = buf;
	uint64_t x = svcGetSystemTick() ^ 0x9E3779B97F4A7C15ULL;
	for (size_t i = 0; i < len; i++) {
		x ^= x << 13; x ^= x >> 7; x ^= x << 17;
		b[i] = (unsigned char)x;
	}
	return 0;
}

int sceIoMkdir(const char *path, int mode)
{
	return mkdir(path, mode);
}

int sceIoRemove(const char *path)
{
	return remove(path);
}

#include <dirent.h>
static DIR *dirs[8];

SceUID sceIoDopen(const char *path)
{
	DIR *d = opendir(path);
	if (!d)
		return -1;
	for (int i = 0; i < 8; i++)
		if (!dirs[i]) {
			dirs[i] = d;
			return i;
		}
	closedir(d);
	return -1;
}

int sceIoDread(SceUID fd, SceIoDirent *e)
{
	if (fd < 0 || fd >= 8 || !dirs[fd])
		return -1;
	struct dirent *de;
	while ((de = readdir(dirs[fd]))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		snprintf(e->d_name, sizeof(e->d_name), "%s", de->d_name);
		return 1;
	}
	return 0;
}

int sceIoDclose(SceUID fd)
{
	if (fd < 0 || fd >= 8 || !dirs[fd])
		return -1;
	closedir(dirs[fd]);
	dirs[fd] = NULL;
	return 0;
}

/* ---------------- time zone ---------------- */

/* UTC (seconds since 1970) -> offset of the console clock, to a quarter hour */
static void learn(int64_t utc)
{
	if (utc_known)
		return;
	int64_t loc = (int64_t)(local_ms_1970() / 1000);
	long d = (long)((loc - utc) / 60);
	/* the console clock may drift a few minutes */
	g_utc_offset_min = (int)((d >= 0 ? d + 7 : d - 7) / 15 * 15);
	utc_known = 1;
}

static void *ntp_thread(void *arg)
{
	(void)arg;
	static const char *hosts[] = { "time.google.com", "pool.ntp.org", "time.cloudflare.com" };
	for (unsigned h = 0; h < sizeof(hosts) / sizeof(hosts[0]) && !utc_known; h++) {
		struct addrinfo hints, *res = NULL;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		if (getaddrinfo(hosts[h], "123", &hints, &res) != 0 || !res)
			continue;
		int fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (fd >= 0) {
			unsigned char pkt[48];
			memset(pkt, 0, sizeof(pkt));
			pkt[0] = 0x1B;                  /* client, version 3 */
			struct pollfd pfd = { fd, POLLIN, 0 };
			if (sendto(fd, pkt, sizeof(pkt), 0, res->ai_addr, res->ai_addrlen) == sizeof(pkt) &&
			    poll(&pfd, 1, 2500) == 1 && recv(fd, pkt, sizeof(pkt), 0) == sizeof(pkt)) {
				uint32_t secs = (uint32_t)pkt[40] << 24 | pkt[41] << 16 | pkt[42] << 8 | pkt[43];
				if (secs > 2208988800U)
					learn((int64_t)(secs - 2208988800U));
			}
			close(fd);
		}
		freeaddrinfo(res);
	}
	return NULL;
}

/* seconds since 1970 of a UTC date (newlib has no timegm) */
static int64_t utc_secs(int y, int mon, int d, int hh, int mm, int ss)
{
	y -= mon <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400;
	long doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	int64_t days = (int64_t)era * 146097 + doe - 719468;
	return days * 86400 + hh * 3600 + mm * 60 + ss;
}

/* The Date header of a plain HTTP answer, when NTP (UDP) is blocked or slow */
static void *http_thread(void *arg)
{
	(void)arg;
	static const char *hosts[] = { "www.google.com", "www.cloudflare.com" };
	static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	for (unsigned h = 0; h < 2 && !utc_known; h++) {
		struct addrinfo hints, *res = NULL;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(hosts[h], "80", &hints, &res) != 0 || !res)
			continue;
		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
			char req[128], buf[1024];
			snprintf(req, sizeof(req), "HEAD / HTTP/1.0\r\nHost: %s\r\n\r\n", hosts[h]);
			send(fd, req, strlen(req), 0);
			struct pollfd pfd = { fd, POLLIN, 0 };
			int n = 0;
			while (n < (int)sizeof(buf) - 1 && poll(&pfd, 1, 3000) == 1) {
				int r = recv(fd, buf + n, sizeof(buf) - 1 - n, 0);
				if (r <= 0) break;
				n += r;
			}
			buf[n] = 0;
			char *d = strstr(buf, "\nDate: ");
			if (!d) d = strstr(buf, "\ndate: ");
			int day, year, hh, mm, ss;
			char m[4];
			if (d && sscanf(d + 7, "%*3s, %d %3s %d %d:%d:%d", &day, m, &year, &hh, &mm, &ss) == 6) {
				for (int i = 0; i < 12; i++)
					if (!strcmp(m, mon[i])) {
						learn(utc_secs(year, i + 1, day, hh, mm, ss));
						break;
					}
			}
		}
		if (fd >= 0) close(fd);
		freeaddrinfo(res);
	}
	return NULL;
}

static void spawn(void *(*fn)(void *))
{
	pthread_t th;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 32 * 1024);
	if (pthread_create(&th, &attr, fn, NULL) == 0)
		pthread_detach(th);
	pthread_attr_destroy(&attr);
}

void compat_learn_utc(void)
{
	spawn(ntp_thread);
	spawn(http_thread);
}
