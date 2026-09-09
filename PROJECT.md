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

### Phase 1: Grundstruktur

Ziele:
- IR-Parser (String → Opcode-Sequenz)
- Value Stack & Locals Storage
- Basis-Opcodes: PUSH, LOADL, STOREL, RET, PRINT
- Test gegen `01_basic.ir`

### Phase 2: Arithmetik & Logik

Opcodes:
- `ADD`, `SUB`, `MUL`, `DIV`, `MOD`
- `NEG`, `NOT`, `NOTBIT`
- `BAND`, `BOR`, `BXOR`, `SHL`, `SHR`, `USHR`
- `NARROWC`, `DUP`, `SWAP`, `DUPP`
- Test gegen `02_control_flow.ir`, etc.

### Phase 3: Control Flow

Opcodes:
- `LABEL <L>` — Sprungziel definieren
- `BEQ`, `BNE`, `BLT`, `BGT`, `BLE`, `BGE` — bedingte Sprünge
- `JMP <L>` — unbedingter Sprung
- Test gegen Schleifen/If-Then-Else

### Phase 4: Funktionen

Opcodes:
- `FUNC <name> <nargs> <nlocals>` — Funktionsanfang
- `ENDFUNC` — Funktionsende
- `CALL <name> <nargs>` — Aufruf
- `RET` — Rückkehr mit Wert
- Test gegen `01_basic.ir` (recursive square/add_and_square)

### Phase 5: Pointer & Arrays

Opcodes:
- `ADDRG <name>` — Globale Adresse
- `ADDRL <i>` — Lokale Adresse (Slot)
- `LOADIND <typtag>` — dereferenzieren
- `STOREIND <typtag>` — dereferenzieren & speichern
- `LOADIDX`, `STOREIDX` — Array-Zugriff
- `PADD`, `PDIFF` — Pointer-Arithmetik
- Test gegen `03_arrays.ir`, `05_pointers.ir`

### Phase 6: OS-9 Modul

- Wrapper für OS-9 Syscalls
- Q9-Datei laden/ausführen
- Integration mit OS-9 Standard Library

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

- **Status**: 🔨 Phase 1 (Start)
- **Zielplattform**: 68k (später auch ARM64, x86_32)
- **Ziel-OS**: OS-9
