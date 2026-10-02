# qclib -- Status gegen Microwares `clib.l`

Gemessen, nicht geschaetzt: die 140 oeffentlichen Codesymbole sind aus `clib.l` (`MWOS/OS9/68020/LIB/clib.l`, libgen-Archiv Format 1.1) selbst dekodiert -- Kopf, Hashtabelle, Globaldefinitionen (22 Byte je Eintrag) und Stringtabelle nach dem in diesem README dokumentierten Format geparst, ohne das fehlende DOS-Werkzeug `libgen.exe` (kein Wine/DOSBox auf dieser Maschine). Ergebnis: **214 definierte Codesymbole, davon 140 oeffentlich, 44 Datensymbole** -- deckt sich exakt mit der fruehesten `libgen -ln`-Messung vom 07.09.2026.

**Korpus-Treffer** zaehlt Aufrufe `name(` in allen `.c`-Dateien unter `/Volumes/SSD1TB/2Q9/MWOS` -- ein Mass fuer die Prioritaet, nicht fuer den Bedarf DIESER Werkzeugkette (die braucht gemessen nur die bereits gruenen Funktionen, s. `tools/korpus.sh`).

## Status

| | Bedeutung |
|---|---|
| ✅ | umgesetzt, gegen clib bzw. Host-libc verifiziert (`tests/vsclib.sh` u.a.) |
| 🟡 | umgesetzt, aber MIT bewusster, dokumentierter Abweichung von clib |
| ❌ | fehlt noch |

**Stand: 140/140 öffentliche Symbolnamen sind in `src/*.a` vorhanden.** Das
ist Symbolabdeckung, keine Behauptung vollständiger ISO-C-/Microware-
Verhaltensgleichheit: 🟡 kennzeichnet bewusst eingeschränkte Implementierungen
oder Ersatzverhalten (z. B. fehlende Zeit-, Signal- oder Transzendental-
Services). ✅-Einträge sind die jeweils verifizierten Funktionen. Die
Korpuszahl priorisiert Nutzung, misst aber keine vollständige Konformität.

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
| ✅ | `qsort` | `stdlib.h` | 22 | bytewise insertion sort with indirect comparator calls. |
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
| ✅ | `clearerr` | `stdio.h` | 5 | resets qclib's per-stream error and EOF flags. |
| 🟡 | `fscanf` | `stdio.h` | 5 | variadic input ABI is not available in the current Q9 subset; returns failure. |
| 🟡 | `gmtime` | `time.h` | 5 | OS-9 clock conversion is not exposed; returns null. |
| ✅ | `ispunct` | `ctype.h` | 5 |  |
| 🟡 | `localeconv` | `locale.h` | 5 | locale object is not exposed; returns null. |
| ✅ | `isgraph` | `ctype.h` | 4 |  |
| 🟡 | `ldiv` | `stdlib.h` | 4 | structure-return ABI is not supported by the current Q9 subset. |
| ✅ | `bsearch` | `stdlib.h` | 3 | binary search with indirect comparator calls. |
| 🟡 | `fgetpos` | `stdio.h` | 3 | qclib has no seek-position service; returns failure. |
| 🟡 | `fsetpos` | `stdio.h` | 3 | qclib has no seek-position service; returns failure. |
| ✅ | `labs` | `stdlib.h` | 3 |  |
| ✅ | `wcstombs` | `stdlib.h` | 3 |  |
| ✅ | `difftime` | `time.h` | 2 | subtracts the two integer timestamps using the 68k FPU. |
| ✅ | `mblen` | `stdlib.h` | 2 |  |

## Wie diese Datei aktuell gehalten wird

`tools/qclib_status.py` misst Symbolnamen und Korpus-Treffer neu, überschreibt
aber derzeit auch die hier gepflegten Verhaltensbewertungen und Notizen. Vor
einer Regeneration deshalb diese Bewertungen sichern bzw. das Skript mit der
Statusliste abgleichen; nicht blind ausführen.

## `os9call.a`-Syscall-Wrapper (2026-10-02, noch nicht ins Englische übertragen)

Diese Tabelle oben deckt nur die `clib.l`-äquivalenten Standard-C-Symbole ab.
Die rohen OS-9-Syscall-Wrapper in `src/os9call.a` (direkte `TRAP #0`-Hüllen,
von Anwendungscode per `extern` genutzt, nicht Teil der 140-Symbol-C-Lib-
Oberfläche) sind hier dokumentiert, solange es keinen passenderen eigenen
Statusabschnitt dafür gibt:

| Funktion | OS-9-Aufruf | Zweck | Status |
| --- | --- | --- | --- |
| `_os_sysdbg` | F$SysDbg ($52) | Systemdebugger aufrufen | ✅ kompiliert/linkt; Laufzeitverhalten im Q9-Flux-Emulator weicht ab (löst dort einen vollständigen Emulator-Neustart aus, da kein Debugger konfiguriert ist -- Emulatoreinschränkung, kein Compiler-/qclib-Bug) |
| `_os_get_prtbl` | F$GPrDBT ($1f) | Prozesstabellen-Puffer lesen | ✅ verifiziert über Host-Pipeline |
| `_os_ev_delete` | F$Event ($53), Subfunktion 3 | Event löschen | ✅ verifiziert über Host-Pipeline |
| `_os_link` | F$Link ($00) | Modul-Link-Zähler erhöhen | ✅ verifiziert über Host-Pipeline (rettet a2/d2, Stack-Offsets +8 wegen zweier gesicherter Register) |
| `_os_gprdsc` | F$GPrDsc ($18) | Prozessdeskriptor eines PID lesen | ✅ verifiziert über Host-Pipeline |

Registerkonventionen stammen aus Microwares `68k_tech.pdf`
("OS-9 for 68K Processors Technical Manual V3.3"). Alle 5 Wrapper wurden
benötigt, um `break.c`/`events.c`/`link.c`/`procs.c` aus den Q9-Tools
(zuvor an `ql68k` mit unresolved symbols gescheitert) linkfähig zu machen;
siehe `../../docs/STATUS_de.md` Abschnitt "qclib-Syscall-Wrapper +
MAX_STRUCT_FIELDS-Fix (2026-10-02)" für den vollen Kontext.

### Fortsetzung selbe Nacht: 9 weitere Wrapper fuer die restlichen ql68k-Linkfehler

| Funktion | OS-9-Aufruf | Zweck | Status |
| --- | --- | --- | --- |
| `_getsys` | F$SetSys ($27) | Systemglobale (D_xxx-Offsets) lesen | ✅ verifiziert über Host-Pipeline |
| `attach` | I$Attach ($80) | Gerät am System anmelden | ✅ verifiziert über Host-Pipeline |
| `detach` | I$Detach ($81) | Gerät vom System abmelden | ✅ verifiziert über Host-Pipeline |
| `_os_gs_devnm` | I$GetStt SS_DevNm ($8d/$0e) | Gerätename eines Pfades lesen | ✅ verifiziert über Host-Pipeline |
| `_gs_devn` | I$GetStt SS_DevNm ($8d/$0e) | wie `_os_gs_devnm`, zweiter Name (pd.c erwartet diesen) | ✅ verifiziert über Host-Pipeline |
| `_os9_gs_free` | I$GetStt SS_Free ($8d/$43) | Freiraum auf Gerät lesen | ✅ verifiziert über Host-Pipeline |
| `_os_gs_fd` | I$GetStt SS_FD ($8d/$0f) | FD-Sektor eines Pfades lesen | ✅ verifiziert über Host-Pipeline; siehe Caveat unten |
| `_os_ss_fd` | I$SetStt SS_FD ($8e/$0f) | FD-Sektor eines Pfades zurückschreiben | ✅ verifiziert über Host-Pipeline |
| `_os_seek` | I$Seek ($88, eigener Aufruf, NICHT GetStt/SetStt) | Dateizeiger versetzen | ✅ verifiziert über Host-Pipeline |
| `q9_gblkmp` | F$GBlkMp ($19) | Freispeicher-Blockliste + Summenzähler kopieren | ✅ verifiziert über Host-Pipeline; Puffergröße 1024 Byte fest verdrahtet (passt zu `mfree.c`s `u_int32 block_map[256]`, einzigem Aufrufer) |
| `_os_gs_pos` | I$GetStt SS_Pos ($8d/$05) | aktuelle Dateiposition lesen | ✅ verifiziert über Host-Pipeline |
| `_os_gs_size` | I$GetStt SS_Size ($8d/$02) | aktuelle Dateigröße lesen | ✅ verifiziert über Host-Pipeline |

Caveat `_os_gs_fd`: `touch.c` ruft dies mit Kopiergröße 0 auf
(`_os_gs_fd(path, 0, &fd)`); laut Spezifikation kopiert der Kernel dann
buchstäblich 0 Byte, der Rest von `fd` bleibt Stack-Müll bis auf das von
`touch.c` selbst gesetzte `fd_date`. Kein Wrapper-Bug (exakt dokumentiertes
Verhalten umgesetzt), aber ein mögliches `touch.c`-eigenes Problem -- nicht
korrigiert, nur festgestellt.

Diese 9 Wrapper lösten 9 von 12 zu Sitzungsbeginn verbliebenen
`ql68k`-Linkfehlern im 46-Datei-Q9-Tools-Korpus (`attr.c` bis `what.c`).
Bewusst zurückgestellt (Stand damals): `chown.c`, `hostname.c`
(netzwerkabhängig), `printenv.c` (Compiler-Namensmangling-Bug, keine
fehlende qclib-Funktion). Volles Bild inkl. Gesamtstand (22/46) und
Umgebungs-Funde (Passwort, bash-Login, DHF-Verzeichnis-Bug) in
`../../docs/STATUS_de.md`, Abschnitt "qclib-Syscall-Wrapper +
MAX_STRUCT_FIELDS-Fix (2026-10-02)".

### `chown(3)` nachgereicht (selber Morgen)

Anders als die obigen 9 ist `chown` kein roher `os9call.a`-Trap-Wrapper,
sondern ein `extra_*.c`/`.a`-Paar wie `bsearch`/`qsort` (siehe Tabelle
oben): `src/extra_chown.c` (interne Logik `qf_chown`, nutzt die neuen
`_os_gs_fd`/`_os_ss_fd`-Wrapper zum Lesen+Zurückschreiben des FD-Sektors)
und `src/extra_chown.a` (Bridge-Stub, identisches Muster wie
`extra_bsearch.a`/`extra_qsort.a`/`extra_file.a`). `owner` ist ein
gepackter `group.user`-Int (hohes Byte Gruppe, niedriges Byte Nutzer, wie
`ql68`s `-gu=`-Option). Dafür wurde `Q9DEFS/include/rbf.h`s `fd_stats`
um benannte `fd_att`/`fd_own_group`/`fd_own_user` erweitert (vorher
opaker `_filler_0[3]`-Block, gleiche Offsets). Host-Pipeline-verifiziert,
`touch.c` unverändert (keine Regression). **Neuer Gesamtstand: 23 von 46
Dateien komplett erfolgreich.**

Echte Emulator-Bestätigung (für `chown` wie für die 9 Wrapper von oben)
bleibt weiterhin offen -- ein zweiter Anlauf lief diesmal auf einen
eigenständigen DHF-Verzeichnis-Bug (Details in `../../docs/STATUS_de.md`,
Abschnitt "Fortsetzung, selber Morgen"), nicht auf ein Problem der
Wrapper selbst. Host-Pipeline-Verifikation gilt weiterhin als
ausreichender Nachweis für diese Sitzung.
