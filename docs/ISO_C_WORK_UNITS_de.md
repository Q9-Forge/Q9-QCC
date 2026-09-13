# Kleine ISO-C-Arbeitseinheiten

Diese Liste zerlegt die große [ISO-C-Lückenliste](ISO_C_GAP_LIST_de.md) in
Aufgaben, die jeweils in einem überschaubaren Arbeitsgang erledigt und getestet
werden können. Präprozessor, Bibliothek, Optimierer und zusätzliche Backends
sind dabei eigene Teilprojekte; ihre Abgrenzung steht in
[SUBPROJECTS_de.md](SUBPROJECTS_de.md). Eine Zeile ist bewusst kein Versprechen, dass
sie immer in genau einer Sitzung fertig wird; bei Bedarf wird sie in
Untereinheiten geteilt.

## ISO-Kürzel

- **C90**: ISO C90/C89-Sprachkern
- **C99**: Neuerungen aus C99
- **C11**: Neuerungen aus C11
- **C17**: im Wesentlichen Fehlerkorrekturen gegenüber C11
- **C23**: aktuelle spätere Sprachversion, zunächst bewusst separat

Die Zuordnung ist eine Planungszuordnung, keine vollständige Klauselreferenz.
Bei der Umsetzung wird die genaue Normstelle jeweils ergänzt.

## Priorisierte Arbeitseinheiten

| ID | Kleine Einheit | ISO | Abhängigkeiten | Status |
|---|---|---|---|---|
| L1 | Zeichen- und Stringliterale, Escape-Sequenzen | C90 | Lexer, globale Daten | offen / P0 |
| L2 | Integer-Suffixe und vollständige Zahlenbasen | C90/C99 | Typmodell | teilweise / P0 — Suffixe `U`, `L`, `UL`, `LU` im Frontend ergänzt; Typwirkung und vollständige Basen noch offen |
| L3 | Präprozessor: `#define` ohne Parameter | C90 | Übersetzungseinheit | offen |
| L4 | Präprozessor: `#include` und Include-Suche | C90 | L3 | offen |
| L5 | Präprozessor: bedingte Übersetzung | C90 | L3 | offen |
| L6 | Funktionsartige Makros, `#` und `##` | C99 | L3 | offen |
| T1 | Qualifizierer `const`, `volatile`, `restrict` | C90/C99 | Typmodell | offen |
| T2 | `void` und `void *` | C90 | Pointermodell | offen |
| T3 | `short`, `long`, `long long` und Rangregeln | C90/C99 | Integerkonversionen | offen |
| T4 | `float`, `double`, Literale und Grundkonversionen | C90 | T3, Backend | offen |
| T5 | `typedef` und Namensauflösung | C90 | Deklaratoren | weitgehend abgeschlossen — skalare und Pointer-Aliase sowie `typedef struct` verifiziert; vollständige Kompatibilitätsdiagnosen bleiben offen |
| T6 | `enum` und Enumerationskonstanten | C90 | T5 | offen / P0 |
| T7 | `struct`-Deklaration und Mitgliedzugriff `.` | C90 | T5, Layout | offen / P0 |
| T8 | Pointer auf `struct` und `->` | C90 | T7, Pointer | offen / P0 |
| T9 | `union` und gemeinsames Speicherlayout | C90 | T7 | offen / P0 |
| T10 | Designated Initializers | C99 | T7, Initialisierer | offen / P0 |
| T11 | Bitfelder | C90 | T7, Layout | offen / P0 |
| T12 | Variable Length Arrays | C99 | Arrays, Stack-Frames | offen |
| E1 | Prä-/Postinkrement und -dekrement | C90 | Lvalues, Zuweisung | offen |
| E2 | Cast-Ausdrücke | C90 | Typkonversionen | offen |
| E3 | Ganzzahlkonversionen und übliche arithmetische Konversionen | C90 | T3, E2 | teilweise |
| E4 | Pointerkonversionen und Kompatibilitätsprüfungen | C90 | T1/T2, E2 | teilweise |
| E5 | `sizeof` | C90 | Typgrößen, Arrays | offen |
| E6 | Kommaoperator und vollständige Lvalue-Regeln | C90 | E1/E2 | offen |
| E7 | Konstante Ausdrücke für Initialisierer und `case` | C90 | E3, Kontrollfluss | offen / P0 |
| S1 | `for`-Schleife | C90 | Labels/Jumps | offen |
| S2 | `do/while` | C90 | S1 | offen |
| S3 | `break` und `continue` | C90 | S1/S2 | offen |
| S4 | `switch`, `case`, `default` | C90 | E7, S3 | offen |
| S5 | Labels und `goto` | C90 | Kontrollfluss-IR | offen |
| S6 | Vollständige Return-Typprüfung | C90 | Funktionsprototypen | teilweise |
| F1 | Funktionsdeklarationen und Prototypen | C90 | Typmodell | teilweise |
| F2 | Kompatibilitätsprüfung von Argumenten | C90 | F1, E3/E4 | offen |
| F3 | Funktionspointer und indirekte Calls | C90 | F1, Pointer-IR | offen |
| F4 | Variadische Funktionen (`stdarg`) | C90/C99 | F2, Runtime | offen |
| O1 | Scope und Linkage lokaler Objekte | C90 | Symboltabelle | teilweise |
| O2 | `static` und `extern` innerhalb einer Datei | C90 | O1 | offen |
| O3 | Mehrere Übersetzungseinheiten und externes Linkage | C90 | O2, Objektformat | offen |
| O4 | Aggregate-Initialisierung für Arrays/Structs | C90 | T7, T10 | teilweise / P0 |
| O5 | `static`- und `const`-Initialisierer globaler Objekte | C90 | O2, O4 | offen |
| D1 | Constraint-Diagnosen und Fehlerklassen | C90 | alle Frontendtypen | teilweise |
| D2 | Implementation-defined/undefined/unspecified Verhalten dokumentieren | C90 | Semantik | offen |
| D3 | Übersetzung mehrerer Dateien reproduzierbar machen | C90 | O3, Präprozessor | offen |
| A1 | Struct-/Union-Layout im IR und QCCVM | C90 | T7/T9 | offen |
| A2 | Struct-/Union-Parameter und Rückgabewerte im 68000-ABI | C90 | A1, Backend | offen |
| A3 | Gleiches ABI im ARM64-Backend | C90 | A1/A2 | offen |
| A4 | Floating-Point-IR und ARM64-Codegen | C90 | T4 | offen |
| A5 | Linker-/Objektformat für ein erstes Zielsystem | C90 | O3, ABI | offen |
| R1 | Minimale Runtime: `memcpy`, `memset`, `memcmp` | C90 | A5 | offen |
| R2 | Minimale I/O-Runtime und `stdio`-Untermenge | C90 | R1, Zielsystem | teilweise |
| R3 | Speicherverwaltung (`malloc`/`free`) | C90 | Pointer, Runtime | offen |
| R4 | Kern-Header (`stddef`, `stdint`, `limits`, `stdbool`) | C99/C11 | Typmodell | offen |
| C11-1 | `_Static_assert` | C11 | D1 | offen |
| C11-2 | `_Alignas`, `_Alignof`, `_Atomic`-Grundsyntax | C11 | T1, Layout | offen |
| C11-3 | `_Generic` | C11 | E2, Typprüfung | offen |
| C11-4 | Thread-Storage und `<threads.h>` | C11 | O2, Runtime | offen |
| C23-1 | C23-Schlüsselwörter, Attribute und neue Deklarationsformen | C23 | C17-Kern | offen |
| C23-2 | C23-Literale und geänderte Präprozessorregeln | C23 | L1-L6 | offen |
| C23-3 | C23-Bibliothek und `nullptr`-bezogene Ergänzungen | C23 | C17-Bibliothek | offen |

## Typen als eigenes Teilprojekt

Der Bereich „Typen“ ist zu groß für einen einzelnen Arbeitsgang. Die sinnvolle
Reihenfolge ist:

Für die aktuell gewünschte Priorität gilt zunächst:

`L1/L2` → `T5/T6` → `T7/T8` → `T9` → `O4/T10` → `E7`

Danach folgt die allgemeine Typausbaustufe:

`T1/T2` → `T3` → `T4` → `T11` → `T12`

Nach jeder Einheit müssen mindestens Typprüfung, IR-Größen, QCCVM und die
betroffenen Backends geprüft werden. Erst nach `T7` ist es sinnvoll, Structs als
Basis für ABI-Arbeiten einzuplanen.

## Definition einer abgeschlossenen Einheit

Eine Einheit ist abgeschlossen, wenn Grammatik/Frontend, IR, QCCVM und die
betroffenen Backends konsistent sind, mindestens ein positiver und ein negativer
Test existieren und `./runtests.sh` weiterhin erfolgreich läuft. Bei großen
Einheiten wird der Eintrag in Untereinheiten wie `T7a`, `T7b` usw. geteilt.
