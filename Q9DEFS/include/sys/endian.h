/* sys/endian.h -- host/network byte order conversion.
 *
 * Derived from the specification, no template used.
 * Status: htonl, htons belegt.
 */
#ifndef Q9_SYS_ENDIAN_H
#define Q9_SYS_ENDIAN_H

#include <SPF/BSD/sys/types.h>

extern u_long  htonl(u_long hostlong);
extern u_short htons(u_short hostshort);

#endif /* Q9_SYS_ENDIAN_H */

/* not defined (no documented value): none */
