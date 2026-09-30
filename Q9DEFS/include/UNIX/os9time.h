/* UNIX/os9time.h -- time value structure.
 *
 * Derived from the specification, no template used.
 * Status: derived (seconds and microseconds since 1970-01-01; member
 * names from the usual convention).
 */
#ifndef Q9_UNIX_OS9TIME_H
#define Q9_UNIX_OS9TIME_H

#include <time.h>

/* derived: seconds and microseconds, 32 bit each */
struct timeval {
	long tv_sec;    /* @0 */
	long tv_usec;   /* @4 */
};

#endif /* Q9_UNIX_OS9TIME_H */

/* not defined (no documented value): none */
