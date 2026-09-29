/* stdio.h -- minimal Q9 standard-I/O interface. Edition: 2026-09-11. */
#ifndef Q9_STDIO_H
#define Q9_STDIO_H

#include <stddef.h>

#define EOF (-1)

/* FILE remains opaque: the subset never accesses its fields, and QCC can read
   typedef int FILE. On 68k, FILE* is therefore a 32-bit pointer like Microware's. */
typedef int FILE;

/* stderr is a macro for (&_niob[2]) in Microware, not a linkable
   Symbol -- am Host ist es __stderrp. Beides ist fuer diese Teilmenge
   bedeutungslos. Ein eigener Nullstrom haelt den erfolgreichen Weg des
   Compilers unabhaengig von jenem internen stdio-Objekt: der erzeugte Parser
   schreibt nur bei ABGELEHNTER Eingabe nach stderr, und dann ist ein
   Fehlschlag beim Schreiben das kleinere Problem als eine unaufloesbare
   Referenz beim Binden. Wer die Meldungen sehen will, legt stderr in main
   auf einen echten Strom (dafuer gibt es weiterhin
   ../tools/bootstrap_prepare.py --diag). */
static FILE* stderr;
/* 2026-09-27: dieselbe Art Platzhalter wie stderr oben, aus demselben Grund
 * (kein linkbares Microware-Objekt in dieser Teilmenge) -- ergaenzt, weil
 * Q9-QCC/src/qcc.c selbst (Selfhosting-Versuch) stdout aktiv per
 * freopen()/fclose() umleitet; das Frontend kannte den Namen bisher gar
 * nicht ("unknown variable 'stdout'"). Ob eine echte Umleitung zur Konsole
 * beim SPAETEREN Linken gegen qclib tatsaechlich funktioniert, ist davon
 * unabhaengig -- hier geht es nur um die Typpruefung. */
static FILE* stdout;
/* 2026-09-27: dieselbe Art Platzhalter wie stderr/stdout oben -- ergaenzt,
 * weil mehrere Q9-Tools/System-Quellen (attr/chown/copy/deiniz u.a.)
 * "stdin" in Vergleichen/Zuweisungen benutzen ("list==stdin"), das Frontend
 * kannte den Namen bisher gar nicht ("unknown variable 'stdin'"). */
static FILE* stdin;

extern FILE* fopen(const char*, const char*);
extern size_t fread(void*, size_t, size_t, FILE*);
extern size_t fwrite(const void*, size_t, size_t, FILE*);
extern int fclose(FILE*);
extern int fprintf(FILE*, const char*, ...);
extern int printf(const char*, ...);
extern int puts(const char*);
extern int fputc(int, FILE*);
extern int fputs(const char*, FILE*);
extern int sprintf(char*, const char*, ...);
/* fgets is needed by the backend for IR input (one instruction per line), and
   ferror checks the result after writing. */
extern char* fgets(char*, int, FILE*);
extern int ferror(FILE*);
extern void clearerr(FILE*);
extern int fflush(FILE*);
extern int getchar(void);
extern int getc(FILE*);
extern int fgetc(FILE*);
extern FILE* freopen(const char*, const char*, FILE*);
extern int ungetc(int, FILE*);
extern FILE* tmpfile(void);
extern char* tmpnam(char*);
extern int fgetpos(FILE*, void*);
extern int fsetpos(FILE*, const void*);
extern int scanf(const char*, ...);
extern int fscanf(FILE*, const char*, ...);
extern int vprintf(const char*, int*);
extern int vfprintf(FILE*, const char*, int*);
extern int vsprintf(char*, const char*, int*);
extern int putchar(int);
extern int putc(int, FILE*);
extern char* gets(char*);
extern void setbuf(FILE*, char*);
extern int setvbuf(FILE*, char*, int, size_t);
extern int fseek(FILE*, long, int);
extern long ftell(FILE*);
extern void rewind(FILE*);
extern int remove(const char*);
extern int rename(const char*, const char*);
extern void perror(const char*);

/* C89 formatted input; implemented by the target runtime. */
extern int sscanf(const char*, const char*, ...);

#endif
