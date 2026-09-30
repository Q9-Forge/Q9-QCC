/* dir.h -- directory entries and directory stream calls.
 *
 * Derived from the specification, no template used.
 * Status: opendir, readdir, closedir belegt; struct dirent, struct direct
 * (only d_name) and DIR derived.
 */
#ifndef Q9_DIR_H
#define Q9_DIR_H

#include <types.h>

/* derived: on-disk directory entry, 32 bytes: 28 name bytes (bit 7 set on
   the last character), then a zero byte and a 3-byte file descriptor LSN;
   the last four bytes are read as one big-endian value, so dir_addr holds
   the LSN. */
struct dirent {
	unsigned char dir_name[28];  /* @0 */
	u_int32       dir_addr;      /* @28 zero byte + 3-byte LSN of the file descriptor */
};

/* derived: entry returned by readdir(); only the name is documented, its
   capacity is not, so it is declared with the variable-length idiom */
struct direct {
	char d_name[1];              /* derived: NUL-terminated entry name, capacity not documented */
};

/* derived: opaque directory stream, internals are not documented */
typedef struct q9_dir_stream DIR;

extern DIR *opendir(const char *filename);
extern struct direct *readdir(DIR *dirp);
extern void closedir(DIR *dirp);

#endif /* Q9_DIR_H */

/* not defined (no documented value):
 *   DIRBLKSIZ
 *   struct direct: member d_addr
 */
