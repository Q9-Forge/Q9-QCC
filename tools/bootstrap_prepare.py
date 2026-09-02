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

Der Host-Weg ohne Microware-Toolchain geht genauso; `-P` ist nicht noetig,
die Zeilenmarker entfernt dieses Werkzeug selbst:
    cc -E Data/qcc_p.c > build/qcc_p.host.i
    python3 tools/bootstrap_prepare.py build/qcc_p.host.i build/qcc_p.bootstrap.c

Die Ausgabe ist absichtlich eine normale, praepozessorfreie C-Datei: Sie kann
direkt mit `build/qcc_p @...` gelesen und ins OS-9-Testimage kopiert werden.

Mit `--diag[=<datei>]` entsteht stattdessen eine DIAGNOSE-Variante: `stderr`
wird in `main` auf einen echten Stream gelegt. Der normale Bootstrap laesst
`stderr` bewusst als nie initialisierten Nullzeiger stehen, damit der
erfolgreiche Pfad nicht von Microwares internem stdio-Objekt abhaengt -- ein
selbstgebauter Compiler kann dann aber nur `SEMERR` melden und nie sagen, WAS
er beanstandet. Fuer die Fehlersuche am 68k-Ziel ist genau das noetig:
    python3 tools/bootstrap_prepare.py build/qcc_p.xcc.i \\
            build/qcc_p.bootstrap.diag.c --diag
"""
from pathlib import Path
import re
import sys

DIAG_DEFAULT = "qcc_diag.txt"

args = [a for a in sys.argv[1:] if not a.startswith("--")]
flags = [a for a in sys.argv[1:] if a.startswith("--")]
diag_path = None
for flag in flags:
    if flag == "--diag":
        diag_path = DIAG_DEFAULT
    elif flag.startswith("--diag="):
        diag_path = flag.split("=", 1)[1]
    else:
        raise SystemExit(f"unknown option: {flag}")
if len(args) != 2:
    raise SystemExit(
        f"usage: {Path(sys.argv[0]).name} <preprocessed.c> <bootstrap.c> "
        f"[--diag[=<file>]]"
    )

source = Path(args[0])
target = Path(args[1])
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
extern size_t strlen(const char*);
extern char* strchr(const char*, int);
extern int strncmp(const char*, const char*, size_t);
/* realloc braucht der erzeugte Parser an zwei Stellen: fuer das Aktions-Log
   und fuer den Eingabepuffer in main(). Die Deklaration stand frueher im
   Rumpf des Parsers und ueberlebte hier nur, weil diese Stelle zufaellig
   HINTER dem Marker oben liegt; seit genParserC sie unbedingt im Kopf ausgibt
   (also davor), faellt sie mit dem Header-Vorspann weg. Sie gehoert ohnehin
   hierher -- das ist die Liste aller libc-Funktionen, die die
   Bootstrap-Teilmenge braucht. Signatur wie von genParserC ausgegeben: die
   Teilmenge kennt kein size_t-typisiertes Allozieren, und auf dem 68k-Ziel
   sind int und Zeiger beide 32 Bit. */
extern char* realloc(char*, int);
"""

# Both preprocessors used in the project turn `stderr` into an implementation
# detail.  Microware uses `(&_niob[2])`; macOS' system headers use
# `__stderrp`.  Neither is meaningful to the QCC bootstrap subset, which has
# the neutral extern/static declaration from the preamble above.
body = (text[offset:]
        .replace("(&_niob[2])", "stderr")
        .replace("__stderrp", "stderr")
        # The optional host-only buffered-output macro is defined before the
        # marker and deliberately removed with the system header prefix.
        # Flushing is not required by the OS-9 bootstrap variant.
        #
        # 2026-09-02: der "((void)0);"-Fall tritt nicht mehr auf. genParserC
        # gibt den Leerlauf-Zweig jetzt als "(void)0" ohne aeussere Klammern
        # aus, weil QCCs voidCastStmt keine Klammern um den Cast erlaubt und
        # der erzeugte Parser damit OHNE dieses Werkzeug lesbar ist (s.
        # q9-cpp/tools/bootstrap.sh). Das Muster bleibt fuer aeltere, schon
        # erzeugte Quellen stehen -- und die Anweisung selbst wird jetzt
        # BEHALTEN, weshalb beide Wege dasselbe IR liefern.
        .replace("QCC_OUTPUT_FLUSH();", "")
        .replace("QCC_OUTPUT_FLUSH()", "")
        .replace("((void)0);", ""))
# Apple libc fortifies sprintf in preprocessed output.  QCC deliberately
# supports the ordinary C call, not Clang's compiler-internal helper.
body = re.sub(
    r"__builtin___sprintf_chk\s*\(\s*([^,]+),\s*0,\s*"
    r"__builtin_object_size\s*\(\s*\1,\s*2\s*>\s*1\s*\?\s*1\s*:\s*0\s*\),\s*",
    r"sprintf(\1, ",
    body,
)
# XCC separates tokens with a blank when it expands a macro, so the
# `TC_SET_CURRENT`/`TC_TYPE_PUSH`/`TC_TYPE_POP` bodies come out as
# `tcCurrentType . base` where Clang writes `tcCurrentType.base`.  QCC's
# `tc_target`/`tc_varref` derive the member name from the raw span and test
# `*nameEnd` directly, so a blank there makes them miss every branch and
# report `unknown assignment target` (or an empty `field=`) instead.
#
# This normalisation is a WORKAROUND, not the fix: `s . a = 1` is valid C
# that QCC rejects, whichever preprocessor produced it.  Repairing the span
# handling in the frontend is the real remedy; until then this keeps the two
# preprocessor paths equivalent.  Member accesses only -- every other
# operator comes out of both preprocessors unchanged (verified 2026-08-31).
_LITERAL = re.compile(r'"(?:\\.|[^"\\])*"' + r"|'(?:\\.|[^'\\])*'")
_SPACED_MEMBER = re.compile(
    r"(?<=[A-Za-z_0-9\])])(?:[ \t]+\.[ \t]*|[ \t]*\.[ \t]+)(?=[A-Za-z_])"
)


def _tighten_member_access(text):
    """Drop blanks around `.` -- outside string and character literals."""
    out, pos, count = [], 0, 0
    for lit in _LITERAL.finditer(text):
        chunk, n = _SPACED_MEMBER.subn(".", text[pos:lit.start()])
        out.append(chunk)
        out.append(lit.group(0))
        count += n
        pos = lit.end()
    chunk, n = _SPACED_MEMBER.subn(".", text[pos:])
    out.append(chunk)
    return "".join(out), count + n


body, tightened = _tighten_member_access(body)

# Clang emits line markers (`# 88 "Data/qcc_p.c"`) even for a single
# translation unit; XCC does not.  QCC has no preprocessor at all, so a
# leftover marker makes it reject the whole file with a bare `FAIL` -- no
# line, no hint.  Dropping them here keeps the two preprocessor paths
# interchangeable instead of relying on a `-P` in every call site.
body = re.sub(r"^[ \t]*#[ \t]*(?:[0-9]+|line\b).*\n?", "", body, flags=re.M)
# Anything else starting with `#` is outside the bootstrap subset as well.
# Better to name it here than to debug a bare `FAIL` later.
leftover = [
    (n, line)
    for n, line in enumerate(body.split("\n"), 1)
    if line.lstrip().startswith("#")
]
if leftover:
    detail = "; ".join(f"line {n}: {line.strip()[:60]}" for n, line in leftover[:5])
    raise SystemExit(
        f"{source}: {len(leftover)} preprocessor directive(s) left in the body "
        f"-- QCC has no preprocessor and would reject the file with a bare "
        f"FAIL. First: {detail}"
    )

# Diagnose-Variante: `stderr` auf einen echten Stream legen.  Die Zuweisung
# muss NACH den Deklarationen von main stehen -- C89 (und QCC) erlauben keine
# Anweisung davor.  Ungepuffert, damit jede Meldung sofort auf der Platte
# steht: bricht der Compiler mitten im Lauf ab, ist genau die letzte Meldung
# die interessante.
_MAIN_DECL = re.compile(
    r"(int\s+main\s*\(\s*int\s+argc\s*,\s*char\s*\*\*\s*argv\s*\)\s*\{[^\n]*\n"
    r"[ \t]*FILE\*[ \t]*inputFile[^\n]*\n)"
)
if diag_path is not None:
    preamble = preamble.replace(
        "extern FILE* fopen(const char*, const char*);",
        "extern FILE* fopen(const char*, const char*);\n"
        "extern void setbuf(FILE*, char*);",
    )
    body, injected = _MAIN_DECL.subn(
        r'\1'
        f'\tstderr = fopen("{diag_path}", "w");\n'
        '\tif (stderr) setbuf(stderr, 0);\n',
        body,
        count=1,
    )
    if not injected:
        raise SystemExit(
            f"{source}: --diag: main's declaration block not found, so stderr "
            f"would stay a null pointer and the diagnostic build would be "
            f"silently useless. Expected 'int main(int argc, char** argv) {{' "
            f"followed by the 'FILE* inputFile;' declaration line."
        )

target.write_text(preamble + body, encoding="utf-8")
print(f"bootstrap source: {target} ({target.stat().st_size} bytes)")
if tightened:
    print(f"  member accesses tightened: {tightened} (see _tighten_member_access)")
if diag_path is not None:
    print(f"  DIAGNOSTIC build: stderr -> {diag_path} (unbuffered)")
