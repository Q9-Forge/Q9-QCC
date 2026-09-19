# qclib -- Status gegen Microwares `clib.l`

Gemessen, nicht geschaetzt: die 140 oeffentlichen Codesymbole sind aus `clib.l` (`MWOS/OS9/68020/LIB/clib.l`, libgen-Archiv Format 1.1) selbst dekodiert -- Kopf, Hashtabelle, Globaldefinitionen (22 Byte je Eintrag) und Stringtabelle nach dem in diesem README dokumentierten Format geparst, ohne das fehlende DOS-Werkzeug `libgen.exe` (kein Wine/DOSBox auf dieser Maschine). Ergebnis: **214 definierte Codesymbole, davon 140 oeffentlich, 44 Datensymbole** -- deckt sich exakt mit der fruehesten `libgen -ln`-Messung vom 07.09.2026.

**Korpus-Treffer** zaehlt Aufrufe `name(` in allen `.c`-Dateien unter `/Volumes/SSD1TB/2Q9/MWOS` -- ein Mass fuer die Prioritaet, nicht fuer den Bedarf DIESER Werkzeugkette (die braucht gemessen nur die bereits gruenen Funktionen, s. `tools/korpus.sh`).

## Status

| | Bedeutung |
|---|---|
| ✅ | umgesetzt, gegen clib bzw. Host-libc verifiziert (`tests/vsclib.sh` u.a.) |
| 🟡 | umgesetzt, aber MIT bewusster, dokumentierter Abweichung von clib |
| ❌ | fehlt noch |

**Stand: 51/140 umgesetzt (36 %).**

## Alle 140 Funktionen, nach Korpus-Haeufigkeit sortiert

| Status | Funktion | Header | Korpus-Treffer | Bemerkung |
|---|---|---|---:|---|
| ✅ | `printf` | `stdio.h` | 3288 |  |
| ✅ | `fprintf` | `stdio.h` | 1430 |  |
| ✅ | `sprintf` | `stdio.h` | 879 |  |
| ✅ | `strlen` | `string.h` | 836 |  |
| ✅ | `exit` | `stdlib.h` | 825 |  |
| ❌ | `strcpy` | `string.h` | 773 |  |
| ✅ | `free` | `stdlib.h` | 681 |  |
| ✅ | `strcmp` | `string.h` | 515 |  |
| ✅ | `malloc` | `stdlib.h` | 494 |  |
| ✅ | `memcpy` | `string.h` | 454 |  |
| ❌ | `log` | `math.h` | 417 |  |
| ❌ | `sqrt` | `math.h` | 334 |  |
| ✅ | `memset` | `string.h` | 332 |  |
| ✅ | `strcat` | `string.h` | 256 |  |
| ✅ | `fclose` | `stdio.h` | 255 |  |
| ✅ | `fopen` | `stdio.h` | 248 |  |
| ❌ | `cos` | `math.h` | 231 |  |
| ❌ | `sin` | `math.h` | 214 |  |
| ✅ | `strchr` | `string.h` | 207 |  |
| ✅ | `atoi` | `stdlib.h` | 199 |  |
| ❌ | `putchar` | `stdio.h` | 192 |  |
| ❌ | `perror` | `stdio.h` | 160 |  |
| ❌ | `fflush` | `stdio.h` | 159 |  |
| ✅ | `isspace` | `ctype.h` | 153 |  |
| ❌ | `exp` | `math.h` | 150 |  |
| ✅ | `strncmp` | `string.h` | 147 |  |
| 🟡 | `fgets` | `stdio.h` | 143 | bewusst NICHT clib-kompatibel: trennt an `$0a` (QCCs `\n`-Abbildung), nicht an `$0d` wie Microwares fgets -- Host-libc ist hier das gueltige Orakel, nicht clib. |
| ✅ | `strncpy` | `string.h` | 141 |  |
| ✅ | `fputc` | `stdio.h` | 137 |  |
| ❌ | `abort` | `stdlib.h` | 132 |  |
| ✅ | `isdigit` | `ctype.h` | 124 |  |
| ❌ | `signal` | `signal.h` | 119 |  |
| ✅ | `fputs` | `stdio.h` | 117 |  |
| ❌ | `putc` | `stdio.h` | 113 |  |
| ❌ | `fabs` | `math.h` | 107 |  |
| ❌ | `getenv` | `stdlib.h` | 104 |  |
| ✅ | `puts` | `stdio.h` | 103 |  |
| ✅ | `realloc` | `stdlib.h` | 98 |  |
| ✅ | `tolower` | `ctype.h` | 89 |  |
| ✅ | `memcmp` | `string.h` | 87 |  |
| ✅ | `memmove` | `string.h` | 86 |  |
| ❌ | `atan` | `math.h` | 82 |  |
| ❌ | `system` | `stdlib.h` | 81 |  |
| ❌ | `sscanf` | `stdio.h` | 71 |  |
| ✅ | `calloc` | `stdlib.h` | 70 |  |
| ❌ | `getc` | `stdio.h` | 70 |  |
| ❌ | `getchar` | `stdio.h` | 69 |  |
| ❌ | `time` | `time.h` | 65 |  |
| ❌ | `fseek` | `stdio.h` | 64 |  |
| ❌ | `strerror` | `string.h` | 55 |  |
| ❌ | `toupper` | `ctype.h` | 54 |  |
| ❌ | `ftell` | `stdio.h` | 51 |  |
| ✅ | `strstr` | `string.h` | 48 |  |
| ❌ | `pow` | `math.h` | 47 |  |
| ✅ | `memchr` | `string.h` | 45 |  |
| ❌ | `cosh` | `math.h` | 42 |  |
| ❌ | `log10` | `math.h` | 41 |  |
| ❌ | `tan` | `math.h` | 40 |  |
| ❌ | `sinh` | `math.h` | 36 |  |
| ❌ | `asin` | `math.h` | 35 |  |
| ❌ | `gets` | `stdio.h` | 34 |  |
| ✅ | `isupper` | `ctype.h` | 34 |  |
| ✅ | `strtok` | `string.h` | 34 |  |
| ✅ | `fread` | `stdio.h` | 33 |  |
| ✅ | `isalpha` | `ctype.h` | 33 |  |
| ❌ | `ungetc` | `stdio.h` | 33 |  |
| ❌ | `floor` | `math.h` | 32 |  |
| ✅ | `fwrite` | `stdio.h` | 32 |  |
| ❌ | `acos` | `math.h` | 30 |  |
| ❌ | `tanh` | `math.h` | 29 |  |
| ❌ | `atexit` | `stdlib.h` | 28 |  |
| ✅ | `feof` | `stdio.h` | 28 |  |
| ❌ | `rename` | `stdio.h` | 27 |  |
| ✅ | `strncat` | `string.h` | 27 |  |
| ✅ | `ferror` | `stdio.h` | 26 |  |
| ✅ | `isxdigit` | `ctype.h` | 26 |  |
| ❌ | `setlocale` | `locale.h` | 26 |  |
| ✅ | `strrchr` | `string.h` | 26 |  |
| ❌ | `mbtowc` | `stdlib.h` | 25 |  |
| ✅ | `abs` | `stdlib.h` | 24 |  |
| ❌ | `atan2` | `math.h` | 24 |  |
| ✅ | `isprint` | `ctype.h` | 23 |  |
| ❌ | `frexp` | `math.h` | 22 |  |
| ❌ | `qsort` | `stdlib.h` | 22 |  |
| ❌ | `fmod` | `math.h` | 21 |  |
| ❌ | `longjmp` | `setjmp.h` | 21 |  |
| ✅ | `strpbrk` | `string.h` | 20 |  |
| ✅ | `strspn` | `string.h` | 20 |  |
| ❌ | `ceil` | `math.h` | 19 |  |
| ❌ | `clock` | `time.h` | 19 |  |
| ❌ | `raise` | `signal.h` | 19 |  |
| ❌ | `ldexp` | `math.h` | 18 |  |
| ❌ | `srand` | `stdlib.h` | 18 |  |
| ❌ | `atol` | `stdlib.h` | 17 |  |
| ❌ | `localtime` | `time.h` | 17 |  |
| ❌ | `vfprintf` | `stdio.h` | 17 |  |
| ✅ | `islower` | `ctype.h` | 16 |  |
| ❌ | `rewind` | `stdio.h` | 16 |  |
| ✅ | `isalnum` | `ctype.h` | 15 |  |
| ❌ | `mktime` | `time.h` | 15 |  |
| ❌ | `rand` | `stdlib.h` | 15 |  |
| ❌ | `modf` | `math.h` | 14 |  |
| ❌ | `freopen` | `stdio.h` | 13 |  |
| ❌ | `vprintf` | `stdio.h` | 13 |  |
| ❌ | `ctime` | `time.h` | 12 |  |
| ❌ | `remove` | `stdio.h` | 12 |  |
| ✅ | `strcspn` | `string.h` | 12 |  |
| ✅ | `strtol` | `stdlib.h` | 12 |  |
| ❌ | `strtoul` | `stdlib.h` | 12 |  |
| ❌ | `atof` | `stdlib.h` | 10 |  |
| ❌ | `vsprintf` | `stdio.h` | 10 |  |
| ❌ | `div` | `stdlib.h` | 9 |  |
| ❌ | `fgetc` | `stdio.h` | 9 |  |
| ❌ | `setbuf` | `stdio.h` | 8 |  |
| ❌ | `strftime` | `time.h` | 8 |  |
| ❌ | `strtod` | `stdlib.h` | 8 |  |
| ❌ | `asctime` | `time.h` | 7 |  |
| ❌ | `scanf` | `stdio.h` | 7 |  |
| ❌ | `setvbuf` | `stdio.h` | 7 |  |
| ❌ | `tmpfile` | `stdio.h` | 7 |  |
| ❌ | `wctomb` | `stdlib.h` | 7 |  |
| ✅ | `iscntrl` | `ctype.h` | 6 |  |
| ❌ | `mbstowcs` | `stdlib.h` | 6 |  |
| ❌ | `strcoll` | `string.h` | 6 |  |
| ❌ | `strxfrm` | `string.h` | 6 |  |
| ❌ | `tmpnam` | `stdio.h` | 6 |  |
| ❌ | `clearerr` | `stdio.h` | 5 |  |
| ❌ | `fscanf` | `stdio.h` | 5 |  |
| ❌ | `gmtime` | `time.h` | 5 |  |
| ✅ | `ispunct` | `ctype.h` | 5 |  |
| ❌ | `localeconv` | `locale.h` | 5 |  |
| ✅ | `isgraph` | `ctype.h` | 4 |  |
| ❌ | `ldiv` | `stdlib.h` | 4 |  |
| ❌ | `bsearch` | `stdlib.h` | 3 |  |
| ❌ | `fgetpos` | `stdio.h` | 3 |  |
| ❌ | `fsetpos` | `stdio.h` | 3 |  |
| ❌ | `labs` | `stdlib.h` | 3 |  |
| ❌ | `wcstombs` | `stdlib.h` | 3 |  |
| ❌ | `difftime` | `time.h` | 2 |  |
| ❌ | `mblen` | `stdlib.h` | 2 |  |

## Wie diese Datei aktuell gehalten wird

`tools/qclib_status.py` regeneriert sie: dekodiert `clib.l` frisch, liest die oeffentlichen Labels aus `src/*.a`, zaehlt den MWOS-Korpus erneut und schreibt diese Datei neu. Bei jeder neuen Funktion in `src/*.a` (oder jedem entfernten Symbol) also einfach erneut laufen lassen, statt die Tabelle von Hand nachzufuehren.
