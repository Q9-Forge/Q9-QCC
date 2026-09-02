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
| `make test` — 50 Regressionsfälle | **50 ok / 0 FAIL** |
| `make difftest` — 61 MWOS-SDK-Header gegen `cc -E` | **0 echte Abweichungen** (6× nur andere Abstände) |
| `Data/qcc_p.c` (282 KB Ausgabe) gegen `cc -E` | gleicher Tokenstrom, nur andere Abstände |
| QCC übersetzt die qcpp-Ausgabe | 89.763 IR-Zeilen, Schlusswort `OK`, **0 Meldungen** |
| IR-Vergleich qcpp-Weg gegen xcc-Weg | **byteidentisch** (1.123.656 Byte) |
| `make os9` — OS-9/68K-Modul | linkt: 44 KB Code, 4,2 MB Daten, 512 KB Stack |

Das byteidentische IR ist der eigentliche Befund: **qcpp ersetzt `xcc -pp` in
der Bootstrap-Kette, ohne das Ergebnis zu verändern.** Noch nicht gemessen:
qcpp *auf* echtem 68030 (Modul linkt, ist aber noch nicht im Emulator
gelaufen).

## Sprachumfang

Vollständig ISO C89 (ANSI X3.159-1989) Abschnitt 3.8:

`#define` (objekt- und funktionsartig), `#undef`, `#include` (`"…"` und
`<…>`), `#if` / `#ifdef` / `#ifndef` / `#elif` / `#else` / `#endif`,
`#line`, `#error`, `#pragma`, die Operatoren `#` und `##`, `defined`,
`__FILE__` / `__LINE__` / `__DATE__` / `__TIME__` / `__STDC__`.

Dazu:

- **`#asm` / `#endasm`** — die Microware-Erweiterung, von Anfang an dabei.
- `#warning` (verbreitete Erweiterung), `#ident` / `#sccs` (werden überlesen),
  `//`-Kommentare (in C89 nicht vorgesehen, aber 22 SDK-Dateien nutzen sie),
  und `# <zahl> "datei"` — die Zeilenmarken anderer Präprozessoren, damit sich
  Werkzeuge verketten lassen.

## Optionen

| Option | Wirkung |
|---|---|
| `-D<name>[=<wert>]` | Makro vorbelegen |
| `-U<name>` | Makro löschen |
| `-I<verzeichnis>` | Suchpfad für `#include` |
| `-ansi` | `__STDC__` auf 1 setzen |
| `-nopredef` | `_OSK`/`_UCC` nicht vorbelegen |
| `-lines` | `#line`-Marken ausgeben |
| `-min` | übersprungene Zeilen nicht mit Umbrüchen auffüllen |
| `-asm-strip` | `#asm`/`#endasm`-Marken weglassen (wie `xcc -pp`) |
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
   `__STDC__`. qcpp macht es genauso.
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

- **`#if` rechnet in `int`, nicht in `long`/`unsigned long`.** C89 3.8.1
  verlangt die größten Typen; auf dem 68k-Ziel und am Host sind `int` und
  `long` beide 32 Bit, und die Vorzeichenlosigkeit von `…u`-Konstanten wird
  nicht nachgebildet. Für die Ausdrücke, die im SDK vorkommen (`defined`,
  kleine Zahlen, `BYTE_ORDER`-Vergleiche), trägt das; ein Ausdruck, der die
  Vorzeichengrenze überschreitet, würde falsch entscheiden.
- **Kein `#define` mit `...`** (C99-Variadic) — Abbruch mit klarer Meldung.
- **`-Dx=a+b` wird als *ein* Token übernommen.** Zahlen und Namen gehen
  richtig durch, ein zusammengesetzter Wert nicht; das wäre ein zweiter
  Lexerlauf über die Kommandozeile.
- **Der Prescan eines Arguments endet an der Argumentgrenze.** Ein
  Makroaufruf, dessen `(` erst hinter dieser Grenze stünde, ist in C89
  undefiniert; qcpp bricht ab statt zu raten.
- **Trigraphen** (`??=` → `#`) fehlen. Im SDK kommt keiner vor.
- Zeilenmarken im Zusammenspiel mit `#include`: innerhalb einer Datei ist die
  Ausgabe zeilentreu, nach einem `#include` verschiebt sich die Zählung des
  einbindenden Textes. Wer die exakte Zuordnung braucht, nimmt `-lines`.

## Testen

```
make test        # 50 Fälle, Eingabe und Sollwert stehen direkt untereinander
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

Einzelne Datei prüfen:

```
./test/difftest.sh ../Data/qcc_p.c -I/Volumes/SSD1TB/projects/MWOS/SRC/DEFS
```

## Der Weg zum eigenen Bootstrap

Heute:

```
xcc -pp Data/qcc_p.c > build/qcc_p.xcc.i        # Wine + Microware
python3 tools/bootstrap_prepare.py …            # Python
```

Mit qcpp (gemessen: erzeugt byteidentisches IR):

```
q9-cpp/build/qcpp -ansi -I$MWOS/SRC/DEFS ../Data/qcc_p.c build/qcc_p.i
python3 ../tools/bootstrap_prepare.py build/qcc_p.i build/qcc_p.bootstrap.c
../build/qcc_p @build/qcc_p.bootstrap.c > build/qcc_p.ir
```

Was danach noch zwischen hier und „nur eigene Werkzeuge" steht:

1. `qcpp` im Emulator auf echtem 68030 laufen lassen (`make os9` baut das
   Modul, es ist noch nicht gefahren).
2. `bootstrap_prepare.py` ablösen — was es tut, ist im Kern das Ersetzen des
   Microware-`stdio`-Vorspanns; mit einem eigenen Präprozessor und einer
   eigenen kleinen `stdio.h` wird das Skript überflüssig.
3. `qcpp` sich selbst vorverarbeiten und von QCC übersetzen lassen (dafür ist
   die Quelle im QCC-Subset geschrieben).
