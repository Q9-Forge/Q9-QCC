/* string.h -- minimal Q9 string and memory interface. Edition: 2026-09-11.
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
extern char* strstr(const char*, const char*);

/* Dazu, was qcc_backend_c.cpp ruft (2026-09-07 nachgezaehlt: strcmp
   154-mal, strncpy 9-mal, memset 6-mal, strtok und strrchr je zweimal,
   strcat und memcpy je einmal). Damit ist das Backend das erste Werkzeug
   der Kette, das mehr als die drei Parser-Funktionen braucht. */
extern int strcmp(const char*, const char*);
extern char* strcat(char*, const char*);
extern char* strncpy(char*, const char*, size_t);
extern char* strrchr(const char*, int);
extern char* strtok(char*, const char*);
extern void* memset(void*, int, size_t);
extern void* memcpy(void*, const void*, size_t);

#endif
