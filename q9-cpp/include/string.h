/* string.h -- Q9-Werkzeugkette, bewusst minimal (s. stddef.h).
 *
 * Nur die drei Funktionen, die der erzeugte Parser wirklich ruft
 * (nachgezaehlt in Data/qcc_p.c: strncmp 274-mal, strlen 11-mal, strchr
 * einmal). Die Liste absichtlich NICHT vorsorglich verlaengern: der Bootstrap
 * wird byteweise mit einem Referenzlauf verglichen, und jede zusaetzliche
 * Deklaration ist eine Aenderung an der Eingabe des Compilers.
 */
#ifndef Q9_STRING_H
#define Q9_STRING_H

#include <stddef.h>

extern size_t strlen(const char*);
extern char* strchr(const char*, int);
extern int strncmp(const char*, const char*, size_t);

#endif
