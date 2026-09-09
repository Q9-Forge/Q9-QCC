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
| Pointer und Pointerarithmetik | erledigt (Ausnahme aus Nachtrag 2026-09-08 seit 2026-09-09 behoben, s. dort) | — |
| Arrays und Array-Decay | teilweise | sehr hoch |
| Funktionspointer | offen | hoch |
| `void` und `void *` | offen | hoch |
| `struct`, `union`, `enum` | teilweise (struct mit gemischten skalaren Feldtypen erledigt 2026-07-24, `enum` erledigt; `union`, Array-/Pointer-Felder und verschachtelte structs offen) | sehr hoch |

**Nachtrag 2026-09-08 — mehrere Zeiger-Deklaratoren in EINER Anweisung
sind ein STILLER Abbruch.** `char *a, *b;` (mit oder ohne `const`, dritter
oder mehr Deklaratoren, immer dasselbe Bild) gibt `FAIL` ohne jede
Meldung — `const char *a;` (EIN Zeiger) geht, `int a, b;` (mehrere
NICHT-Zeiger-Deklaratoren) geht, nur die Kombination bricht. Gefunden
beim Bau des Peephole-Optimierers (`Source/qcc_backend_peephole.c`),
Umgehung dort: je ein eigener Deklarator pro Zeile statt einer
gemeinsamen Anweisung.

**Nachtrag 2026-09-09 — BEHOBEN, und zwar an DREI Stellen, nicht nur der
oben gefundenen.** `pointerDecl` stand in `varDecl`/`structField`/
`plainGlobalDecl` bis eben je EINMAL vor der ganzen Deklaratorliste statt
vor jedem einzelnen Deklarator — echtes C haengt den Stern an den
Deklarator, nicht an den gemeinsamen Typ. Bei Lokalen und Struct-Feldern
war das der oben beschriebene stille Parse-Fehler; bei GLOBALEN Variablen
(eigener Rohtext-Mechanismus in `tc_globalend`/`tcGlobalOne`, da
`Data/qcc_p.c` mehrere Deklaratoren pro Zeile dort schon fuer
Nicht-Zeiger-Faelle wie `static TCType a[512], b[512][64];` unterstuetzte)
war der Bug NIE ein Parse-Fehler, sondern STILL FALSCHER Code: der fuer
weitere Deklaratoren wiederverwendete Typ-Praefix schleppte den Stern des
ERSTEN Deklarators mit, `char *a, *b;` wurde zu `char **b` (Doppelzeiger)
verfaelscht, `char* a, b;` machte das eigentlich nicht-zeigende `b`
faelschlich ZUM Zeiger. Fix: `pointerDecl` jetzt Teil von
`varDeclarator`/`structDeclarator`/`globalDeclarator`; `tc_pointerdecl`
SETZT den Zeigergrad pro Aufruf neu (Basis `tcBasePointers` aus dem Typ,
z. B. einem Zeiger-`typedef`) statt ihn aufzuaddieren; `tc_globalend`
filtert Sterne beim Kopieren des wiederverwendeten Praefixes jetzt heraus
(mit erzwungenem Trenner-Leerzeichen, falls Typwort und Stern im
Originaltext ohne Leerraum aneinanderstiessen, z. B. `char** p, q;`).
Fuenf neue `tc_check`-Faelle (rein und gemischt, lokal/Struct-Feld/global)
in `runtests.sh`, volle Suite inkl. Abgleichtest weiterhin gruen.

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

**`arr[i].feld[j]`/`ptr[i].feld[j]` BEHOBEN (08.09.2026).** Structschritt
und Feldschritt in einem Ausdruck brauchten genau den zweiten
Index-Scratch, der hier vorher als offenes Vorhaben stand —
`tcStashChainedIndex`/`tcEmitStashedFieldIndex`, acht Emissionsstellen
(lokal/global x Array-von-structs/Pointer-auf-struct x lesend/
schreibend). Einzelheiten im Nachtrag weiter unten bei `s.t[i][j]`.
Weiterhin kein Aufrufer, also weiterhin nicht umgesetzt: zweidimensionale
Zeigerarrays als Feld.

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

**Nachtrag 2026-09-07 — `const` am Strukturfeld wurde geparst und
verworfen; BEHOBEN am selben Tag.** Vier Formen liefen **still** durch,
während dasselbe bei einer *Variablen* korrekt gemeldet wird:

| | vorher | jetzt |
|---|---|---|
| `s.cp[0] = …` bei `const char *cp` | still durch | `cannot assign through pointer to const` |
| `p->cp[0] = …` | still durch | dito |
| `s.n = 1` bei `const int n` | still durch | `cannot assign to const struct field` |
| `p->n = 1` | still durch | dito |

Die Grammatik nannte den Grund selbst: `fieldConstKw` war eine
**aktionslose** Kopie von `constKw`, denn ein Verweis auf `constKw` löst
`tc_const` aus, dessen `tcPendingConst` erst beim nächsten Parameter oder
Lokalen konsumiert wird — dort erzwänge es fälschlich Konstantheit. Der
Ausweg war richtig, die Folge nicht.

Jetzt hat `fieldConstKw` eine **eigene** Aktion mit einer **eigenen**
Flagge, die nur Felder betrifft: `tc_fieldconst` setzt sie, die
Deklaratoren der Zeile verbrauchen sie (`const int a, b;` trifft **beide**,
nachgemessen), und `tc_fieldconstend` löscht sie nach der ganzen
`structField`-Zeile. `tcPendingConst` bleibt unberührt.

Unterschieden wird wie bei Variablen (`tc_local`): bei einem **Zeiger**
macht `const` den *Pointee* konstant (`pointeeConst` im Feldtyp, geprüft in
`tcEmitPtrFieldIndex` und nur beim Schreiben), bei allem anderen das
**Feld** selbst (`tcStructFieldConst`, geprüft an den **sieben**
Schreibstellen in `tc_target` — nicht drei; die drei mit Index-Zweig sind
nur eine Teilmenge).

**Was ausdrücklich erlaubt bleibt** (alles nachgemessen): `s.cp = b` — nur
der Pointee ist konstant, der Zeiger selbst nicht; Lesen eines
const-Felds; Lesen *durch* ein const-Zeigerfeld; das Nicht-const-Nachbar­
feld; und die nächste Zeile bzw. die nächste `struct` werden nicht
angesteckt. QCCs eigener Parser hängt an genau einer dieser Formen
(`actionLog[i].start = start` bei `const char* start`) — deshalb vor dem
Selbsthost einzeln geprüft.

**Nachtrag 2026-09-07 — ein 2D-Array als Strukturfeld scheiterte STILL.
Der Zugriff `feld[i][j]` selbst ist seit 08.09.2026 UMGESETZT** (s.
Nachtrag am Ende dieses Absatzes; die Deklaration/Meldung unten war der
erste Schritt davon).
Gewöhnliche 2D- und 3D-Arrays gehen; ein 2D-Array *im struct*
(`struct S { char t[4][8]; }`) verhielt sich so:

| Form | vorher | jetzt |
|---|---|---|
| Deklaration, `sizeof` | geht | geht |
| `z = sp->t[i]` (Zeiger auf Zeile i) | geht | geht |
| `s.t[i]` über den Punkt | gemeldet („only supported via `->`") | unverändert |
| **`s.t[i][j]` / `sp->t[i][j]`** | **stilles `FAIL`, keine Meldung** | **gemeldet** |

Die Ursache war die Grammatik: `member [ index ]` — also genau **ein**
optionaler Index nach dem Feld. Zwei Indizes konnte sie nicht lesen, und
ein Parse-Abbruch hat keine Meldung. Jetzt steht dort
`member [ index { index } ]`, **nicht** um die Form zu können, sondern um
sie ablehnen zu können: `chained indexing of a struct field (field[i][j])
not supported in this version`.

**Umgesetzt ist sie weiterhin nicht** — dafür bräuchte es zwei Indizes in
*einem* Ausdruck, also einen zweiten Index-Scratch nach dem Muster von
`tcEmitPointerIndexChain`. Das ist **dieselbe Maschinerie**, die auch
`arr[i].feld[j]` braucht: ein Umbau würde beide Lücken schließen.

**Nachtrag 2026-09-08 — UMGESETZT.** Der Umbau von oben schließt jetzt
beide Lücken: `feld[i][j]` (zweidimensionales Array-Feld, sechs
Emissionsstellen — Punkt lokal/global, Pfeil) und `arr[i].feld[j]`/
`ptr[i].feld[j]` (eindimensionales Array-Feld hinter einer
Array-von-structs- bzw. Pointer-Indizierung, acht Emissionsstellen).
Kern: der ZWEITE (zuletzt gepushte) Indexwert wird mit
`tcStashChainedIndex()` in einen Scratch-Global zwischengelagert,
während dazwischenliegender Code (Feld- bzw. Array-Elementadresse) den
ERSTEN konsumiert — dieselbe Technik wie `tcEmitPointerIndexChain`
(Scratch-Global statt Stack-Rotation, die die IR nicht kennt), hier auf
genau einen gemerkten Wert vereinfacht, weil an beiden Stellen nie mehr
als zwei Indexebenen vorkommen können (ein Feld ist höchstens 2D,
structs schachteln nicht). Zwei neue Hilfsfunktionen tragen die Regel:
`tcEmitFieldRowColIndex` (2D-Feld, Zeilen- dann Spaltenschritt) und
`tcEmitStashedFieldIndex` (1D-Feld hinter arr[i]/ptr[i]). Verifiziert
über QCCVM mit echten Werten (lesend und schreibend, lokal/global, alle
acht bzw. sechs Stellen), volle Regressionssuite grün, BEIDE
Selbsthost-Fixpunkte (r68/l68 und qr68/ql68/qclib) unverändert
byteidentisch nach der Änderung an `tc_varref`/`tc_target`.

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
aber nicht in einer `extern`-Deklaration. BEHOBEN 2026-09-08.** Gemessen:

| | vorher | jetzt |
|---|---|---|
| `extern int f(void); f();` | `wrong argument count (expected 1, got 0)` | geht |
| `extern int f(); f();` | geht | geht |
| `int f(void){ … }` (Definition) | geht | geht |
| `typedef int (*fp)(void); fp p; p();` | dieselbe Luecke | geht |

Ursache: `type` (in `externParam`) schliesst `"void"` als eigenen Typ ein,
also las `(void)` als EIN Parameter vom Typ `void`. `funcParams` kannte das
Problem schon und loeste es mit zwei echten NTS-Alternativen
(`voidParams | normalParams`, Klammern JEWEILS im Zweig, s. Kommentar dort
zur Backtracking-Falle bei `"void* p"`). `externDecl` und `fnPtrTypedef`
teilten sich bis dahin nur `externParamList` OHNE diese Absicherung — beide
haengen an derselben Regel und hatten deshalb dieselbe Luecke (Geschwister-
Suche, s. `feedback_schrittweiten`). Fix: neue `externParams`/`fnPtrParams`
mit `voidParams | externRealParams`, `voidParams` dabei aus `funcParams`
WIEDERVERWENDET (deren Aktion ist bereits leer). Sechs neue Faelle in
`runtests.sh` (Q9-Parsec), inkl. der `void*`-Backtracking-Probe.

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

