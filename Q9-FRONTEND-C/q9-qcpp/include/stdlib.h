/* stdlib.h -- Q9-Werkzeugkette, bewusst minimal (s. stddef.h).
 *
 * Nur was qcc_backend_c.cpp wirklich ruft (nachgezaehlt: exit einmal,
 * strtol einmal). Die Liste absichtlich NICHT vorsorglich verlaengern --
 * dieselbe Regel wie in string.h: der Bootstrap wird byteweise mit einem
 * Referenzlauf verglichen, und jede zusaetzliche Deklaration ist eine
 * Aenderung an der Eingabe des Compilers.
 *
 * malloc/free stehen bewusst NICHT hier: das Backend kommt mit festen
 * globalen Tabellen aus (so ist es fuer den Emulator ausgelegt, s.
 * docs/STATUS.md), und qclib hat keine Freigabeliste.
 */
#ifndef Q9_STDLIB_H
#define Q9_STDLIB_H

extern void exit(int);
extern long strtol(const char*, char**, int);
extern char* realloc(char*, int);
extern char* getenv(const char*);
extern int system(const char*);

#endif
