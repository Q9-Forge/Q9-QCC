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

## Wie printf an seine Argumente kommt

Zwei Grenzen von QCC bestimmen die Bauform, beide gemessen:

1. **QCC kann variadische Funktionen nicht definieren.** `int f(char*, ...)`
   ergibt Schlusswort `FAIL` — und zwar **still, ohne Meldung**. In der
   *Deklaration* geht `...` dagegen; deshalb kann `qr68` printf aufrufen.
2. **Die Aufrufkonvention ist Microwares**, nicht die reine Stack-Form. An
   einem echten Fünf-Argument-Aufruf gemessen, liegt beim Eintritt:

   | | |
   |---|---|
   | `d0` | Formatzeichenkette |
   | `d1` | erstes variadisches Argument |
   | `4(a7)`, `8(a7)`, … | die weiteren, aufsteigend |
   | `(a7)` | die Rücksprungadresse — **mitten darin** |

   Der Aufrufer räumt die Stack-Argumente selbst ab.

`src/printf.a` löst das in zehn Befehlen: es schreibt `d1` auf den Platz der
Rücksprungadresse und `d0` darüber. Danach hängen fmt, arg1, arg2 …
**lückenlos** zusammen, und der C-Rumpf ist ein gewöhnliches
`int printf_a(int *args)` — ohne varargs, ohne `&param`, also ganz im
QCC-Subset. Die Rücksprungadresse wird dabei über den **Stack** gerettet
und nicht in einem Register: welche Register den Aufruf überleben, wäre
eine eigene Messung, und diese Fassung braucht sie nicht.

Ausgegeben wird direkt über den Systemaufruf — `error_code
_os_write(path_id, const void*, u_int32 *count)`, wobei `count` ein
IN/OUT-**Zeiger** ist und Pfad 1 die Standardausgabe. Für den Aufruf aus C
erzeugt QCC von sich aus genau diese Konvention.

## `_iob` ist freier, als der Header vermuten lässt

`q9_cstart.a` liest `_iob` **nicht** als FILE-Feld — es schreibt im
Fehlerpfad eine Meldung als rohe Bytes hinein (`movea.l #_iob,a1` /
`adda.l a6,a1` / `mover`), und `_fcbs` zeigt darauf. Weil nur eigener Code
darauf zugreift, ist Microwares 13-Feld-Struktur (`_ptr/_base/_end/_flag/
_fd/…`, `FOPEN_MAX 32`) hier **nicht bindend**. `src/iob.a` stellt vorerst
nur den Platz bereit; sobald `fopen` dazukommt, bekommt qclib ein eigenes,
dann hier dokumentiertes Layout.

`_iobinit`, `_initarg` und `_utinit` sind bewusst **leer**. Das ist kein
Platzhalter, sondern der gemessene Bedarf: der Startcode ruft sie, aber für
die Ausgabe über `_os_write` ist nichts einzurichten.

## Stand

**Ein Programm, dessen printf-Ausgabe ganz aus qclib kommt, läuft auf
echtem 68030** (2026-09-05). `test/hello68k.sh` bindet `test/hello.c` gegen
`qclib.l` — **ohne ein einziges clib-Modul** — und prüft die Ausgabe im
Emulator:

```
Hallo Welt
42 -7 0
hex ff, Zeichen A, Prozent %
```

3 von 3 Zeilen richtig. Modul: 4054 Byte. `printf` kann `%d %i %u %x %c %s
%%`; eine unbekannte Angabe wird unverändert durchgereicht, statt still
verschluckt zu werden.

`make` baut `build/qclib.l` (3 ROFs, 5157 Byte) mit der **eigenen Kette**:
`qcpp` → `qcc_p` → `qcc_backend -os9 -part` → `qr68`. Jede Quelldatei wird
ein eigener psect und damit ein eigener ROF — so bindet der Linker nur ein,
was wirklich gebraucht wird.

## Die Systemaufrufe bringt qclib selbst mit

`os_lib.l` ist wie `clib.l` ein libgen-Archiv. Statt dessen Format
nachzubauen, liefert `src/os9call.a` die zwei gebrauchten Aufrufe selbst —
unter OS-9/68000 ist ein Systemaufruf nichts weiter als `TRAP #0` mit dem
Servicewort dahinter. Alles daran ist gemessen, nichts geraten:

| | Quelle |
|---|---|
| Trap-Muster | `Q9-QCC/runtime/os9/q9defs.d`: „Das Servicewort folgt im OS-9/68000-ABI direkt auf TRAP #0" |
| `I$Write` = `$8a`, `F$Exit` = `$06` | `MWOS/OS9/SRC/DEFS/funcs.h` (dort ausgeschrieben; in `funcs.a` sind es `do.b`-Zähler) |
| `d0` = Pfad, `d1` = Anzahl, `a0` = Puffer | aus laufendem Code abgelesen: `PrtMsg` in `q9_cstart.a` |

Die Servicenummern stehen bewusst **in der Datei** und nicht in einem `use`
auf die MWOS-Definitionen: qclib soll ohne fremden Baum übersetzbar sein.

**Damit bindet das Testprogramm gegen genau drei Dinge** — sich selbst, den
eigenen Startcode und `qclib.l`. `sys.l` wird nicht einmal mehr berührt,
`os_lib` und `clib` gar nicht:

```
== 1/4 Modul binden (NUR qclib -- kein clib, kein os_lib, kein sys.l) ==
  ok (4038 Byte)
  ok      Hallo Welt
  ok      42 -7 0
  ok      hex ff, Zeichen A, Prozent %
  3 von 3 Zeilen richtig
```

## Die Kette ist geschlossen

`test/hello68k.sh` bindet jetzt mit dem **eigenen Binder**. Damit steckt
vom Präprozessor bis zum fertigen Modul kein fremdes Werkzeug mehr darin:

```
qcpp -> qcc_p -> qcc_backend -> qr68 -> ql68   gegen qclib.l
```

```
== 1/4 Modul binden (eigener Binder, nur gegen qclib) ==
  ok (4038 Byte, gebunden mit ql68)
  ok      Hallo Welt
  ok      42 -7 0
  ok      hex ff, Zeichen A, Prozent %
  3 von 3 Zeilen richtig
```

Dass `ql68` das kann, hat diese Runde erst gebracht: es konnte
Bibliotheken bislang nur als Symbolsammlungen lesen (`sys.l` hat 1747
Globale, alle `equ`). `qclib.l` war die erste Bibliothek mit echtem Code.
Die Einzelheiten stehen im README von `Q9-ql68` — samt zweier Befunde, die
dabei herauskamen: `l68` bindet **bedarfsgesteuert** statt in
Bibliotheksreihenfolge, und der `$8000`-Datenbias ist wirklich ein
**Minus** (bei 32-Bit-Bezügen sichtbar, bei 16-Bit-Feldern nicht).

Gegengeprüft: dasselbe Programm, mit `l68` gebunden, ist **byteidentisch**.

## Der Gegenlauf gegen clib

`test/vsclib.sh` bindet **dasselbe** Programm zweimal — einmal gegen
`qclib.l` (mit `ql68`) und einmal gegen Microwares `clib.l` (mit `l68`,
denn das libgen-Archiv liest nur der) — und lässt **beide Module im selben
Emulatorlauf** nacheinander laufen. Dass es derselbe Lauf ist, schließt
aus, dass ein Unterschied bloß der Umgebung geschuldet wäre.

```
  qclib: 4034 Byte, clib: 16064 Byte
  qclib:                          clib:
      Hallo Welt                      Hallo Welt
      42 -7 0                         42 -7 0
      hex ff, Zeichen A, Prozent %    hex ff, Zeichen A, Prozent %

  *** ZEICHENGLEICH -- qclib verhaelt sich wie Microwares clib ***
```

**Gleiches Verhalten bei einem Viertel der Größe.** Der Unterschied ist
kein Kunststück, sondern die Folge dessen, was oben gemessen wurde:
Microwares `printf` zieht die Gleitkomma-Formatierung und die
Multibyte-/Kanji-Behandlung mit herein, auch wenn das Programm nur `%d`
und `%s` benutzt.

> **Eine Falle beim Bauen dieses Tests**, die fast durchgerutscht wäre: der
> erste Lauf meldete einen Unterschied — es war die eigene Marker-Zeile,
> mit der das Skript die beiden Ausgaben im Log trennt. Der Filter verglich
> ein Stück Messaufbau mit. Wer Ausgaben aus einem Terminal-Log schneidet,
> muss die Schnittmarken selbst wieder herausnehmen.

## Die Dateifunktionen

`fopen`, `fclose`, `fread`, `fwrite` und `puts` — damit ist der gemessene
Zielkorpus vollständig. Drei Dinge waren dafür zu klären:

**Die Registerkonvention der Systemaufrufe steht in keinem der
vorhandenen Handbücher.** Also am Original abgelesen: ein Testprogramm
gegen `os_lib.l` gebunden und die eingebundenen Routinen mit
`tools/dis.py` disassembliert.

| Aufruf | | Register |
|---|---|---|
| `I$Open` | `$84` | `d0.w` = Zugriffsmodus, `a0` = Name → Pfadnummer in `d0`, auf 16 Bit maskiert |
| `I$Read` | `$89` | `d0.w` = Pfad, `d1.l` = Anzahl, `a0` = Puffer → `d1` = gelesen |
| `I$Close` | `$8f` | `d0.w` = Pfad |
| `I$Create` | `$83` | wie `I$Open`, dazu die Dateirechte in `d1.w` |

Das bestätigte zugleich das schon gebaute `I$Write`. Der Fehlerpfad ist
überall gleich: Übertragsbit gesetzt, Code in `d1`.

**Ein `FILE*` ist hier die Adresse eines Tabelleneintrags**, der die
OS-9-Pfadnummer enthält. Microwares dreizehnfeldrige Struktur wäre nur
bindend, wenn fremder Code sie läse — der einzige fremde Leser wäre
Microwares eigene clib, gegen die niemand gleichzeitig bindet. Bewusst
**ungepuffert**: die Werkzeuge der Kette holen ihre Eingabe mit einem
einzigen `fread`, da trägt eine Pufferschicht nichts bei.

**Fünf Adapter statt fünf Sonderfälle.** QCC benennt eine Definition
`tc_fopen`, während der Aufrufer den nackten Namen sucht — und seine
Aufrufkonvention ist der Microware-ABI genau entgegengesetzt, weil es von
links nach rechts pusht. Statt das je Funktion nachzubilden, legt jeder
Adapter in `src/file.a` ein zusammenhängendes Argumentfeld an und
übergibt dessen Adresse; der C-Rumpf ist dann immer `qf_xxx(int *a)`.
Dieselbe Bauform wie bei printf.

### Was der Gegenlauf gefunden hat

`puts` hängte `$0d` an statt `$0a`. Der Wert war aus `q9_cstart.a`
**abgeleitet** (`move.b #CR,-1(a1)`), nicht gemessen — und falsch: auf dem
Terminal erschien `puts gehtgeschrieben 11` statt zweier Zeilen.

Durchgerutscht war das, weil der eigene Test mit `grep -F` arbeitete und
der gesuchte Text auch **mitten in einer Zeile** steht. Er prüft jetzt auf
die ganze Zeile (`grep -qxF`).

> Genau dafür gibt es den Vergleich gegen das Original: eigene
> Erwartungswerte können denselben Denkfehler enthalten wie der Code.

### Was noch fehlt
- **`printf_c.r` braucht QCCs Laufzeitkern** (`tc_udiv_u32`, `tc_umod_u32`,
  `tc_extcall_tmp`) für die Ziffernzerlegung. Ein QCC-Programm bringt ihn
  mit, deshalb löst es sich beim Binden auf; für eine Bibliothek, die auch
  ohne QCC-Programme taugt, ist das noch zu klären.
- Der Gegenlauf **gegen clib** als Vergleich steht noch aus: bisher wird die
  Ausgabe gegen erwartete Zeilen geprüft, nicht gegen die von Microwares
  printf.
- `fopen`/`fclose`/`fread`/`fwrite` — die restlichen sechs Symbole des
  Zielkorpus.
