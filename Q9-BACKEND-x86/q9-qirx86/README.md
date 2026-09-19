# q9-qirx86 – x86-32 Backend

`qirx86` übersetzt Q9 Stack-IR (`.ir`) in x86-32-Assemblertext (`.sx86`).

Die Optimierung, Assemblierung und das Linken sind separate Werkzeuge:

```text
.ir -> qirx86 -> .sx86 -> qox86 -> qrx86 -> .r -> qlx86 -> Modul
```

Die produktiven Quellen liegen unter `src/`.

Host-Build:

```sh
make -C Q9-BACKEND-x86/q9-qirx86
```

## Architektur

- **Assembler-Output**: Intel-Syntax x86-32
- **Stack-basiertes Modell**: IR-Stack wird via EAX/Stack emuliert (wie 68k, nur x86)
- **Position-Independent Code (PIC)**: CALL-$+5 + POP + LEA Trick für Datenzugriffe
- **Relocation**: Handled by Linker (qo/ql), not by backend

## IR Semantics

Authoritative source: `/Volumes/SSD1TB/projects/Q9-Forge/Q9-QCC/docs/IR_OPCODES.md`

Every backend (68k, ARM64, C, x86) must produce the same result as `tools/qccvm.py`.

## Unterstützte IR-Opcodes

### Stack & Memory
PUSH, LOADL, STOREL, LOADG, STOREG, ADDRL, ADDRG, LOADIND, STOREIND, LOADIDX, STOREIDX

### Arithmetik
ADD, SUB, NEG, MUL, DIV, MOD, UDIV, UMOD

### Vergleiche
CMPEQ, CMPNE, CMPLT, CMPLE, CMPGT, CMPGE, CMPULT, CMPULE, CMPUGT, CMPUGE

### Bitweise
BAND, BOR, BXOR, NOT, NOTBIT, SHL, SHR, USHR, NARROWC

### Kontrollflusss
JMP, JZ, JNZ, LABEL

### Funktionen
CALL, RET, RETP, PUSHFN, CALLP, CALLIND

### Speicher & Daten
GLOBAL, GARRAY, GINIT (parsed, linker handles relocation)

## Frame Layout (x86-32 cdecl)

```
[ESP]           = Top of stack
[EBP + 4]       = Return address
[EBP + 8+]      = Parameters (param0 at highest offset)
[EBP - 4:...]   = Local variables
```
