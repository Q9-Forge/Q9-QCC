# q9-cpp — der C-Präprozessor der Q9-Werkzeugkette

Ausführbares Programm: **`qcpp`**

```
qcpp [Optionen] <eingabe.c> <ausgabe.i>
```

## Warum es das gibt

QCC hat **keine** Präprozessorstufe — in `Data/qcc.ebnf` kommt `#` nur als
Zeichenklasse vor. Die Bootstrap-Kette hängt deshalb bis heute an zwei
Fremdteilen:

```
xcc -pp Data/qcc_p.c            (Microware, über Wine)
python3 tools/bootstrap_prepare.py  …
```

Beides steht auf dem Mac, nicht auf OS-9. Ein Image, das *ausschließlich*
eigene Werkzeuge enthält, kann es also nicht mitnehmen. `qcpp` schließt genau
diese Lücke.

## Stand (2026-09-02)

| Prüfung | Ergebnis |
|---|---|
| `make test` — 64 Regressionsfälle | **64 ok / 0 FAIL** |
| `make difftest` — 61 MWOS-SDK-Header gegen `cc -E` | **0 echte Abweichungen** (6× nur andere Abstände) |
| `Data/qcc_p.c` (282 KB Ausgabe) gegen `cc -E` | gleicher Tokenstrom, nur andere Abstände |
| QCC übersetzt die qcpp-Ausgabe | 89.763 IR-Zeilen, Schlusswort `OK`, **0 Meldungen** |
| IR-Vergleich qcpp-Weg gegen xcc-Weg | **byteidentisch** (1.123.656 Byte) |
| `make os9` — OS-9/68K-Modul | linkt: 44 KB Code, 4,2 MB Daten, 512 KB Stack |
| `tools/test_68k.sh` — Lauf auf echtem 68030 | Ausgabe **byteidentisch** zum Hostlauf |
| `tools/bootstrap.sh` — Kette ohne xcc/Wine/Python | IR **byteidentisch** zum alten Weg (1.123.692 Byte) |
| `tools/selfhost_68k.sh` — qcpp von QCC gebaut, auf 68030 | Ausgabe **byteidentisch** zum Hostlauf |
| `tools/target_chain_68k.sh` — beide Stufen **auf dem Ziel** | vorverarbeitete Quelle und IR **byteidentisch** |

Drei Befunde tragen das:

1. **Das byteidentische IR** — qcpp ersetzt `xcc -pp` in der Bootstrap-Kette,
   ohne das Ergebnis zu verändern.
2. **qcpp läuft auf echtem 68030** (Q9-Flux, OS-9-Image) und liefert dort
   byteidentisch dasselbe wie am Host. Wiederholbar über
   `tools/test_68k.sh`.
3. **Die Bootstrap-Kette braucht kein Fremdteil mehr** — kein xcc, kein Wine,
   kein Python (`tools/bootstrap.sh`), und das Ergebnis ist byteidentisch zum
   alten Weg.
4. **qcpp trägt sich selbst**: von sich selbst vorverarbeitet, von QCC
   übersetzt, auf echtem 68030 gelaufen — mit byteidentischem Ergebnis
   (`tools/selfhost_68k.sh`).
5. **Die Sprachverarbeitung läuft auf dem Ziel**: qcpp verarbeitet seine
   eigene Quelle *auf dem 68030* vor, und QCC übersetzt das dort — beide
   Ergebnisse byteidentisch zum Hostlauf (`tools/target_chain_68k.sh`).

## Sprachumfang

Vollständig ISO C89 (ANSI X3.159-1989) Abschnitt 3.8:

`#define` (objekt- und funktionsartig), `#undef`, `#include` (`"…"` und
`<…>`), `#if` / `#ifdef` / `#ifndef` / `#elif` / `#else` / `#endif`,
`#line`, `#error`, `#pragma`, die Operatoren `#` und `##`, `defined`,
`__FILE__` / `__LINE__` / `__DATE__` / `__TIME__` / `__STDC__`.

Dazu:

- **`#asm` / `#endasm`** — die Microware-Erweiterung, von Anfang an dabei.
- **`//`-Zeilenkommentare** — in C89 nicht vorgesehen, aber 22 SDK-Dateien
  nutzen sie. Getestet ist dabei auch, was leicht schiefgeht: der Kommentar
  endet an der Zeile und verschluckt keine Direktive der Folgezeile, in einer
  Zeichenkette ist `//` kein Kommentar, und im Makrorumpf gehört er nicht zum
  Rumpf.
- **`#pragma once`** wird beachtet — und nicht durchgegeben, genau wie bei
  `cc -E` (gemessen). Erkannt wird der Pfad, unter dem geladen wurde; derselbe
  Header über zwei verschiedene Pfade fällt also nicht auf. Echte
  Präprozessoren nehmen dafür Gerät und Inode, das braucht Systemaufrufe, die
  auf den beiden Zielen unterschiedlich aussehen.
- `#warning` (verbreitete Erweiterung), `#ident` / `#sccs` (werden überlesen),
  und `# <zahl> "datei"` — die Zeilenmarken anderer Präprozessoren, damit sich
  Werkzeuge verketten lassen.
- **Alle drei Zeilenendeformen**: LF (Host), CR+LF (DOS, so liegen Teile der
  SDK-Quellen), **CR (OS-9 — so liegt dort jede Textdatei)**. Vereinheitlicht
  wird zentral im Zeichenleser, der restliche Lexer kennt nur LF.

## Optionen

| Option | Wirkung |
|---|---|
| `-D<name>[=<wert>]` | Makro vorbelegen |
| `-U<name>` | Makro löschen |
| `-I<verzeichnis>` | Suchpfad für `#include` |
| `-ansi` | `__STDC__` auf 1 setzen |
| `-nopredef` | `_OSK`/`_UCC`/`_Q9`/`_Q9OS` nicht vorbelegen |
| `-lines` | `#line`-Marken ausgeben |
| `-min` | übersprungene Zeilen nicht mit Umbrüchen auffüllen |
| `-asm-strip` | `#asm`/`#endasm`-Marken weglassen (wie `xcc -pp`) |
| `-v` | gelesene und geschriebene Byteanzahl melden |
| `-fdate=<text>`, `-ftime=<text>` | Werte für `__DATE__` / `__TIME__` |

## Am Original gemessen, nicht hergeleitet

Vier Verhaltensweisen von `xcc -pp` wurden mit einer Sonde durch die echte
Toolchain bestimmt (Wine + `xcc.exe -pp`), weil sie sich aus keiner
Dokumentation ergaben:

1. **In `#asm`-Blöcken werden Makros expandiert.** `move.l #ASMVAL,d0` kam als
   `move.l #42,d0` heraus. C-Kommentare im Block verschwinden,
   `*`-Assemblerkommentare bleiben.
2. **`xcc -pp` entfernt die Marker `#asm`/`#endasm`.** qcpp *behält* sie —
   eine Ausgabe, in der Assemblertext nicht mehr als solcher erkennbar ist,
   lässt jede nachfolgende Stufe über C-Syntaxfehlern stehen statt über der
   Sache. `-asm-strip` stellt das xcc-Verhalten her.
3. **Vordefiniert sind `_OSK` und `_UCC`** — und weder `_OS9000` noch
   `__STDC__`. qcpp macht es genauso und setzt zusätzlich **`_Q9`** („mit den
   Q9-Werkzeugen übersetzt", also qcpp/QCC statt xcc/Ultra C) und **`_Q9OS`**
   („Ziel ist Q9-OS"). Beide braucht man, weil `_OSK` bei Microware genauso
   gesetzt ist und deshalb nicht zur Unterscheidung taugt. `-nopredef` nimmt
   alle vier weg.
4. `#`/`##` und die Doppelexpansion (`XSTR(VAL)` → `"42"`) verhalten sich
   normgerecht.

Zu Punkt 3 gehört eine Folgerung, die beim Bauen wichtig wird: **153
SDK-Header schalten an `__STDC__` zwischen K&R- und Prototyp-Deklarationen
um.** Ohne `-ansi` bekommt man den K&R-Zweig, den QCC nicht lesen kann — für
Läufe, deren Ausgabe an QCC geht, ist `-ansi` also die richtige Wahl.

## Bewusste Entscheidungen

- **Geschrieben im QCC-Subset.** Keine Unions, kein `->`, kein `float`, nur
  Blockkommentare, Arraygrößen als Literale, feste Tabellen statt `malloc`,
  Zeichenketten über einen internierenden Pool. Damit kann QCC diese Datei
  selbst übersetzen, sobald qcpp sich selbst vorverarbeitet.
  **Keine zweite Fassung in „richtigem" C oder C++** — die Aufteilung
  `qcc_backend.cpp` / `qcc_backend_c.cpp` hat dieses Projekt schon einmal 233
  Zeilen unbemerkte Divergenz gekostet.
- **Ergebnis in eine Datei, Diagnosen auf die Standardausgabe** (wie
  `qcc_backend`, nicht wie `cc -E`). Grund: `stderr` ist bei Microware C ein
  Makro auf ein internes stdio-Objekt (`&_niob[2]`) und kein linkbares Symbol
  — genau das, was `bootstrap_prepare.py` bis heute wieder herausoperieren
  muss. Ohne `stderr` entfällt das.
- **Meldungsformat wie bei QCC:** `qcpp: <datei>:<zeile>: <text>`.
- **An Modellgrenzen wird abgebrochen, nicht geraten.** Volle Tabelle,
  unbekannte Direktive, `##`-Ergebnis, das kein einzelnes Token ist: Abbruch
  mit Meldung. Die Arraygrößen stehen doppelt (Literal in der Deklaration,
  Variable für die Prüfung, weil QCCs `constSize` nur Zahlen kennt) —
  `selfCheck()` vergleicht beides beim Start per `sizeof`, damit die
  Verdopplung nicht auseinanderlaufen kann.
- **Zeilentreue statt kompakter Ausgabe.** Übersprungene Zeilen (Direktiven,
  ausgeschaltete `#if`-Zweige) werden durch genau so viele Umbrüche ersetzt.
  Damit zeigen die Meldungen der nächsten Stufe auf dieselbe Zeile wie in der
  Quelle — nach der Diagnosen-Überarbeitung in QCC (`9d192cc`) ist das mehr
  wert als ein paar Kilobyte. `-min` schaltet es ab.
- **`__DATE__`/`__TIME__` sind fest** (`-fdate=`/`-ftime=` setzt sie). Der
  Bootstrap wird byteweise mit einem Referenzlauf verglichen; eine Uhr in der
  Ausgabe würde diesen Vergleich unbrauchbar machen.

## Grenzen (bekannt und bewusst)

- **`#if` rechnet in 32 Bit — das ist die Zielbreite, keine Nachlässigkeit.**
  OS-9/68K hat 32-Bit-`long`, und qcpp erzeugt Text *für dieses Ziel*, also
  wird auch so entschieden. Am Host weicht `cc -E` deshalb bei Ausdrücken ab,
  die 32 Bit überschreiten: `(1 << 31) > 0` ist dort wahr, weil macOS
  64-Bit-`long` hat, und hier falsch, weil `1 << 31` in 32 Bit negativ ist.
  Beides ist für die jeweilige Breite richtig; Testfall 20d schreibt die
  Zielentscheidung fest.
  Die **Vorzeichenlosigkeit** wird mitgeführt, wie C89 3.8.1 es verlangt:
  `0xFFFFFFFF > 0` ist wahr, `0x80000000 / 2` ergibt `0x40000000`,
  `0x80000000 >> 4` ergibt `0x08000000` — und ohne vorzeichenlosen Operanden
  bleibt es vorzeichenbehaftet (`-7 / 2 == -3`, `-8 >> 1 == -4`). Bis zum
  2026-09-03 war das falsch: es wurde durchgehend vorzeichenbehaftet
  gerechnet.
- **Kein `#define` mit `...`** (C99-Variadic) — Abbruch mit klarer Meldung.
- **`-Dx=a+b` wird als *ein* Token übernommen.** Zahlen und Namen gehen
  richtig durch, ein zusammengesetzter Wert nicht; das wäre ein zweiter
  Lexerlauf über die Kommandozeile.
- **Der Prescan eines Arguments endet an der Argumentgrenze.** Ein
  Makroaufruf, dessen `(` erst hinter dieser Grenze stünde, ist in C89
  undefiniert; qcpp bricht ab statt zu raten.
- **Trigraphen** (`??=` → `#`) fehlen. Im SDK kommt keiner vor.
- **Tiefe Schachtelung bricht mit Meldung ab, nicht mit Absturz.** Bei
  `F(F(F(…)))` füllt zuerst der Argumentspeicher (das rohe Argument wird auf
  jeder Ebene erneut abgelegt, also quadratisch); für Formen mit winzigen
  Argumenten greift zusätzlich eine Tiefengrenze. Ohne die wäre der C-Stack
  die Grenze — auf dem Ziel mit 512 KB Stack zuerst.
- Zeilenmarken im Zusammenspiel mit `#include`: innerhalb einer Datei ist die
  Ausgabe zeilentreu, nach einem `#include` verschiebt sich die Zählung des
  einbindenden Textes. Wer die exakte Zuordnung braucht, nimmt `-lines`.

## Testen

```
make test        # 64 Fälle, Eingabe und Sollwert stehen direkt untereinander
make difftest    # Differenztest gegen cc -E über die MWOS-SDK-Header
make check       # beides
```

Die beiden Suiten prüfen bewusst Verschiedenes: `run_tests.sh` prüft, was beim
Schreiben *bedacht* wurde (samt aller Abbruchfälle), `difftest.sh` prüft an
echtem Material, was *nicht* bedacht wurde — und braucht dafür kein Orakel,
das erst selbst richtig sein müsste. Verglichen wird der Tokenstrom, nicht die
Formatierung: zwei normgerechte Präprozessoren dürfen anders umbrechen.
Abweichungen, die nur die Abstände betreffen, werden getrennt gezählt und
gemeldet, gelten aber nicht als Fehler.

Der Differenztest bindet jeden Header **zweimal** ein. Das ist keine
Kosmetik: vorher band er ihn genau einmal ein und konnte damit strukturell
nicht sehen, dass `#pragma once` nicht beachtet wurde — der Fehler saß hinter
einer Prüfung, die ihn nicht sehen konnte.

Einzelne Datei prüfen:

```
./test/difftest.sh ../Data/qcc_p.c -I/Volumes/SSD1TB/projects/MWOS/SRC/DEFS
```

## Selbsthost

```
./tools/selfhost_68k.sh
```

```
Stufe 0  qcpp (Host)      src/qcpp.c        ->  qcpp.self.c
Stufe 1  QCC (Host)       @qcpp.self.c      ->  self.ir      (15.437 Zeilen, OK, 0 Meldungen)
Stufe 2  qcc_backend -> r68 -> l68          ->  68k-Modul
Stufe 3  Modul im Emulator  /dd/qcpptest.c  ->  /dd/self.i
Prüfung  self.i == Hostlauf derselben Quelle (byteweise)  ->  gleich, 165 Byte
```

In dieser Kette kommt für die **Sprache** kein Fremdwerkzeug mehr vor: kein
xcc, kein Host-`cc`, kein Python. Offen bleiben `r68` und `l68` — Assembler
und Binder, keine Compiler.

### Was dabei über QCCs Subset herauskam

Zwei Konstrukte musste qcpp aufgeben, beide mit einem Zwischenzeiger statt
eines doppelten Index — und beide **gemessen**, nicht vermutet:

| Konstrukt | QCC | Ausweg |
|---|---|---|
| `punctList[q][0]` (Zeigerfeld zweimal indiziert) | `SEMERR` | `pc = punctList[q]; pc[0]` |
| `&argv[i][k]` (Adresse eines doppelten Index) | `FAIL` | `a = argv[i]; &a[k]` |

Ausdrücklich geprüft und **in Ordnung**: lesende Doppelindizes über `char**`
(`argv[i][0]`), `&a[k]` auf einem `char*`, `&global[i]` auf einem Array,
Zeigerarithmetik `a + 2`, und `tab[n++] = "x"`. Die Grenze liegt also nicht
beim Doppelindex an sich, sondern beim Zeigerfeld und bei der Adresse eines
solchen Ausdrucks.

Den Ort des Parse-Fehlers zu finden war der aufwendigere Teil: QCC meldet bei
Syntaxfehlern nur `FAIL` ohne Position. Bisektion über die Klammertiefe der
**vorverarbeiteten** Datei führte hin — nicht über die Einrückung, denn die
gibt es in qcpps Ausgabe nicht mehr, dort sieht jede Zeile wie eine
Top-Level-Zeile aus.

### Woran der Testlauf dreimal scheiterte, obwohl qcpp lief

Alle drei Fehlschläge lagen im Testgerüst, nicht im Programm — und der
letzte war ein echter Denkfehler:

1. Nach dem Absenden eines Kommandos auf den **Prompt** zu warten geht nicht:
   der steht nach dem vorigen Kommando noch im Puffer und trifft sofort. Der
   Emulator wurde dadurch beendet, bevor das Modul geladen war.
2. Ein Fehlermuster `qcpp: <irgendwas>` trifft schon auf `qcpp: g` — also
   mitten in der `-v`-Zeile. Expect nimmt das im Strom **zuerst passende**
   Muster, nicht das zuerst notierte. Jetzt an der Diagnoseform
   `datei:zeile:` geankert.
3. Dann wurde auf die Lesemeldung von `-v` gewartet — die kommt aber am
   **Anfang** des Laufs und beweist nur, dass das Modul geladen wurde. Genau
   deshalb meldet `-v` jetzt auch das Schreiben (`geschrieben: N Byte nach
   …`), und zwar nach `fclose()`: das ist ein Endesignal, das der Prompt nie
   war.

### Zwei Beobachtungen am Rand

- **Das Modul ist 4,3 MB groß**, obwohl der Code nur einige Zehntausend Byte
  ausmacht: QCCs Backend legt die genullten Tabellen in den
  **initialisierten** Datenbereich, statt sie zu reservieren. Zum Vergleich
  dasselbe Programm über xcc: 44 KB Modul mit 4,2 MB Datenbereich. Funktional
  gleichwertig, aber das Laden im Emulator dauert entsprechend.
- **ToolSheds `ident` stürzt an diesem Modul ab** (Exit 138, SIGBUS). Das
  Skript liest den Modulkopf deshalb direkt und prüft Sync `$4AFC` und
  `M$Size` gegen die Dateigröße.

## Die Kette auf dem Ziel

```
./tools/target_chain_68k.sh          # rund 20 Minuten
```

Der Unterschied zu `selfhost_68k.sh`: dort laufen Vorverarbeitung und
Übersetzung am Host, und nur das fertige Modul läuft auf dem 68030. Hier
läuft die **Sprachverarbeitung selbst** auf dem Ziel:

```
Vorbereitung (Host)  qcpp -> QCC -> Backend -> r68 -> l68   =>  zwei Module
Stufe 1  (68030)     qcpp  /dd/qcpp.c        ->  /dd/qcpp.self.c
Stufe 2  (68030)     qcc  @/dd/qcpp.self.c   ->  /dd/qcpp.ir
Prüfung  (Host)      beide Ergebnisse byteweise gegen den Hostlauf
```

Gemessen: Stufe 1 liefert 48.490 Byte, Stufe 2 15.840 IR-Zeilen (211.265
Byte), **beide byteidentisch** zum Hostlauf derselben Quelle. Beide Werkzeuge
im Image sind selbstgebaut — qcpp aus qcpps eigenem IR (4,3 MB Modul), QCC aus
QCCs eigenem IR (867 KB Modul, 1 MB Stack).

Was in dieser Kette **noch nicht** auf dem Ziel läuft: `qcc_backend`, `r68`
und `l68` — also der Weg vom IR zum Modul. Der Präprozessor und das
Compiler-Frontend sind dort angekommen, die Codeerzeugung und das Binden
nicht.

Auch hier war der Fehlschlag im Testgerüst und nicht im Programm, und diesmal
besonders lehrreich: das Endesignal `echo STUFE2_FERTIG` stand **wörtlich in
der abgesendeten Kommandozeile**, und das Terminal echot sie. Expect traf also
den eigenen Befehl, beendete den Emulator, bevor QCC gerechnet hatte — und
die IR-Datei fehlte danach. Ein von Hand gefahrener Lauf davor war nur
zufällig gutgegangen, weil ein `dir`-Kommando dahinter QCC die Zeit gab. Das
Skript räumt das Echo jetzt ausdrücklich ab, bevor es auf die Ausgabe wartet.

## Der Lauf auf echtem 68030

```
./tools/build_os9.sh          # Modul bauen
./tools/test_68k.sh           # bauen, ins Image, Emulator, byteweise vergleichen
```

`test_68k.sh` klont das Ausgangsimage (`cp -c`, CoW und damit kostenlos),
legt Modul und Prüfquelle per ToolShed hinein, fährt den Emulator über
`test/run_68k.exp`, holt die Ausgabe wieder heraus und vergleicht sie
**byteweise** mit dem Hostlauf derselben Quelle. Der Vergleich ist der Punkt:
dass ein Modul im Emulator ohne Absturz durchläuft, sagt für sich genommen
wenig.

Zwei Dinge hat erst dieser Lauf gezeigt — beide hätte kein Hosttest gefunden:

- **OS-9 beendet Textzeilen mit CR.** Ohne CR-Behandlung war die ganze Datei
  *eine* Zeile: qcpp las die Eingabe vollständig (590 Byte) und gab **nichts**
  aus, weil das erste `#define` den gesamten Rest als Makrorumpf schluckte.
  Das ist der Grund für die zentrale Umbruchnormalisierung, und dafür, dass
  drei Fälle in der Hostsuite jetzt mit CR- und CR+LF-Eingaben laufen.
- **Ein einzelnes `fread` über 2 MB kam auf OS-9 mit 0 zurück.** Jetzt wird in
  4-KB-Häppchen gelesen; `-v` meldet die gelesene Byteanzahl, damit sich so
  etwas auf dem Ziel selbst beantworten lässt statt per Neubau.

Dazu eine Falle im Testgerüst, die einen *erfolgreichen* Lauf als Fehlschlag
gemeldet hat: das übliche Prompt-Muster aus Raute-oder-Dollar am Pufferende
trifft auf jede Raute, die gerade am Puffernde steht — und
Präprozessorausgabe ist voll davon (`move.l #40,d0`, `#asm`). `run_68k.exp`
ankert deshalb am echten Prompt.

## Bootstrap ohne Fremdteile

```
./tools/bootstrap.sh
```

Vorher brauchte die Kette zwei Dinge, die es auf OS-9 nicht gibt:

```
xcc -pp Data/qcc_p.c > build/qcc_p.xcc.i        # Microware, über Wine
python3 tools/bootstrap_prepare.py …            # Header-Vorspann wegschneiden
```

Jetzt:

```
q9-cpp/build/qcpp -Iq9-cpp/include Data/qcc_p.c build/qcc_p.q9.c
build/qcc_p @build/qcc_p.q9.c > build/qcc_p.q9.ir
```

Das Skript prüft dabei nicht nur, dass QCC durchläuft (89.769 IR-Zeilen,
Schlusswort `OK`, 0 Meldungen), sondern vergleicht das IR **byteweise** mit
dem alten Weg, solange dessen Referenz (`build/qcc_p.bootstrap.ir`) noch
herumliegt. Ergebnis: gleich, 1.123.692 Byte.

### Was das Python-Werkzeug getan hat und warum es entfällt

| Aufgabe von `bootstrap_prepare.py` | Warum sie wegfällt |
|---|---|
| expandierten SDK-Header-Vorspann wegschneiden | eigene Header in `include/` |
| `stderr` von `(&_niob[2])` bzw. `__stderrp` auf ein normales Symbol bringen | eigenes `stdio.h` deklariert es |
| Apples `__builtin___sprintf_chk` zurückbauen | ohne Apple-Header kommt es nicht vor |
| Leerzeichen um `.` entfernen (xcc setzt sie bei Makroexpansion, 128 Stellen) | qcpp setzt dort keine |
| `((void)0);` aus `QCC_OUTPUT_FLUSH()` löschen | der Generator gibt jetzt `(void)0` aus |

Der letzte Punkt war der einzige, der eine Änderung außerhalb von q9-cpp
brauchte: **QCCs Anweisungsliste kennt keine allgemeine Ausdrucksanweisung**,
sondern nur `voidCastStmt = "(" "void" ")" expr ";"` — `((void)0);` ist
gültiges C, das QCC mit `FAIL` ablehnt. Die drei Aufrufstellen von
`QCC_OUTPUT_FLUSH()` sind reine Anweisungen in einem Block, die äußeren
Klammern tragen dort nichts; deshalb gibt `genParserC` (Q9-Parsec
`Source/codegen.cpp`) jetzt `(void)0` aus. Nebenwirkung, die die Byteidentität
erst hergestellt hat: das Python-Skript entfernte diese Anweisungen nur in
der geklammerten Form, hielt also *weniger* IR als der neue Weg — seit der
Änderung behalten beide Wege sie, und das IR ist gleich.

Die eigenen Header sind bewusst die Schnittmenge, die der Bootstrap braucht
(`stddef.h`: `size_t`; `stdio.h`: `FILE`, `stderr` und acht Funktionen;
`string.h`: `strlen`, `strchr`, `strncmp` — nachgezählt in `Data/qcc_p.c`).
Sie werden absichtlich **nicht** vorsorglich erweitert: das IR wird byteweise
verglichen, und jede zusätzliche Deklaration ist eine Änderung an der Eingabe
des Compilers.

Noch am Python-Werkzeug hängt nur die Diagnosevariante
(`bootstrap_prepare.py --diag`), die `stderr` in `main` auf einen echten
Strom legt — das ist eine Code-Einfügung, die ein Header nicht leisten kann.

Was zwischen hier und „nur eigene Werkzeuge" noch steht:

1. ~~`qcpp` auf echtem 68030 laufen lassen~~ — erledigt, byteidentisch zum
   Hostlauf (`tools/test_68k.sh`).
2. ~~`bootstrap_prepare.py` ablösen~~ — erledigt für den Hauptweg
   (`tools/bootstrap.sh`), offen bleibt nur `--diag`.
3. ~~`qcpp` sich selbst vorverarbeiten und von QCC übersetzen lassen~~ —
   erledigt, mit Lauf auf echtem 68030 (`tools/selfhost_68k.sh`).
4. `r68`/`l68` sind die letzten Fremdteile der Kette — Assembler und Binder.
5. Der Präprozessor auf dem Ziel ist damit da, der Kernel fehlt noch — das
   ist der andere Zweig des Ziels „Image mit ausschließlich eigenen
   Werkzeugen".
