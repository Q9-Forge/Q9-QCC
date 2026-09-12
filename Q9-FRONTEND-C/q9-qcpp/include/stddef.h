/* stddef.h -- Q9-Werkzeugkette, bewusst minimal.
 *
 * These headers are not replacements for Microware headers; they are the
 * subset needed by the bootstrap, and everything here must be readable by QCC.
 * The SDK headers cannot satisfy that requirement: 153 of them
 * switch on __STDC__ between prototypes and K&R declarations, carry compiler
 * attributes, and define stderr as a macro for an internal
 * stdio-Objekt (&_niob[2]).
 *
 * This is why tools/bootstrap_prepare.py existed: it removed the
 * expanded SDK preamble and added a custom preamble. With these headers the
 * same preamble is produced normally through #include.
 */
#ifndef Q9_STDDEF_H
#define Q9_STDDEF_H

/* On the 68k target and the host, int and pointers are both 32 bits; the
   bootstrap subset has no separate unsigned-size model. */
typedef unsigned int size_t;

/* NULL was added on 2026-09-07 because qcc_backend_c.cpp needs it
   (strtok(NULL, ...)). Without this definition QCC reports "unknown variable
   NULL". The diagnostic is valid, but the missing definition was the cause.
   Zero is sufficient: the subset has no (void*)0, and a null pointer is zero
   on both 68k and the host. */
#define NULL 0

#endif
