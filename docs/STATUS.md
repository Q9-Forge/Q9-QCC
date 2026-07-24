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
  Alignment, Feldzugriff lesend/schreibend, lokale Variablen; Array-/Pointer-Felder
  und verschachtelte structs noch offen, siehe SELFHOSTING_LUECKENLISTE.md)
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
- TinyVM als ausführbares Testorakel
- 68000-Backend mit Simulatorprüfung
- natives ARM64/Darwin-Backend mit Runtime

## Verifikation

`./runtests.sh` meldet aktuell:

```text
96 Tiny-C-Programme korrekt
68000-Pointer-End-to-End-Test korrekt
ARM64/Darwin-Test korrekt
struct-Feldzugriff (einheitlich + gemischt + anonym im typedef) 68000 + ARM64 korrekt
switch/case 68000 + ARM64 korrekt
static-Lokale-Persistenz 68000 + ARM64 korrekt
=== ALLE TESTS OK ===
```

## Nächster sinnvoller Schritt

Zwei unabhängige Stränge stehen zur Wahl:

1. **Sprachfeatures:** weiter ein klar abgegrenztes Feature pro Schritt.
   `const` (inkl. Pointee-Constness) und `static` (inkl. konstantem
   Initialisierer) sind seit 2026-07-24 erledigt (siehe oben). Naheliegende
   Kandidaten: nicht-konstanter `static`-Initialisierer (braucht einen
   Runs-once-Guard mit hidden Flag-Global, siehe docs/FORTSCHRITT.md),
   Array-Felder in `struct` (z. B. `char name[32]`, eigener Folgeschritt zu
   den seit 2026-07-24 gemischten skalaren Feldtypen), `void *`, weitere
   Arrayformen.
2. **Q9-Ausführbarkeit:** Speicherbedarf des Generators ist bereits verkleinert
   (siehe Abschnitt oben) -- nächster Schritt ist, den xcc-Build+Ausführungstest
   auf dem echten Q9-Emulator zu wiederholen und damit zu bestätigen, danach
   das Tiny-C-68k-Backend um echte Laufzeit-Anbindung (`putint`/`putchar`/
   `exit` gegen `clib.l`) erweitern.

Vor jeder Sprach-Erweiterung sind Frontend, IR, TinyVM, 68000- und
ARM64-Backend sowie ein Regressionstest zu prüfen.

