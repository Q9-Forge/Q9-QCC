/* time.h -- Q9-Werkzeugkette, bewusst minimal (s. stddef.h/string.h).
 *
 * Local interface; it does not copy Microware's copyrighted DEFS/time.h.
 * It provides types, not functions: time_t/struct tm are needed to parse
 * Q9-Tools/System/touch,date,dir, but
 * time()/localtime() are intentionally not declared yet: qclib does not have
 * the required OS-9 clock syscall (a future project). These headers otherwise
 * remain aligned with what qclib actually provides (see string.h); a
 * declaration without an implementation would cause a later linker error
 * instead of an early, clear
 * fruehen, klaren "unknown function".
 *
 * struct tm contains only the six fields read by these tools; add the others
 * when required.
 */
#ifndef Q9_TIME_H
#define Q9_TIME_H

#include <stddef.h>

typedef long time_t;

struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
};

extern time_t time(time_t*);
extern long clock(void);
extern struct tm* localtime(const time_t*);
extern struct tm* gmtime(const time_t*);
extern time_t mktime(struct tm*);
extern char* asctime(const struct tm*);
extern char* ctime(const time_t*);
extern size_t strftime(char*, size_t, const char*, const struct tm*);

#endif
