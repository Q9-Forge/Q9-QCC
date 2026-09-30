/* stdlib.h -- standard library header extended by the allocation calls.
 *
 * Derived from the specification, no template used.
 * Status: malloc, free belegt. This file forwards to the project's own
 * standard stdlib.h (via include_next) and only adds the prototypes; they
 * can equally be added to that header directly.
 */
#ifndef Q9_STDLIB_EXT_H
#define Q9_STDLIB_EXT_H

#include_next <stdlib.h>
#include <stddef.h>

extern void *malloc(size_t size);
extern void free(void *ptr);

#endif /* Q9_STDLIB_EXT_H */

/* not defined (no documented value): none */
