/* dir.h -- directory entries and directory stream calls.
 *
 * Derived from the specification, no template used.
 * Status: opendir, readdir, closedir belegt; struct dirent, struct direct
 * (only d_name) and DIR derived.
 */
#ifndef Q9_DIR_H
#define Q9_DIR_H

/* RBF directory transfer block size documented by the SDK dir interface. */
#define DIRBLKSIZ 256

#include <types.h>

/* derived: on-disk directory entry, 32 bytes: 28 name bytes (bit 7 set on
   the last character), then a zero byte and a 3-byte file descriptor LSN;
   the last four bytes are read as one big-endian value, so dir_addr holds
   the LSN. */
struct dirent {
	unsigned char dir_name[28];  /* @0 */
	u_int32       dir_addr;      /* @28 zero byte + 3-byte LSN of the file descriptor */
};

/* Library directory entry; distinct from the 32-byte on-disk dirent. */
#define MAXNAMLEN 32
struct direct {
    long d_addr;
    short d_reclen;
    short d_namlen;
    char d_name[33];
};

/* derived: opaque directory stream, internals are not documented */
struct q9_dir_stream;
typedef struct q9_dir_stream DIR;

extern DIR *opendir(const char *filename);
extern struct direct *readdir(DIR *dirp);
extern void closedir(DIR *dirp);

#endif /* Q9_DIR_H */
