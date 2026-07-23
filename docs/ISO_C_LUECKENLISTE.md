# ISO-C-Lückenliste und Zielplanung

Stand: **2026-07-22**

## Zieldefinition

„Kompletter ISO-Compiler“ muss zuerst auf eine Sprachversion und ein Zielsystem
festgelegt werden. Für die Planung verwenden wir zunächst **ISO C17** als
vollständige Sprachbasis. C23 wird danach als eigener Erweiterungsblock behandelt.

Ein Compiler besteht dabei aus mindestens vier getrennten Bereichen:

1. Übersetzung der C-Sprache (Frontend und Codegenerator)
2. Präprozessor und Übersetzungseinheiten
3. ABI, Linker-Anbindung und Ziel-Runtime
4. C-Standardbibliothek

Tiny-C deckt bisher nur einen kleinen, ausführbaren Kern von Bereich 1 ab.

## Legende

- **erledigt**: im aktuellen Tiny-C-Pfad implementiert und getestet
- **teilweise**: Grundfunktion vorhanden, ISO-Semantik noch unvollständig
- **offen**: noch nicht implementiert
- **separat**: gehört primär in Präprozessor, ABI oder Bibliothek

## 1. Lexik und Präprozessor

| Thema | Status | Priorität |
|---|---|---|
| Kommentare und Whitespace | erledigt | — |
| Integer-, Zeichen- und Bool-Literale | teilweise | hoch |
| String-Literale und Escape-Sequenzen | offen | hoch |
| Floating-Literale und Fließkommatypen | offen | hoch |
| vollständige Tokenregeln/Zeichensätze | teilweise | hoch |
| `#include`, Makros, bedingte Übersetzung | offen | sehr hoch |
| `#define` mit Parametern und `##`/`#` | offen | hoch |
| `#pragma`, vordefinierte Makros, Include-Suche | offen | mittel |

## 2. Typen und Deklaratoren

| Thema | Status | Priorität |
|---|---|---|
| `char`, signed/unsigned Integer | teilweise | hoch |
| `short`, `long`, `long long` | offen | hoch |
| `_Bool` und Qualifizierer | teilweise/offen | hoch |
| `float`, `double`, `long double` | offen | hoch |
| Pointer und Pointerarithmetik | erledigt | — |
| Arrays und Array-Decay | teilweise | sehr hoch |
| Funktionspointer | offen | hoch |
| `void` und `void *` | offen | hoch |
| `struct`, `union`, `enum` | teilweise (struct mit einheitlichem Feldtyp, `enum` erledigt) | sehr hoch |
| `typedef` | erledigt (Skalar-/Pointer-Aliase) | — |
| Bitfelder und `_Alignas`/`_Alignof` | offen | mittel |
| variable length arrays | offen | mittel |

## 3. Ausdrücke und Operatoren

| Thema | Status | Priorität |
|---|---|---|
| arithmetische Operatoren | erledigt | — |
| Vergleiche und Gleichheit | erledigt | — |
| `&&`, `||` mit Kurzschluss | erledigt | — |
| bitweise Operatoren und Shifts | erledigt | — |
| Zuweisungen und kombinierte Zuweisungen | erledigt | — |
| Prä-/Postinkrement und -dekrement | erledigt (nur einfache int/unsigned/char-Skalare) | — |
| Casts und implizite Konversionen | teilweise | sehr hoch |
| `sizeof` und `_Alignof` | teilweise (`sizeof` auf int/char/bool/unsigned/struct, keine Pointer, kein `_Alignof`) | hoch |
| Kommaoperator | offen | mittel |
| vollständige Constant Expressions | offen | hoch |
| Sequenzierungs- und Undefined-Behavior-Regeln | offen | sehr hoch |

## 4. Anweisungen und Funktionen

| Thema | Status | Priorität |
|---|---|---|
| Ausdrucksanweisungen, Block, `if/else`, `while` | erledigt | — |
| `for` und `do/while` | erledigt | — |
| `switch`, `case`, `default` | teilweise (gestapelte Case-Label, aber KEIN Fallthrough mit Code zwischen Bodies) | hoch |
| `break` und `continue` | erledigt | — |
| `goto` und Labels | offen | mittel |
| `return` | teilweise | hoch |
| Funktionsdefinitionen und Parameter | erledigt | — |
| Prototypen und separate Deklarationen | teilweise | sehr hoch |
| `inline`, `_Noreturn`, variadische Funktionen | offen | mittel |

## 5. Übersetzungseinheiten und Semantik

| Thema | Status | Priorität |
|---|---|---|
| lokale/globale Objekte | teilweise | sehr hoch |
| Speicherklassen `static`, `extern`, `register`, `_Thread_local` | offen | hoch |
| Sichtbarkeitsbereiche und Namensräume | teilweise | sehr hoch |
| Linkage über mehrere Dateien | offen | sehr hoch |
| Initialisierer für Aggregate | teilweise | hoch |
| konstante und nichtkonstante globale Initialisierer | offen | hoch |
| Diagnose von Constraint-Verletzungen | teilweise | sehr hoch |
| Übersetzung in mehreren Phasen | offen | hoch |
| definiertes Verhalten vs. Implementation-defined/undefined | offen | sehr hoch |

## 6. Codegenerator, ABI und Runtime

| Thema | Status | Priorität |
|---|---|---|
| Stack-IR und TinyVM | erledigt | — |
| 68000-Backend und Simulator | funktionsfähig | — |
| ARM64/Darwin-Backend | funktionsfähig | — |
| vollständiges Ziel-ABI für ein Betriebssystem | teilweise | sehr hoch |
| Register-/Stack-Calling-Convention für alle C-Typen | teilweise | sehr hoch |
| Struct-/Union-Rückgabe und -Parameter | offen | sehr hoch |
| Floating-Point-Codegen | offen | hoch |
| Linker-/Objektformat-Anbindung | offen | sehr hoch |
| Debug-Informationen | offen | niedrig |
| Optimierung und Codequalität | offen | mittel |

## 7. ISO-C-Standardbibliothek

Die Bibliothek ist ein eigenes Projektpaket und darf nicht mit dem Parserumfang
verwechselt werden. Für einen praktisch nutzbaren Compiler werden mindestens
benötigt:

- `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`, `<limits.h>`
- `<stdio.h>` und `<stdlib.h>`
- `<string.h>` und `<ctype.h>`
- `<assert.h>`, `<errno.h>`, `<locale.h>`
- `<math.h>` inklusive Floating-Point-Runtime
- `<time.h>`
- später `<signal.h>`, `<setjmp.h>`, `<threads.h>` und weitere optionale Teile

Der genaue Umfang hängt vom Zielsystem ab. Für Q9 wäre zunächst eine kleine
zielsystemspezifische Runtime sinnvoll, nicht sofort die komplette Bibliothek.

## Empfohlene Ausbaustufen

### Stufe A: brauchbares C-Subset

`for`, `do/while`, `break`, `continue`, `typedef` (erledigt, 2026-07-23),
`struct` mit einheitlichem Feldtyp (erledigt, 2026-07-23; gemischte Feldtypen
noch offen, siehe SELFHOSTING_LUECKENLISTE.md), Prä-/Postinkrement (erledigt,
2026-07-23), `sizeof` auf Basistypen/struct (erledigt, 2026-07-23; auf Pointer
weiterhin offen), noch offen: Casts, Funktionsprototypen, `enum`-Typsicherheit
(Konstanten sind erledigt, aber ohne eigenen Typ) und ein robuster Präprozessor.

### Stufe B: C17-Sprachkern

Alle Standardtypen, Konversionen, Qualifizierer, Speicherklassen, mehrere
Übersetzungseinheiten, vollständige Initialisierer, Funktionspointer und ein
festgelegtes ABI einschließlich Structs und Floating Point.

### Stufe C: Zielsystem und Bibliothek

Linker-/Objektformat, Startcode, Speicherverwaltung, I/O und schrittweise die
C-Standardbibliothek. Erst hier wird aus dem Sprachcompiler ein benutzbares
Entwicklungssystem.

### Stufe D: C23

Erst nach C17: neue C23-Schlüsselwörter und Sprachregeln, Attribute, geänderte
Deklarationsmöglichkeiten, neue Bibliotheksbestandteile und die jeweilige
Implementierungsdokumentation.

## Empfehlung für das Projekt

Wir sollten nicht „ISO komplett“ als nächsten Einzelauftrag behandeln. Der
sinnvolle nächste Plan ist ein klarer **C17-Kern ohne Bibliotheksvollständigkeit**:

1. `for`/`do`, `break`/`continue`, Inkrement
2. `typedef`, Prototypen, `struct`/`enum`
3. Casts, Konversionen, `sizeof`, Qualifizierer
4. Präprozessor und mehrere Übersetzungseinheiten
5. ABI-/Runtime-Entscheidung für ein erstes echtes Zielsystem

Nach jeder Stufe wird der Umfang neu bewertet. So bleibt sichtbar, welche Arbeit
Sprachsemantik, welche Arbeit Backend und welche Arbeit Bibliothek ist.

