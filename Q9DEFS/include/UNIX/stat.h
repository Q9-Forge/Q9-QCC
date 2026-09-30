/* UNIX/stat.h -- file status structure.
 *
 * Derived from the specification, no template used.
 * Status: member order as documented; offsets and the total size of 36
 * bytes are derived (time_t assumed 32 bit).
 */
#ifndef Q9_UNIX_STAT_H
#define Q9_UNIX_STAT_H

#include <time.h>

/* derived: member order documented, the list may not be complete */
struct stat {
	unsigned short st_mode;      /* @0  */
	unsigned short st_nlink;     /* @2  */
	unsigned short st_uid;       /* @4  */
	unsigned short st_gid;       /* @6  */
	unsigned long  st_size;      /* @8  */
	time_t         st_atime;     /* @12 */
	time_t         st_mtime;     /* @16 */
	time_t         st_ctime;     /* @20 */
	long           st_ino;       /* @24 */
	long           st_dev;       /* @28 */
	long           st_rdev;      /* @32 */
};

#endif /* Q9_UNIX_STAT_H */

/* not defined (no documented value): none */
