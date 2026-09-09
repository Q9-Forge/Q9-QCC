# Q9-Run — Projekt-Dokumentation

## Zweck

**Q9-Run** ist eine Referenz-Implementierung eines Stack-basierten Interpreters für die QCC Intermediate Representation (IR).

Während die 68k- und ARM64-Backends den IR in echten Maschinencode übersetzen, führt Q9-Run den IR direkt aus. Dies ermöglicht:

1. **Schnelle Iteration** beim Compiler-Frontend-Development (kein Assemble/Link)
2. **Debugging** durch direkte IR-Ausführung
3. **Validierung** gegen die kanonische Semantik (`qccvm.py`)
4. **Cross-Platform Testing** (nicht an 68k/ARM64 Codegen gekoppelt)
5. **OS-9 Integration** — Q9-Run läuft später als eigenständiges OS-9 Modul

## IR-Format

Die Stack-IR ist ein textbasiertes Format mit Opcodes und Operanden:

```
FUNC main 0 0          ; Funktion "main" mit 0 Parametern
PUSH 42                ; Konstante auf Stack
PRINT                  ; Ausgeben (putint)
PUSH 0
RET                    ; Return mit Wert 0
ENDFUNC
```

**Vollständige Referenz**: `docs/IR_OPCODES_de.md`

## Architektur

### VM-Kern

Die Virtual Machine besteht aus:

- **Opcode-Stream** — Sequenz von Befehlen (Parsing aus `.ir`)
- **Stack** — Für Werte (32-Bit), Pointer (variable Breite), Boolean
- **Locals** — Slot-basierte lokale Variablen pro Frame
- **Globals** — Speicher für globale Variablen und Arrays
- **Call Stack** — Für Funktionsaufrufe (Return-Address, Frame-Pointer)

### Fetch-Decode-Execute Schleife

```c
while (!halted) {
    opcode = fetch();
    args = fetch_args(opcode);
    dispatch(opcode, args);  // PUSH, ADD, CALL, etc.
}
```

### Speicher-Layout

```
┌─────────────────────────────────────────┐
│ Globals (GLOBAL, GARRAY, GINIT)         │
├─────────────────────────────────────────┤
│ Heap (nicht in dieser Phase)            │
├─────────────────────────────────────────┤
│ Call Stack (Frame + Locals)             │
├─────────────────────────────────────────┤
│ Value Stack                             │
└─────────────────────────────────────────┘
```

## Implementation

### Phase 1: Grundstruktur (COMPLETE ✅)

**Status**: Fully implemented

Opcodes implemented:
- `PUSH <n>` — Push constant
- `LOADL <i>` — Load local variable
- `STOREL <i>` — Store local variable
- `PRINT` — Print integer (putint)
- `RET` — Return from function
- Arithmetic: `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `NEG`
- Comparison: `EQ`, `NE`, `LT`, `LE`, `GT`, `GE`

**Features**:
- IR-Parser (String → Opcode-Sequenz)
- Value Stack & Locals Storage (dynamic slots)
- String pool for name interning
- Basic fetch-decode-execute loop

**Tested**: Simple arithmetic, printing, local variables

### Phase 2: Arithmetik & Logik (COMPLETE ✅)

**Status**: Fully implemented and tested

Opcodes implemented:
- Arithmetic: `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `NEG`
- Comparisons (signed): `CMPEQ`, `CMPNE`, `CMPLT`, `CMPLE`, `CMPGT`, `CMPGE`
- Comparisons (unsigned): `CMPULT`, `CMPUGE`, `CMPULE`, `CMPUGT` (not yet used)
- Stack ops: `DUP`, `SWAP` (from Phase 1, still working)

**Technical features**:
- Integer arithmetic with proper overflow/underflow semantics
- Division by zero detection
- Comparison results as 0/1 (boolean)
- Legacy aliases (EQ/NE/LT/etc.) for backward compatibility

**Tested**:
- Arithmetic: 10+5=15, 20-3=17, 7×6=42, 30÷4=7, 17%5=2, -42 ✓
- Comparisons: All 6 signed comparisons with equality/inequality cases ✓
- Matches qccvm.py reference output exactly ✓

**Deferred to Phase 3**: Bit operations (NOTBIT, BAND, BOR, BXOR), narrowing (NARROWC)

### Phase 3: Control Flow (COMPLETE ✅)

**Status**: Fully implemented and tested

Opcodes implemented:
- `LABEL <name>` — Define jump target
- `JMP <label>` — Unconditional jump
- `BEQ <label>` / `JZ <label>` — Jump if stack-top == 0
- `BNE <label>` — Jump if stack-top != 0

**Technical features**:
- Label lookup table (built on IR load)
- PC management with conditional increment (jumps set should_increment=0)
- Proper return address tracking for nested calls
- Return address saved AFTER opcode fetch but BEFORE PC increment

**Tested**:
- Simple jumps (JMP, forward/backward)
- Conditional jumps (BEQ/JZ with true/false conditions)
- Nested function calls with control flow (02_control_flow.ir)
- max() function with conditional return
- sum_below() function with loops
- Output: 7, 10 matches qccvm.py reference exactly ✓

**Key fix**: Return address calculation (code_addr = pc + 1) due to PC being fetched then used for dispatch

### Phase 4: Funktionen (COMPLETE ✅)

**Status**: Fully implemented and tested

Opcodes implemented:
- `FUNC <name> <nargs> <nlocals>` — Funktionsanfang
- `CALL <name> <nargs>` — Aufruf
- `RET` — Rückkehr mit Wert
- `ENDFUNC` — Funktionsende (skip marker)

**Technical features**:
- Function lookup table (built on IR load)
- Frame management with dynamic local slots (0..255 per frame)
- Return address stack for nested calls
- String pool lifetime management (kept alive in VM)

**Tested**: `01_basic.ir` passing (recursive calls: square/add_and_square)
- Output: `25, 25` ✓ matches qccvm.py reference

**Next**: Phase 2 (Arithmetik), then Phase 3 (Control Flow) as they are prereqs for more complex tests

### Phase 5: Pointer & Arrays (not started)

### Phase 6: OS-9 Modul (not started)

## Test-Strategie

Jede Phase hat **Beispiel-IR-Dateien** aus `Q9-QRun/examples/`:

1. `01_basic.ir` — FUNC/CALL/RET/PRINT
2. `02_control_flow.ir` — Labels & Branches
3. `03_arrays.ir` — LARRAY/LOADIDX/STOREIDX
4. `04_structs.ir` — Struct-Layouts mit Offsets
5. `05_pointers.ir` — Pointer-Operationen
6. `06_strings.ir` — Globale String-Arrays
7. `07_multifile_*` — FUNCDECL/GLOBALDECL

**Validierung**: Jeder Test vergleicht die Q9-Run Ausgabe mit der erwarteten `qccvm.py` Ausgabe.

## Files

### Source/

- `qrun_main.c` — Entry Point, Datei-Laden, main()
- `qrun_ir.c/h` — IR-Parser (String → AST/Opcode-Array)
- `qrun_vm.c/h` — VM-Engine (Fetch-Decode-Execute, Dispatch)
- `qrun_runtime.c/h` — Runtime (PRINT, Built-ins, Speicher)
- `qrun_stack.c/h` — Stack & Locals Management

### tests/

- Unit-Tests für jeden Opcode
- Integration-Tests gegen `.ir` Dateien

### docs/

- `ARCHITECTURE.md` — VM-Architektur detailliert
- `OPCODES_QUICK.md` — Opcode-Zusammenfassung
- `DEBUGGING.md` — Debugging-Tipps (Trace-Mode, etc.)

## Zeitrahmen

Keine Schätzungen, aber die Phasen bauen linear aufeinander auf. Phase 1-3 sind fundamentaler, 4-5 Erweiterungen, Phase 6 ist später (OS-9 Integration).

## Abhängigkeiten

- **Intern**: Q9-QRun (Dokumentation & Beispiele)
- **Extern**: ANSI C Compiler (clang/gcc)
- **Referenz**: `Q9-QCC/tools/qccvm.py` für Semantik-Validierung

## Commit-Strategie

Nach jeder Phase:
1. Alle Dateien müssen compilieren (`make`)
2. Tests müssen durchlaufen (`make test`)
3. Dokumentation aktualisieren
4. Commit mit beschreibender Message

Beispiel:
```
Phase 1: IR-Parser & Basic Stack

- IR-Parser (PUSH, LOADL, STOREL, RET, PRINT)
- Stack & Locals Storage
- Test gegen 01_basic.ir erfolgreich
```

## Autoren & Status

- **Status**: 🎯 Phase 3 (Control Flow) COMPLETE ✅, Phases 1-4 DONE ✅✅✅✅
- **Zielplattform**: 68k (später auch ARM64, x86_32)
- **Ziel-OS**: OS-9
- **Latest Tests**: 02_control_flow.ir (max/sum_below) ✅, Jump/Branch logic ✅
