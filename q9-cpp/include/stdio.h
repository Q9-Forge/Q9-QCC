/* stdio.h -- Q9-Werkzeugkette, bewusst minimal (s. stddef.h). */
#ifndef Q9_STDIO_H
#define Q9_STDIO_H

#include <stddef.h>

/* FILE bleibt ein undurchsichtiger Wert: die Teilmenge fasst nie hinein, und
   ein "typedef int FILE" laesst sich von QCC lesen. Auf dem 68k ist ein
   FILE* damit ein 32-Bit-Zeiger wie bei Microware. */
typedef int FILE;

/* stderr ist bei Microware ein MAKRO auf (&_niob[2]), also kein linkbares
   Symbol -- am Host ist es __stderrp. Beides ist fuer diese Teilmenge
   bedeutungslos. Ein eigener Nullstrom haelt den erfolgreichen Weg des
   Compilers unabhaengig von jenem internen stdio-Objekt: der erzeugte Parser
   schreibt nur bei ABGELEHNTER Eingabe nach stderr, und dann ist ein
   Fehlschlag beim Schreiben das kleinere Problem als eine unaufloesbare
   Referenz beim Binden. Wer die Meldungen sehen will, legt stderr in main
   auf einen echten Strom (dafuer gibt es weiterhin
   ../tools/bootstrap_prepare.py --diag). */
static FILE* stderr;

extern FILE* fopen(const char*, const char*);
extern size_t fread(void*, size_t, size_t, FILE*);
extern int fclose(FILE*);
extern int fprintf(FILE*, const char*, ...);
extern int printf(const char*, ...);
extern int fputc(int, FILE*);
extern int fputs(const char*, FILE*);
extern int sprintf(char*, const char*, ...);
/* fgets braucht das Backend fuer seine IR-Eingabe (eine Zeile je
   Anweisung), ferror fuer die Kontrolle nach dem Schreiben. */
extern char* fgets(char*, int, FILE*);
extern int ferror(FILE*);

#endif
