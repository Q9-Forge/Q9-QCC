/* rbf.h -- disk (random block file) structures and file descriptor calls.
 *
 * Derived from the specification, no template used.
 * Status: prototypes belegt; Sector0 and fd_stats are derived from the
 * documented on-disk layouts. Only members with a documented C name are
 * named; the gaps are covered by unnamed filler so that the documented
 * offsets hold.
 */
#ifndef Q9_RBF_H
#define Q9_RBF_H

#include <types.h>

/* derived: identification sector (LSN 0); layout from the disk format.
   The type name denotes a pointer to the sector layout (the consumers
   cast a sector buffer to it and access the members with ->). */
typedef struct {
	unsigned char dd_tot[3];       /* @0  total sectors, big-endian, 3 bytes */
	unsigned char dd_tks;          /* @3  (name from the disk format description) */
	u_int16       dd_map;          /* @4  bytes in the allocation map */
	u_int16       dd_bit;          /* @6  sectors per map bit */
	unsigned char dd_dir[3];       /* @8  (name from the disk format description) */
	unsigned char _filler_11[20];  /* @11 .. @30, members without a documented C name */
	char          dd_name[32];     /* @31 volume name, bit 7 set on last character */
} *Sector0;

/* derived: file descriptor sector; layout from the disk format */
typedef struct {
	unsigned char _filler_0[3];    /* @0  attributes and owner */
	unsigned char fd_date[5];      /* @3  year, month, day, hour, minute */
	unsigned char _filler_8[248];  /* @8  .. @255, remaining descriptor members */
} fd_stats;

extern error_code _os_gs_fd(path_id path, u_int32 size, fd_stats *fdbuf);
extern error_code _os_ss_fd(path_id path, fd_stats *fdinfo);

#endif /* Q9_RBF_H */

/* not defined (no documented value): none */
