# Projektstatus

Stand: **2026-07-24**

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
- Der Programmstart auf dem echten Q9-System scheitert aber aktuell:
  Datensegment ~34,6 MB, das System hat nur 16 MB RAM (14 MB frei). Ursache:
  bewusst nur feste globale Puffer (keine dynamische Speicherverwaltung),
  aber großzügig für einen modernen Mac dimensioniert.
- **Nächster Schritt hierzu:** Größe der großen statischen Puffer in
  `ebnf.cpp`/`codegen.cpp` analysieren und für ein 16-MB-Zielsystem
  verkleinern, ohne echte Grammatiken (Referenz: `oberon0`) zu brechen. Der
  vollständige, sofort reproduzierbare Ablauf (Env-Setup, Kommentarform-
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
- `typedef` (Skalar-/Pointer-Aliase)
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
- TinyVM als ausführbares Testorakel
- 68000-Backend mit Simulatorprüfung
- natives ARM64/Darwin-Backend mit Runtime

## Verifikation

`./runtests.sh` meldet aktuell:

```text
79 Tiny-C-Programme korrekt
68000-Pointer-End-to-End-Test korrekt
ARM64/Darwin-Test korrekt
struct-Feldzugriff (einheitlich + gemischt) 68000 + ARM64 korrekt
switch/case 68000 + ARM64 korrekt
=== ALLE TESTS OK ===
```

## Nächster sinnvoller Schritt

Zwei unabhängige Stränge stehen zur Wahl:

1. **Sprachfeatures:** weiter ein klar abgegrenztes Feature pro Schritt.
   Naheliegende Kandidaten: `static`/`const` (101+99 echte Fundstellen im
   Generator selbst, siehe `docs/SELFHOSTING_LUECKENLISTE.md`, vermutlich
   reine Grammatik-Arbeit ohne Backend-Änderung wie die meisten heutigen
   Features), `typedef struct { ... } Name;` (anonymes struct inline im
   typedef, direkter Aufsatz auf die jetzt gemischten Feldtypen), Array-Felder
   in `struct` (z. B. `char name[32]`, eigener Folgeschritt zu den seit
   2026-07-24 gemischten skalaren Feldtypen), `void *`, weitere Arrayformen.
2. **Q9-Ausführbarkeit:** Speicherbedarf des Generators für ein 16-MB-
   Zielsystem verkleinern (siehe Abschnitt oben) -- danach echte Ausführung
   auf Q9 testen, und danach das Tiny-C-68k-Backend um echte
   Laufzeit-Anbindung (`putint`/`putchar`/`exit` gegen `clib.l`) erweitern.

Vor jeder Sprach-Erweiterung sind Frontend, IR, TinyVM, 68000- und
ARM64-Backend sowie ein Regressionstest zu prüfen.

