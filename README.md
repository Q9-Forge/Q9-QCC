# Q9-qclib — die C-Bibliothek der Q9-Werkzeugkette

Ersetzt Microwares `clib` — das letzte Fremdteil der Kette. Nach
[Q9-qcpp](../Q9-QCC/q9-cpp) (Präprozessor), [Q9-qr68](../Q9-qr68)
(Assembler) und [Q9-ql68](../Q9-ql68) (Binder) fehlt nur noch die
Laufzeitbibliothek, damit ein OS-9-System entsteht, in dem kein fremdes
Werkzeug mehr steckt.

## Methode

Wie bei den Schwesterprojekten: **gemessen, nicht abgeleitet.** Der
Unterschied zu qr68 und ql68 ist allerdings grundsätzlich und bestimmt
alles Weitere:

> **Für `clib` gibt es keine Quellen.** Im SDK liegen nur die
> Binärbibliotheken (`OS9/{68000,68020,68040,CPU32}/LIB/clib.l`), die
> Header unter `SRC/DEFS` und das Handbuch. `SRC.zip` enthält
> ausschließlich `MACROS/` und `ROM/`.

Bei `qr68` und `ql68` war das fremde Werkzeug das **Orakel**: dieselbe
Eingabe hineingeben und die Ausgaben byteweise vergleichen. Das geht hier
nicht — es gibt keine gemeinsame Eingabe. Der Prüfstein ist deshalb
**Verhaltensgleichheit auf echtem 68030**: dasselbe Programm, einmal gegen
`clib` und einmal gegen `qclib` gebunden, muss dieselbe Ausgabe liefern.

## Der Zielkorpus, gemessen

Nicht geschätzt, sondern aus den ROFs der eigenen Kette gelesen (die
externen Namen eines ROF stehen im Format, das `Q9-qr68/README.md`
dokumentiert):

| ROF | externe Namen |
|---|---|
| `qr68.r` | `printf fopen fclose fread fwrite exit _os_write` |
| `q9_cstart.r` | `_initarg _iob _iobinit _utinit _os_exit exit main end` |

`main` kommt aus dem Programm, `end` setzt der Binder, und
`_os_write`/`_os_exit` sind Systemaufrufe aus `os_lib.l` — **nicht** aus
clib. Es bleiben **zehn Symbole**, die qclib liefern muss:

```
printf  fopen  fclose  fread  fwrite  exit
_iob  _iobinit  _initarg  _utinit
```

## Was daran transitiv hängt — und warum printf der Brocken ist

`l68 -m` zeigt, was wirklich eingebunden wird: **59 der 333 clib-Module**
(plus 19 aus `os_lib.l`). Die Liste erklärt, wo das Gewicht liegt:

| Gruppe | Module |
|---|---|
| stdio | `_a_fopen o_fopen fclose fread fwrite fflush ftell setvbuf _filbuf _iob iobinit _tmpfile __tidyup _tidyup cfinish` |
| printf | `o_printf vprintf _frm_new _frm_old _flg_msk setbase` |
| **Gleitkomma** (nur für `%f`/`%e`) | `dtoa tens_tbl floatcom frexp fabs _fperr` |
| **Multibyte/Kanji** (nur für `%lc`/`%ls`) | `mbtowc wctomb mbstowcs wcstombs g_mb_len kanji chcodes case` |
| Speicher | `memory.c` (10×), `ansimem` (2×), `stack stackovr` |
| Strings | `strchr strlen strings fmove isspace` |
| Start/Ende | `initarg atexit_t trapinit` |

**Ein `printf` ohne `%f` und ohne Multibyte spart über die Hälfte davon.**
Ob die Kette das braucht, ist eine Messfrage und keine Geschmacksfrage:
`qr68`, `ql68` und `qcpp` geben nur Zahlen und Zeichenketten aus.

## `clib.l` ist kein ROF-Stapel

Ein wichtiger Befund für den Binder: `sys.l` ist eine schlichte **Folge von
ROFs** (7 Stück, 1747 Globale — daran ist der eigene Parser validiert).
`clib.l` dagegen enthält **kein einziges `$DEADFACE`**. Es ist ein
**libgen-Archiv, Format 1.1**, 333 Module, erzeugt am 10.11.2001 17:37:21.

Das Orakel dafür ist **`libgen`, nicht `rdump`** — rdump kann nur „merged
ROF .l files" und meldet sonst `is not a relocatable module`. Nützlich sind
`libgen -li` (Bibliotheksinfo), `-lu` (Modulnamen), `-ln` (nm-Stil).

Der Kopf ist entschlüsselt (Handbuch `ultrac_use.pdf`, Kap. 9 „Library
Format Created by libgen", plus Nachrechnen):

| Größe | Feld |
|---|---|
| 4 | Kennung (`2d00d5bc`) |
| 2 | Formattyp |
| 6 | Datum (Jahr−1900, Monat, Tag, Stunde, Minute, Sekunde) |
| 2 | Edition |
| 4 | Offset des Bibliotheksnamens im String-Table |
| 4 | Größe der Globaldefinitions-Hashtabelle |
| 4 | Größe der Globaldefinitionen |
| 4 | Größe des String-Table |
| 4 | Größe des psect-Abschnitts |
| 4 | Größe der internen Referenzen |
| 4 | Größe der externen Referenzen |

Der Kopf endet auf `$2a`, dort beginnt die Hashtabelle. Ein
Globaldefinitions-Eintrag ist 22 Byte. Gegenprobe: das aus dem Kopf
gerechnete Datum stimmt auf die Sekunde mit `libgen -li`.

> **Daraus folgt die Bauform von `qclib.l`: eine schlichte ROF-Folge.**
> Das Handbuch erlaubt sie ausdrücklich („A simple library file can also be
> created by concatenating one or more ROFs"), `l68` **und** `ql68` lesen
> sie beide — und damit muss niemand Microwares Archivformat nachbauen.
> Zu beachten ist nur die Reihenfolge: in einer einfachen Bibliothek muss
> ein psect **vor** jedem stehen, der ihn braucht.

## Stand

Angelegt am 2026-09-05. Bisher steht die Bestandsaufnahme oben; Code
folgt.
