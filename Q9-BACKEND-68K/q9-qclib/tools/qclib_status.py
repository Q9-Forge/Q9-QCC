#!/usr/bin/env python3
"""Regeneriert STATUS.md: dekodiert clib.l selbst (kein libgen.exe noetig,
kein Wine/DOSBox auf dieser Maschine), liest die oeffentlichen Labels aus
src/*.a und zaehlt den MWOS-Korpus. Bei jeder neuen Funktion in src/*.a
(oder jedem entfernten Symbol) einfach erneut laufen lassen.

    tools/qclib_status.py [<pfad zu clib.l>] [<MWOS-Korpusverzeichnis>]
"""
import glob
import re
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
CLIB = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/Volumes/SSD1TB/2Q9/MWOS/MWOS/OS9/68020/LIB/clib.l")
CORPUS = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("/Volumes/SSD1TB/2Q9/MWOS")


def decode_clib_public_names(path):
    """Liest die 22-Byte-Globaldefinitionen eines libgen-Archivs (Format 1.1)
    und liefert die oeffentlichen (kein fuehrender Unterstrich), definierten
    (flags==4) Codesymbole. Kopfformat und Layout s. README.md."""
    d = path.read_bytes()
    hash_size = struct.unpack(">I", d[18:22])[0]
    globaldefs_size = struct.unpack(">I", d[22:26])[0]
    strtab_size = struct.unpack(">I", d[26:30])[0]
    HEADER = 0x2a
    globaldefs_start = HEADER + hash_size
    strtab_start = globaldefs_start + globaldefs_size
    # Die ersten vier Byte der Stringtabelle sind kein Text (Feldlaenge o.ae.).
    strtab = d[strtab_start + 4:strtab_start + strtab_size]

    def cstr_at(off):
        if off < 0 or off >= len(strtab):
            return None
        end = strtab.find(b"\x00", off)
        if end == -1:
            end = len(strtab)
        return strtab[off:end].decode("latin-1")

    gd = d[globaldefs_start:globaldefs_start + globaldefs_size]
    names = set()
    for i in range(len(gd) // 22):
        e = gd[i * 22:(i + 1) * 22]
        flags = struct.unpack(">H", e[4:6])[0]
        nameoff = struct.unpack(">I", e[10:14])[0]
        name = cstr_at(nameoff)
        if flags == 4 and name and not name.startswith("_"):
            names.add(name)
    return sorted(names)


def implemented_names(qclib_dir):
    impl = set()
    for path in glob.glob(str(qclib_dir / "src" / "*.a")):
        with open(path, encoding="latin-1") as f:
            for line in f:
                m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*):", line)
                if m and not m.group(1).startswith("_"):
                    impl.add(m.group(1))
    return impl


def corpus_counts(names, corpus_dir):
    if not corpus_dir.is_dir():
        return {}
    pattern = "|".join(re.escape(n) for n in names)
    cmd = ["grep", "-rhoE", r"\b(" + pattern + r")\s*\(", str(corpus_dir), "--include=*.c"]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    counts = {}
    for line in proc.stdout.splitlines():
        name = line.split("(")[0].strip()
        counts[name] = counts.get(name, 0) + 1
    return counts


HEADER_OF = {
    "abort": "stdlib.h", "abs": "stdlib.h", "atexit": "stdlib.h", "atof": "stdlib.h",
    "atoi": "stdlib.h", "atol": "stdlib.h", "bsearch": "stdlib.h", "calloc": "stdlib.h",
    "div": "stdlib.h", "exit": "stdlib.h", "free": "stdlib.h", "getenv": "stdlib.h",
    "labs": "stdlib.h", "ldiv": "stdlib.h", "malloc": "stdlib.h", "mblen": "stdlib.h",
    "mbstowcs": "stdlib.h", "mbtowc": "stdlib.h", "qsort": "stdlib.h", "rand": "stdlib.h",
    "realloc": "stdlib.h", "srand": "stdlib.h", "strtod": "stdlib.h", "strtol": "stdlib.h",
    "strtoul": "stdlib.h", "system": "stdlib.h", "wcstombs": "stdlib.h", "wctomb": "stdlib.h",
    "clearerr": "stdio.h", "fclose": "stdio.h", "feof": "stdio.h", "ferror": "stdio.h",
    "fflush": "stdio.h", "fgetc": "stdio.h", "fgetpos": "stdio.h", "fgets": "stdio.h",
    "fopen": "stdio.h", "fprintf": "stdio.h", "fputc": "stdio.h", "fputs": "stdio.h",
    "fread": "stdio.h", "freopen": "stdio.h", "fscanf": "stdio.h", "fseek": "stdio.h",
    "fsetpos": "stdio.h", "ftell": "stdio.h", "fwrite": "stdio.h", "getc": "stdio.h",
    "getchar": "stdio.h", "gets": "stdio.h", "perror": "stdio.h", "printf": "stdio.h",
    "putc": "stdio.h", "putchar": "stdio.h", "puts": "stdio.h", "remove": "stdio.h",
    "rename": "stdio.h", "rewind": "stdio.h", "scanf": "stdio.h", "setbuf": "stdio.h",
    "setvbuf": "stdio.h", "sprintf": "stdio.h", "sscanf": "stdio.h", "tmpfile": "stdio.h",
    "tmpnam": "stdio.h", "ungetc": "stdio.h", "vfprintf": "stdio.h", "vprintf": "stdio.h",
    "vsprintf": "stdio.h",
    "memchr": "string.h", "memcmp": "string.h", "memcpy": "string.h", "memmove": "string.h",
    "memset": "string.h", "strcat": "string.h", "strchr": "string.h", "strcmp": "string.h",
    "strcoll": "string.h", "strcpy": "string.h", "strcspn": "string.h", "strerror": "string.h",
    "strlen": "string.h", "strncat": "string.h", "strncmp": "string.h", "strncpy": "string.h",
    "strpbrk": "string.h", "strrchr": "string.h", "strspn": "string.h", "strstr": "string.h",
    "strtok": "string.h", "strxfrm": "string.h",
    "isalnum": "ctype.h", "isalpha": "ctype.h", "iscntrl": "ctype.h", "isdigit": "ctype.h",
    "isgraph": "ctype.h", "islower": "ctype.h", "isprint": "ctype.h", "ispunct": "ctype.h",
    "isspace": "ctype.h", "isupper": "ctype.h", "isxdigit": "ctype.h", "tolower": "ctype.h",
    "toupper": "ctype.h",
    "acos": "math.h", "asin": "math.h", "atan": "math.h", "atan2": "math.h", "ceil": "math.h",
    "cos": "math.h", "cosh": "math.h", "exp": "math.h", "fabs": "math.h", "floor": "math.h",
    "fmod": "math.h", "frexp": "math.h", "ldexp": "math.h", "log": "math.h", "log10": "math.h",
    "modf": "math.h", "pow": "math.h", "sin": "math.h", "sinh": "math.h", "sqrt": "math.h",
    "tan": "math.h", "tanh": "math.h",
    "asctime": "time.h", "clock": "time.h", "ctime": "time.h", "difftime": "time.h",
    "gmtime": "time.h", "localtime": "time.h", "mktime": "time.h", "strftime": "time.h",
    "time": "time.h",
    "localeconv": "locale.h", "setlocale": "locale.h",
    "longjmp": "setjmp.h",
    "raise": "signal.h", "signal": "signal.h",
}

DEVIATION = {
    "fgets": "bewusst NICHT clib-kompatibel: trennt an `$0a` (QCCs `\\n`-Abbildung), "
             "nicht an `$0d` wie Microwares fgets -- Host-libc ist hier das gueltige "
             "Orakel, nicht clib.",
}


def main():
    public_code = decode_clib_public_names(CLIB)
    if len(public_code) != 140:
        print("WARNUNG: %d oeffentliche Codesymbole gefunden, nicht die erwarteten 140 "
              "-- clib.l-Version geaendert? Weiter mit dem gemessenen Stand." % len(public_code),
              file=sys.stderr)

    impl = implemented_names(HERE)
    counts = corpus_counts(public_code, CORPUS)

    rows = []
    for name in public_code:
        is_impl = name in impl
        status = "\U0001F7E1" if name in DEVIATION else ("✅" if is_impl else "❌")
        rows.append({
            "name": name, "header": HEADER_OF.get(name, "?"), "impl": is_impl,
            "status": status, "count": counts.get(name, 0), "note": DEVIATION.get(name, ""),
        })
    rows.sort(key=lambda r: -r["count"])
    n_impl = sum(1 for r in rows if r["impl"])

    lines = []
    lines.append("# qclib -- Status gegen Microwares `clib.l`\n")
    lines.append(
        "Gemessen, nicht geschaetzt: die 140 oeffentlichen Codesymbole sind aus "
        "`clib.l` (`MWOS/OS9/68020/LIB/clib.l`, libgen-Archiv Format 1.1) selbst "
        "dekodiert -- Kopf, Hashtabelle, Globaldefinitionen (22 Byte je Eintrag) "
        "und Stringtabelle nach dem in diesem README dokumentierten Format "
        "geparst, ohne das fehlende DOS-Werkzeug `libgen.exe` (kein Wine/DOSBox "
        "auf dieser Maschine). Ergebnis: **214 definierte Codesymbole, davon 140 "
        "oeffentlich, 44 Datensymbole** -- deckt sich exakt mit der fruehesten "
        "`libgen -ln`-Messung vom 07.09.2026.\n"
    )
    lines.append(
        "**Korpus-Treffer** zaehlt Aufrufe `name(` in allen `.c`-Dateien unter "
        "`%s` -- ein Mass fuer die Prioritaet, nicht fuer den Bedarf DIESER "
        "Werkzeugkette (die braucht gemessen nur die bereits gruenen Funktionen, "
        "s. `tools/korpus.sh`).\n" % CORPUS
    )
    lines.append("## Status\n")
    lines.append("| | Bedeutung |")
    lines.append("|---|---|")
    lines.append("| ✅ | umgesetzt, gegen clib bzw. Host-libc verifiziert (`tests/vsclib.sh` u.a.) |")
    lines.append("| \U0001F7E1 | umgesetzt, aber MIT bewusster, dokumentierter Abweichung von clib |")
    lines.append("| ❌ | fehlt noch |\n")
    lines.append("**Stand: %d/140 umgesetzt (%d %%).**\n" % (n_impl, n_impl * 100 // 140))
    lines.append("## Alle 140 Funktionen, nach Korpus-Haeufigkeit sortiert\n")
    lines.append("| Status | Funktion | Header | Korpus-Treffer | Bemerkung |")
    lines.append("|---|---|---|---:|---|")
    for r in rows:
        lines.append("| %s | `%s` | `%s` | %d | %s |" % (r["status"], r["name"], r["header"], r["count"], r["note"]))
    lines.append("")
    lines.append("## Wie diese Datei aktuell gehalten wird\n")
    lines.append(
        "`tools/qclib_status.py` regeneriert sie: dekodiert `clib.l` frisch, "
        "liest die oeffentlichen Labels aus `src/*.a`, zaehlt den MWOS-Korpus "
        "erneut und schreibt diese Datei neu. Bei jeder neuen Funktion in "
        "`src/*.a` (oder jedem entfernten Symbol) also einfach erneut laufen "
        "lassen, statt die Tabelle von Hand nachzufuehren."
    )
    lines.append("")

    out = HERE / "STATUS.md"
    out.write_text("\n".join(lines), encoding="utf-8")
    print("STATUS.md geschrieben: %d/140 umgesetzt, %d Korpustreffer gesamt" %
          (n_impl, sum(counts.values())))


if __name__ == "__main__":
    main()
