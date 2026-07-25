# Selfhosting-Lückenliste

Stand: **2026-07-25 (Nachtrag: Mehrdatei-Übersetzung erledigt -- damit ist auch der letzte "sehr hoch"-Punkt aus Abschnitt 1 abgearbeitet)**

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
| `struct` (auch anonym via `typedef struct`) | `codegen.cpp:568-571` (`ActionRoutine.text`), `ebnf.cpp:398-408` (`TabEntry.mode`) | **teilweise** (2026-07-24/25: GEMISCHTE skalare Feldtypen, `typedef struct { ... } Name;` mit anonymem struct inline, Array-Felder UND Pointer-Felder (2026-07-25, IMMER 8 Byte Groesse/Ausrichtung -- architekturneutrales Layout, 68k nutzt nur die ersten 4 Byte) jetzt moeglich, echtes Byte-Layout mit natuerlichem Alignment. Direkte `rec.name[i]`-Syntax funktioniert; direkte Indizierung DURCH ein Pointer-Feld (`p.field[i]`) bewusst noch nicht (Zugriff nur ueber Pointer-Zwischenvariable, analog zur fruehren Array-Feld-Einschraenkung). Bewusst noch offen: verschachtelte structs. **`arr[i].feld` (2026-07-25, erledigt fuer LOKALE, fest dimensionierte struct-Arrays:** ein ARRAY von structs, per Laufzeit-Index adressiert, dann Feldzugriff -- neue Grammatikform (`directTarget`/`varRef` jetzt `ident [index...] [.member [index]]` statt Alternation) + neuer IR-Opcode `IPADDN` (Zeiger-Skalierung um eine LAUFZEIT-Byte-Groesse wie `tcStructByteSize`, da `IPADD` nur feste Typtag-Groessen kennt). Ein vorausgesetzter, eigenstaendiger Bug wurde dabei zuerst repariert: die Speicher-Allokation fuer Arrays von structs war kaputt (`tc_localdecl` reservierte fuer `struct Rec arr[3];` nur Platz fuer EIN Element, `[N]`-Suffix bei struct-Locals wurde ignoriert) -- siehe FORTSCHRITT.md fuer Details. Verifiziert in TinyVM, 68000 (`tiny68sim` + echter `r68`) und ARM64. **Fuer den Selfhosting-Piloten weiterhin OFFEN:** `routinesC` in `codegen.cpp` ist `ActionRoutine*` -- ein POINTER auf ein malloc/realloc-gewachsenes Array, KEIN fest dimensioniertes lokales Array wie `struct Rec arr[3];`. Der jetzt gebaute Zugriffspfad deckt nur `tcLocalType(slot).base=='s'` MIT bekannter `tcLocalArrayLen` ab -- `ptr[i].feld` fuer eine Pointer-Variable vom Typ `struct X*` ist eine eigene, noch nicht gebaute Zugriffsform (braucht dieselbe `IPADDN`-Idee, aber ausgehend von einem geladenen Pointer-Wert statt einer PUSHADDR-Blockadresse). Ausserdem weiterhin offen: globale struct-Variablen/-Arrays (`tc_globalend` parst `struct` als Globaltyp ueberhaupt nicht). | sehr hoch |
| `enum` | `codegen.cpp:42` (`AstKind`), `ebnf.cpp:1165` (`BLK_NONE` etc.) | **erledigt** (2026-07-23, Nachtrag: `enum Name var;` als Deklaration moeglich, `enum Name` auch als Parameter-/Rueckgabetyp; im Speicher/Typsystem bleibt es schlicht `int`, keine eigene Typidentitaet -- entspricht C) | hoch |
| `union` | `tiny-regex.cpp:45` (anonyme Union in `regex_t`) | fehlt | mittel |
| `typedef` (auch für Structs) | durchgehend in allen drei Dateien | **erledigt** (2026-07-24: Skalar-/Pointer-Aliase, `typedef struct Name Alias;` und die im Generator gebräuchliche Form `typedef struct { ... } Name;` mit anonymem struct INLINE im typedef funktionieren jetzt alle. Der typedef-Zielname dient dabei intern als struct-Tag -- bewusste, harmlose Vereinfachung gegenüber striktem C, das dort keinen Tag kennt) | sehr hoch |
| mehrdimensionale Arrays | 26 Fundstellen, z. B. `ruleNameList[MAX_RULE_NAMES][IDENT_LEN+1]`, `dfsPath[...][...]`, `lexBlockOn[...][...]` | **erledigt** (2026-07-24: alle 26 Fundstellen sind exakt 2D, waren damit bereits mit der urspruenglichen 2D-Loesung abgedeckt -- `arr[i][j]` wird im Frontend zu einem flachen row-major-Index zusammengefuehrt, kein neuer Opcode/Backend-Change. Zusaetzlich seit 2026-07-24 (spaeter) generalisiert auf beliebig viele Dimensionen (`TC_MAXDIMS=6`, Horner-Schema ueber Scratch-Globals), obwohl der Generator selbst keine 3D+-Arrays braucht) | sehr hoch |
| `for`-Schleife | 69 echte Vorkommen (nicht mitgezählt: 2 nur in erzeugten Strings) | **erledigt** (2026-07-23) | hoch |
| `do`/`while`-Schleife | `codegen.cpp:782` (Fixpunkt-Iteration über Regel-Nullbarkeit) | **erledigt** (2026-07-23) | hoch |
| `switch`/`case` | 13 echte Vorkommen in allen drei Dateien | **erledigt fuer die reale Nutzung** (2026-07-23: alle 13 Fundstellen nutzen entweder `break` oder gestapelte leere Case-Label -- genau das unterstuetzt Tiny-C jetzt; echtes Fallthrough MIT Code zwischen Bodies fehlt, wird aber nirgends im Generator gebraucht) | hoch |
| `static` (Funktionen/lokale Variablen als Speicherklasse, nicht nur globale Objekte) | 101 echte Vorkommen, u. a. alle großen Tabellenpuffer | **erledigt** (2026-07-24: bei globalen Variablen/Funktionen ein reines No-op -- deckt damit die weit ueberwiegende Mehrheit der 101 Fundstellen ab, da fast alle file-scope `static` auf Tabellenpuffern sind, keine lokalen; bei lokalen Variablen echte Aufruf-uebergreifende Persistenz ohne Initialisierer, siehe docs/FORTSCHRITT.md) | hoch |
| `const`-Qualifizierer | 99 echte Vorkommen (meist `const char*`-Parameter) | **erledigt** (2026-07-24: `const` bei Skalaren/Arrays UND Pointee-Constness bei Pointertypen -- `const char* line`-Parameter wie im Generator selbst wird jetzt semantisch durchgesetzt (Schreiben durch den Pointer verboten, Pointer selbst bleibt frei zuweisbar, `p++`-Idiom funktioniert), TCType.pointeeConst-Bit siehe docs/FORTSCHRITT.md) | hoch |
| `sizeof` | `codegen.cpp:333,1151,...`; `ebnf.cpp:1043,1062` (Puffergrößen an Hilfsfunktionen reichen) | **erledigt** (2026-07-23, Nachtrag: `sizeof(variable)` auf lokale/globale Skalare und Arrays ergänzt -- genau die Form, die alle echten Fundstellen hier nutzen, z. B. `sizeof(line)`) | hoch |
| Prä-/Postinkrement (`++`/`--`) | nicht im Original-Scope dieser Liste, aber jetzt erledigt (2026-07-23) fuer einfache int/char/unsigned-Skalare | — |
| Casts | nicht im Original-Scope dieser Liste, aber jetzt teilweise erledigt (2026-07-23) fuer int/unsigned/char/bool | — |
| einfache `#define`-Konstanten (objektartig, ohne Parameter) | keine parametrisierten Makros im Generator-Source gefunden -- nur einfache Namenskonstanten nötig | fehlt (Präprozessor komplett offen) | hoch (aber kleiner Umfang als volles CPP) |
| Mehrdatei-Übersetzung (`ebnf.cpp`/`codegen.cpp`/`tiny-regex.cpp` + zugehörige `.h`) | Generator ist auf 3 `.cpp` + 3 `.h` verteilt | **erledigt** (2026-07-25: bare Funktionsprototyp ohne Rumpf für normale interne bsr/bl-Verlinkung -- bewusst getrennt vom bestehenden `extern`/Microware-ABI-Feature; `extern <typ> <name>;` für globale Variablen; `static` bekommt echte Bedeutung; neue IR-Pseudo-Opcodes `FUNCDECL`/`GLOBALDECL`. Live gegen echte Toolchains verifiziert: 68k/OS-9 über echten `l68`-Link, ARM64 über echte getrennte `.o`-Kompilate + `clang`/`ld`-Link. Bekannte Grenze: keine Header-Datei/`#include`-Mechanismus, Signaturkonsistenz zwischen Deklaration und Definition wird nur vom TinyVM-Merge-Werkzeug geprüft, nicht von den echten Linkern -- siehe docs/STATUS.md) | sehr hoch |
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

**Strategiewechsel 2026-07-24:** Statt diese drei Punkte als eigene Tiny-C-
Runtime NACHZUBAUEN, ruft man die ECHTEN OS-9/Microware-`clib.l`-Funktionen
direkt auf -- vorausgesetzt man trifft deren Aufrufkonvention. Diese ist im
Ultra-C/C++-Prozessorhandbuch dokumentiert (`DOC/PDF/ultrac_pg.pdf`, "Passing
Arguments to Functions": 1./2. Argument in `d0`/`d1`, Rest auf dem Stack in
umgekehrter Reihenfolge; bei variadischen Funktionen wie `printf` alles auf
dem Stack) und weicht von Tiny-Cs eigener (rein stapelbasierter) interner
Aufrufkonvention ab. `clib.l` exportiert die Symbole klarnamig ohne
Underscore-Praefix (`strcmp`, `printf`, `malloc`, `strlen`, `fopen` u.a.,
per `strings` bestaetigt). Tiny-C hat dafuer jetzt `extern`-Deklarationen
(kein Rumpf, nur Signatur) + einen eigenen Aufrufpfad im 68k-Backend
(`CALLEXT`/`CALLEXTP`), der genau dieser ABI folgt -- siehe docs/FORTSCHRITT.md
fuer die Details. Die Platzierung wurde end-to-end gegen handgeschriebene
Mock-Stubs verifiziert (kein echter Q9-/`clib.l`-Zugriff in dieser Umgebung
moeglich). Die dafuer noetige Assembler-Stufe ist inzwischen ebenfalls
erledigt (2026-07-24): ein `-os9`-Ausgabemodus im 68k-Backend erzeugt
`nam`/`psect`/`ends`-gerahmten, mit `*`-Vollkommentaren und `align 4`/`dc.l`
(statt `even`/`ds.l`, die der echte `r68` als "bad mnemonic" ablehnt)
versehenen Code -- ein Testfall mit DATA/BSS-Globalen UND einem
`extern`-Aufruf (CALLEXT) wurde erfolgreich durch den ECHTEN `r68.exe` (via
Wine/MWOS) zu einer relokierbaren `.r`-Datei assembliert.

**Echtes Linken + echte Ausfuehrung: ebenfalls erledigt (2026-07-24).**
`putint`/`putuint`/`putchar` rufen im `-os9`-Modus den echten, ungepufferten
`_os_write`-Syscall aus `clib.l` auf; ein damit gelinktes Testprogramm lief
ECHT auf dem Q9-Emulator und gab die korrekten Werte aus. Dabei drei
eigenstaendige Bugs gefunden und behoben: (1) Backend nutzte `a6` als eigenen
Frame-Pointer, obwohl Microwares ABI `a6` als statischen Datenzeiger fuer die
GESAMTE Laufzeit reserviert (`a5` ist das echte Frame-Register) -- Fix nur im
`-os9`-Modus. (2) `jsr <name>` fuer externe Aufrufe wird von `r68` absolut
statt PC-relativ kodiert (bricht sobald das Modul nicht bei Adresse 0 laedt)
-- Fix: `bsr` statt `jsr`. (3) Die Ausfuehrung beginnt immer am ersten Byte
der ERSTEN Datei auf der `l68`-Kommandozeile -- `cstart.r` MUSS zuerst
stehen, sonst laeuft das Programm ohne jede Laufzeit-Initialisierung los.
Siehe docs/FORTSCHRITT.md fuer alle Details (inkl. eines vierten,
unabhaengigen Bugs im `%`-Operator, der beim Debuggen nebenbei gefunden
wurde). NOCH OFFEN: String-Literale in Tiny-C (ohne die ist ein echter
`printf("format", ...)`-Aufruf mit Formatstring nicht schreibbar, nur mit
rein numerischen/Pointer-Argumenten oder ueber den jetzt echten `_os_write`-Weg).

| Funktion(en) | Belegstellen (Anzahl) | Bemerkung |
|---|---|---|
| `strcmp`, `strncmp`, `strlen`, `strstr`, `strchr` | ca. 120 Aufrufe insgesamt in `ebnf.cpp`+`codegen.cpp` | Kernwerkzeug für Tabellen-/Namensvergleich; jetzt per `extern`-Deklaration gegen die echte `clib.l` aufrufbar UND linkbar (Codegen UND Linken erledigt, siehe oben) statt als Tiny-C-Runtime nachgebaut |
| `printf`/`fprintf` (Textausgabe des generierten Codes) | `ebnf.cpp`: 73, `codegen.cpp`: 236 | Der Generator IST im Kern ein Textgenerator -- ohne formatierte Ausgabe kein Codegen. `extern`-Deklaration + variadischer CALLEXT-Codegen + echtes Linken erledigt; braucht zusaetzlich String-Literale (noch offen) fuer den Formatstring |
| Datei-I/O (`fopen`/`fread`/`fwrite`/`fclose`) | durchgehend zum Einlesen der `.ebnf`/`.lextab` und Schreiben der `_p.c`/`.s68`-Ausgabe | Aufrufkonvention (bis zu 4 Argumente, `fread`/`fwrite`) durch den 4-Argumente-Testfall (2 Register + 2 Stack) bereits verifiziert; echtes Linken jetzt ebenfalls erledigt (siehe oben) |

Diese drei Punkte sind der eigentliche Hebel: Selbst wenn Tiny-C morgen
`struct`/`for`/`switch` könnte, bräuchte es zusätzlich eine kleine
Standardbibliothek (String-Vergleich, formatierte Ausgabe, Datei-I/O), bevor
der Generator überhaupt lauffähig wäre -- der Weg dahin ist jetzt aber
"echte `clib.l` anbinden" statt "in Tiny-C nachbauen".

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
   gemischten skalaren Feldtypen, Array-Feldern UND direkter `rec.name[i]`-
   Indizierung (erledigt, 2026-07-24), `typedef` inkl.
   `typedef struct { ... } Name;` inline (erledigt, 2026-07-24), `enum`
   (erledigt), `for`/`switch` (erledigt), `static`/`const` (erledigt,
   2026-07-24, siehe docs/FORTSCHRITT.md fuer die bewussten
   Einschraenkungen), mehrdimensionale Arrays inkl. 3D+ (erledigt,
   2026-07-24, `TC_MAXDIMS=6`) -- **damit ist dieser Punkt fuer ALLE real
   im Generator vorkommenden Faelle sowie generalisiert dauber hinaus
   abgeschlossen.** Noch offen (kleiner Umfang, kein Blocker fuer den
   Generator selbst): `union` (nur 1 Fundstelle), Pointer-Felder/
   verschachtelte structs.
2. **Mini-Runtime aus Abschnitt 3** anbinden: statt String-Vergleichsfunktionen,
   formatierte Ausgabe und Datei-I/O in Tiny-C nachzubauen, die echten
   `clib.l`-Funktionen direkt aufrufen (Strategiewechsel 2026-07-24). `extern`-
   Deklarationen + Microware-ABI-Aufrufcodegen (CALLEXT/CALLEXTP), der
   `-os9`-r68-Ausgabemodus, echtes `l68`-Linken gegen `clib.l` (inkl. echter
   Ausfuehrung auf dem Q9-Emulator) UND String-Literale (inkl. als
   `printf`-Formatstring, inkl. eines am echten Q9 gefundenen und behobenen
   variadischen-Aufruf-ABI-Bugs) sind alle erledigt (2026-07-24) -- **dieser
   Punkt ist damit abgeschlossen.**
3. **Speicherbedarf der statischen Puffer** (Abschnitt 6) fuer ein reales
   16-MB-Zielsystem: **erledigt (2026-07-24)** und live auf dem echten
   Q9-Emulator bestaetigt (Datensegment ~34,6 MB -> ~1 MB, `ebnf_gen`
   laeuft fehlerfrei), siehe docs/STATUS.md.
4. **Mehrdatei-Übersetzung** (Abschnitt 1, letzter Punkt): **erledigt
   (2026-07-25)** -- damit ist der komplette Sprachmittel-Fahrplan aus
   Abschnitt 1 abgearbeitet. Der nachgebaute Generator könnte jetzt
   grundsätzlich wie das Original auf mehrere Dateien verteilt werden
   (bekannte Grenze: keine Header-Datei/Signaturkonsistenzprüfung durch
   die echten Linker, siehe docs/STATUS.md).
5. **L1-Zusatzbedarf** (`goto`, Funktionszeiger, Abschnitt 4) erst, wenn
   tatsächlich der generierte Parser-Zwilling selbst gehostet werden soll --
   nicht vorher, um keine Sprachmittel vorzuziehen, die für L2 gar nicht
   nötig sind.
6. **L3 (STL-Backends)** bewusst zurückstellen oder dauerhaft host-seitig
   lassen (siehe Empfehlung in Abschnitt 5).

Diese Reihenfolge überschneidet sich stark mit Stufe A/B der
`ISO_C_LUECKENLISTE.md` (`struct`/`enum`/`typedef`/`for`/`switch` stehen dort
ohnehin schon als "sehr hoch"/"hoch") -- die beiden Listen ziehen also
weitgehend am selben Strang, nur dass diese hier zusätzlich den konkreten
Bibliotheks- und Mehrdateibedarf des eigenen Werkzeugs sichtbar macht.
