/* modes.h -- file access mode bits and the basic path I/O calls.
 *
 * Derived from the specification, no template used.
 * Status: the _os_* prototypes are belegt; the FAM_* / S_I* values are
 * checked against each target's SDK. Directory and public-permission
 * bits differ between OS-9000/x86 and the legacy 68k interface.
 */
#ifndef Q9_MODES_H
#define Q9_MODES_H

#include <types.h>

#define FAP_READ   0x01
#define FAP_WRITE  0x02

#define FAM_READ   0x01   /* derived: mode bit 0 */
#define FAM_WRITE  0x02   /* derived: mode bit 1 */
#define FAM_EXEC   0x04   /* derived: mode bit 2 */
#ifdef __Q9_X86__
/* OS-9000 SDK modes.h: directory/share and public permissions differ
 * from the legacy 68k permission byte. */
#define FAM_DIR       0x8000
#define FAM_NOCREATE  0x0400
#define FAM_APPEND    0x1000
#define FAM_SIZE      0x2000
#define FAM_NONSHARE  0x4000
#define FAM_BLKMODE   0x0080
#else
#define FAM_DIR    0x80
#endif

#define S_IREAD    FAM_READ    /* derived: synonym of FAM_READ */
#define S_IWRITE   FAM_WRITE   /* derived: synonym of FAM_WRITE */
/* Permission bits are selected per OS ABI, not inferred from CPU byte order. */
#ifdef __Q9_X86__
#define S_IGREAD   0x0010
#define S_IGWRITE  0x0020
#define S_IGEXEC   0x0040
#define S_IOREAD   0x0100
#define S_IOWRITE  0x0200
#define S_IOEXEC   0x0400
#define S_ISHARE   0x4000
#define S_IFDIR    0x8000
#else
#define S_IOREAD   0x08
#define S_IOWRITE  0x10
#define S_IOEXEC   0x20
#define S_ISHARE   0x40
#define S_IFDIR    0x80
#endif

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
 */
