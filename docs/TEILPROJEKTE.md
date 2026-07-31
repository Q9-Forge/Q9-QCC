# Teilprojekte und Prioritäten

Der Weg zu einem ISO-C-System wird in getrennte Teilprojekte aufgeteilt. So
bleibt der Sprachkern überschaubar und Präprozessor, Bibliothek, Optimierung und
Zielsysteme können unabhängig wachsen.

## Hauptprojekt: C-Sprachkern

Dieses Projekt enthält Grammatik, semantisches Typmodell, IR, QCCVM und die
bereits vorhandenen Ziel-Backends nur soweit sie zum Testen des Sprachkerns
notwendig sind.

### Priorität P0: zuerst bearbeiten

1. Stringkonstanten und Stringliterale
2. Zeichen-, Integer- und sonstige Literale einschließlich Konstantenausdrücken
3. `typedef` und die zugehörige Namensauflösung
4. `enum` und Enumerationskonstanten
5. `struct` mit Layout und Mitgliedzugriff
6. Pointer auf `struct` und `->`
7. `union` und gemeinsames Speicherlayout
8. Aggregate- und Designated-Initialisierung

Diese Reihenfolge verbindet Syntax, Typmodell, Speicherlayout und Initialisierung
in kleinen, aufeinander aufbauenden Einheiten.

### Danach im Sprachkern

- `const`, `volatile`, `restrict`
- `void` und `void *`
- weitere Integerbreiten und Konversionsregeln
- `sizeof`, Casts und vollständige Lvalue-Regeln
- `for`, `do/while`, `break`, `continue`, `switch`, `goto`
- Funktionspointer
- Funktionsprototypen (bare, ohne Rumpf), `static`-Sichtbarkeit, `extern` bei
  globalen Variablen und mehrere Übersetzungseinheiten: **erledigt für
  QCC (2026-07-25)**, siehe `docs/STATUS.md`/`docs/FORTSCHRITT.md`
  ("Mehrdatei-Übersetzung") -- live gegen echten `l68`- und `clang`/`ld`-Link
  verifiziert, bewusste Grenze: kein `#include`-Mechanismus/Header-Datei

## Eigenes Teilprojekt: Präprozessor

Der Präprozessor wird als eigenes Programm bzw. eigene Bibliothek vor dem
Parser ausgeführt. Er liefert eine bereinigte Übersetzungseinheit an den
Compiler und kennt keine IR- oder Backend-Details.

Geplante Stufen:

1. Tokenisierung und Zeilenfortsetzung
2. `#define` ohne Parameter
3. `#include` mit Include-Suchpfad
4. `#if`, `#ifdef`, `#ifndef`, `#else`, `#elif`, `#endif`
5. Makros mit Parametern sowie `#` und `##`
6. vordefinierte Makros und Diagnose von Präprozessorfehlern

## Eigenes Teilprojekt: C-Standardbibliothek und Header

Headerdateien und Implementierungen werden nicht in die Sprachgrammatik
eingebaut. Sie bilden ein separates Runtime-/Library-Projekt mit einer klaren
ABI-Schnittstelle zum Compiler.

Geplante Stufen:

- Basistyp-Header: `stddef.h`, `stdint.h`, `limits.h`, `stdbool.h`
- Speicher und Zeichenketten: `memcpy`, `memset`, `memcmp`, `strlen` usw.
- Ein-/Ausgabe: zunächst kleine `stdio`-Untermenge
- Speicherverwaltung: `malloc`, `calloc`, `realloc`, `free`
- danach Mathematik, Zeit, Fehlerbehandlung und weitere ISO-Header

## Eigenes Teilprojekt: Optimierer

Der Optimierer arbeitet auf einer stabilen IR und wird erst begonnen, wenn die
Semantiktests des Sprachkerns breit genug sind. Mögliche erste Pässe sind:

- konstante Faltung
- Entfernen unerreichbarer Sprünge
- lokale redundante Loads/Stores
- einfache Peephole-Optimierung pro Backend

Semantikänderungen gehören nicht in den Optimierer.

## Eigenes Teilprojekt: weitere Backends und Zielsysteme

Neue CPU-Backends, Objektformate, Linker-Anbindung und Ziel-Runtimes werden von
der Sprachsemantik getrennt geplant. 68000 und ARM64 bleiben zunächst
Test-Backends; Q9, x86-64 oder weitere Architekturen erhalten eigene Pakete.

