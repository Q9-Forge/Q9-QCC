/* process.h -- process identification and process-table calls.
 *
 * Derived from the specification, no template used.
 * Status: the _os_* prototypes are belegt; process_id is derived.
 */
#ifndef Q9_PROCESS_H
#define Q9_PROCESS_H

#include <types.h>

/* derived: a process ID is one word (16 bit); underlying type not documented */
typedef u_int16 process_id;

extern error_code _os_get_prtbl(void *buffer, u_int32 *count);
extern error_code _os_gprdsc(process_id procid, void *buffer, u_int32 *count);
extern error_code _os_sysdbg(void *param1, void *param2);

#endif /* Q9_PROCESS_H */

/* not defined (no documented value):
 *   Pr_desc, pr_desc (process descriptor typedefs) and their members
 *   _age, _fcalls, _group, _icalls, _id, _pagcnt, _pid, _pmodul, _prior,
 *   _rbytes, _state, _user, _wbytes
 */
