#ifndef VITAIRC3DS_VITA_COMPAT_H
#define VITAIRC3DS_VITA_COMPAT_H

/* The few PS Vita system calls the shared VitaIRC core makes, implemented
 * with the 3DS's own services (see compat.c). */

#include <stdint.h>
#include <stddef.h>

typedef struct {
	unsigned short year, month, day, hour, minute, second;
	unsigned int   microsecond;
} SceDateTime;

typedef uint64_t SceUInt64;

/* Local time (the 3DS clock keeps local time, without a time zone). */
int       sceRtcGetCurrentClockLocalTime(SceDateTime *t);
/* UTC: local time minus the offset learnt from the network (or set in Settings). */
int       sceRtcGetCurrentClock(SceDateTime *t, int tz);
/* Microseconds since boot (monotonic). */
SceUInt64 sceKernelGetProcessTimeWide(void);
int       sceKernelDelayThread(unsigned int usec);
int       sceKernelGetRandomNumber(void *buf, size_t len);
int       sceIoMkdir(const char *path, int mode);
int       sceIoRemove(const char *path);

/* directory listing */
typedef int SceUID;
typedef struct { char d_name[256]; } SceIoDirent;
SceUID    sceIoDopen(const char *path);
int       sceIoDread(SceUID fd, SceIoDirent *e);     /* 1 = entry, 0 = end */
int       sceIoDclose(SceUID fd);

/* Minutes to add to UTC to get local time; compat_utc_known() is 0 until learnt. */
extern int g_utc_offset_min;
int        compat_utc_known(void);
/* Asks an NTP server for the time once (background thread) to learn the offset. */
void       compat_learn_utc(void);

#endif
