/* sg_codes.h -- get-status helper calls.
 *
 * Derived from the specification, no template used.
 * Status: all prototypes belegt.
 */
#ifndef Q9_SG_CODES_H
#define Q9_SG_CODES_H

#include <types.h>

extern error_code _os9_gs_free(path_id path, u_int32 *free_space);
extern error_code _os_gs_devnm(path_id path, char *name);
extern error_code _os_gs_pos(path_id path, u_int32 *position);
extern error_code _os_gs_size(path_id path, u_int32 *size);

#endif /* Q9_SG_CODES_H */

/* not defined (no documented value): none */
