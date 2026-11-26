/* Q9 calendar interface. time_t is a signed 32-bit count of seconds since
 * 1970 on the target. The x86 runtime currently uses UTC (no TZ/DST rules). */
#ifndef Q9_TIME_H
#define Q9_TIME_H

#include <stddef.h>
#include <types.h>

typedef long time_t;

struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

extern error_code _os_getime(time_t *, u_int32 *);
extern error_code _os_setime(u_int32 time);
extern time_t time(time_t*);
extern long clock(void);
extern double difftime(time_t, time_t);
extern struct tm* localtime(const time_t*);
extern struct tm* gmtime(const time_t*);
extern time_t mktime(struct tm*);
extern char* asctime(const struct tm*);
extern char* ctime(const time_t*);
extern size_t strftime(char*, size_t, const char*, const struct tm*);

#endif
