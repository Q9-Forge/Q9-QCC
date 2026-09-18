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
extern long strtol(const char*, char**, int);
extern char* realloc(char*, int);
extern char* getenv(const char*);
extern int system(const char*);

/* Added 2026-09-18 for the qclib STATUS.md gap-closing test. */
extern void abort(void);

#endif
