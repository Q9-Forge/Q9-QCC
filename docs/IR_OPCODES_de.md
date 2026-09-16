# QCC Stack-IR — Opcode-Referenz

*English version: [IR_OPCODES.md](IR_OPCODES.md)*

Stand: **2026-07-25**

Ausführliches Referenzdokument zur Text-IR, die zwischen dem generierten
QCC-Frontend-Parser und den Backends (QCCVM, 68000, ARM64, C) steht.
Kurzfassung mit Einbettung in den Gesamtkontext: `docs/ARCHITEKTUR.md`
Abschnitt 10 (10.5 zeigt denselben Opcode-Satz kompakter).

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
- Zwei Opcode-Familien sind **backend-only** und von QCCVM nicht
  ausführbar: `CALLEXT`/`CALLEXTP` (echte `extern`-Aufrufe gegen Microware-
  `clib.l`, nur 68k) und `FUNCDECL`/`GLOBALDECL` (Mehrdatei-Vorwärts-
  deklarationen ohne Rumpf — reine Backend-/Linker-Information).

## Programmstruktur / Deklarationen

| Opcode | Stack-Effekt | Beschreibung |
|---|---|---|
| `GLOBAL <name> [init]` | — | globale skalare Variable, optionaler Initialwert |
| `GARRAY <name> <typtag> <len>` | — | globales Array fester Länge |
| `GINIT <name> <idx> <wert>` | — | Initialwert für ein Array-Element (mehrfach pro Array) |
| `FUNC <name> <nargs>` | — | Funktionsbeginn; Slots `0..nargs-1` = Parameter |
| `ENDFUNC` | — | Funktionsende (Rahmengröße = höchster Slot+1, vom Backend ermittelt) |
| `LABEL <L>` | — | definiert Sprungziel `L` |
| `FUNCDECL <name> <argc> [static]` | — | Vorwärtsdeklaration ohne Rumpf (Mehrdatei/gegenseitige Rekursion); backend-only. `static=1` erhält die private Namensverfremdung, wenn eine Übersetzungseinheit gezielt in Backend-Teile zerlegt wird. |
| `GLOBALDECL <name> <typ> [static]` | — | `extern`-Variable ohne eigene Allokation; backend-only. Das optionale Flag hat dieselbe Bedeutung wie bei `FUNCDECL`. |

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
| `D2I` | `d → i` | `double` nach Ganzzahl, **schneidet Richtung null ab** (C-Regel, auf dem 68k `fintrz`) |
| `DDUP` / `DDROP` | | eigene Formen, weil `DUP`/`DROP` bei 8 Byte mehrdeutig wären |

Auf dem 68k werden daraus FPU-Befehle (`fadd.x`, `fcmp.x` + `FBcc`,
`fintrz.x`), gerechnet wird intern mit 80 Bit, geladen und gespeichert mit
64 — dieselbe Aufteilung, die xcc verwendet. **Genauigkeit:** das VM-Orakel
rechnet mit Pythons 64 Bit. Für Vergleiche zwischen Orakel und Hardware
taugen deshalb nur Werte, die in beiden exakt sind.

**Noch nicht in der IR:** gemischte Arithmetik und Vergleiche von `double`
und Ganzzahl (das Frontend meldet sie), Initialisierer an globalen
`double`, und `float` als eigener Typ.

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

- `docs/ARCHITEKTUR.md` Abschnitt 10 — Entstehung der IR, Emissions-Muster
  (wie Parser-Aktionen die IR erzeugen), Funktions-ABI (Slots/Frames).
- `tools/qccvm.py` — Referenzinterpreter, gleichzeitig Test-Orakel für
  alle Backends.
- `docs/SELFHOSTING_GAP_LIST_de.md` / `[[qcc-vollport-status]]` (Memory)
  — Kontext zum laufenden QCC-Vollport von `codegen.cpp`/`parsec.cpp`.
