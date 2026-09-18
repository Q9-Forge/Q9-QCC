# Q9-QCC RISC-V-32 Backend

A complete RISC-V-32 compiler backend for Q9-QCC, targeting OS-9000 modules.

## Architecture

The backend consists of three tools that form a compilation pipeline:

### 1. qirv32 - IR to RISC-V Assembly (q9-qirv32/)
- **Input**: `.ir` (stack-based intermediate representation)
- **Output**: `.srv32` (RISC-V-32 assembly)
- **Purpose**: Translates abstract IR to RISC-V-32 assembly language
- **Key Features**:
  - Stack-based execution model
  - RISC-V calling convention (a0-a7 for args/returns, ra for return address)
  - Automatic stack frame management
  - Local variable support
  - Global variable handling
  - Function prologue/epilogue generation

### 2. qov32 - Assembler (q9-qov32/)
- **Input**: `.srv32` (RISC-V-32 assembly)
- **Output**: `.r` (OS-9000 relocatable object format)
- **Purpose**: Assembles RISC-V instructions to binary ROF format
- **Key Features**:
  - RISC-V instruction encoding (R/I/S/B/U/J types)
  - Register name aliasing (x0-x31, abi names)
  - Symbol table generation
  - DEADFACE sync word injection (0xDEADFACE)
  - Section management (code, initialized data)
  - Label resolution

### 3. qlv32 - Linker (q9-qlv32/)
- **Input**: `.r` (OS-9000 relocatable objects)
- **Output**: `.module` (OS-9000 executable module)
- **Purpose**: Links ROF objects to produce loadable OS-9000 modules
- **Key Features**:
  - Symbol table merging
  - Code/data section layout
  - OS-9000 header generation
  - 56-byte header with metadata
  - DEADFACE sync preservation
  - Relocation record support

## Compilation Pipeline

```
Source Code (C)
     ↓
Frontend (qcir)
     ↓
Intermediate Representation (.ir)
     ↓
qirv32 (IR → Assembly)
     ↓
RISC-V Assembly (.srv32)
     ↓
qov32 (Assembly → Object)
     ↓
Relocatable Object (.r)
     ↓
qlv32 (Object → Module)
     ↓
OS-9000 Module (.module)
```

## RISC-V Calling Convention

- **Arguments**: a0-a7 (8 registers)
- **Return value**: a0
- **Return address**: ra (x1)
- **Stack pointer**: sp (x2)
- **Caller-saved**: a0-a7, t0-t6
- **Callee-saved**: s0-s11, sp, ra
- **Frame pointer**: s0 (optional)

## OS-9000 Module Format

All modules include:
- **56-byte big-endian header**:
  - 0x00-0x03: DEADFACE sync word (0xDEADFACE)
  - 0x04-0x05: Type/Language (0x0000)
  - 0x06-0x07: Attributes/Revision
  - 0x08-0x09: Valid flag
  - 0x0A-0x0B: Series (999 for Q9 assembler)
  - 0x0C-0x11: Timestamp (6 bytes)
  - 0x12-0x13: Edition number
  - 0x14-0x17: Static storage size
  - 0x18-0x1B: Initialized data size
  - 0x1C-0x1F: Code section size
  - 0x20-0x23: Stack size (default 4096)
  - 0x24-0x27: Entry point
  - 0x28-0x2B: Trap entry (-1 if none)
  - 0x2C-0x2F: Remote static storage
  - 0x30-0x33: Remote initialized data
  - 0x34-0x37: Debug info size

- **PSECT Name** (NUL-terminated string)
- **Global Symbol Table** (count + entries)
- **Code Section** (binary machine code)
- **Initialized Data Section** (binary data)
- **External/Local Symbol Records** (0 count for now)

## Building

```bash
cd Q9-BACKEND-riscv
make              # Build all three tools
make clean        # Clean build artifacts
```

Individual tools:
```bash
cd q9-qirv32 && make      # Build IR backend
cd q9-qov32 && make       # Build assembler
cd q9-qlv32 && make       # Build linker
```

## Usage

### IR to Assembly
```bash
./q9-qirv32/build/qirv32 program.ir program.srv32
```

### Assembly to ROF
```bash
./q9-qov32/build/qov32 program.srv32 program.r
```

### ROF to Module
```bash
./q9-qlv32/build/qlv32 program.r -O=program.module
```

### Full Pipeline
```bash
qirv32 prog.ir prog.srv32 && \
qov32 prog.srv32 prog.r && \
qlv32 prog.r -O=prog.module
```

## Testing

### Test IR File
```
FUNC	main	0
	PUSH	5
	PUSH	10
	ADD
	RET
ENDFUNC
```

### Pipeline Verification
```bash
$ qirv32 test.ir test.srv32
OK: 5 IR lines, 1 funcs, 0 globals

$ qov32 test.srv32 test.r
Generated .r module: test.r (XX bytes)
OK: XX bytes code, 0 bytes data, 0 symbols

$ qlv32 test.r -O=test.module
Read XX bytes from test.r
[ROF 0] PSECT: tc_prog  Type/Lang: 0x0000  Code: XX  Init: 0  Stat: 0
Total: XX bytes code, 0 bytes initialized data
Wrote XX bytes to test.module
OK

$ od -An -tx1 test.module | head -1
de ad fa ce ...  # DEADFACE sync verified!
```

## Supported IR Instructions

### Stack Operations
- PUSH <value>
- LOADL <slot> (load local)
- STOREL <slot> (store local)
- LOADG <name> (load global)
- STOREG <name> (store global)
- DROP (pop without storing)
- DUP (duplicate top of stack)
- SWAP (exchange top two values)

### Memory Operations
- LOADIND <type> (load via pointer)
- STOREIND <type> (store via pointer)
- LOADIDX <type> (load array element)
- STOREIDX <type> (store array element)
- ADDRL <slot> (address of local)
- ADDRG <name> (address of global)

### Arithmetic
- ADD, SUB, MUL, DIV, MOD
- UDIV, UMOD (unsigned)
- AND, OR, XOR, NOT, NOTB
- SHL, SHR, SHRA (shift operations)

### Comparisons
- CMPEQ, CMPNE, CMPLT, CMPLE, CMPGT, CMPGE
- CMPULT, CMPULE, CMPUGT, CMPUGE (unsigned)
- PCMPEQ, PCMPNE, ... (pointer comparisons)

### Control Flow
- LABEL <name>
- CALL <func> <nargs>
- CALLIND <nargs> (call function pointer)
- CALLEXT <func> <nargs> (external C call)
- RET, RETP (return)

### Function Structure
- FUNC <name> <nargs> [static]
- ENDFUNC
- FUNCDECL <name> <nargs> (forward declaration)

### Data
- GLOBAL <name> [init] [type] [static]
- GARRAY <name> <type> <length> [static]
- GLOBALDECL <name> <type> [static]

## Implementation Notes

### Stack Layout
```
[sp]            <- top of stack (grows down)
[sp-4]          <- local 0
[sp-8]          <- local 1
...
[fp]            <- frame pointer
[fp+4]          <- return address
```

### Instruction Encoding
- R-type: funct7(7) | rs2(5) | rs1(5) | funct3(3) | rd(5) | opcode(7)
- I-type: imm(12) | rs1(5) | funct3(3) | rd(5) | opcode(7)
- S-type: imm[11:5] | rs2(5) | rs1(5) | funct3(3) | imm[4:0] | opcode(7)
- B-type: imm[12] | imm[10:5] | rs2(5) | rs1(5) | funct3(3) | imm[4:1] | imm[11] | opcode(7)
- U-type: imm[31:12](20) | rd(5) | opcode(7)
- J-type: imm[20] | imm[10:1] | imm[11] | imm[19:12] | rd(5) | opcode(7)

### Register Aliases
- x0 = zero (hardwired to 0)
- x1 = ra (return address)
- x2 = sp (stack pointer)
- x3 = gp (global pointer)
- x5-x7 = t0-t2 (temporary)
- x8 = s0/fp (saved/frame pointer)
- x9 = s1 (saved)
- x10-x17 = a0-a7 (arguments)
- x18-x27 = s2-s11 (saved)
- x28-x31 = t3-t6 (temporary)

## Limitations & Future Work

- Memory addressing modes (lw/sw with offset) need better parsing
- No relocation records yet (linking is basic)
- No external symbol resolution
- No floating-point support
- Stack size is hardcoded to 4096 bytes
- No optimization passes

## Version History

- **v0.1** (2026-09-17): Initial implementation
  - Three-tool pipeline complete
  - RISC-V instruction encoding working
  - OS-9000 module format compatible
  - DEADFACE sync confirmed
  - Basic testing verified

## Technical References

- RISC-V ISA Specification: https://riscv.org/
- OS-9000 Module Format: Microware documentation
- Q9-QCC Frontend: qcir produces .ir format
- Q9 IR Specification: docs/IR_OPCODES.md

## License

Part of Q9-QCC compiler suite. Follows same license as Q9-Forge project.
