# Projektstatus

## 2026-07-29: Q9/OS-9-Tiny-C-Bootstrap verifiziert

Die erste praktische Bootstrap-Stufe ist abgeschlossen. Auf dem Q9-Emulator
funktioniert die Kette `C → IR → 68k-Assembler → r68 → l68 → OS-9-Modul`.
Ein aus `test_one.tc` erzeugtes Modul läuft auf OS-9 und gibt `42` aus.
Details und die reproduzierbaren Kommandos stehen in
[`docs/OS9_BOOTSTRAP.md`](OS9_BOOTSTRAP.md).

Zusätzlich läuft der selbstübersetzte EBNF-Parser als OS-9-Modul und kann eine
Tiny-C-Datei über `argv[1]` öffnen und syntaktisch prüfen.

Stand: **2026-07-29 (Nachtrag: Schritt 3 [Live-Q9-Verifikation]
abgeschlossen -- die echte `-largedata`/`-os9`-Kette läuft live auf Q9,
inklusive Link und sichtbarer Ausgabe. Die folgenden historischen Einträge
bleiben als Entwicklungstagebuch erhalten. Ein sechster, neuer Laufzeitfehler
ist beim ersten vollständigen End-zu-Ende-Testlauf aufgetaucht und wird
gerade untersucht -- Details in der Claude-Memory-Datei
`tinyc-vollport-status.md`, die für diesen Strang aktueller ist als dieser
Abschnitt hier.)**

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
- Der **komplette EBNF-Generator** (`parsec.cpp`+`codegen.cpp`) kompiliert und
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
  ~1 MB gesunken (`size build/parsec`), passt jetzt auch für ein 8-MB-System.
  Bestätigt: die Microware-`stdlib.h` stellt `malloc`/`realloc`/`free` bereit,
  betrifft also nur Tiny-C als Sprache fürs spätere Selfhosting, nicht die
  OS-9-Zielplattform (siehe `docs/SELFHOSTING_LUECKENLISTE.md`, neue Zeile
  "malloc/realloc/free"). `./runtests.sh` komplett grün nach der Umstellung.
- **Bestätigt auf dem echten Q9-Emulator (2026-07-24, Nachtrag):** kompletter
  EBNF-Generator (Source-Stand nach PR #26, inkl. aller Tiny-C-Sprachfeatures
  bis String-Literale) mit `xcc -tp=68030,ld` neu gebaut, per ToolShed als
  `PROJECTS/ebnf_gen/ebnf_gen` eingespielt und live ausgeführt. `ident` zeigt
  **Data size $FE7D0 = 1.042.384 Byte (~1 MB)** statt der vorherigen ~34,6 MB
  -- passt jetzt klar in die 16-MB-RAM-Konfiguration. `./ebnf_gen seqtest`
  lief fehlerfrei durch (Parsertabelle korrekt ausgegeben, `seqtest_p.c` +
  `seqtest.s68` + `seqtest.lextab`/`.lexlst` erzeugt), kein Absturz, keine
  PMMU-Fehler. Der vollständige, reproduzierbare xcc-Ablauf (Env-Setup,
  Kommentarform-Konvertierung, `xcc`-Aufruf, ToolShed-Transfer) steht in der
  Memory-Datei `q9-xcc-toolchain-milestone.md`. Damit ist dieser Strang
  vollständig abgeschlossen.

Diese Untersuchung ist inhaltlich unabhängig von der Tiny-C-Sprachfeature-
Arbeit unten und betrifft ausschließlich den Generator selbst, nicht Tiny-C.

### Selfhosting L2 Vollport: `parsec.cpp` nach Tiny-C ABGESCHLOSSEN (2026-07-26)

**Der komplette Vollport von `Source/parsec.cpp` (2149 Zeilen) nach
`SourceTinyC/ebnf.tc` ist fertig** -- zusammen mit dem bereits am 2026-07-25
abgeschlossenen `codegen.cpp`-Vollport (`SourceTinyC/codegen.tc`) sind damit
BEIDE Kerndateien des EBNF-Generators als Tiny-C-Quelltext vorhanden. Ein
echter `l68`-Link von `ebnf.tc` (inkl. seiner eigenen `main()`) gegen
`codegen.tc` läuft zum ersten Mal komplett OHNE unresolved Symbole durch
(Regressionstest in `runtests.sh`, letzter Eintrag) -- der resultierende
2,1-MB-`.out`-Binary ist ein vollständig gelinktes OS-9-Modul.

Chronologischer Fortschritt, alle gefundenen Tiny-C-Sprachquirks und die
Details jedes einzelnen Portierungsschritts stehen in `docs/FORTSCHRITT.md`
(Abschnitt "Selfhosting L2 Vollport: `parsec.cpp`") und der Memory-Datei
`[[tinyc-vollport-status]]` -- hier nur die Kurzfassung:

- Alle neun Kernfunktionen der rekursiven-Abstiegs-Parsergruppe
  (`rule`/`expression`/`term`/`factor`/`block`/`repeat`/`option`/`ident`/
  `literal`) sowie der komplette Lexer (`lexikalischeAnalyse`/`getNext`/
  `getAktChar`/`comment`/`getAktLine`) und die Arbeitsdatei-Ein-/Ausgabe
  (`writeWorkfile`/`loadWorkfileAsGrammar`/`loadPreservedTests`/`runTests`)
  sind portiert.
- **Bewusste, dokumentierte Abweichung vom Original:** Tiny-C `main()` kann
  keine Kommandozeilenargumente (`argc`/`argv`) empfangen -- die komplette
  Original-`main()`-Logik lebt deshalb in `ebnfMain(char* baseArg)`, `main()`
  selbst ruft sie mit einem fest einprogrammierten Platzhalter-Basisnamen
  (`"tinyc"`) auf. Für einen echten, dateinamen-flexiblen OS-9-Build müsste
  das durch einen Syscall zum Lesen der OS-9-Kommandozeile ersetzt werden --
  nicht implementiert.
- Dabei zwei bisher unentdeckte Backend-Bugs gefunden und gefixt
  (`Source/tinyc_backend_c.cpp`): interne Sprungmarken (`tc_L<n>`,
  `tc_cmp_yes/done_<n>`) und die `-largedata`-Tabellen (`tc_functab`/
  `tc_gadata`) kollidierten beim Mehrdatei-Link, weil ihre Zähler in jeder
  Datei wieder bei 0 starten -- gefixt mit demselben `psectName`-Suffix-Muster
  wie die bestehende `static`-Namensverfremdung.
- Rohes Zeiger-Dereferenzieren (`*p` lesen/schreiben, nicht nur `p[i]`) wurde
  zum ersten Mal im gesamten Projekt gebraucht (Lexer-Scanner) -- vorab per
  Standalone-Test gegen TinyVM verifiziert, funktioniert einwandfrei.
- **Schritt 3 (Live-Q9-Verifikation) LÄUFT (Stand 2026-07-26 spät abends):**
  fünf unabhängige, tiefe Bugs im `-largedata`/`-os9`-68k-Backend
  gefunden+gefixt (alle nur auf echter Hardware sichtbar, da weder TinyVM
  noch `tools/tiny68sim.py` echte CPU-Flags/Register-Konventionen/Modul-
  Relokation nachbilden): Tabellen-Relokation, a3/a4-Registerkonvention nach
  externen `clib.l`-Aufrufen, `moveq`-Flag-Clobber zwischen Vergleich und
  bedingtem Branch, und a3/a4-Konvention nach INTERNEN Cross-File-Aufrufen.
  Alle committet+gemergt (`Source/tinyc_backend_c.cpp`, PR #48+#49). Beim
  ersten kompletten End-zu-Ende-Testlauf mit dem bereinigten Stand ist ein
  SECHSTER, neuer (milderer, von OS-9 abgefangener statt Emulator-
  abstürzender) Laufzeitfehler aufgetaucht -- noch nicht behoben, aktueller
  Ermittlungsstand in der Claude-Memory-Datei `tinyc-vollport-status.md`
  (Bisektionsmethodik + genaue nächste Schritte dort dokumentiert).

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
  (z. B. `char name[8]`, direkte `p.field[i]`-Indizierung ODER über eine
  Pointer-Zwischenvariable); Pointer-Felder und verschachtelte structs noch
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
  UND nicht-konstantem Laufzeit-Initialisierer (Runs-once-Guard mit
  verstecktem bool-Flag-Global, siehe docs/FORTSCHRITT.md). Bewusst OHNE
  struct/Array in dieser Version, siehe docs/FORTSCHRITT.md
- `void` als Funktions-Rückgabetyp und `void *` als generischer, bidirektional
  zu jedem anderen Pointer gleicher Tiefe kompatibler Pointer (Zuweisung/
  Parameter/Rückgabe ohne Cast); `void *` selbst nicht dereferenzierbar/
  indizierbar/arithmetikfähig (sauber diagnostiziert), bare `void` bleibt
  außerhalb des Rückgabetyps verboten, siehe docs/FORTSCHRITT.md
- Mehrdimensionale Arrays (`int m[2][3]`, `int m[2][3][4]`, `char names[3][4]`,
  bis `TC_MAXDIMS`=6 Dimensionen) bei lokalen/globalen Variablen --
  `arr[i1]..[iN]` wird im Frontend per Horner-Schema zu einem flachen Index
  zusammengeführt (kein neuer Opcode, kein Backend-Change), flache
  Initialisierer funktionieren mit; bewusst NICHT bei struct-Feldern/
  Parametern oder verschachtelten Brace-Initialisierern, siehe
  docs/FORTSCHRITT.md
- `extern`-Deklarationen für nicht in Tiny-C definierte Funktionen (z. B. echte
  OS-9/Microware-`clib`-Funktionen wie `strcmp`/`printf`/`malloc`) -- Aufruf
  über die dokumentierte Microware-68K-ABI (`CALLEXT`/`CALLEXTP`): die FEST
  deklarierten Parameter gehen nach `d0`/`d1` (genau wie bei einem
  nicht-variadischen Aufruf), NUR der variadische `"..."`-Überschuss geht auf
  den Stack -- korrigiert 2026-07-24 nach einem am echten Q9 gefundenen
  PMMU-Absturz beim ersten echten `printf(fmt, ...)`-Aufruf (frühere Annahme
  "variadisch = alles auf dem Stack" war nie gegen echten Compiler-Code
  verifiziert), NUR im 68000-Backend, siehe docs/FORTSCHRITT.md
- String-Literale (`char*`) -- erzeugen einen anonymen globalen char-Array-
  Konstanten (`GARRAY`/`GINIT` + Nullterminator) und liefern dessen Adresse
  (`ADDRG`), dieselben IR-Opcodes wie ein initialisiertes globales char-Array,
  kein neuer Opcode/Backend-Change; als `const char*` an `extern`-Aufrufe
  (z. B. ein `printf`-Formatstring) übergebbar. Auch als Array-Initialisierer
  nutzbar (`char msg[6] = "hallo";`, lokal UND global) -- kopiert die Bytes
  direkt in die Array-Slots (nicht nur eine Adresse), ein exakt passendes
  Array ohne Platz für den Nullterminator ist wie in echtem C erlaubt.
  Bewusst NICHT Teil dieser Version: String-Vergleich/-Verkettung, `sizeof`
  auf einem Literal ohne Zwischenvariable, siehe docs/FORTSCHRITT.md
- Direkte Indizierung ohne Zwischenvariable (`func()[i]`, `"text"[i]`) --
  `postfixIndex`-Huellregel um die bestehende `index`-Regel, `PADD`+`LOADIND`
  statt `PTRINDEX` (Laufzeitreihenfolge auf dem Stack ist hier `[Pointer,
  Indexwert]` wie bei `p + n`, nicht wie bei einer benannten Pointer-Variable),
  kein neuer Opcode/Backend-Change. Bewusst NICHT Teil dieser Version:
  Indizierung als Zuweisungsziel, verkettete Postfix-Indizierung (`f()[0][1]`),
  Indizierung auf `"(" expr ")"`, siehe docs/FORTSCHRITT.md
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
- **Mehrdatei-Übersetzung** (erledigt 2026-07-25, siehe eigener Abschnitt unten) --
  bare Funktionsprototyp ohne Rumpf + `extern` bei globalen Variablen für echte
  getrennte Kompilation, `static` bekommt echte Bedeutung, live gegen echten
  `r68`+`l68`- UND `clang`/`ld`-Link verifiziert

## Verifikation

`./runtests.sh` meldet aktuell:

```text
135 Tiny-C-Programme korrekt
68000-Pointer-End-to-End-Test korrekt
ARM64/Darwin-Test korrekt
struct-Feldzugriff (einheitlich + gemischt + anonym im typedef + Array-Feld) 68000 + ARM64 korrekt
switch/case 68000 + ARM64 korrekt
static-Lokale-Persistenz 68000 + ARM64 korrekt
void/void* 68000 + ARM64 korrekt
2D-Array-Indizierung 68000 + ARM64 korrekt
extern-Aufruf-ABI (2 Register / 2 Register+2 Stack / variadisch / char*-String-Literal) 68000 korrekt, ARM64 lehnt sauber ab
String-Literal-Adressierung (GARRAY/GINIT/ADDRG) 68000 + ARM64 korrekt
-os9-Ausgabemodus: DATA/BSS/Scratch-Puffer + CALLEXT assemblieren fehlerfrei mit dem echten r68 (via Wine)
68k signed/unsigned MUL/DIV/MOD + Fakultaet korrekt (inkl. der 2026-07-24 gefundenen %-Regression)
Mehrdatei M1: Funktionsaufruf+Global ueber Dateigrenze, static-Isolation, Duplicate-/Signatur-Konsistenzpruefung (TinyVM-Merge-Tool)
Mehrdatei M2: echter r68+l68-Link zweier getrennt kompilierter Dateien + Duplicate-Symbol-Erkennung durch l68
Mehrdatei M3: echte getrennte .o-Kompilate + clang/ld-Link + Duplicate-Symbol-Erkennung durch ld
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
   Aufrufcodegen), der `-os9`-r68-Ausgabemodus, echtes `l68`-Linken gegen
   `clib.l` (inkl. echter Ausführung auf dem Q9) UND String-Literale (inkl.
   Übergabe als `const char*` an einen `extern`-Aufruf) sind seit 2026-07-24
   erledigt (siehe oben). Ebenfalls seit 2026-07-24 erledigt: ein echter
   extern-ABI-Bug (variadische Aufrufe legten faelschlich ALLE Argumente auf
   den Stack statt nur den `"..."`-Ueberschuss) wurde am echten Q9 gefunden
   und behoben -- `printf("value: %d\n", x)` gegen die reale `clib.l` liefert
   jetzt korrekt `value: 42`, siehe docs/FORTSCHRITT.md. Ebenfalls seit
   2026-07-24 erledigt: String-Literale als Array-Initialisierer
   (`char msg[6] = "hallo";`, lokal und global, mit Laengen- und
   Typpruefung), nicht-konstanter `static`-Initialisierer (Runs-once-Guard
   mit hidden Flag-Global), direkte Indizierung ohne Zwischenvariable
   (`func()[i]`, `"text"[i]`), direkte `p.field[i]`-Indizierung von
   struct-Array-Feldern UND mehr als 2 Array-Dimensionen (bis
   `TC_MAXDIMS`=6). Damit ist der urspruengliche Sprachfeature-Fahrplan aus
   der Selfhosting-Lueckenliste (Abschnitt 1) vollstaendig abgearbeitet --
   Mehrdatei-Uebersetzung (siehe unten) ist seit 2026-07-25 ebenfalls
   erledigt -- damit ist der Sprachmittel-Fahrplan aus
   docs/SELFHOSTING_LUECKENLISTE.md Abschnitt 1 vollstaendig abgearbeitet.
2. **Q9-Ausführbarkeit:** Speicherbedarf des Generators ist verkleinert UND
   seit 2026-07-24 auf dem echten Q9-Emulator bestätigt (siehe Abschnitt
   oben) -- dieser Strang ist damit abgeschlossen. Die Tiny-C-68k-Backend-
   Laufzeit-Anbindung (`putint`/`putchar` gegen `clib.l`) ist bereits erledigt
   (siehe oben); `exit`/Rückgabewert-Weitergabe an
   `F$Exit` noch nicht gesondert geprüft.

Vor jeder Sprach-Erweiterung sind Frontend, IR, TinyVM, 68000- und
ARM64-Backend sowie ein Regressionstest zu prüfen.

## Mehrdatei-Übersetzung (2026-07-25, M1-M3 erledigt)

Getrennt kompilierte Tiny-C-Dateien können jetzt echt separat übersetzt und
gelinkt werden (Nutzerwunsch: bei größeren Projekten soll nicht mehr alles
in einem Rutsch kompiliert werden müssen). Vier Meilensteine (M0-Spike +
M1-M3), alle live gegen die echten Toolchains verifiziert:

- **Neue Deklarationsformen:** ein bare Funktionsprototyp ohne Rumpf
  (`int f(int x);`) für normale interne `bsr`/`bl`-Verlinkung -- bewusst
  GETRENNT vom bestehenden `extern` (das bleibt fest an die Microware-ABI/
  `CALLEXT` für echte `clib.l`-Aufrufe gebunden). `extern <typ> <name>;`
  deklariert jetzt auch globale Variablen ohne Speicherallokation.
- **`static` bekommt zum ersten Mal echte Bedeutung** bei globalen
  Funktionen/Variablen (bisher reines No-op).
- **Neue IR-Pseudo-Opcodes** `FUNCDECL`/`GLOBALDECL`: "existiert, ist aber
  nicht hier definiert".
- **`tools/tinyc_merge.py`** (neu): Mini-Linker-Simulation für TinyVM, da
  TinyVM selbst kein Objektdatei-/Linker-Modell hat -- prüft doppelte
  Definitionen, genau ein `main`, `static`-Sichtbarkeit und als Bonus
  Signatur-Konsistenz (Parameterzahl) zwischen Deklaration und Definition.
- **M0-Spike-Ergebnis (wichtiger, planändernder Fund):** `r68`/`l68` (68k/
  OS-9-Ziel) kennen GAR KEINE Export/Import-Direktive -- `xdef` (die
  ursprüngliche Annahme) und acht weitere Kandidaten (`global`/`public`/
  `def`/`entry`/`export`/`section`/`external`/`symbol`) wurden allesamt als
  "bad mnemonic" abgelehnt, empirisch mit zwei handgeschriebenen `.a`-Modulen
  gegen den echten `r68`+`l68` getestet. Jedes Label ist beim Linken
  automatisch für JEDE andere gelinkte Datei sichtbar. Für `static` bedeutet
  das: KEINE echte Durchsetzung möglich, nur Namensverfremdung
  (`tc_<name>__<psect>`) zur Kollisionsvermeidung -- ohne die würde `static`
  seinen Hauptzweck verfehlen (zwei Dateien könnten keinen privaten Helfer
  gleichen Namens mehr haben, `l68` lehnt das als "duplicate symbol" ab,
  ebenfalls empirisch bestätigt).
- **ARM64/Mach-O verhält sich GRUNDLEGEND ANDERS:** `ld` unterstützt ECHTE
  lokale Symbole -- ein Label ohne `.globl` ist für andere Objektdateien
  unsichtbar (empirisch verifiziert: zwei `.o` mit je einem lokalen,
  gleichnamigen Symbol linken ohne Konflikt). Für `static` reicht hier
  reines Weglassen von `.globl`, KEINE Namensverfremdung nötig.
- **68k-Backend zusätzlich:** neues `-runtime`-Flag (nur mit `-part`).
  Grund (live gefunden): der gemeinsame 68k-Core (`mul`/`div`) sowie
  `putint`/`putchar`/`tc_io_write` + deren Scratch-Speicher wurden bisher
  IMMER emittiert -- bei getrennter Kompilation hätte das garantiert zu
  "duplicate symbol" geführt (jede Datei hätte ihre eigene Kopie
  mitgebracht). Genau EINE Datei im Mehrdatei-Programm muss `-runtime`
  zusätzlich zu `-part` setzen.
- **Live verifiziert:** 68k/OS-9 über echten `r68`+`l68`-Link (zwei
  getrennt kompilierte Dateien, Funktionsaufruf + geteilte globale
  Variable über die Dateigrenze, `static`-Isolation, Symbolkarte bestätigt
  korrekte Auflösung); ARM64 über echte getrennte `.o`-Kompilate (`clang -c`)
  + `clang`/`ld`-Link (gleiches Testprogramm, `nm` bestätigt lokale vs.
  globale Symbole). Beide Ziele: ein zweiter Testfall bestätigt, dass der
  jeweilige echte Linker eine ECHTE Namenskollision (zwei nicht-static
  Definitionen desselben Symbols) zuverlässig als "duplicate symbol"
  ablehnt.
- **Bekannte, bewusst akzeptierte Grenze:** kein `#include`-Mechanismus,
  keine gemeinsame Header-Datei -- Nutzer müssen Funktionssignaturen/
  Global-Typen von Hand in beiden Dateien konsistent halten (wie rohes C
  ohne Header-Disziplin). Für die ECHTEN Backends (68k, ARM64) gibt es
  dafür KEINE Prüfung (reale Linker kennen nur Namen, keine Typen) -- nur
  der TinyVM-Merge-Check bietet das als Bonus.

`./runtests.sh`: 8 neue Mehrdatei-Tests (4x TinyVM/M1, 2x echter 68k/`l68`-
Link/M2, 2x echter ARM64/`clang`+`ld`-Link/M3), alle grün neben den 135
bestehenden Tiny-C-Programmen.
