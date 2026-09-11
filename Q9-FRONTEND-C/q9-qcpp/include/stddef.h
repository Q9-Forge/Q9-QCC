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

/* Auf dem 68k-Ziel und am Host sind int und Zeiger beide 32 Bit; die
   Bootstrap-Teilmenge kennt kein eigenes vorzeichenloses Groessenmodell. */
typedef unsigned int size_t;

/* NULL fehlte hier und ist 2026-09-07 dazugekommen: qcc_backend_c.cpp
   braucht es (strtok(NULL, ...)), und ohne die Definition meldet QCC
   "unknown variable NULL" -- richtig gemeldet, aber an der falschen
   Stelle gesucht. Die 0 genuegt: die Teilmenge kennt kein (void*)0, und
   auf dem 68k wie am Host ist ein Nullzeiger die Null. */
#define NULL 0

#endif
