/* signal.h -- standard signal header extended by the sleep call.
 *
 * Derived from the specification, no template used.
 * Status: _os9_sleep belegt. This file forwards to the project's own
 * standard signal.h (via include_next) and only adds the prototype; the
 * prototype can equally be added to that header directly.
 */
#ifndef Q9_SIGNAL_EXT_H
#define Q9_SIGNAL_EXT_H

#include_next <signal.h>
#include <types.h>

extern error_code _os9_sleep(u_int32 *ticks);

#endif /* Q9_SIGNAL_EXT_H */

/* not defined (no documented value): none */
