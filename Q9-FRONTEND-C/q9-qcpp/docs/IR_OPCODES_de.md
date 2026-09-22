# QCC Stack-IR — Opcode-Referenz

*English version: [IR_OPCODES.md](IR_OPCODES.md)*

Stand: **2026-09-22**

Ausführliches Referenzdokument zur Text-IR, die zwischen dem generierten
QCC-Frontend-Parser und den Backends (QCCVM, 68000, ARM64, C) steht.
(Toter Verweis auf `docs/ARCHITEKTUR.md` entfernt 2026-09-22 — diese Datei
existiert in Q9-QCC nicht, nur in Q9-PARSEC/Q9-RUN/Q9-FRONTEND-Quant.)

## Modell

- **Operanden-Stack-Maschine**, kein Register-Modell. Jede Zeile ist ein
  Opcode, optional gefolgt von Argumenten, getrennt durch Whitespace.
  Kommentarzeilen beginnen mit `;` oder `#`.
- **Werte und Pointer sind getrennte Konzepte.** Ein Pointer ist intern ein
  Paar `(Block, Offset)` (siehe `Pointer`-Klasse in `tools/qccvm.py`),
  kein simpler Integer — Pointer-Arithmetik läuft über eigene Opcodes
  (`PADD`/`IPADD`/`PSUB`/`PDIFF`), nicht über `ADD`/`SUB`.
- **Typtags** (`<typtag>`), wo relevant: `c`/`b` = 1 Byte (char/bool),
  `p` = Pointer-Breite (architekturabhängig, 68k kleiner als ARM64),
  alles andere = 4 Byte (int/unsigned/enum).
- **Kanonische Semantik-Quelle:** `tools/qccvm.py` — jeder Backend-Codegen
  (68k, ARM64, C) muss für dieselbe IR dasselbe Ergebnis liefern wie der
  QCCVM-Interpreter. Bei Zweifeln an der Semantik eines Opcodes: dort
  nachschauen, nicht raten.
- Drei Opcode-Familien sind **backend-only** und von QCCVM nicht
  ausführbar: `CALLEXT`/`CALLEXTP` (echte `extern`-Aufrufe gegen Microware-
  `clib.l`, nur 68k), `FUNCDECL`/`GLOBALDECL` (Mehrdatei-Vorwärts-
  deklarationen ohne Rumpf — reine Backend-/Linker-Information) und
  die Modul-/System-Opcodes `MODHEADER`, `ENTRY`, `DISPATCHTAB`, `INLINEASM`.
  **Entschieden 2026-09-22 (Branch `QCC-DEFMODUL`):** QCCVM verwirft diese
  drei Opcodes nicht als Fehler, sondern ignoriert nur die Modul-/
  Dispatch-Metadaten selbst (keine Simulation von Calling-Convention-
  Semantik wie Registern/Carry-Flag) und interpretiert den reinen
  Funktionsrumpf ganz normal weiter — damit bleibt die C-Logik einer
  Treiberfunktion vorab am Orakel testbar, auch ohne dass QCCVM ein
  OS-9-Modul simuliert. ARM64- und C-Backend lehnen diese drei Opcodes
  dagegen mit klarer Fehlermeldung ab — als „noch nicht implementiert",
  nicht als grundsätzlich ausgeschlossen: ein künftiges `#DEFOS Q9` auf
  ARM64 (z. B. bare-metal) soll dieselbe IR irgendwann genauso verarbeiten
  können wie heute der 68k.

## Programmstruktur / Deklarationen

| Opcode | Stack-Effekt | Beschreibung |
|---|---|---|
| `GLOBAL <name> [init]` | — | globale skalare Variable, optionaler Initialwert |
| `GARRAY <name> <typtag> <len>` | — | globales Array fester Länge |
| `GINIT <name> <idx> <wert>` | — | Initialwert für ein Array-Element (mehrfach pro Array) |
| `GINITD <name> <idx> <hi> <lo>` | — | Initialwert eines globalen `double`, als zwei 32-Bit-Hälften (hi zuerst) — wie `PUSHD`. **Welche Hälfte zuerst im Speicher landet, entscheidet das Backend**: der 68k schreibt zwei `dc.l` (big-endian), ARM64 ein `.quad` (little-endian). Deshalb ein eigener Opcode statt zweier `GINIT` |
| `FUNC <name> <nargs> [convention]` | — | Funktionsbeginn; Slots `0..nargs-1` = Parameter. Optionales `convention`-Attribut (`driver`, `interrupt`, `trap`, `naked`) steuert ABI-Prolog/Epilog im Backend |
| `ENDFUNC` | — | Funktionsende (Rahmengröße = höchster Slot+1, vom Backend ermittelt) |
| `LABEL <L>` | — | definiert Sprungziel `L` |
| `FUNCDECL <name> <argc> [static]` | — | Vorwärtsdeklaration ohne Rumpf (Mehrdatei/gegenseitige Rekursion); backend-only. `static=1` erhält die private Namensverfremdung, wenn eine Übersetzungseinheit gezielt in Backend-Teile zerlegt wird. |
| `GLOBALDECL <name> <typ> [static]` | — | `extern`-Variable ohne eigene Allokation; backend-only. Das optionale Flag hat dieselbe Bedeutung wie bei `FUNCDECL`. |

## Modul-Header & System-Schnittstellen (OS-9 / Native)

Diese Opcodes transportieren Metadaten für native Betriebssystemmodule (z. B. OS-9 Treiber, File-Manager, Trap-Handler, Kernel). Sie stehen typischerweise ganz am Anfang des `.qir`-Streams noch vor den Deklarationen.

**`MODHEADER` verwendet Schlüssel-Wert-Paare** (`schlüssel=wert`) statt
fester Positionen — Entscheidung vom 2026-09-22 (Branch `QCC-DEFMODUL`,
siehe `STATUS_DEFMODUL.md` Abschnitt 1.d): die Reihenfolge der Paare ist
beliebig, jedes Feld ist über seinen Namen eindeutig statt über seine
Position. `DISPATCHTAB` bleibt davon bewusst unberührt positional — diese
Reihenfolge entspricht den physischen Offsets der echten OS-9-Sprung-
tabelle im Modulkopf und ist Teil des Binärformats, nicht der IR-Notation.

| Opcode | Stack-Effekt | Beschreibung |
|---|---|---|
| `MODHEADER name=<name> type=<type> subtype=<subtype> attr=<attr> edition=<edition> stack=<stack>` | — | Definiert Zielmodul-Metadaten für das Backend (`type`: `prog`, `driver`, `manager`, `system`, `noos`/`baremetal`, `traphandler`). Reihenfolge der Schlüssel-Wert-Paare beliebig. Das Backend generiert daraus das Wurzel-`psect` samt Modul-Header bzw. ein Flat-Binary bei `noos`. |
| `ENTRY <sym>` | — | Primärer Einstiegspunkt des Moduls (bei `prog`/`noos`). Bei `driver`/`manager`/`traphandler` ergibt sich der Einstieg implizit aus dem ersten `DISPATCHTAB`-Eintrag; `ENTRY` ist dort optional/informativ. |
| `DISPATCHTAB <sym1> <sym2> ...` | — | Emittiert am Modulkopf eine geordnete Sprungverteiler-Tabelle. Reihenfolge FEST, nicht vertauschbar. Für OS-9-Treiber (RBF) 7 Einträge: `init`, `read`, `write`, `getstat`, `setstat`, `term`, `trap` — der siebte (`trap`) darf laut Microware-Handbuch auf 0 zeigen, muss als Tabellenslot aber vorhanden sein ("branch table with seven entries"). |
| `ALIGN <grenze>` | — | Richtet die nachfolgende Ausgabe auf eine Byte-Grenze aus (z. B. `4096` für eine 4-KB-Seite), stammt von `#DEFMODUL ALIGN` (`OS9_SYSTEM_INTERFACE.md` §2). Ergänzt 22.09.2026, bisher nur als Quelldirektive dokumentiert, noch kein eigener IR-Opcode-Eintrag gewesen. |
| `ORG <adresse>` | — | Setzt die absolute Basisadresse bzw. füllt den Raum bis zur Zieladresse auf (für Flat-Binaries / ROM-Images). |
| `SECTION <name> [adresse]` | — | Wechselt in ein benanntes `psect` mit optionaler Zieladresse (für Multi-Regionen wie ROM und Fast-RAM). |
| `INLINEASM "<assembler-zeile>"` | — | Reicht hardwarenahe CPU-Assemblerzeilen transparent durch das Backend in die Ziel-Assemblerausgabe. |

## Werte laden/speichern

Lokal (`L`) und global (`G`), je getrennt nach int/char/pointer:

| Opcode | Stack-Effekt | Beschreibung |
|---|---|---|
| `PUSH <n>` | `→ n` | Integer-Konstante |
| `LOADL <i>` / `STOREL <i>` | `→ v` / `v →` | lokaler int-Slot |
| `LOADC <i>` / `STOREC <i>` | `→ v` / `v →` | lokaler char-Slot (maskiert auf `0xff`) |
| `LOADP <i>` / `STOREP <i>` | `→ p` / `p →` | lokaler Pointer-Slot |
| `LOADG <name>` / `STOREG <name>` | `→ v` / `v →` | globale int-Variable |
| `LOADGC <name>` / `STOREGC <name>` | `→ v` / `v →` | globale char-Variable |
| `LOADGP <name>` / `STOREGP <name>` | `→ p` / `p →` | globaler Pointer |
| `LARRAY` | — | lokales Array reservieren |

## Adressen, Arrays, Pointer

| Opcode | Stack-Effekt | Beschreibung |
|---|---|---|
| `ADDRL <i>` | `→ p` | Adresse eines lokalen Slots |
| `ADDRG <name>` | `→ p` | Adresse einer globalen Variable |
| `PUSHADDR L/G/P <i>` | `→ p` | Adresse eines lokalen/globalen Arrays bzw. eines Pointer-Werts selbst |
| `LOADIDX L/P/G <i> <typtag>` | `idx → v` | Array-Element lesen, Index vom Stack |
| `STOREIDX L/P/G <i> <typtag>` | `idx, v →` | Array-Element schreiben |
| `PTRINDEX <typtag>` | `p, idx → p'` | Pointer+Index → skalierte Adresse (echter Pointer, `p[i]`-Muster) |
| `LOADIND <typtag>` | `p → v` | durch Pointer dereferenzieren (lesen) |
| `STOREIND <typtag>` | `p, v →` | durch Pointer dereferenzieren (schreiben) |
| `PADD <typtag>` | `p, n → p'` | Pointer + Ganzzahl, skaliert nach fester Typgröße |
| `IPADD <typtag>` | `n, p → p'` | wie `PADD`, andere Operandenreihenfolge auf dem Stack |
| `IPADDN <bytesize>` | `n, p → p'` | wie `IPADD`, aber mit einer **Laufzeit**-Bytegröße statt fixer Typtag-Größe (gebraucht für `arr[i].feld` bei Arrays von structs, da `IPADD` nur feste Typtag-Größen kennt) |
| `PSUB <typtag>` | `p, n → p'` | Pointer − Ganzzahl |
| `PDIFF <typtag>` | `p1, p2 → n` | Pointer − Pointer → skalierte Ganzzahl-Differenz (beide müssen zum selben Block gehören) |

## Arithmetik/Logik

| Opcode | Stack-Effekt | Beschreibung |
|---|---|---|
| `ADD` `SUB` `MUL` `DIV` `MOD` | `a, b → r` | signed, `DIV`/`MOD` runden Richtung 0 (C-Semantik, nicht floor) |
| `UDIV` `UMOD` | `a, b → r` | unsigned-Varianten (32-Bit) |
| `NEG` | `a → -a` | unäres Minus |
| `NOT` | `a → r` | logisches Nicht → 0/1 |
| `NOTBIT` | `a → r` | bitweises Komplement (32-Bit) |
| `BAND` `BXOR` `BOR` | `a, b → r` | bitweise Und/Xor/Oder (32-Bit) |
| `SHL` | `a, n → r` | Shift links |
| `SHR` | `a, n → r` | Shift rechts, signed/arithmetisch |
| `USHR` | `a, n → r` | Shift rechts, unsigned/logisch |
| `NARROWC` | `a → r` | auf ein Byte einschränken (char-Zuweisung/-Cast) |
| `DUP` | `a → a, a` | oberstes Stackelement duplizieren (Wert) |
| `SWAP` | `a, b → b, a` | oberste zwei Stackelemente vertauschen |
| `DUPP` | `p → p, p` | wie `DUP`, für Pointer (semantisch identisch, eigener Opcode zur Klarheit im Backend) |

## Gleitkomma (`double`, seit 2026-09-16)

Ein `double` liegt **nie in einem Slot, sondern immer als Block** — die
Slots sind zielabhängig breit (68k 4 Byte, ARM64 16), ein Block ist es
nicht. Eine lokale Variable wird deshalb mit `LARRAY <i> d 1` reserviert,
eine globale mit `GARRAY <name> d 1`, genau wie bei einer struct-Variablen.
Auf dem Operandenstapel belegt ein `double` 8 Byte.

| Opcode | Stack-Effekt | Beschreibung |
|---|---|---|
| `PUSHD <hi> <lo>` | `→ d` | Literal als zwei 32-Bit-Hälften, hi zuerst. Zwei Ganzzahlen statt eines Gleitkommatextes, weil die IR von Werkzeugen gelesen wird, die selbst kein Gleitkomma haben |
| `LOADD <i>` / `STORED <i>` | `→ d` / `d →` | lokale `double`-Variable (Block) |
| `LOADGD <n>` / `STOREGD <n>` | `→ d` / `d →` | globale `double`-Variable |
| `DADD` `DSUB` `DMUL` `DDIV` | `d d → d` | Grundrechenarten |
| `DNEG` | `d → d` | Vorzeichenwechsel |
| `DCMPEQ` `DCMPNE` `DCMPLT` `DCMPLE` `DCMPGT` `DCMPGE` | `d d → i` | Vergleiche, Ergebnis ganzzahlig 0/1 |
| `I2D` | `i → d` | Ganzzahl nach `double` |
| `I2DUNDER` | `i d → d d` | wandelt den Wert **unter** dem obersten um. Gebraucht für `1 + a`: dort liegt die Ganzzahl beim Emittieren schon unter dem `double` |
| `D2I` | `d → i` | `double` nach Ganzzahl, **schneidet Richtung null ab** (C-Regel, auf dem 68k `fintrz`) |
| `DDUP` / `DDROP` | | eigene Formen, weil `DUP`/`DROP` bei 8 Byte mehrdeutig wären |
| `DSWAP` | `x d → d x` | tauscht das oberste `double` mit dem **4-Byte-Wert darunter** (Adresse oder Index). Gebraucht beim Postfix-`++`/`--` über eine Adresse, wo der alte Wert als Ergebnis unter der Adresse bleiben muss. `SWAP` taugt dort nicht — es tauscht zwei Langworte und zerrisse die acht Byte. Auf ARM64 und in der VM belegt ein `double` genau ein Stackelement, dort ist es derselbe Tausch wie `SWAP` |

Auf dem 68k werden daraus FPU-Befehle (`fadd.x`, `fcmp.x` + `FBcc`,
`fintrz.x`), gerechnet wird intern mit 80 Bit, geladen und gespeichert mit
64 — dieselbe Aufteilung, die xcc verwendet. **Genauigkeit:** das VM-Orakel
rechnet mit Pythons 64 Bit. Für Vergleiche zwischen Orakel und Hardware
taugen deshalb nur Werte, die in beiden exakt sind.

Gemischte Ausdrücke (`a + 1`, `10 - a`) lösen die üblichen arithmetischen
Konversionen aus; welcher Opcode das tut, hängt an der Stapellage.

**Noch nicht in der IR:** Initialisierer an globalen `double` und `float`
als eigener Typ.

## Vergleiche

Alle Vergleiche: `a, b → 0|1`.

| Familie | Opcodes |
|---|---|
| signed int | `CMPLT` `CMPGT` `CMPLE` `CMPGE` `CMPEQ` `CMPNE` |
| unsigned int | `CMPULT` `CMPUGT` `CMPULE` `CMPUGE` |
| Pointer | `PCMPEQ` `PCMPNE` `PCMPLT` `PCMPLE` `PCMPGT` `PCMPGE` (mit Block-Identitätsprüfung; `EQ`/`NE` erlauben zusätzlich den Nullpointer-Vergleich) |

## Kontrollfluss / Funktionsaufrufe

| Opcode | Stack-Effekt | Beschreibung |
|---|---|---|
| `JMP <L>` | — | unbedingter Sprung |
| `JZ <L>` | `a →` | Sprung wenn `a == 0` |
| `JNZ <L>` | `a →` | Sprung wenn `a != 0` |
| `CALL <name> <nargs>` | `a1..aN → r` | Argumente links→rechts gepusht, Ergebnis auf Stack |
| `CALLP <name> <nargs>` | `a1..aN → p` | wie `CALL`, Ergebnis ist ein Pointer (reine Kennzeichnung fürs Backend) |
| `PUSHFN <name>` | `→ p` | Adresse einer QCC-Funktion als Wert (Funktionszeiger); im `-largedata`-Modus über die Funktionsindirektionstabelle berechnet, sonst PC-relativ |
| `CALLIND <nargs>` / `CALLINDP <nargs>` | `f, a1..aN → r`/`p` | indirekter Aufruf; der Funktionszeiger liegt ZUUNTERST (unter den Argumenten), darüber wie bei `CALL` die Argumente links→rechts |
| `RET` / `RETP` | `r →` | Rückgabewert vom Stack, Rahmen abbauen, zurück zum Aufrufer |
| `CALLEXT <name> <argc> <...>` / `CALLEXTP ...` | `a1..aN → r`/`p` | Aufruf einer echten `extern`-Funktion über die Microware-ABI (feste Parameter in `d0`/`d1`, nur der variadische `"..."`-Überschuss auf dem Stack); backend-only (68k) |

## Sonstiges

| Opcode | Stack-Effekt | Beschreibung |
|---|---|---|
| `DROP` | `a →` | oberen Stackwert verwerfen (unbenutztes Ausdrucksergebnis) |
| `PRINT` | `a →` | Debug-Ausgabe als signed int (Builtin `putint`) |
| `PRINTU` | `a →` | Debug-Ausgabe als unsigned int (Builtin `putuint`) |
| `PRINTC` | `a →` | Debug-Ausgabe als Zeichen (Builtin `putchar`) |

## Siehe auch

- `docs/OS9_SYSTEM_INTERFACE.md` — Ziel-Betriebssystem (`#DEFOS`), Modul-Definition (`#DEFMODUL`), Calling-Conventions (`modul driver`/`interrupt`/`trap`/`naked`), Syscall-Archetypen (`modul syscall`) und Inline-Assembler.
- `tools/qccvm.py` — Referenzinterpreter, gleichzeitig Test-Orakel für
  alle Backends.
- `docs/SELFHOSTING_GAP_LIST_de.md` / `[[qcc-vollport-status]]` (Memory)
  — Kontext zum laufenden QCC-Vollport von `codegen.cpp`/`parsec.cpp`.
