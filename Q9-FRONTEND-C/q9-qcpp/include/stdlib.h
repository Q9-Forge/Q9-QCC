/* stdlib.h -- minimal Q9 standard-library interface. Edition: 2026-09-11.
 *
 * Only functions actually called by qcc_backend_c.cpp (counted: exit once,
 * strtol once). Do not extend the list proactively: as with string.h, the
 * bootstrap is compared byte-for-byte with a reference run, and every extra
 * declaration changes the compiler input.
 *
 * malloc/free are intentionally NOT declared here: the backend uses fixed
 * global tables (as required by the emulator; see docs/STATUS.md), and qclib
 * has no allocation interface yet.
 */
#ifndef Q9_STDLIB_H
#define Q9_STDLIB_H

extern void exit(int);
extern void abort(void);
/* qcir kennt keinen Funktionszeiger als Parameter eines extern-Prototyps --
   deshalb wie bei qsort ueber einen typedef (2026-09-27). */
typedef void (*atexit_fn)(void);
extern int atexit(atexit_fn);
extern long strtol(const char*, char**, int);
extern char* realloc(char*, int);
extern char* getenv(const char*);
extern int system(const char*);
extern int abs(int);
extern int atoi(const char*);
extern long atol(const char*);
extern long labs(long);
extern unsigned long strtoul(const char*, char**, int);
extern int mblen(const char*, int);
extern int mbtowc(int*, const char*, int);
extern int wctomb(char*, int);
extern int mbstowcs(int*, const char*, int);
extern int wcstombs(char*, const int*, int);
extern double atof(const char*);
extern double strtod(const char*, char**);
typedef int (*qsort_cmp)(const void*, const void*);
extern void* bsearch(const void*, const void*, int, int, qsort_cmp);
extern void qsort(void*, int, int, qsort_cmp);
extern int div(int, int);
extern long ldiv(long, long);
extern int rand(void);
extern void srand(unsigned int);

#endif
