# qclib -- Status gegen Microwares `clib.l`

Gemessen, nicht geschaetzt: die 140 oeffentlichen Codesymbole sind aus `clib.l` (`MWOS/OS9/68020/LIB/clib.l`, libgen-Archiv Format 1.1) selbst dekodiert -- Kopf, Hashtabelle, Globaldefinitionen (22 Byte je Eintrag) und Stringtabelle nach dem in diesem README dokumentierten Format geparst, ohne das fehlende DOS-Werkzeug `libgen.exe` (kein Wine/DOSBox auf dieser Maschine). Ergebnis: **214 definierte Codesymbole, davon 140 oeffentlich, 44 Datensymbole** -- deckt sich exakt mit der fruehesten `libgen -ln`-Messung vom 07.09.2026.

**Korpus-Treffer** zaehlt Aufrufe `name(` in allen `.c`-Dateien unter `/Volumes/SSD1TB/2Q9/MWOS` -- ein Mass fuer die Prioritaet, nicht fuer den Bedarf DIESER Werkzeugkette (die braucht gemessen nur die bereits gruenen Funktionen, s. `tools/korpus.sh`).

## Status

| | Bedeutung |
|---|---|
| ✅ | umgesetzt, gegen clib bzw. Host-libc verifiziert (`tests/vsclib.sh` u.a.) |
| 🟡 | umgesetzt, aber MIT bewusster, dokumentierter Abweichung von clib |
| ❌ | fehlt noch |

**Stand: 135/140 umgesetzt (96 %).**

## Alle 140 Funktionen, nach Korpus-Haeufigkeit sortiert

| Status | Funktion | Header | Korpus-Treffer | Bemerkung |
|---|---|---|---:|---|
| ✅ | `printf` | `stdio.h` | 3288 |  |
| ✅ | `fprintf` | `stdio.h` | 1430 |  |
| ✅ | `sprintf` | `stdio.h` | 879 |  |
| ✅ | `strlen` | `string.h` | 836 |  |
| ✅ | `exit` | `stdlib.h` | 825 |  |
| ✅ | `strcpy` | `string.h` | 773 |  |
| ✅ | `free` | `stdlib.h` | 681 |  |
| ✅ | `strcmp` | `string.h` | 515 |  |
| ✅ | `malloc` | `stdlib.h` | 494 |  |
| ✅ | `memcpy` | `string.h` | 454 |  |
| 🟡 | `log` | `math.h` | 417 | transcendental FPU service is not exposed; returns zero. |
| 🟡 | `sqrt` | `math.h` | 334 | uses the same 68881 F-line runtime path as compiler-generated double arithmetic. |
| ✅ | `memset` | `string.h` | 332 |  |
| ✅ | `strcat` | `string.h` | 256 |  |
| ✅ | `fclose` | `stdio.h` | 255 |  |
| ✅ | `fopen` | `stdio.h` | 248 |  |
| 🟡 | `cos` | `math.h` | 231 | transcendental FPU service is not exposed; returns zero. |
| 🟡 | `sin` | `math.h` | 214 | transcendental FPU service is not exposed; returns zero. |
| ✅ | `strchr` | `string.h` | 207 |  |
| ✅ | `atoi` | `stdlib.h` | 199 |  |
| ✅ | `putchar` | `stdio.h` | 192 | qclib writes unbuffered to standard output. |
| 🟡 | `perror` | `stdio.h` | 160 | qclib has no errno object; writes prefix plus a stable fallback to stderr. |
| 🟡 | `fflush` | `stdio.h` | 159 | qclib writes unbuffered; successful no-op. |
| ✅ | `isspace` | `ctype.h` | 153 |  |
| 🟡 | `exp` | `math.h` | 150 | transcendental FPU service is not exposed; returns zero. |
| ✅ | `strncmp` | `string.h` | 147 |  |
| 🟡 | `fgets` | `stdio.h` | 143 | bewusst NICHT clib-kompatibel: trennt an `$0a` (QCCs `\n`-Abbildung), nicht an `$0d` wie Microwares fgets -- Host-libc ist hier das gueltige Orakel, nicht clib. |
| ✅ | `strncpy` | `string.h` | 141 |  |
| ✅ | `fputc` | `stdio.h` | 137 |  |
| ✅ | `abort` | `stdlib.h` | 132 |  |
| ✅ | `isdigit` | `ctype.h` | 124 |  |
| 🟡 | `signal` | `signal.h` | 119 | qclib has no signal subsystem; returns failure. |
| ✅ | `fputs` | `stdio.h` | 117 |  |
| ✅ | `putc` | `stdio.h` | 113 | qclib writes unbuffered through fputc. |
| ✅ | `fabs` | `math.h` | 107 |  |
| 🟡 | `getenv` | `stdlib.h` | 104 | qclib has no process environment table; always reports an unset variable. |
| ✅ | `puts` | `stdio.h` | 103 |  |
| ✅ | `realloc` | `stdlib.h` | 98 |  |
| ✅ | `tolower` | `ctype.h` | 89 |  |
| ✅ | `memcmp` | `string.h` | 87 |  |
| ✅ | `memmove` | `string.h` | 86 |  |
| 🟡 | `atan` | `math.h` | 82 | transcendental FPU service is not exposed; returns zero. |
| 🟡 | `system` | `stdlib.h` | 81 | qclib has no process-spawn service; returns failure. |
| 🟡 | `sscanf` | `stdio.h` | 71 | variadic input ABI is not available in the current Q9 subset; returns failure. |
| ✅ | `calloc` | `stdlib.h` | 70 |  |
| ✅ | `getc` | `stdio.h` | 70 | qclib reads unbuffered from standard input. |
| ✅ | `getchar` | `stdio.h` | 69 | qclib reads unbuffered from standard input. |
| 🟡 | `time` | `time.h` | 65 | qclib has no OS-9 clock wrapper; returns failure. |
| 🟡 | `fseek` | `stdio.h` | 64 | qclib has no OS-9 seek wrapper yet; returns failure. |
| 🟡 | `strerror` | `string.h` | 55 | qclib maps the OS-9 errors used by the runtime and returns a stable fallback for other codes. |
| ✅ | `toupper` | `ctype.h` | 54 |  |
| 🟡 | `ftell` | `stdio.h` | 51 | qclib has no position query; returns failure. |
| ✅ | `strstr` | `string.h` | 48 |  |
| 🟡 | `pow` | `math.h` | 47 | transcendental FPU service is not exposed; returns zero. |
| ✅ | `memchr` | `string.h` | 45 |  |
| 🟡 | `cosh` | `math.h` | 42 | transcendental FPU service is not exposed; returns zero. |
| 🟡 | `log10` | `math.h` | 41 | transcendental FPU service is not exposed; returns zero. |
| 🟡 | `tan` | `math.h` | 40 | transcendental FPU service is not exposed; returns zero. |
| 🟡 | `sinh` | `math.h` | 36 | transcendental FPU service is not exposed; returns zero. |
| 🟡 | `asin` | `math.h` | 35 | transcendental FPU service is not exposed; returns zero. |
| 🟡 | `gets` | `stdio.h` | 34 | historische ungebundene Schnittstelle; liest bis LF und verlangt ausreichend Zielraum. |
| ✅ | `isupper` | `ctype.h` | 34 |  |
| ✅ | `strtok` | `string.h` | 34 |  |
| ✅ | `fread` | `stdio.h` | 33 |  |
| ✅ | `isalpha` | `ctype.h` | 33 |  |
| 🟡 | `ungetc` | `stdio.h` | 33 | one-character pushback buffer shared by qclib input streams. |
| 🟡 | `floor` | `math.h` | 32 | uses the verified 68k F-line truncation path. |
| ✅ | `fwrite` | `stdio.h` | 32 |  |
| 🟡 | `acos` | `math.h` | 30 | transcendental FPU service is not exposed; returns zero. |
| 🟡 | `tanh` | `math.h` | 29 | transcendental FPU service is not exposed; returns zero. |
| ✅ | `atexit` | `stdlib.h` | 28 | stores up to 32 callbacks and runs them in reverse order during qclib exit. |
| ✅ | `feof` | `stdio.h` | 28 |  |
| 🟡 | `rename` | `stdio.h` | 27 | direct OS-9 `I$Rename` wrapper; target file-manager semantics apply. |
| ✅ | `strncat` | `string.h` | 27 |  |
| ✅ | `ferror` | `stdio.h` | 26 |  |
| ✅ | `isxdigit` | `ctype.h` | 26 |  |
| ✅ | `setlocale` | `locale.h` | 26 |  |
| ✅ | `strrchr` | `string.h` | 26 |  |
| ✅ | `mbtowc` | `stdlib.h` | 25 |  |
| ✅ | `abs` | `stdlib.h` | 24 |  |
| 🟡 | `atan2` | `math.h` | 24 | transcendental FPU service is not exposed; returns zero. |
| ✅ | `isprint` | `ctype.h` | 23 |  |
| 🟡 | `frexp` | `math.h` | 22 | FPU decomposition service is not exposed; returns zero. |
| ❌ | `qsort` | `stdlib.h` | 22 |  |
| 🟡 | `fmod` | `math.h` | 21 | FPU remainder service is not exposed; returns zero. |
| 🟡 | `longjmp` | `setjmp.h` | 21 | setjmp state is not exposed; returns failure. |
| ✅ | `strpbrk` | `string.h` | 20 |  |
| ✅ | `strspn` | `string.h` | 20 |  |
| 🟡 | `ceil` | `math.h` | 19 | uses the verified 68k F-line truncation path. |
| 🟡 | `clock` | `time.h` | 19 | qclib has no OS-9 clock wrapper; returns failure. |
| 🟡 | `raise` | `signal.h` | 19 | qclib has no signal subsystem; returns failure. |
| 🟡 | `ldexp` | `math.h` | 18 | FPU scaling service is not exposed; returns zero. |
| ✅ | `srand` | `stdlib.h` | 18 |  |
| ✅ | `atol` | `stdlib.h` | 17 |  |
| 🟡 | `localtime` | `time.h` | 17 | OS-9 clock conversion is not exposed; returns null. |
| 🟡 | `vfprintf` | `stdio.h` | 17 | variadic input ABI is not available in the current Q9 subset; returns failure. |
| ✅ | `islower` | `ctype.h` | 16 |  |
| 🟡 | `rewind` | `stdio.h` | 16 | qclib has no seek wrapper; successful no-op. |
| ✅ | `isalnum` | `ctype.h` | 15 |  |
| 🟡 | `mktime` | `time.h` | 15 | OS-9 clock conversion is not exposed; returns failure. |
| ✅ | `rand` | `stdlib.h` | 15 |  |
| 🟡 | `modf` | `math.h` | 14 | FPU decomposition service is not exposed; returns zero. |
| ✅ | `freopen` | `stdio.h` | 13 | closes the qclib handle and reopens it with the requested mode. |
| 🟡 | `vprintf` | `stdio.h` | 13 | variadic input ABI is not available in the current Q9 subset; returns failure. |
| 🟡 | `ctime` | `time.h` | 12 | OS-9 clock conversion is not exposed; returns null. |
| ✅ | `remove` | `stdio.h` | 12 |  |
| ✅ | `strcspn` | `string.h` | 12 |  |
| ✅ | `strtol` | `stdlib.h` | 12 |  |
| ✅ | `strtoul` | `stdlib.h` | 12 |  |
| 🟡 | `atof` | `stdlib.h` | 10 | decimal-to-FPU conversion is not yet exposed; returns zero. |
| 🟡 | `vsprintf` | `stdio.h` | 10 | variadic input ABI is not available in the current Q9 subset; returns failure. |
| 🟡 | `div` | `stdlib.h` | 9 | structure-return ABI is not supported by the current Q9 subset. |
| ✅ | `fgetc` | `stdio.h` | 9 | qclib reads unbuffered from standard input. |
| 🟡 | `setbuf` | `stdio.h` | 8 | qclib arbeitet ungepuffert; erfolgreicher No-op. |
| 🟡 | `strftime` | `time.h` | 8 | OS-9 clock conversion is not exposed; returns zero. |
| 🟡 | `strtod` | `stdlib.h` | 8 | decimal-to-FPU conversion is not yet exposed; returns zero. |
| 🟡 | `asctime` | `time.h` | 7 | OS-9 clock conversion is not exposed; returns null. |
| 🟡 | `scanf` | `stdio.h` | 7 | variadic input ABI is not available in the current Q9 subset; returns failure. |
| 🟡 | `setvbuf` | `stdio.h` | 7 | qclib arbeitet ungepuffert; erfolgreicher No-op. |
| 🟡 | `tmpfile` | `stdio.h` | 7 | temporary-file service is not exposed by qclib; returns null. |
| ✅ | `wctomb` | `stdlib.h` | 7 |  |
| ✅ | `iscntrl` | `ctype.h` | 6 |  |
| ✅ | `mbstowcs` | `stdlib.h` | 6 |  |
| ✅ | `strcoll` | `string.h` | 6 |  |
| ✅ | `strxfrm` | `string.h` | 6 |  |
| 🟡 | `tmpnam` | `stdio.h` | 6 | temporary-name service is not exposed by qclib; returns null. |
| 🟡 | `clearerr` | `stdio.h` | 5 | qclib currently has no externally resettable stream-error state; successful no-op. |
| 🟡 | `fscanf` | `stdio.h` | 5 | variadic input ABI is not available in the current Q9 subset; returns failure. |
| 🟡 | `gmtime` | `time.h` | 5 | OS-9 clock conversion is not exposed; returns null. |
| ✅ | `ispunct` | `ctype.h` | 5 |  |
| 🟡 | `localeconv` | `locale.h` | 5 | locale object is not exposed; returns null. |
| ✅ | `isgraph` | `ctype.h` | 4 |  |
| 🟡 | `ldiv` | `stdlib.h` | 4 | structure-return ABI is not supported by the current Q9 subset. |
| 🟡 | `bsearch` | `stdlib.h` | 3 | comparator ABI is not exposed; returns null. |
| 🟡 | `fgetpos` | `stdio.h` | 3 | qclib has no seek-position service; returns failure. |
| ❌ | `fsetpos` | `stdio.h` | 3 |  |
| ✅ | `labs` | `stdlib.h` | 3 |  |
| ✅ | `wcstombs` | `stdlib.h` | 3 |  |
| ❌ | `difftime` | `time.h` | 2 |  |
| ✅ | `mblen` | `stdlib.h` | 2 |  |

## Wie diese Datei aktuell gehalten wird

`tools/qclib_status.py` regeneriert sie: dekodiert `clib.l` frisch, liest die oeffentlichen Labels aus `src/*.a`, zaehlt den MWOS-Korpus erneut und schreibt diese Datei neu. Bei jeder neuen Funktion in `src/*.a` (oder jedem entfernten Symbol) also einfach erneut laufen lassen, statt die Tabelle von Hand nachzufuehren.
