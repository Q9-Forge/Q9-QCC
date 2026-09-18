# qov32 - RISC-V-32 Assembler

Assembles RISC-V-32 assembly language to OS-9000 relocatable object format (.r).

## Usage

```bash
./qov32 <input.srv32> <output.r>
```

- Input: RISC-V assembly with directives
- Output: OS-9000 relocatable object in binary format
- Always produces DEADFACE sync word (0xDEADFACE)

## Input Format

### Directives

```asm
nam		<moduleName>    # Set module name
psect	<name>,<type>,<attr>,<edition>,<stack>,<entry>
		                # Start code section
vsect                       # Start data section
tends                       # End data section
dc.l	<value> [,<value>]* # Initialize 4-byte word(s)
ds.l	<count>             # Reserve 4-byte word(s)
```

### Labels

```asm
label:
	instruction
```

### Instructions

Supported RISC-V instructions:

**Arithmetic (R-type)**:
- add, sub, mul, div, divu, rem, remu

**Logical (R-type)**:
- and, or, xor, not

**Shift (R-type)**:
- sll, srl, sra

**Immediate (I-type)**:
- addi, andi, ori, xori
- lw (load word)
- lhu (load halfword unsigned)
- lbu (load byte unsigned)

**Store (S-type)**:
- sw (store word)
- sh (store halfword)
- sb (store byte)

**Branch (B-type)**:
- beq, bne, blt, bge, bltu, bgeu

**Jump (J-type)**:
- jal (jump and link)
- jalr (jump and link register)

**Pseudo-Instructions**:
- li (load immediate)
- la (load address)
- ret (return)
- call (call function)

## Output Format

OS-9000 Relocatable Object (.r format):

```
[56-byte big-endian header]
[PSECT name (NUL-terminated)]
[Global symbols count + list]
[Code section (binary)]
[Data section (binary)]
[External symbols (0 for now)]
[Local symbols (0 for now)]
```

### Header Layout

```
Offset  Size  Field
0x00    4     DEADFACE sync (0xDEADFACE)
0x04    2     Type/Language
0x06    2     Attributes/Revision
0x08    2     Valid flag
0x0A    2     Series
0x0C    6     Timestamp (Y-1900, M, D, H, Min, S)
0x12    2     Edition
0x14    4     Static storage size
0x18    4     Initialized data size
0x1C    4     Code size
0x20    4     Stack size
0x24    4     Entry point
0x28    4     Trap entry
0x2C    4     Remote static
0x30    4     Remote idata
0x34    4     Debug size
```

## RISC-V Instruction Encoding

### R-type Format
```
[funct7: 7][rs2: 5][rs1: 5][funct3: 3][rd: 5][opcode: 7]
```

### I-type Format
```
[imm: 12][rs1: 5][funct3: 3][rd: 5][opcode: 7]
```

### S-type Format (Store)
```
[imm[11:5]: 7][rs2: 5][rs1: 5][funct3: 3][imm[4:0]: 5][opcode: 7]
```

### B-type Format (Branch)
```
[imm[12]|imm[10:5]: 7][rs2: 5][rs1: 5][funct3: 3][imm[4:1]|imm[11]: 5][opcode: 7]
```

### U-type Format
```
[imm[31:12]: 20][rd: 5][opcode: 7]
```

### J-type Format (JAL)
```
[imm[20]|imm[10:1]|imm[11]|imm[19:12]: 20][rd: 5][opcode: 7]
```

## Register Names

- **x0-x31**: Register indices (x0 = zero, x1 = ra, x2 = sp, etc.)
- **zero, ra, sp, gp, tp**: Special names
- **t0-t6**: Temporary registers
- **s0-s11**: Saved registers
- **a0-a7**: Argument/return registers
- **fp**: Frame pointer (same as s0)

## Example

Input (test.srv32):
```asm
	nam	example
	psect	example,0,0,1,4096,0

start:
	li	a0, 5
	add	a0, a0, a1
	beq	a0, a1, start
	jalr	x0, 0(ra)

	vsect
	dc.l	42
	tends
```

Output (test.r):
- Binary file with DEADFACE sync at offset 0x00
- Code section contains encoded RISC-V instructions
- Data section contains initialized values
- Global symbol table lists "start" and any referenced labels

## Limitations

- Memory addressing modes (lw offset(reg) format) have limited support
- No relocation records yet (linking relies on absolute positions)
- Labels must be on separate lines (cannot appear inline)
- 12-bit immediates only (li pseudo-instruction handles 20-bit loading)
- No support for external symbols yet

## Warnings

- Unknown instructions are reported but don't cause errors
- Missing operands will silently use 0

## Build

```bash
gcc -O2 -Wall -Wextra -o qov32 src/qov32.c
```

Or use included Makefile:
```bash
make
```

## Testing

```bash
./qov32 test.srv32 test.r
# Check output with: od -tx1 test.r | head -1
# Should show: de ad fa ce ...
```
