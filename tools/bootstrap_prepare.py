#!/usr/bin/env python3
"""Bereitet den von XCC vorverarbeiteten QCC-Parser fuer QCC selbst vor.

XCC ersetzt Teile von stdio.h durch seine interne `_niob`-Darstellung.  Das
ist fuer den Microware-Compiler korrekt, aber kein Bestandteil der
Quellsprache von QCC.  Dieses Werkzeug entfernt den voll expandierten
Header-Vorspann, setzt die wenigen wirklich benoetigten Deklarationen davor
und benennt den stderr-Makroausdruck in eine normale externe Variable um.

Aufruf:
    xcc -pp Data/qcc_p.c > build/qcc_p.xcc.i
    python3 tools/bootstrap_prepare.py build/qcc_p.xcc.i build/qcc_p.bootstrap.c

Die Ausgabe ist absichtlich eine normale, praepozessorfreie C-Datei: Sie kann
direkt mit `build/qcc_p @...` gelesen und ins OS-9-Testimage kopiert werden.
"""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit(f"usage: {Path(sys.argv[0]).name} <xcc-preprocessed.c> <bootstrap.c>")

source = Path(sys.argv[1])
target = Path(sys.argv[2])
text = source.read_text(encoding="utf-8", errors="replace")
marker = "static const char* p;"
offset = text.find(marker)
if offset < 0:
    raise SystemExit(f"{source}: generated-parser marker not found: {marker}")

preamble = """typedef int FILE;
typedef unsigned int size_t;
/* `stderr` is a Microware-stdio macro (`&_niob[2]`), not a linkable clib
   symbol.  QCC's bootstrap subset has no macro expansion and only needs the
   diagnostic stream on rejected input.  A private null stream keeps the
   successful compiler path independent of that internal stdio object. */
static FILE* stderr;
extern FILE* fopen(const char*, const char*);
extern size_t fread(void*, size_t, size_t, FILE*);
extern int fclose(FILE*);
extern int fprintf(FILE*, const char*, ...);
extern int printf(const char*, ...);
extern int fputc(int, FILE*);
extern int fputs(const char*, FILE*);
extern int sprintf(char*, const char*, ...);
extern void* malloc(size_t);
extern void* realloc(void*, size_t);
extern size_t strlen(const char*);
extern char* strchr(const char*, int);
extern int strncmp(const char*, const char*, size_t);
"""

body = text[offset:].replace("(&_niob[2])", "stderr")
target.write_text(preamble + body, encoding="utf-8")
print(f"bootstrap source: {target} ({target.stat().st_size} bytes)")
