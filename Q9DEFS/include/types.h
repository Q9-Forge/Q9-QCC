/* types.h -- fixed-size integer types and basic system typedefs.
 *
 * Derived from the specification, no template used.
 * Status: the type names are given by the specification; the underlying
 * types are derived from the documented widths (16 / 32 bit).
 */
#ifndef Q9_TYPES_H
#define Q9_TYPES_H

/* derived: consumers reach the short BSD names (u_char, u_int, ...) through
   this header without including the BSD type header themselves, so it is
   pulled in here */
#include <SPF/BSD/sys/types.h>

/* derived: 16-bit unsigned integer, unsigned short is 16 bit */
typedef unsigned short u_int16;

/* derived: 32-bit unsigned integer, unsigned int is 32 bit */
typedef unsigned int u_int32;

/* OS-9000/x86 service blocks carry a 32-bit path identifier. Keep the
 * existing 68k declaration until its public runtime interface is audited. */
#ifdef __Q9_X86__
typedef u_int32 path_id;
#else
typedef u_int16 path_id;
#endif

/* derived: 0 means success, any other value is an error code; the exact
   underlying type is not documented, a plain int is used */
typedef int error_code;

#endif /* Q9_TYPES_H */

/* not defined (no documented value): none */
