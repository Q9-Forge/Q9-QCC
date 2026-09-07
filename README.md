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

## Traegt die Bibliothek ein ECHTES Werkzeug?

`hello.c` beweist wenig — ein paar Zeilen Ausgabe und eine kleine Datei.
Der belastbare Test ist **qr68 selbst**: 1,19 MB Modul, gegen `qclib`
statt gegen Microwares `clib` gebunden, das auf echtem 68030 eine Quelle
assembliert. `test/qr68_68k.sh`:

```
== 1/5 qr68 gegen qclib binden ==
  ok (1197368 Byte, gegen qclib statt clib)
== 4/5 Emulator ==
  <<< QR68 LIEF AUF 68K DURCH, 1540 Byte Code >>>
== 5/5 Ergebnis vergleichen ==
  BYTEIDENTISCH (1913 Byte)
```

Hier laufen `fopen`, `fread`, `fwrite` und `fclose` über eine 153-KB-Quelle
und ein 1913-Byte-Ergebnis — **jedes falsche Byte wäre aufgefallen**. Der
Emulatorlauf kommt unverändert aus `Q9-qr68` (`test/run_68k.exp`), damit
ein Unterschied nur an der Bibliothek liegen kann.

Gebunden wird dabei mit **`ql68`** — seit es auch ferne **LEAs** über die
Sprungtabelle führt. Das war nötig, weil der Bezug auf QCCs Laufzeitanker
(`tc_extcall_tmp`) in einem 1,19-MB-Modul außer Reichweite gerät und ein
*Datenbezug* ist, kein Sprung. Damit steckt in der ganzen Kette kein
fremdes Werkzeug mehr.

## Alle drei Werkzeuge laufen auf echtem 68030

| Werkzeug | Modul | Test | Nachweis |
|---|---|---|---|
| **qr68** | 1,19 MB | `test/qr68_68k.sh` | assembliert eine 153-KB-Quelle, byteidentisch |
| **qcpp** | 4,35 MB | `test/qcpp_68k.sh` | präprozessiert, byteidentisch |
| **ql68** | 1,70 MB | `test/ql68_68k.sh` | **bindet auf dem Ziel**, byteidentisch |

Alle drei gegen `qclib` gebunden, alle drei mit `ql68` gebunden. Beim
letzten Schritt bindet `ql68` sich selbst und bindet dann auf dem 68030 ein
Programm gegen `qclib` — dieselbe Aufgabe wie am Host, dasselbe Ergebnis
Byte für Byte.

Bis dahin waren Grenzen zu weiten: drei Puffer in `qr68` (qcpps
Assemblerquelle ist 7,27 MB) und zwei in `ql68`.

### Wie ql68 überhaupt übersetzbar wurde

`ql68` scheiterte an QCC zunächst **viermal — jedes Mal mit Schlusswort
`FAIL` und ohne eine einzige Meldung**:

| Ursache | Befund |
|---|---|
| `int printf(char*, ...)` ohne `extern` | mit `extern` nimmt QCC es an, ohne nicht |
| `static char b[(32*1024*1024)]` | `constSize` kennt nur **Zahlen**, keine Ausdrücke |
| `static int x = 0x00010000;` | eine **globale** Variable verträgt kein Hex-Literal (dezimal geht) |
| mehrzeilige Meldungstexte | **Zeichenkettenverkettung** — die bekannteste QCC-Grenze überhaupt |

Dass QCC dabei **schweigt**, ist der eigentliche Befund. Gefunden wurden
alle vier durch Bisektion über das Präprozessat — wobei der erste Anlauf
falsch war, weil er mitten in Funktionen schnitt und deshalb überall `FAIL`
sah. Erst das Schneiden **an Funktionsgrenzen** hat gestimmt.

Nebenbei bekam `ql68` dadurch einen `_Q9OS`-Zweig mit Zielmaßen (512 KB
statt 32 MB): QCCs Backend legt genullte Felder in den *initialisierten*
Datenbereich, ein 32-MB-Puffer wäre also ein 32-MB-Modul.

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

## QCC selbst läuft gegen qclib — die Kette ist frei von fremden Teilen

**2026-09-07.** `test/qcc_68k.sh`: QCCs eigener Parser, gebunden mit
`qr68` + `ql68` gegen `qclib` — **ohne Wine, ohne `clib.l`, ohne
`os_lib.l`, ohne `sys.l`** — übersetzt auf echtem 68030 seinen eigenen
Quelltext zu **byteidentischem IR** (89 769 Zeilen, 1 123 692 Byte),
Schlusswort `OK`, null Fehlermeldungen. Modul 859 682 Byte.

Bis hierher band QCCs Selbsthost-Test (`Q9-QCC/tools/test_selfhost_68k.sh`)
über Wine mit `r68` und `l68` gegen Microwares drei Bibliotheken. **Er ist
das letzte Fremdteil gewesen**; qr68, qcpp und ql68 waren seit 2026-09-06
frei.

Der Emulatorlauf ist dabei **derselbe** wie beim clib-Weg: er steht als
`Q9-QCC/test/expect/selfhost_68k.exp` und wird von beiden Prüfständen über
die Umgebung parametrisiert. Nur so kann ein Unterschied zwischen den
Ergebnissen an der Bibliothek liegen und nicht daran, dass zwei Kopien des
Laufs auseinandergelaufen sind.

### Der Zielkorpus ist voll: acht Funktionen dazu

Gemessen an `stage2.r`, nicht geschätzt — die *deklarierten* libc-Namen der
Bootstrap-Quelle sind 13, die *referenzierten* 14:

```
fprintf  fputc  fputs  sprintf  strlen  strchr  strncmp  realloc
```

`fwrite` wird von QCC gar nicht gebraucht (aber von den anderen
Werkzeugen). Damit definiert `qclib.l` **15 öffentliche Namen** — gegen
**140** in Microwares `clib.l` (mit `libgen -ln` ausgezählt: 214 definierte
Codesymbole, davon 140 ohne führenden Unterstrich). Die 333 clib-Module
sind also zu elf Prozent überhaupt gebraucht.

### Was die Formatangaben verlangten — abgezählt, nicht angenommen

An QCCs Bootstrap-Quelle: **203 `%d`, 155 `%s`, 67 `%c`, 30 `%.*s`,
7 `%ld`**. Die beiden letzten waren nicht selbstverständlich:

- **`%ld` steht in den `printf`-Aufrufen, die das IR erzeugen** — also auf
  dem byteidentischen Pfad. Die Längenangabe `l` wird übergangen, und das
  ist keine Nachlässigkeit: auf dem 68k sind `int` und `long` beide 32 Bit.
- **`%.*s`** (Genauigkeit aus dem Argument) kommt nur in Diagnosen vor,
  musste aber trotzdem stimmen.

Eine Genauigkeit an einer *Zahl* (`%.3d`, in C89 die Mindestziffernzahl)
fällt bewusst in den Durchreichzweig, statt stillschweigend zu
verschwinden — genauso eine Breitenangabe wie `%20s`.

### `stderr` ist ein Nullzeiger — und das ist eine Freiheit

QCCs Bootstrap-Quelle erklärt `stderr` als **nie zugewiesenen** statischen
Zeiger („a private null stream keeps the successful compiler path
independent of that internal stdio object") und ruft damit 217-mal
`fprintf(stderr, ...)`. Gegen clib läuft das ins Ungewisse. qclib legt
`FILE* == 0` auf **Pfad 2**, den Fehlerkanal.

**Diese Entscheidung hat sich sofort bezahlt:** der erste Ziellauf brach mit
`qcc: kein Speicher fuer Aktions-Log` ab — einer Meldung, die genau über
diesen Weg kam. Gegen clib wäre der Lauf stumm gescheitert.

### realloc: die erste Fassung war falsch, und die Messung hat es gezeigt

`realloc` ist die einzige der acht Funktionen, die echte Arbeit ist: das
Aktions-Log des erzeugten Parsers wächst durch **Verdoppeln**, der Inhalt
muss also mitwandern.

Die **erste Fassung** holte jeden neuen Block frisch mit `F$SRqMem` und gab
den alten danach zurück. Damit leben beim Umschichten kurz der alte **und**
der neue Block — Spitzenbedarf also das Dreifache. Auf dem Ziel scheiterte
das:

```
QM: 6291464 Byte angefragt, rc=237
```

`237` = `$ed` = **`E$NoRAM`** (`MWOS/SRC/DEFS/errno.h`).

Der Weg zur Ursache lief über **`test/mem68k.sh` und `test/memprobe.c`**,
eine Messsonde, die in *einem* Emulatorlauf vier Dinge feststellt: den
größten freien Block, die echte Leiter des Parsers, den größten Block
*danach* und wie weit es darüber hinaus trägt. Gemessen:

| | |
|---|---|
| RAM der Maschine | 16 MB, davon 14 348 K frei |
| größter freier Block | 14 622 720 Byte |
| Leiter des Aktions-Logs | 12 288 → 6 291 456 Byte |
| nach dem Abgeben wieder frei | 14 622 720 Byte |

Die letzte Zeile war die wichtige: sie hat **ein Leck ausgeschlossen** und
damit auf den Spitzenbedarf gezeigt. Ohne sie hätte ich geraten. Und
`groesster()` misst den größten **zusammenhängenden** Block — deshalb fällt
er um mehr als das Gehaltene, das ist Fragmentierung und kein Verlust.

**Die zweite Fassung hält eine Arena** (größter freier Block minus 2 MB
Reserve) und unterteilt sie mit einem Belegungszeiger — und der **zuletzt
ausgegebene Block wächst an der Stelle**. Damit kostet die
Verdopplungsleiter des Parsers **keine einzige Kopie und keinen
Spitzenbedarf**: 512 KB Eingabepuffer plus 6 MB Log, fertig. Das ist genau
das Muster der Kette (ein fester Puffer, ein wachsender Block).

Microwares clib macht es im Grundsatz genauso: ihr `memory.c` führt eine
eigene Segmentverwaltung (`_cmem_base`, `_cmem_segs`, `_cmem_allocp`) über
Systemspeicher, den es in großen Stücken holt — disassembliert sind dort
`TRAP $5c` (`F$SRqCMem`) und `$29` (`F$SRtMem`), also **dieselbe Quelle**
wie hier.

Was diese Fassung **nicht** kann: Speicher wieder hergeben. Es gibt keine
Freigabeliste, qclib hat kein `free` — die Kette ruft keines — und beim
Prozessende gibt OS-9 die Arena ohnehin zurück.

### `F$SRqMem`: dreifach belegt, weil hier nichts abgeleitet werden durfte

1. **Handbuch** (`68k_tech.pdf`, „F$SRqMem System Memory Request"):
   `d0.l` ein = Byteanzahl, `d0.l` aus = gewährte Anzahl, `(a2)` = Zeiger,
   Fehler über das Übertragsbit mit dem Code in `d1.w`. Mit **`-1`** in
   `d0.l` kommt der *größte freie Block* — damit lässt sich die Obergrenze
   der Maschine messen.
2. **Microwares eigener Rumpf** in `os_lib.l`, disassembliert: derselbe
   Registersatz, aber `TRAP $5c` (`F$SRqCMem`) mit der Farbe als drittem
   Argument. Das Handbuch dazu: „F$SRqMem is equivalent to a F$SRqCMem
   request with a color of 0."
3. **Der eigene Kernel** (`Q9-OS/src/kernel/q9kernel_entry.a`) dokumentiert
   für `F$SRqMem` genau diese Belegung — und kennt `$28`, nicht `$5c`.
   Deshalb steht in `os9call.a` `$28`: qclib soll auch auf dem eigenen
   System laufen.

Eine Falle steckt im dritten Argument: es liegt bei `4(a7)`, aber das
gerettete `a2` auf dem Stack schiebt es auf `8(a7)`. Wer dort `4(a7)` liest,
bekommt die Rücksprungadresse und schreibt den Zeiger dorthin.

### Nebenbefund: QCCs `sizeof` ist auf einem 32-Bit-Ziel zu groß

Beim Nachrechnen der 6 291 456 Byte fiel auf: das sind 262 144 Einträge à
**24** Byte. Ein `struct { int id; const char *start; const char *end; }`
ist auf dem 68k aber **12** Byte groß. Gemessen an QCCs eigener Ausgabe:

```c
a = sizeof(ActionLogEntry);   /* PUSH 24  -- richtig waere 12 */
b = sizeof(char *);           /* PUSH 1   -- richtig waere 4  */
```

QCC rechnet also mit acht Byte je Strukturglied und hält einen Zeiger für
ein Byte groß. Der Parser läuft damit richtig — sein Speicherbedarf ist
aber **doppelt so hoch wie nötig**. Das ist ein Befund für Q9-QCC, kein
Bibliotheksproblem, und deshalb hier nur notiert.

### Der Prüfstand von qclib

| Test | was er prüft |
|---|---|
| `test/hello68k.sh` | 14 Ausgabezeilen aller 15 Funktionen auf echtem 68030 |
| `test/vsclib.sh` | **dasselbe Programm gegen clib** im selben Emulatorlauf, zeichengleich |
| `test/qr68_68k.sh` | qr68 assembliert auf dem Ziel, byteidentisch |
| `test/qcpp_68k.sh` | qcpp präprozessiert auf dem Ziel, byteidentisch |
| `test/ql68_68k.sh` | ql68 bindet auf dem Ziel, byteidentisch |
| `test/qcc_68k.sh` | **QCC übersetzt sich auf dem Ziel, IR byteidentisch** |
| `test/mem68k.sh` | Messung, kein Soll-Ist: was die Maschine an Speicher hergibt |

Die Erwartungswerte in `hello68k.sh` sind **nicht ausgedacht**, sondern aus
dem Gegenlauf gegen clib übernommen. Das ist die Lehre aus dem
`puts`-Fehler: von Hand hingeschriebene Sollwerte können denselben
Denkfehler enthalten wie der Code.

## Zehn Funktionen mehr — für QCCs Backend

**2026-09-07.** `qcc_backend` war das einzige Glied der Kette, das nie auf
dem 68030 gelaufen ist. Was es dafür an Bibliothek brauchte, ist gemessen
und nicht geschätzt: `strcmp` 154-mal, `fputs` 141-mal, `fprintf` 126-mal,
`sprintf` 32-mal, dazu `strncpy`, `memset`, `strtok`, `strrchr`, `strtol`,
`strcat`, `memcpy`, `fgets`, `ferror`. Zehn davon fehlten.

Damit definiert `qclib.l` **25 öffentliche Namen** gegen clibs 140.

### `fgets` weicht bewusst von clib ab — gemessen, nicht entschieden

Vor der ersten Zeile Code stand die Frage, welches Byte eine Zeile beendet.
OS-9 nutzt herkömmlich CR (`$0d`), C schreibt LF (`$0a`) vor. Statt zu
wählen, hat `test/lineend68k.sh` clib gefragt: eine Datei mit
`A $0d B $0a C $0d $0a D`, gelesen mit clibs `fgets`:

```
zeile 1 laenge 2: 65 13          A + CR
zeile 2 laenge 4: 66 10 67 13    B + LF + C + CR   <- LF hat NICHT getrennt
zeile 3 laenge 2: 10 68          LF + D
```

**Microwares `fgets` trennt an `$0d`.** Das ist bei Microware in sich
stimmig — deren C bildet `n` auf CR ab. QCC bildet es auf `$0a` ab, und
alle Dateien dieser Kette entstehen damit (an `printf` nachgemessen). Also
trennt qclibs `fgets` an `$0a`, und **für diese eine Funktion ist der
Gegenlauf gegen clib kein gültiges Orakel**. Es ist stattdessen die
Host-libc: `test/fgets68k.sh` übersetzt dieselbe Quelle mit `clang` und
vergleicht byteweise — inklusive Abschneiden bei `n-1` und letzter Zeile
ohne Umbruch.

Operative Folge, die einmal Zeit gekostet hätte: **IR-Dateien mit ToolShed
`copy -r` ins Abbild bringen, nicht mit `copy -l`** — das setzt
OS-9-Zeilenenden.

### `fgets` puffert, `fread` nicht

Zeilenweise Lesen ohne Puffer wäre ein Systemaufruf je **Byte**, und das
Backend liest eine IR-Datei von über einem Megabyte — rund eine Million
`I$Read` auf einem 68030. Deshalb holt `fgets` einen Block von 1 KB und
gibt die Zeilen daraus heraus; der Puffer entsteht erst beim ersten `fgets`
und nur für die Dateien, die ihn brauchen (über `realloc`, also aus der
Arena).

`fread` bleibt unverändert ungepuffert — die Werkzeuge holen ihre Eingabe
in *einem* `fread`. Die Folge davon steht im Quelltext: `fgets` und `fread`
auf derselben Datei zu mischen geht schief, weil `fread` die schon
gepufferten Bytes überspringt. Die Kette tut es nicht.

### `ferror` brauchte eine Fehlerkennung, die es vorher nicht gab

`qcc_backend` prüft nach dem Schreiben `if (ferror(out))`. Dafür merken
sich `qf_write` und `qf_read` jetzt einen Fehlschlag je offener Datei —
vorher war ein Schreibfehler nur am kleineren Rückgabewert erkennbar, und
den prüft kaum ein Aufrufer. Das Dateiende (`E$EOF` = 211) zählt dabei
ausdrücklich **nicht** als Fehler.

### `strncpy` genau nach C89

Ist die Quelle kürzer als `n`, wird mit Nullen **aufgefüllt**; ist sie es
nicht, steht am Ende **keine** Null. Deshalb schreibt `qcc_backend` hinter
jedem `strncpy` die Null selbst. Wer sie in der Bibliothek immer setzt,
wäre bequemer und falsch — der Gegenlauf gegen clib prüft genau das
(`strncpy 120 121 0 0 0 35`: die `#` an Stelle 5 bleibt stehen).

### `strtol` kappt bei Überlauf und sagt es

Basis 2..36 und 0 (Präfix entscheidet), führender Leerraum und Vorzeichen
werden überlesen, `end` zeigt danach auf das erste nicht verbrauchte
Zeichen — ohne eine einzige Ziffer auf den **Anfang**, so verlangt es C89,
und `qcc_backend` prüft genau das. Bei Überlauf wird gekappt und weiter
gezählt, damit `end` stimmt; C89 will zusätzlich `errno = ERANGE`, und
qclib hat kein `errno` — das steht im Quelltext ausgeschrieben statt
verschwiegen.
