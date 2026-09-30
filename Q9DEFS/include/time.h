/* time.h -- standard time header extended by the system time call.
 *
 * Derived from the specification, no template used.
 * Status: _os_setime belegt. This file forwards to the project's own
 * standard time.h (via include_next) and only adds the prototype; the
 * prototype can equally be added to that header directly.
 */
#ifndef Q9_TIME_EXT_H
#define Q9_TIME_EXT_H

#include_next <time.h>
#include <types.h>

extern error_code _os_setime(u_int32 time);

#endif /* Q9_TIME_EXT_H */

/* not defined (no documented value): none */
