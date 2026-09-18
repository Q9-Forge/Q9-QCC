# qirv32 - RISC-V-32 IR Backend

Translates Q9-QCC intermediate representation (.ir) to RISC-V-32 assembly language.

## Usage

```bash
./qirv32 <input.ir> [output.srv32]
```

- If output file is not specified, writes to stdout
- Reads tab-separated IR instructions
- Generates AT&T syntax RISC-V assembly with tabs

## Input Format

Line-based text format with tab-separated opcodes and arguments:

```
FUNC	functionName	numArgs
	IR_OPCODE	arg1	arg2	...
	...
ENDFUNC

GLOBAL	varName	initialValue	type
```

## Output Format

RISC-V-32 assembly with directives:

```asm
	nam	tc_prog
	psect	tc_prog,0,0,1,0,0

functionName:
	<RISC-V instructions>

	vsect
varName:
	dc.l	initialValue
	tends
```

## Key Features

- Stack-based execution model (uses sp, ra, a0)
- Automatic frame size calculation
- Local variable slot assignment
- Global variable symbol generation
- RISC-V calling convention support

## RISC-V Registers Used

- **a0**: Temporary/return value
- **a1**: Temporary
- **sp**: Stack pointer (x2)
- **ra**: Return address (x1)
- **t0**: Temporary
- **t1**: Temporary

## Stack Frame Layout

```
[sp]           <- stack top (after prologue adjustment)
[sp-4]         <- local variable slot 0
[sp-8]         <- local variable slot 1
...
[sp-N]         <- saved return address (for nested calls)
```

## Error Handling

Exits with error on:
- Malformed IR (invalid opcodes, argument count mismatch)
- Buffer overflow (MAX_IR_LINES, MAX_GLOBALS, MAX_FUNCS)
- Invalid local variable indices
- Duplicate function/global names

## Example

Input (test.ir):
```
FUNC	add	2
	LOADL	0
	LOADL	1
	ADD
	RET
ENDFUNC
```

Output (test.srv32):
```
	nam	tc_prog
	psect	tc_prog,0,0,1,0,0

add:
	addi	sp, sp, -4
	sw	ra, 0(sp)

	; LOADL 0
	lw	a0, -4(sp)
	...
	; RET
	lw	a0, 0(sp)
	lw	ra, 0(sp)
	addi	sp, sp, 4
	jalr	x0, 0(ra)
```

## Limitations

- Only handles 32-bit integers
- Local arrays require explicit frame size calculation
- No inline assembly
- No optimization

## Build

```bash
gcc -O2 -Wall -Wextra -o qirv32 src/qcc_backend_riscv.c
```

Or use included Makefile:
```bash
make
```
