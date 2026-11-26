/* process.h -- process identification and process-table calls.
 *
 * Derived from the specification, no template used.
 * Status: the _os_* prototypes are belegt; process_id is derived.
 */
#ifndef Q9_PROCESS_H
#define Q9_PROCESS_H

#include <types.h>

/* derived: a process ID is one word (16 bit); underlying type not documented */
#ifdef __Q9_X86__
typedef u_int32 process_id;
/* F$GPrDsc edition 1 returns this backward-compatible prefix, not the
 * native thread descriptor. SDK old_prdsc and kernel cpf_tab agree on
 * these offsets. Reserved bytes are not interpreted as kernel pointers. */
struct q9_os9000_process {
    u_int32 p_sync;
    u_int32 p_id;
    u_int32 p_pid;
    u_int32 p_owner;
    u_int32 p_reserved16[4];
    void *p_pmodul;
    u_int32 p_pdsize;
    u_int16 p_prior;
    u_int16 p_age;
    u_int32 p_sched;
    u_int32 p_state;
    unsigned char p_reserved52[20];
    u_int32 p_timbeg;
    u_int32 p_uticks;
    u_int32 p_sticks;
    unsigned char p_reserved84[132];
    u_int32 p_scall;
    u_int32 p_fcalls;
    u_int32 p_icalls;
    void *p_data;
    u_int32 p_datasz;
};
#else
typedef u_int16 process_id;
#endif

extern error_code _os_get_prtbl(void *buffer, u_int32 *count);
extern error_code _os_gprdsc(process_id procid, void *buffer, u_int32 *count);
extern error_code _os_sysdbg(void *param1, void *param2);

#endif /* Q9_PROCESS_H */

/* not defined (no documented value):
 *   Pr_desc, pr_desc (process descriptor typedefs) and their members
 *   _age, _fcalls, _group, _icalls, _id, _pagcnt, _pid, _pmodul, _prior,
 *   _rbytes, _state, _user, _wbytes
 */
