/* UNIX/os9def.h -- compatibility helpers.
 *
 * Derived from the specification, no template used.
 * Status: bzero belegt.
 */
#ifndef Q9_UNIX_OS9DEF_H
#define Q9_UNIX_OS9DEF_H

extern void bzero(void *addr, unsigned int len);
extern void bcopy(const void *source,void *destination,unsigned int len);
extern int bcmp(const void *left,const void *right,unsigned int len);

#endif /* Q9_UNIX_OS9DEF_H */

/* not defined (no documented value): none */
