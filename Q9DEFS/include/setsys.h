/* setsys.h -- system global offsets for _getsys().
 *
 * Derived from the specification, no template used.
 * Status: nothing in this header has a documented value.
 */
#ifndef Q9_SETSYS_H
#define Q9_SETSYS_H

#include <types.h>
#ifdef __Q9_X86__
/* I$GetDL returns a kernel address; callers must not dereference it. */
extern error_code q9_x86_device_list(u_int32 *address);
#endif

#endif /* Q9_SETSYS_H */

/* not defined (no documented value):
 *   D_DevCnt, D_DevSiz, D_DevTbl
 */
