/* stddef.h -- Q9-Werkzeugkette, bewusst minimal.
 *
 * Diese Header sind KEIN Ersatz fuer die Microware-Header, sondern die
 * Schnittmenge, die der Bootstrap braucht: alles darin muss von QCC selbst
 * lesbar sein. Die SDK-Header koennen das nicht sein -- 153 von ihnen
 * schalten an __STDC__ zwischen Prototypen und K&R-Deklarationen um, tragen
 * Compilerattribute und definieren stderr als Makro auf ein internes
 * stdio-Objekt (&_niob[2]).
 *
 * Genau deshalb gab es tools/bootstrap_prepare.py: es schnitt den
 * expandierten SDK-Vorspann weg und setzte eine eigene Praeambel davor. Mit
 * diesen Headern entsteht dieselbe Praeambel auf dem normalen Weg -- durch
 * #include.
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
