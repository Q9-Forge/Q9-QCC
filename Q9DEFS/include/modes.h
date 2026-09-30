/* modes.h -- file access mode bits and the basic path I/O calls.
 *
 * Derived from the specification, no template used.
 * Status: the _os_* prototypes are belegt; the FAM_* / S_I* values are
 * derived from the documented bit scheme (read 1, write 2, execute 4,
 * directory 128) and marked below.
 */
#ifndef Q9_MODES_H
#define Q9_MODES_H

#include <types.h>

#define FAM_READ   0x01   /* derived: mode bit 0 */
#define FAM_WRITE  0x02   /* derived: mode bit 1 */
#define FAM_EXEC   0x04   /* derived: mode bit 2 */
#define FAM_DIR    0x80   /* derived: mode bit 7 (open a directory) */

#define S_IREAD    FAM_READ    /* derived: synonym of FAM_READ */
#define S_IWRITE   FAM_WRITE   /* derived: synonym of FAM_WRITE */
#define S_IEXEC    FAM_EXEC    /* derived: synonym of FAM_EXEC */

extern error_code _os_close(path_id path);
extern error_code _os_open(const char *name, u_int32 mode, path_id *path);
extern error_code _os_read(path_id path, void *buffer, u_int32 *count);
extern error_code _os_write(path_id path, void *buffer, u_int32 *count);
extern error_code _os_create(const char *name, u_int32 mode, path_id *path, u_int32 perm, ...);
extern error_code _os_seek(path_id path, u_int32 position);
extern error_code _os_delete(const char *name, u_int32 mode);

#endif /* Q9_MODES_H */

/* not defined (no documented value):
 *   S_IFDIR, S_IOEXEC, S_IOREAD, S_IOWRITE, S_ISHARE,
 *   FAP_READ, FAP_WRITE
 */
