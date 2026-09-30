/* sys/types.h -- BSD-style short type names.
 *
 * Derived from the specification, no template used.
 * Status: u_int, u_char, u_short, u_long belegt (standard BSD names with
 * documented widths); caddr_t derived.
 */
#ifndef Q9_SPF_BSD_SYS_TYPES_H
#define Q9_SPF_BSD_SYS_TYPES_H

typedef unsigned int   u_int;     /* 32 bit */
typedef unsigned char  u_char;
typedef unsigned short u_short;   /* 16 bit */
typedef unsigned long  u_long;    /* 32 bit on the target */

/* derived: pointer type for untyped data (char *), used for pointer parameters */
typedef char *caddr_t;

#endif /* Q9_SPF_BSD_SYS_TYPES_H */

/* not defined (no documented value): none */
