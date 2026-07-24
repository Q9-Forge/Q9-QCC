# Projektstatus

Stand: **2026-07-24 (Nachtrag: Speicherbedarf verkleinert)**

## Wichtig für eine neue Sitzung (auch mit anderer KI)

An diesem Projekt arbeiten nicht immer dieselbe KI/Session. Deshalb **vor** dieser
Datei zusätzlich prüfen:

- `git status` und `git branch -a` im Repo-Wurzelverzeichnis -- Arbeit kann auf
  einem noch nicht gemergten Branch liegen, unabhängig davon was hier steht.
- `gh pr list` -- offene Pull Requests, die noch Review/Merge brauchen.

**Stand 2026-07-23, Sitzungsende:** `main` ist sauber, alle 11 PRs dieser
Sitzung gemergt (#1--#11), kein offener PR, `./runtests.sh` komplett grün.
Kein Zwischenstand liegt irgendwo uncommittet -- eine neue Sitzung kann direkt
mit einer neuen Aufgabe starten.

### Q9/OS-9-Ausführbarkeit (neues Untersuchungsfeld, 2026-07-23)

Parallel zur Sprachfeature-Arbeit wurde geprüft, ob der Weg zur echten
Ausführung auf der Zielplattform (Q9-Emulator, OS-9/68k) funktioniert:

- Ein triviales, mit der echten Microware-`xcc`-Toolchain kompiliertes
  C-Programm läuft nachweislich auf Q9 (ToolShed-Transfer, Modul-Format,
  Ausführung -- alles bestätigt funktionsfähig).
- Der **komplette EBNF-Generator** (`ebnf.cpp`+`codegen.cpp`) kompiliert und
  linkt inzwischen ebenfalls erfolgreich mit `xcc` zu einem validen OS-9-
  Modul. Voraussetzung dafür (dauerhaft im Repo, PR #11): alle C++-Templates
  aus `Source/msvc_compat.h` entfernt (waren der einzige Ort im ganzen
  Projekt mit Templates; `xcc`s Template-Engine stürzte dabei ab).
- Der Programmstart auf dem echten Q9-System scheiterte zunächst: Datensegment
  ~34,6 MB, das System hat nur 16 MB RAM (14 MB frei). Ursache: bewusst nur
  feste globale Puffer (keine dynamische Speicherverwaltung), aber großzügig
  für einen modernen Mac dimensioniert.
- **Behoben (2026-07-24):** Der weit überwiegende Teil (~32 MB) waren zwei
  feste Tabellen in `codegen.cpp` für die ACTION/ROUTINE-Aktionsschnittstelle
  (`routinesC`/`routines68k`, je 256 Slots à 64 KB Text, unabhängig davon ob
  überhaupt Aktionen benutzt werden). Weder Anzahl noch Länge der Routinen
  lässt sich für eine beliebige Grammatik sinnvoll im Voraus festlegen --
  gelöst über `malloc`/`realloc`-Verdopplung (klein anfangen, bei Bedarf
  wachsen) statt fester Arrays. Host-Datensegment damit von ~34,6 MB auf
  ~1 MB gesunken (`size build/ebnf`), passt jetzt auch für ein 8-MB-System.
  Bestätigt: die Microware-`stdlib.h` stellt `malloc`/`realloc`/`free` bereit,
  betrifft also nur Tiny-C als Sprache fürs spätere Selfhosting, nicht die
  OS-9-Zielplattform (siehe `docs/SELFHOSTING_LUECKENLISTE.md`, neue Zeile
  "malloc/realloc/free"). `./runtests.sh` komplett grün nach der Umstellung.
- **Noch offen / nächster Schritt hierzu:** Der xcc-Build+Ausführungstest auf
  dem echten Q9-Emulator wurde mit dieser Änderung noch NICHT wiederholt
  (letzter xcc-Test war vor der Umstellung) -- das ist der nächste konkrete
  Schritt, um den 8/16-MB-Erfolg auch auf der Zielplattform zu bestätigen.
  Der vollständige, reproduzierbare xcc-Ablauf (Env-Setup, Kommentarform-
  Konvertierung, `xcc`-Aufruf, ToolShed-Transfer) steht in der Memory-Datei
  `q9-xcc-toolchain-milestone.md`.

Diese Untersuchung ist inhaltlich unabhängig von der Tiny-C-Sprachfeature-
Arbeit unten und betrifft ausschließlich den Generator selbst, nicht Tiny-C.

## Kurzfassung

Der EBNF-Generator erzeugt aus Grammatiken Parsercode. Als vollständiger
Referenzpfad ist Tiny-C verfügbar:

`Data/tinyc.ebnf` → generierter Parser → Stack-IR → TinyVM / 68000 / ARM64

Alle derzeitigen Regressionstests sind erfolgreich.

## Aktuell implementiert

- Ganzzahl-, unsigned-, char-, bool- und Nullwerte
- Funktionen mit Parametern, Rückgabewerten, Verschachtelung und Rekursion
- lokale/globale Variablen und eindimensionale Arrays
- arithmetische, relationale, logische und bitweise Operatoren
- Kurzschlussauswertung von `&&` und `||`
- ternärer Operator `?:`
- Zuweisungen und zusammengesetzte Zuweisungen
- Kontrollfluss: `if/else`, `while`, `for`, `do/while`, `break`, `continue`
- `typedef` (Skalar-/Pointer-Aliase, `typedef struct Name Alias;`, sowie
  `typedef struct { ... } Name;` mit anonymem struct inline)
- `struct` mit gemischten skalaren Feldtypen (echtes Byte-Layout mit natürlichem
  Alignment, Feldzugriff lesend/schreibend, lokale Variablen) inkl. Array-Feldern
  (z. B. `char name[8]`, Zugriff nur über eine Pointer-Zwischenvariable, direkte
  `p.field[i]`-Syntax noch offen); Pointer-Felder und verschachtelte structs noch
  offen, siehe SELFHOSTING_LUECKENLISTE.md
- `enum` (benannte int-Konstanten)
- `sizeof` (int/char/bool/unsigned/struct, keine Pointer)
- Prä-/Postinkrement `++`/`--` (einfache int/unsigned/char-Skalare)
- `switch`/`case`/`default` (gestapelte Case-Label, kein Fallthrough mit Code zwischen Bodies)
- Casts `(int)`/`(unsigned int)`/`(char)`/`(bool)` (kein Pointer-/typedef-Ziel)
- `sizeof(variable)` zusaetzlich zu `sizeof(Typ)` (Skalare und Arrays, lokal+global)
- `enum Name` als Typ (Deklaration, Parameter, Rueckgabetyp), bleibt intern `int`
- Pointer: Deklaration, `&`, `*`, `p[i]`, Pointerparameter/-rückgabe,
  Pointervergleich, skalierte Arithmetik und Pointerdifferenz
- `const` bei globalen/lokalen Variablen, Arrays und Parametern (Skalar-/Array-
  Bindung wird gegen Zuweisung/++/-- geschützt) inkl. Pointee-Constness
  (`const T*`: Schreiben durch den Pointer verboten, Pointer selbst bleibt
  frei zuweisbar -- `p++`-Idiom funktioniert weiterhin), siehe docs/FORTSCHRITT.md
- `static`: bei globalen Variablen/Funktionen ein reines No-op (interne
  Verlinkung ist bei einer einzigen Übersetzungseinheit bedeutungslos); bei
  lokalen Variablen echte Aufruf-übergreifende Persistenz (als GLOBAL
  registriert, funktioniert in TinyVM, 68000 und ARM64), inkl. konstantem
  Initialisierer (Zahl/Negativ/bool-Literal). Bewusst OHNE nicht-konstanten
  Initialisierer und OHNE struct/Array in dieser Version, siehe
  docs/FORTSCHRITT.md
- `void` als Funktions-Rückgabetyp und `void *` als generischer, bidirektional
  zu jedem anderen Pointer gleicher Tiefe kompatibler Pointer (Zuweisung/
  Parameter/Rückgabe ohne Cast); `void *` selbst nicht dereferenzierbar/
  indizierbar/arithmetikfähig (sauber diagnostiziert), bare `void` bleibt
  außerhalb des Rückgabetyps verboten, siehe docs/FORTSCHRITT.md
- Zweidimensionale Arrays (`int m[2][3]`, `char names[3][4]`) bei lokalen/
  globalen Variablen -- `arr[i][j]` wird im Frontend zu einem flachen Index
  zusammengeführt (kein neuer Opcode, kein Backend-Change), flache
  Initialisierer funktionieren mit; bewusst NICHT bei struct-Feldern/
  Parametern, mehr als 2 Dimensionen oder verschachtelten Brace-
  Initialisierern, siehe docs/FORTSCHRITT.md
- `extern`-Deklarationen für nicht in Tiny-C definierte Funktionen (z. B. echte
  OS-9/Microware-`clib`-Funktionen wie `strcmp`/`printf`/`malloc`) -- Aufruf
  über die dokumentierte Microware-68K-ABI (`CALLEXT`/`CALLEXTP`: 1./2.
  Argument in `d0`/`d1`, Rest auf dem Stack, bei variadischen Funktionen wie
  `printf` alles auf dem Stack), NUR im 68000-Backend, siehe docs/FORTSCHRITT.md
- 68000-Backend: optionaler `-os9`-Ausgabemodus fuer den ECHTEN Microware-
  Assembler `r68` (`nam`/`psect`/`ends`-Rahmung, `*`-Vollkommentare,
  `align 4`/`dc.l` statt `even`/`ds.l` -- Microwares r68 kennt letztere nicht,
  empirisch via Wine/MWOS ermittelt); Default-Modus (vasm) bleibt
  unverändert/byte-identisch, siehe docs/FORTSCHRITT.md
- **Echtes Linken gegen `clib.l` (Microware-Linker `l68`) UND echte Ausführung
  auf dem echten Q9-Emulator: erledigt (2026-07-24).** `putint`/`putuint`/
  `putchar` rufen im `-os9`-Modus jetzt den echten, ungepufferten
  `_os_write`-Syscall aus `clib.l` auf (Ganzzahl->ASCII-Wandlung komplett in
  eigenem 68k-Code, kein `printf`/keine String-Literale nötig). Ein Testprogramm
  wurde mit `r68`+`l68` zu einem echten OS-9-Modul gelinkt, per ToolShed auf
  das Q9-Image kopiert und auf dem LAUFENDEN Emulator per Telnet ausgeführt --
  korrekte Ausgabe (`42`, `-7`, `X`), kein Absturz. Dabei drei eigenständige
  Linker-/ABI-Bugs gefunden und behoben (falsches Frame-Register `a6` statt
  `a5`, `jsr` statt `bsr` für externe Aufrufe, falsche `l68`-Dateireihenfolge)
  plus einen unabhängigen, vorher nie entdeckten Bug im `%`-Operator des
  68000-Backends (`tc_mod_i32`/`tc_umod_u32` multiplizierten Quotient*Dividend
  statt Quotient*Divisor). Details siehe docs/FORTSCHRITT.md.
- TinyVM als ausführbares Testorakel
- 68000-Backend mit Simulatorprüfung
- natives ARM64/Darwin-Backend mit Runtime

## Verifikation

`./runtests.sh` meldet aktuell:

```text
111 Tiny-C-Programme korrekt
68000-Pointer-End-to-End-Test korrekt
ARM64/Darwin-Test korrekt
struct-Feldzugriff (einheitlich + gemischt + anonym im typedef + Array-Feld) 68000 + ARM64 korrekt
switch/case 68000 + ARM64 korrekt
static-Lokale-Persistenz 68000 + ARM64 korrekt
void/void* 68000 + ARM64 korrekt
2D-Array-Indizierung 68000 + ARM64 korrekt
extern-Aufruf-ABI (2 Register / 2 Register+2 Stack / variadisch) 68000 korrekt, ARM64 lehnt sauber ab
-os9-Ausgabemodus: DATA/BSS/Scratch-Puffer + CALLEXT assemblieren fehlerfrei mit dem echten r68 (via Wine)
68k signed/unsigned MUL/DIV/MOD + Fakultaet korrekt (inkl. der 2026-07-24 gefundenen %-Regression)
=== ALLE TESTS OK ===
```

Zusätzlich (nicht Teil von `./runtests.sh`, da echter Q9-Zugriff nötig): ein `-os9`-Modul mit
`putint`/`putchar` wurde mit `r68`+`l68 -a` (Reihenfolge: `cstart.r` zuerst!) gegen
`clib.l`/`os_lib.l`/`sys.l` gelinkt, per ToolShed auf `local_images/OS9SYS.hda` kopiert und
auf dem laufenden Q9-Emulator per Telnet ausgeführt -- Ausgabe `42\r-7\rX` bytegenau bestätigt.
```

## Nächster sinnvoller Schritt

Zwei unabhängige Stränge stehen zur Wahl:

1. **Sprachfeatures:** weiter ein klar abgegrenztes Feature pro Schritt.
   `const` (inkl. Pointee-Constness), `static` (inkl. konstantem
   Initialisierer), Array-Felder in `struct`, `void`/`void *`,
   zweidimensionale Arrays, `extern`-Deklarationen (inkl. Microware-ABI-
   Aufrufcodegen), der `-os9`-r68-Ausgabemodus UND echtes `l68`-Linken gegen
   `clib.l` (inkl. echter Ausführung auf dem Q9) sind seit 2026-07-24 erledigt
   (siehe oben). Naheliegende Kandidaten: String-Literale (Voraussetzung für
   `printf`-Formatstrings -- `_os_write` selbst ist bereits echt nutzbar),
   mehr als 2 Array-Dimensionen, direkte `p.field[i]`-Indizierung von
   struct-Array-Feldern (braucht kombinierte member+index-Kette in der
   Grammatik), nicht-konstanter `static`-Initialisierer (braucht einen
   Runs-once-Guard mit hidden Flag-Global, siehe docs/FORTSCHRITT.md).
2. **Q9-Ausführbarkeit:** Speicherbedarf des Generators ist bereits verkleinert
   (siehe Abschnitt oben) -- nächster Schritt ist, den xcc-Build+Ausführungstest
   auf dem echten Q9-Emulator zu wiederholen und damit zu bestätigen. Die
   Tiny-C-68k-Backend-Laufzeit-Anbindung (`putint`/`putchar` gegen `clib.l`)
   ist bereits erledigt (siehe oben); `exit`/Rückgabewert-Weitergabe an
   `F$Exit` noch nicht gesondert geprüft.

Vor jeder Sprach-Erweiterung sind Frontend, IR, TinyVM, 68000- und
ARM64-Backend sowie ein Regressionstest zu prüfen.

