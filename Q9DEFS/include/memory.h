/* memory.h -- memory type ("color") codes.
 *
 * Derived from the specification, no template used.
 * Status: SYSRAM, VIDEO1, VIDEO2 belegt; MEM_ANY derived.
 */
#ifndef Q9_MEMORY_H
#define Q9_MEMORY_H

#define MEM_ANY  0      /* derived: no specific memory type */
#define SYSRAM   0x01   /* system memory */
#define VIDEO1   0x80   /* video memory plane A */
#define VIDEO2   0x81   /* video memory plane B */

#endif /* Q9_MEMORY_H */

/* not defined (no documented value): none */
