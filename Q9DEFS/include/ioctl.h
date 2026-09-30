/* ioctl.h -- device control call.
 *
 * Derived from the specification, no template used.
 * Status: ioctl belegt.
 */
#ifndef Q9_IOCTL_H
#define Q9_IOCTL_H

#include <SPF/BSD/sys/types.h>

extern int ioctl(unsigned int fd, unsigned int request, caddr_t arg);

#endif /* Q9_IOCTL_H */

/* not defined (no documented value): none */
