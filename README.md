# Q9-Run — Stack-IR Interpreter für QCC

Ein **Stack-basierter Interpreter für die QCC-Zwischenrepräsentation (IR)** in C.

## Übersicht

Q9-Run führt `.ir`-Dateien aus, die vom QCC-Frontend generiert werden. Der Interpreter:

- ✅ Lädt und parst Stack-IR in Text-Form
- ✅ Führt alle QCC-IR-Opcodes aus (Stack-Maschine)
- ✅ Simuliert lokale/globale Speicher, Funktionsaufrufe
- ✅ Ist eine **kanonische Referenz-Implementierung** (wie `qccvm.py`, aber in C)
- ✅ Kann später als **OS-9 Modul** laufen (68k-Ziel)

## Struktur

```
Q9-Run/
├── Source/           # C-Quelldateien
│   ├── qrun_main.c   # Hauptprogramm
│   ├── qrun_vm.c     # VM-Engine (Fetch-Decode-Execute)
│   ├── qrun_ir.c     # IR-Parser
│   └── qrun_vm.h     # Header & API
├── tests/            # Unit Tests
├── examples/         # Test-IR-Dateien (von Q9-QRun)
├── docs/             # Dokumentation
├── build/            # Build-Output
└── tools/            # Hilfsskripte
```

## Roadmap

- **Phase 1**: IR-Parser + Stack-Grundstruktur
- **Phase 2**: Arithmetic/Logic Opcodes (ADD, MUL, DIV, etc.)
- **Phase 3**: Control Flow (Labels, Branches)
- **Phase 4**: Function Calls (FUNC, ENDFUNC, CALL)
- **Phase 5**: Pointers & Arrays (LOADIND, STOREIDX)
- **Phase 6**: OS-9 Modul Integration

## Quellenverweise

- **IR-Referenz**: [Q9-QRun](../Q9-QRun/docs/IR_OPCODES_de.md)
- **Kanonische Semantik**: `Q9-QCC/tools/qccvm.py`
- **Architektur**: [Q9-Parsec ARCHITEKTUR.md](../Q9-Parsec/docs/ARCHITEKTUR.md)

## Build & Test

```bash
make
make test
```

## Status

- 🔨 **In Progress** — Phase 1 (IR-Parser + Basic Stack)
