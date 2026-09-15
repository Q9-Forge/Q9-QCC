# Gleitkomma in der IR — Entwurf (2026-09-16)

Dieser Entwurf beantwortet die Frage, die `FLOAT_PLAN_de.md` als Schritt 2
offenlässt: **wie sieht ein `double` in der IR aus?** Er steht auf
Messungen, nicht auf Annahmen; jede Zahl unten ist nachgerechnet.

## Der Befund, der alles entscheidet

Die Slots der IR sind **nicht** einheitlich breit — das ist schon heute so:

| Ziel | Breite eines Slots | gemessen in |
|---|---|---|
| 68k | 4 Byte | `qcc_backend_c.cpp`, `-4 * (slotN - …)` |
| ARM64 | **16 Byte** | `qcc_arm64_backend_c.cpp`, `16 * (n - …)` |

Ein `double` (8 Byte) passt also in einen ARM64-Slot, aber nicht in einen
68k-Slot. Ein Opcode, der „der Wert liegt in Slot i“ bedeutet, trägt damit
je nach Ziel etwas anderes — und genau dafür gibt es in der IR bereits eine
Antwort.

## Die Antwort steht schon in der IR: Blockspeicher

`LARRAY` legt seinen Speicher **hinter** den Slots ab. `arrayOffset()`
beginnt bei `fn->locals * 4` und vergibt von dort blockweise. Lokale
structs nutzen genau das — im Frontend steht dazu der Satz, ein struct
brauche **immer** Block-Speicher, nie die einzellige Form.

**Ein `double` ist aus Sicht der Ablage dasselbe wie ein struct von 8 Byte.**
Daraus folgt der ganze Entwurf:

- Eine lokale `double`-Variable wird als Block reserviert, nicht als Slot.
  Das ist `LARRAY <slot> d 1`, sobald `tagSize('d')` als 8 definiert ist —
  ein neuer Opcode ist dafür nicht nötig.
- Eine globale `double`-Variable ist `GARRAY <name> d 1`, wie eine globale
  struct-Variable.
- Die Slot-Offsetrechnung beider Backends bleibt **unverändert**. Das ist
  der eigentliche Gewinn: kein Umbau an der Stelle, an der sich 68k und
  ARM64 ohnehin unterscheiden.

Die Alternative — ein `double` belegt zwei aufeinanderfolgende Slots —
wäre auf ARM64 32 Byte für einen 8-Byte-Wert und würde die Slotzählung
zielabhängig machen. Sie wird verworfen.

## Vorgeschlagene Opcodes

| Opcode | Stack | Bedeutung |
|---|---|---|
| `PUSHD <hi> <lo>` | `→ d` | Literal als zwei 32-Bit-Hälften, hi zuerst. Erzeugt von `tools/dec2ieee.c` |
| `LOADD <i>` / `STORED <i>` | `→ d` / `d →` | lokale `double`-Variable (Block, s.o.) |
| `LOADGD <n>` / `STOREGD <n>` | `→ d` / `d →` | globale `double`-Variable |
| `DADD` `DSUB` `DMUL` `DDIV` | `d d → d` | Grundrechenarten |
| `DNEG` | `d → d` | Vorzeichenwechsel |
| `DCMPEQ` … `DCMPGE` | `d d → i` | Vergleiche, Ergebnis ganzzahlig 0/1 |
| `I2D` | `i → d` | Ganzzahl nach `double` |
| `D2I` | `d → i` | `double` nach Ganzzahl, **schneidet Richtung null ab** |
| `DDUP` / `DDROP` | | eigene Formen, weil ein `double` auf dem 68k-Operandenstack 8 Byte belegt und `DUP`/`DROP` sonst mehrdeutig wären |

Warum `PUSHD` zwei Dezimalzahlen und keinen Gleitkommatext: Die IR wird von
Werkzeugen gelesen, die selbst kein Gleitkomma haben. Zwei Ganzzahlen sind
auf jedem Ziel eindeutig; ein Text wie `3.14` verlangte in jedem Leser
einen eigenen Konverter.

## Die 68k-Seite ist bereits fertig

| Baustein | Stand |
|---|---|
| Dezimal → IEEE-754 | `tools/dec2ieee.c`, auf echtem 68030 geprüft |
| FPU-Befehle im Assembler | qr68, 75 Formen byteidentisch zu r68 |
| FPU rechnet auf dem Ziel | `tests/fprun68k.sh`, sechs Rechnungen |

Die Abbildung der Opcodes auf Befehle steht damit fest, gemessen an xcc:

| IR | 68k |
|---|---|
| `LOADD`/`STORED` | `fmove.d <ea>,fp0` / `fmove.d fp0,<ea>` |
| `DADD` … | `fadd.x` `fsub.x` `fmul.x` `fdiv.x` (intern 80 Bit) |
| `DCMPxx` | `fcmp.x` und ein `FBcc` mit umgekehrter Bedingung |
| `I2D` / `D2I` | `fmove.l d0,fp0` / `fintrz.x` + `fmove.l fp0,d0` |
| Aufrufe | `fmovem.x fp1/fp0,-(sp)` beim Betreten, zurück beim Verlassen |

## Zwei Dinge, die noch zu entscheiden sind

**1. Aufrufkonvention.** QCC übergibt heute **alle** Argumente auf dem
Stack und gibt in `d0` zurück (gemessen: `move.l #1,-(a7)` … `bsr`). xcc
übergibt das erste Argument in `d0`, ein `double` in `d0/d1`, und gibt
ebenso zurück. Solange QCC nur eigene Funktionen ruft, genügt die eigene
Konvention: ein `double` sind dann 8 Byte auf dem Stack. Sobald `printf("%f")`
aus der Microware-`clib` gerufen werden soll, muss die xcc-Konvention
nachgebildet werden — das betrifft aber nur den `CALLEXT`-Pfad und kann
später kommen.

**2. Genauigkeit.** Die 68k-FPU rechnet intern mit 80 Bit (`fadd.x`), die
VM in `qccvm.py` mit Pythons 64. Für Vergleichstests dürfen deshalb nur
Werte verwendet werden, die in beiden exakt sind, oder es muss nach jedem
Schritt auf `double` gerundet werden. Das ist kein Nebenschauplatz: sonst
weichen Orakel und Ziel in der letzten Stelle voneinander ab, und niemand
weiß, wer recht hat.

## Vorgeschlagene Reihenfolge

1. `tagSize('d') = 8` in beiden Backends und im VM-Orakel; `double`-Variablen
   als Block. Prüfbar, ohne dass eine einzige Rechnung stattfindet.
2. `PUSHD`, `LOADD`/`STORED`, `LOADGD`/`STOREGD` im Orakel und im Frontend;
   `tc_floatlit` ruft den Konverter statt zu melden.
3. Arithmetik und Vergleiche im Orakel, dann im 68k-Backend.
4. Konversionen, dann ARM64.
