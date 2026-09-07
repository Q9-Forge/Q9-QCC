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

QCC deckt bisher nur einen kleinen, ausführbaren Kern von Bereich 1 ab.

## Legende

- **erledigt**: im aktuellen QCC-Pfad implementiert und getestet
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
| `struct`, `union`, `enum` | teilweise (struct mit gemischten skalaren Feldtypen erledigt 2026-07-24, `enum` erledigt; `union`, Array-/Pointer-Felder und verschachtelte structs offen) | sehr hoch |

**Nachtrag 2026-09-07 — Zeigerarrays als Strukturfeld gehen jetzt.**
`char* args[6]` in einer Struct war bis dahin abgelehnt („pointer arrays as
struct field not supported in this version"). Gebraucht hat es
`qcc_backend_c.cpp`: seine `Instr`-Struktur hält die Argumente einer
IR-Anweisung so, mit 106 Zugriffsstellen. Solange das fehlte, war das
Backend nicht selbst übersetzbar und konnte deshalb **nie auf dem 68030
laufen** — das einzige Glied der Kette mit dieser Lücke.

Der Kern war die **Schrittweite**: `IPADD` skaliert mit der Größe des
Typtags, und ein Zeiger ist auf dem 68k vier Byte — im Struct belegt er
aber acht (`TC_PTR_SLOT`, damit dasselbe Offset auch für ARM64 stimmt).
Für Zeigerarrays wird der Schritt deshalb in **Byte** angegeben
(`IPADDN 8`), dasselbe Mittel, das die 2D-Zeilen schon nutzten. Die Regel
steht in *einer* Funktion (`tcEmitFieldIndexStep`) statt an den sechs
Emissionsstellen — drei lesend, drei schreibend.

**Nachtrag 2026-09-07, zweite Runde — Indizierung *durch* ein skalares
Zeigerfeld geht jetzt auch** (`s.ptr[j]`, `sp->ptr[j]`, lesend und
schreibend, lokal und global). Vorher an sechs Emissionsstellen abgelehnt.
Der Kern: die Aufrufer haben nur die **Adresse des Feldes** auf dem
Stapel, gebraucht wird aber der Zeiger *im* Feld — also erst `LOADIND p`,
dann der Indexschritt. Die Regel steht in `tcEmitPtrFieldIndex`, einer
Funktion für alle sechs Stellen.

**Eine siebte Stelle bleibt abgelehnt:** nach einem Funktionsaufruf
(`f().feld[i]`) gilt eine andere Stapelordnung — dort steht `PADD` mit
vertauschten Operanden statt `IPADD` —, und dafür gibt es keinen Aufrufer.
Eine zweite Ordnung nebenher wäre die nächste Fehlerquelle.

**Weiterhin abgelehnt und sauber gemeldet** (die Kette braucht es nicht,
und eine Meldung ist besser als eine falsche Schrittweite):
`arr[i].feld[j]` — Structschritt und Feldschritt in *einem* Ausdruck; das
braucht einen zweiten Index-Scratch wie `tcEmitPointerIndexChain` und ist
damit ein eigenes Vorhaben. Ebenso zweidimensionale Zeigerarrays als Feld
(kein Aufrufer).

**Nachtrag 2026-09-07 — ein STILLER Falschcode-Fehler bei `&arr[i]`,
behoben.** `tc_addressref` emittierte für die Adresse eines
Struct-Array-Elements `PTRINDEX` mit dem Typtag, und `tcTypeTag` gibt für
eine Struct `'i'` — also **vier Byte Schrittweite** statt der
Strukturgröße. Bei einer 80 Byte großen Struct zeigte `&arr[1]` vier Byte
hinter `arr[0]`. Aufgefallen ist es erst am Backend auf dem 68030: es holt
seine Anweisungen mit `insP = &ir[irCount]`, und alle landeten
übereinander — sichtbar als ein `op`-Feld `FUNCLOADPUSHCMPLJZ` aus je vier
Zeichen. Im Testbestand kam **kein einziges `&arr[i]` auf ein
Struct-Array** vor; jetzt prüfen es die Fälle 27 und 28 in
`tools/test_struct_68k.sh`, und ihre Sollwerte diskriminieren (mit falscher
Schrittweite bleiben sie auf 0).

**Nachtrag 2026-09-07 — ein zweiter stiller Falschcode-Fehler, behoben:**
eine ganze Struct über einen **Zeiger** zu kopieren
(`struct S *p; v = p[i];`) erzeugte `LOADP / PTRINDEX i / LOADIND i` —
vier Byte Schrittweite *und* ein `LOADIND`, wo die Adresse gebraucht wird;
damit landete ein Datenwert als Quelladresse in A0. Dieselbe Bauform war
für den ARRAY-Fall (`v = arr[i]`) am 2026-09-01 repariert worden, der
Zeigerfall blieb dabei stehen — gefunden erst, als beim Nachmessen der
`&arr[i]`-Sache auch die Nachbarstellen geprüft wurden. Jetzt
`LOADP / IPADDN <Größe>` ohne `LOADIND`: bei einer Struct **ist** die
Adresse der Wert. Fall 35 in `tools/test_struct_68k.sh`.

**Lehre daraus, für die nächste Änderung dieser Art:** eine Regel über
Schrittweiten gilt nie nur an *einer* Stelle. Beim `&arr[i]`-Fund waren es
zwei Emissionsstellen, bei den Zeigerarrays sechs, bei der Indizierung
durch ein Zeigerfeld wieder sechs — und der Zeigerfall von `v = p[i]` ist
1:1 der Array-Fall von vor sechs Tagen. Wer eine solche Stelle anfasst,
sucht die Geschwister mit.

**Nachtrag 2026-09-07 — `const` am Strukturfeld wird geparst und
verworfen.** Aufgefallen beim Umsetzen der Indizierung durch ein
Zeigerfeld: `struct P { const char *cp; }` mit `p.cp[0] = 'x'` läuft
**durch**, während dasselbe bei einer *Variablen* korrekt gemeldet wird
(`cannot assign through pointer to const`). Die Grammatik sagt den Grund
selbst: `fieldConstKw` ist eine **aktionslose** Kopie von `constKw`, denn
ein Verweis auf `constKw` würde `tc_const` auslösen, dessen
`tcPendingConst` erst beim nächsten Parameter oder Lokalen konsumiert wird
— dort erzwänge es fälschlich Konstantheit. Das ist also ein bewusst
gewählter Ausweg um einen Zustandsübertrag, keine Nachlässigkeit.

Eine Prüfung auf `pointeeConst` an den drei Schreibstellen stand
zwischenzeitlich im Code und war **wirkungslos**, weil das Feld die
Qualifikation nie trägt; sie ist wieder heraus und die Tatsache steht
stattdessen bei `tcEmitPtrFieldIndex`. Wer es angeht, fängt bei
`fieldConstKw` an: die Kennung müsste dort gesetzt, in `tc_structfield` je
Deklarator angewandt und am Zeilenende gelöscht werden (`const int a, b;`
soll beide treffen).

**Nachtrag 2026-09-07 — die Verschachtelungsgrenze war um vier zu knapp.**
`TC_MAX_CTRL` (vorher das Literal 64 an sechs Stellen) ist jetzt 128.
Gemessen: `qcc_backend_c.cpp` braucht **68** — seine Opcode-Verteilung ist
eine lange `else if`-Kette, und jedes Glied ist im Modell eine Ebene tiefer.
Mit 67 kippt es, mit 68 läuft es durch. Zu tief zu schachteln bleibt eine
echte Grenze *mit Meldung*.
| `typedef` | erledigt (Skalar-/Pointer-Aliase, `typedef struct Name Alias;`, `typedef struct { ... } Name;` anonym inline seit 2026-07-24) | — |
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
| Casts und implizite Konversionen | teilweise (nur int/unsigned/char/bool, kein Pointer/typedef als Cast-Ziel) | sehr hoch |
| `sizeof` und `_Alignof` | teilweise (`sizeof` auf int/char/bool/unsigned/struct, keine Pointer, kein `_Alignof`) | hoch |

**Nachtrag 2026-09-07, gemessen.** Zwei Dinge, die auseinanderzuhalten sind:

**1. Das 8-Byte-Layout für Zeigerfelder ist ABSICHT, kein Mangel.**
`Data/qcc.lextab` begründet es an der Stelle selbst (2026-07-25): ein
Zeigerfeld belegt *immer* 8 Byte, damit **ein einzelnes
frontend-berechnetes Offset für beide Backends gültig bleibt** — 68k-Zeiger
sind 4 Byte, ARM64-Zeiger 8, und dieselbe IR wird von beiden verarbeitet
(`Source/qcc_arm64_backend.cpp` ist verzeichnet). Folge: ein
`struct { int id; const char *start; const char *end; }` ist **24** Byte
groß, nicht 12, und `sizeof` liefert konsequenterweise 24. Das *nur* in
`sizeof` auf 12 zu ändern wäre schlimmer als der Status quo — ein
`n * sizeof(eintrag)` würde dann zu wenig anfordern.

Der Preis ist messbar: QCCs erzeugter Parser braucht für sein Aktions-Log
**6 291 456 statt 3 145 728 Byte** (262 144 Einträge). Aufgefallen beim
Ziellauf gegen `qclib`, wo diese Anforderung in `E$NoRAM` lief. Wer den
Speicher halbieren will, muss das **Frontend zielabhängig** machen (etwa
ein `-m32`), und dann ist die IR nicht mehr für beide Backends dieselbe.
Das ist eine Architekturentscheidung, keine Fehlerbehebung.

**2. `sizeof` auf einen Zeigertyp log SCHWEIGEND — behoben 2026-09-07.**
`sizeof(char *)` ergab `PUSH 1`, also die Größe von `char`, mit
Schlusswort `OK` und **ohne jede Meldung**. Die Unterstützung fehlt
bewusst (siehe Zeile oben), aber `tc_sizeof` *wollte* das melden — der
Zweig war nur nie erreichbar: `tc_sizeof` ruft `tc_type` für dieselbe
Spanne noch einmal auf, und `TC_SET_CURRENT` setzt `pointers` dabei auf 0
zurück, obwohl die Aktion an `pointerDecl` vorher schon gezählt hatte.
`tc_sizeof` zählt den Zeigergrad jetzt selbst. Ein
`malloc(n * sizeof(char*))` bekam vorher ein Viertel des Nötigen.

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

**Nachtrag 2026-09-07 — `(void)` als Parameterliste geht in der DEFINITION,
aber nicht in einer `extern`-Deklaration.** Gemessen:

| | |
|---|---|
| `extern int f(void); f();` | `wrong argument count (expected 1, got 0)` |
| `extern int f(); f();` | geht |
| `int f(void){ … }` (Definition) | geht |

Die Deklaration liest `(void)` also als **einen** Parameter. Aufgefallen beim
Bau einer Messsonde (`extern int *vsectbase(void);`), und es kostet genau so
lange, wie man braucht, um die leere Klammer zu probieren. Immerhin wird es
**gemeldet** und nicht verschwiegen. Die leere Klammer ist in dieser
Teilmenge die richtige Form.

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
| Stack-IR und QCCVM | erledigt | — |
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

`for`, `do/while`, `break`, `continue`, `typedef` inkl. `typedef struct { ... }
Name;` anonym inline (erledigt, 2026-07-24),
`struct` mit gemischten skalaren Feldtypen (erledigt, 2026-07-24; Array-Felder,
Pointer-Felder und verschachtelte structs noch offen, siehe
SELFHOSTING_GAP_LIST_de.md), Prä-/Postinkrement (erledigt,
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

