/* string.h -- minimal Q9 string and memory interface. Edition: 2026-09-11.
 *
 * Only the functions actually called by the generated parser
 * (nachgezaehlt in Data/qcc_p.c: strncmp 274-mal, strlen 11-mal, strchr
 * once). Do not extend the list proactively: the bootstrap is compared
 * byte-for-byte with a reference run, and every extra declaration changes the
 * compiler input.
 */
#ifndef Q9_STRING_H
#define Q9_STRING_H

#include <stddef.h>

extern size_t strlen(const char*);
extern char* strchr(const char*, int);
extern int strncmp(const char*, const char*, size_t);
extern char* strstr(const char*, const char*);

/* Additional functions called by qcc_backend_c.cpp (counted on 2026-09-07:
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
extern int memcmp(const void*, const void*, size_t);
extern void* memmove(void*, const void*, size_t);
extern void* memchr(const void*, int, size_t);
extern char* strncat(char*, const char*, size_t);
extern size_t strspn(const char*, const char*);
extern size_t strcspn(const char*, const char*);
extern char* strpbrk(const char*, const char*);
extern int strcoll(const char*, const char*);

#endif
