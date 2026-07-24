# Selfhosting-Lückenliste

Stand: **2026-07-24 (Nachtrag: anonymes struct inline im typedef)**

## Zieldefinition

Frage: Welche Sprachmittel und Bibliotheksfunktionen braucht Tiny-C, damit die
**eigene Toolchain** (EBNF-Generator + generierter Parser) irgendwann in Tiny-C
geschrieben und von ihr selbst übersetzt werden könnte -- "Selfhosting" im
klassischen Compilerbau-Sinn?

Diese Liste ist NICHT aus dem ISO-C-Standard abgeleitet (siehe dazu
`docs/ISO_C_LUECKENLISTE.md`), sondern direkt am tatsächlichen Quellcode der
Toolchain gemessen: Es wurde durchsucht, welche C/C++-Konstrukte
`Source/ebnf.cpp`, `Source/codegen.cpp`, `Source/tiny-regex.cpp` sowie die
generierten Parser-Zwillinge (`Data/*_p.c`) wirklich verwenden. Die Liste
haken wir ab, sobald Tiny-C das jeweilige Sprachmittel beherrscht -- unabhängig
davon, ob es in der ISO-Liste eine andere Priorität hat.

## Die drei Selfhosting-Schichten

Der Datenfluss (siehe `docs/ENTWICKLERHANDBUCH.md`) hat drei sehr
unterschiedliche Bausteine, die getrennt zu betrachten sind:

| Schicht | Datei(en) | Heutiger Stil | Aufwand |
|---|---|---|---|
| **L1: generierter Parser-Zwilling** | `Data/*_p.c` (z. B. `tinyc_p.c`, 2967 Zeilen) | bereits fast reines C, wird von `codegen.cpp` per `fprintf` erzeugt | am kleinsten -- natürlicher erster Bootstrap-Schritt |
| **L2: EBNF-Generator selbst** | `Source/ebnf.cpp` (2149 Z.), `Source/codegen.cpp` (1527 Z.), `Source/tiny-regex.cpp` (494 Z.) | C-Stil-C++: keine STL, größtenteils feste globale Puffer, **seit 2026-07-24 mit gezieltem `malloc`/`realloc`/`free`** (siehe unten) | größter Brocken, aber architektonisch am nächsten an Tiny-C |
| **L3: IR-Backends** | `Source/tinyc_backend.cpp`, `Source/tinyc_arm64_backend.cpp` | modernes C++: `std::vector`/`map`/`string`/`fstream`/Exceptions | eigener, andersartiger Umbau -- siehe unten |

**Update 2026-07-24 (Q9-Speicherbedarf):** Der frühere Befund "L2 braucht keine
dynamische Speicherverwaltung" gilt nicht mehr uneingeschränkt. Ursache des
~34,6-MB-Datensegments (siehe unten) waren fast ausschließlich zwei feste
Tabellen in `codegen.cpp` (`routinesC`/`routines68k`, je `ACTION_ROUTINE_MAX=256
× ACTION_ROUTINE_LEN=65536` Byte Text ≈ 32 MB zusammen) für die ACTION/ROUTINE-
Aktionsschnittstelle -- eine Größe, die weder für ein 8/16-MB-Zielsystem noch
für sehr große Projekte auf dem Host jemals richtig dimensioniert werden kann,
weil Anzahl und Länge der Routinen pro Grammatik unbekannt sind. Gelöst über
`malloc`/`realloc`-Verdopplung (klein anfangen, bei Bedarf wachsen, siehe
`pushRoutine`/`growBuf` in `codegen.cpp`) statt fester Arrays. Bestätigt: die
Microware-`clib`/`stdlib.h` (`/Volumes/SSD1TB/projects/MWOS/SRC/DEFS/stdlib.h`)
stellt `malloc`/`realloc`/`free` bereit, betrifft also nur Tiny-C selbst als
Sprache (siehe neue Zeile in der Tabelle unten), nicht die OS-9-Zielplattform.
Alle übrigen Tabellen in L2 bleiben feste globale Arrays -- die Hürde für
Selfhosting von L2 ist dadurch etwas, aber nicht grundlegend gestiegen: Tiny-C
braucht vor einem L2-Selfhosting-Versuch zusätzlich einen `malloc`/`free`-
Laufzeit-Baustein (aktuell nicht vorhanden, siehe Tabelle).

## Legende

- **fehlt**: Tiny-C hat das Sprachmittel noch nicht (siehe `FORTSCHRITT.md`/ISO-Liste)
- **teilweise**: Grundfunktion vorhanden, für den konkreten Gebrauch hier noch nicht ausreichend
- **vorhanden**: Tiny-C kann das bereits

## 1. Sprachmittel, die der Generator selbst braucht (L2)

Alle Fundstellen wurden geprüft: Sie stehen im echten Kontrollfluss der Tools,
nicht nur innerhalb von `fprintf(fp, "...")`-Textbausteinen, die Code für L1
erzeugen.

| Sprachmittel | Belegstellen (Beispiele) | Tiny-C-Status | Priorität |
|---|---|---|---|
| `struct` (auch anonym via `typedef struct`) | `codegen.cpp:44,52,563`; `ebnf.cpp:396,483,571,789` | **teilweise** (2026-07-24: GEMISCHTE skalare Feldtypen UND `typedef struct { ... } Name;` mit anonymem struct inline im typedef jetzt moeglich, echtes Byte-Layout mit natuerlichem Alignment -- deckt Beispiele wie `{ char name[32]; TCType type; }` fuer die Skalarfelder ab (`TCType` selbst besteht nur aus `char`/`unsigned char`-Feldern). Bewusst noch offen, je eigener Folgeschritt: Array-Felder wie `char name[32]`, Pointer-Felder (68k 4 Byte vs. ARM64 8 Byte wuerde das frontend-berechnete Layout architekturabhaengig machen) und verschachtelte structs.) | sehr hoch |
| `enum` | `codegen.cpp:42` (`AstKind`), `ebnf.cpp:1165` (`BLK_NONE` etc.) | **erledigt** (2026-07-23, Nachtrag: `enum Name var;` als Deklaration moeglich, `enum Name` auch als Parameter-/Rueckgabetyp; im Speicher/Typsystem bleibt es schlicht `int`, keine eigene Typidentitaet -- entspricht C) | hoch |
| `union` | `tiny-regex.cpp:45` (anonyme Union in `regex_t`) | fehlt | mittel |
| `typedef` (auch für Structs) | durchgehend in allen drei Dateien | **erledigt** (2026-07-24: Skalar-/Pointer-Aliase, `typedef struct Name Alias;` und die im Generator gebräuchliche Form `typedef struct { ... } Name;` mit anonymem struct INLINE im typedef funktionieren jetzt alle. Der typedef-Zielname dient dabei intern als struct-Tag -- bewusste, harmlose Vereinfachung gegenüber striktem C, das dort keinen Tag kennt) | sehr hoch |
| mehrdimensionale Arrays | 26 Fundstellen, z. B. `ruleNameList[MAX_RULE_NAMES][IDENT_LEN+1]`, `dfsPath[...][...]`, `lexBlockOn[...][...]` | fehlt (Tiny-C hat nur 1D) | sehr hoch |
| `for`-Schleife | 69 echte Vorkommen (nicht mitgezählt: 2 nur in erzeugten Strings) | **erledigt** (2026-07-23) | hoch |
| `do`/`while`-Schleife | `codegen.cpp:782` (Fixpunkt-Iteration über Regel-Nullbarkeit) | **erledigt** (2026-07-23) | hoch |
| `switch`/`case` | 13 echte Vorkommen in allen drei Dateien | **erledigt fuer die reale Nutzung** (2026-07-23: alle 13 Fundstellen nutzen entweder `break` oder gestapelte leere Case-Label -- genau das unterstuetzt Tiny-C jetzt; echtes Fallthrough MIT Code zwischen Bodies fehlt, wird aber nirgends im Generator gebraucht) | hoch |
| `static` (Funktionen/lokale Variablen als Speicherklasse, nicht nur globale Objekte) | 101 echte Vorkommen, u. a. alle großen Tabellenpuffer | fehlt (nur globale Objekte) | hoch |
| `const`-Qualifizierer | 99 echte Vorkommen (meist `const char*`-Parameter) | fehlt | hoch |
| `sizeof` | `codegen.cpp:333,1151,...`; `ebnf.cpp:1043,1062` (Puffergrößen an Hilfsfunktionen reichen) | **erledigt** (2026-07-23, Nachtrag: `sizeof(variable)` auf lokale/globale Skalare und Arrays ergänzt -- genau die Form, die alle echten Fundstellen hier nutzen, z. B. `sizeof(line)`) | hoch |
| Prä-/Postinkrement (`++`/`--`) | nicht im Original-Scope dieser Liste, aber jetzt erledigt (2026-07-23) fuer einfache int/char/unsigned-Skalare | — |
| Casts | nicht im Original-Scope dieser Liste, aber jetzt teilweise erledigt (2026-07-23) fuer int/unsigned/char/bool | — |
| einfache `#define`-Konstanten (objektartig, ohne Parameter) | keine parametrisierten Makros im Generator-Source gefunden -- nur einfache Namenskonstanten nötig | fehlt (Präprozessor komplett offen) | hoch (aber kleiner Umfang als volles CPP) |
| Mehrdatei-Übersetzung (`ebnf.cpp`/`codegen.cpp`/`tiny-regex.cpp` + zugehörige `.h`) | Generator ist auf 3 `.cpp` + 3 `.h` verteilt | fehlt (Linkage über mehrere Dateien) | sehr hoch |
| `malloc`/`realloc`/`free` (dynamische Speicherverwaltung) | `codegen.cpp`: `pushRoutine`/`growBuf` für die ACTION/ROUTINE-Tabellen (seit 2026-07-24, ersetzt vormals feste 32-MB-Arrays) | fehlt (Tiny-C hat keinen Heap-Allokator) | hoch (neu seit 2026-07-24; vorher nicht gebraucht) |

## 2. Was NICHT extra gebraucht wird
- **Keine echten C++-Templates/Exceptions/STL im Generator selbst** -- die
  kommen erst in L3 vor (siehe unten), nicht in L2.
- **Keine variadischen FunktionsDEFINITIONEN** im Generator -- `printf`/
  `fprintf` werden nur AUFGERUFEN (Bibliotheksfunktion), nirgends definiert
  der Generator selbst eine Funktion mit `...`. Tiny-C muss also nicht sofort
  eigene variadische Funktionen unterstützen, nur variadische Aufrufe gegen
  eine mitgelieferte Runtime-Funktion erlauben.

## 3. Bibliotheks-/Laufzeitbedarf des Generators (kein Sprachmerkmal, sondern Runtime)

| Funktion(en) | Belegstellen (Anzahl) | Bemerkung |
|---|---|---|
| `strcmp`, `strncmp`, `strlen`, `strstr`, `strchr` | ca. 120 Aufrufe insgesamt in `ebnf.cpp`+`codegen.cpp` | Kernwerkzeug für Tabellen-/Namensvergleich; müsste als kleine Tiny-C-Runtime nachgebaut werden |
| `printf`/`fprintf` (Textausgabe des generierten Codes) | `ebnf.cpp`: 73, `codegen.cpp`: 236 | Der Generator IST im Kern ein Textgenerator -- ohne formatierte Ausgabe kein Codegen |
| Datei-I/O (`fopen`/`fread`/`fwrite`/`fclose`) | durchgehend zum Einlesen der `.ebnf`/`.lextab` und Schreiben der `_p.c`/`.s68`-Ausgabe | aktuell in Tiny-C komplett offen (nur `putchar` im 68k-Runtime-Vertrag laut `STATUS.md`) |

Diese drei Punkte sind der eigentliche Hebel: Selbst wenn Tiny-C morgen
`struct`/`for`/`switch` könnte, bräuchte es zusätzlich eine kleine
Standardbibliothek (String-Vergleich, formatierte Ausgabe, Datei-I/O), bevor
der Generator überhaupt lauffähig wäre.

## 4. Zusatzbedarf für L1 (generierter Parser-Zwilling selbst hosten)

Der generierte Code (`Data/*_p.c`) verwendet Konstrukte, die im Generator
selbst NICHT vorkommen, weil der Generator sie nur als Text emittiert:

| Sprachmittel | Fundstelle im Generator (als emittierter String) | Warum L1 das braucht |
|---|---|---|
| `goto` + Sprungmarken | `codegen.cpp:865,880,885,894,907,922,932,945,954` (alle in `fprintf(fp, "...goto L%d...")`) | Backtracking-Kontrollfluss im generierten rekursiven Parser läuft über `goto`/Label, nicht über strukturierte Schleifen |
| Funktionszeiger + Struct mit Funktionszeigerfeld | `codegen.cpp:1048-1050`: `typedef void (*ActionFn)(const char*, const char*); typedef struct { ActionFn fn; ... } ActionLogEntry;` | Der ACTION/ROUTINE-Mechanismus (Backtracking-sicheres Aktions-Log, siehe `ebnf-projekt.md`-Erinnerung) hängt direkt an Funktionszeigern |

Das heißt: **Layer 1 ist NICHT einfach eine Untermenge von Layer 2** --
`goto` und Funktionszeiger sind für den generierten Parser zwingend, obwohl
der Generator selbst sie an keiner Stelle in eigener Logik braucht.

## 5. L3 -- die beiden C++/STL-Backends (andersartiger Umbau)

`Source/tinyc_backend.cpp` und `Source/tinyc_arm64_backend.cpp` sind die
jüngsten Dateien im Projekt und einzige Stellen mit echtem "modernem" C++:
`std::vector`, `std::map`, `std::string`, `std::ifstream`/`ofstream`,
`std::runtime_error`/`invalid_argument`, `std::stoi`, `std::to_string`.

Das ist kein Sprachfeature-Rückstand, sondern eine Architekturfrage: Um diese
Dateien selbst zu hosten, müsste man

- `std::vector`/`map` durch Arrays fester Größe + lineare Suche ersetzen
  (genau der Stil, den `ebnf.cpp`/`codegen.cpp` schon konsequent nutzen),
- `std::string` durch feste `char[]`-Puffer + die String-Runtime aus Abschnitt 3,
- Exceptions durch Rückgabecodes/Fehlerflags (wie der Rest der Toolchain es
  bereits macht),
- `ifstream`/`ofstream` durch dieselbe Datei-I/O-Runtime wie L2.

**Empfehlung:** L3 muss für ein erstes Selfhosting-Milestone nicht
zwingend mit -- ein Compiler, der zunächst nur Frontend + Generator (L1+L2)
selbst hostet und die finale Maschinencode-Erzeugung weiter über einen
host-seitigen C++-Compiler laufen lässt, ist ein legitimer Zwischenschritt
(viele reale Selfhosting-Compiler haben das genauso gemacht).

## 6. Echter Kompilier-/Ausführungstest mit der Ziel-Toolchain (2026-07-23)

Statt nur zu analysieren, wurde ausprobiert: der komplette Generator
(`ebnf.cpp`+`codegen.cpp`) wurde tatsächlich mit der echten Microware-`xcc`-
Toolchain (die Q9 selbst nutzt) kompiliert und gelinkt. Ergebnis: **funktioniert**,
mit drei konkreten Erkenntnissen für den Selfhosting-Weg:

- **C++-Templates waren der einzige echte Blocker im Sprachumfang.**
  `Source/msvc_compat.h` hatte Template-Überladungen (Array-Größen-Deduktion
  für `strcpy_s`/`strncpy_s`/etc.), an denen `xcc`s Compiler (Ultra C/C++ 2.5,
  Baujahr 2001) mit einem internen Fehler abstürzte. Da Templates sonst
  nirgends im Projekt vorkamen, wurden alle 75 betroffenen Aufrufstellen auf
  explizite `sizeof()`-Form umgestellt und die Templates entfernt (PR #11,
  dauerhaft im Repo) -- funktioniert identisch auf allen Plattformen.
- **Kommentarform ist ein reines Cross-Compiler-Problem, kein Sprachfeature.**
  `xcc` akzeptiert kein C99-`//`, nur `/* */`. Für den xcc-Testlauf wurde ein
  string-literal-bewusster Konverter gebraucht (naive Zeilen-Regex zerstört
  `"//"`-Stringwerte, die als Grammatik-Konfigurationsdaten im Projekt
  vorkommen!). Diese Konvertierung ist NICHT im Repo, nur für den xcc-Testlauf
  einmalig angewendet -- eine dauerhafte Lösung (z.B. eigener Build-Schritt
  oder Umstellung der Kommentarkonvention) ist noch offen.
- **Datensegment-Größe ist der eigentliche Blocker für die Ausführung.**
  Das gelinkte Modul braucht ~34,6 MB Datensegment (feste globale Puffer,
  großzügig für einen modernen Mac dimensioniert), das Q9-Zielsystem hat aber
  nur 16 MB RAM. Kompilieren+Linken funktioniert trotzdem (32-Bit- statt
  16-Bit-Datenreferenzen via `xcc -tp=68030,ld` nötig, da schon die reine
  Adressierung sonst an der 64-KB-Grenze scheitert), aber der Programmstart
  auf dem echten System schlägt fehl. Volle Details und der exakt
  reproduzierbare Ablauf stehen in der Claude-Memory-Datei
  `q9-xcc-toolchain-milestone.md`.

Das bedeutet: Der Sprachmittel-Fahrplan aus Abschnitt 1 (struct/typedef/enum/
for/switch/...) ist zwar weitgehend abgearbeitet, aber für ein WIRKLICH
lauffähiges Selfhosting-Ergebnis kommt noch ein bisher nicht erfasster Punkt
dazu: **Speicherbedarf der statischen Puffer für das Zielsystem verkleinern.**

## Empfohlene Reihenfolge

1. **Sprachmittel aus Abschnitt 1** in Tiny-C nachziehen: `struct` mit
   gemischten skalaren Feldtypen (erledigt, 2026-07-24), `typedef` inkl.
   `typedef struct { ... } Name;` inline (erledigt, 2026-07-24), `enum`
   (erledigt), `for`/`switch` (erledigt), noch offen: mehrdimensionale
   Arrays, `static`/`const`, `union` (nur 1 Fundstelle), Array-Felder in
   `struct`.
2. **Mini-Runtime aus Abschnitt 3** bauen: String-Vergleichsfunktionen,
   formatierte Ausgabe, minimale Datei-I/O -- ohne die ist der Generator
   funktional nicht nachbaubar, unabhängig von der Sprachsyntax. NOCH OFFEN.
3. Erst danach **Mehrdatei-Übersetzung** (Abschnitt 1, letzter Punkt) angehen,
   damit der nachgebaute Generator wie das Original auf mehrere Dateien
   verteilt werden kann.
4. **L1-Zusatzbedarf** (`goto`, Funktionszeiger, Abschnitt 4) erst, wenn
   tatsächlich der generierte Parser-Zwilling selbst gehostet werden soll --
   nicht vorher, um keine Sprachmittel vorzuziehen, die für L2 gar nicht
   nötig sind.
5. **L3 (STL-Backends)** bewusst zurückstellen oder dauerhaft host-seitig
   lassen (siehe Empfehlung in Abschnitt 5).
6. **Unabhängig vom Sprachmittel-Fahrplan (Abschnitt 6):** Speicherbedarf der
   statischen Puffer in `ebnf.cpp`/`codegen.cpp` für ein reales 16-MB-
   Zielsystem verkleinern -- das ist jetzt der einzige bekannte Blocker
   zwischen "kompiliert mit der echten Toolchain" und "läuft wirklich auf Q9".

Diese Reihenfolge überschneidet sich stark mit Stufe A/B der
`ISO_C_LUECKENLISTE.md` (`struct`/`enum`/`typedef`/`for`/`switch` stehen dort
ohnehin schon als "sehr hoch"/"hoch") -- die beiden Listen ziehen also
weitgehend am selben Strang, nur dass diese hier zusätzlich den konkreten
Bibliotheks- und Mehrdateibedarf des eigenen Werkzeugs sichtbar macht.
