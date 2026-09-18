# qlv32 - RISC-V-32 Linker

Links OS-9000 relocatable objects (.r format) to produce executable OS-9000 modules.

## Usage

```bash
./qlv32 <input.r> -O=<output.module>
```

- Input: OS-9000 relocatable object (.r file)
- Output: Loadable OS-9000 executable module
- Processes DEADFACE-marked ROF files

## What It Does

1. **Parses ROF Header**: Reads and validates 56-byte OS-9000 header
2. **Verifies Sync**: Checks for DEADFACE (0xDEADFACE) sync word
3. **Extracts Symbols**: Builds global symbol table from ROF globals
4. **Calculates Layout**: Determines final addresses for code/data sections
5. **Merges Sections**: Combines code and data sections
6. **Generates Module**: Outputs final executable with updated header

## Input Format (ROF)

OS-9000 Relocatable Object Format:

```
[56-byte header with DEADFACE sync]
[PSECT name string]
[Global symbol table]
[Code section (binary)]
[Initialized data section (binary)]
[External symbol references]
[Local symbol references]
```

The linker verifies:
- DEADFACE sync at offset 0x00
- Header consistency (code size, data size, stack size)
- No remote storage (not supported yet)
- No debug info (not supported yet)

## Output Format (Module)

OS-9000 Executable Module (same format as ROF, but with resolved addresses):

```
[56-byte header with DEADFACE sync - updated]
[PSECT name]
[Merged global symbol table]
[Linked code section]
[Linked data section]
[Empty external symbol table]
[Empty local symbol table]
```

### Header Fields

```
Offset  Size  Description
0x00    4     DEADFACE sync (preserved)
0x04    2     Type/Language (from root ROF)
0x06    2     Attributes/Revision
0x08    2     Valid flag (0)
0x0A    2     Series (999)
0x0C    6     Timestamp (fixed: 126, 9, 16, 17, 57, 31)
0x12    2     Edition number
0x14    4     Static storage size (0)
0x18    4     Total initialized data size
0x1C    4     Total code size
0x20    4     Stack size
0x24    4     Entry point offset
0x28    4     Trap entry (-1)
0x2C    4     Remote static (0)
0x30    4     Remote idata (0)
0x34    4     Debug size (0)
```

## Symbol Table

Global symbols include:
- **Name**: NUL-terminated string
- **Type**: 0x0001 = data, 0x0004 = code
- **Address**: Absolute address in linked module

Symbol resolution:
- Searches backward through symbol table (last definition wins)
- Merges symbols from input ROF
- Preserves symbol types and ordering

## Layout Calculation

The linker calculates final addresses:

```
Code Base:  0
Code 0:     0 to [ROF[0].code_size)
Code 1:     [ROF[0].code_size] to [sum of all code]

Data Base:  After all code
Data 0:     [code_total] to [code_total + ROF[0].data_size)
Data 1:     ...

Symbols:    Updated with new base addresses
```

## Relocation

Current implementation:
- No relocation records processed
- Assumes all symbols are absolute
- First ROF defines entry point
- Type/language from root ROF propagated to output

Future:
- Process relocation records
- Resolve external references
- Handle indirect calls

## Example Usage

```bash
# Assemble to ROF
$ qov32 program.srv32 program.r
Generated .r module: program.r (116 bytes)

# Link to module
$ qlv32 program.r -O=program.module
Read 116 bytes from program.r
[ROF 0] PSECT: tc_prog  Type/Lang: 0x0000  Code: 32  Init: 4  Stat: 0
Total: 32 bytes code, 4 bytes initialized data
Wrote 116 bytes to program.module
OK

# Verify DEADFACE sync
$ od -An -tx1 program.module | head -1
de ad fa ce 00 00 00 00 00 00 03 e7 ...
```

## Symbol Resolution Example

ROF contains globals:
```
main:       address 0x0000, code
add:        address 0x0008, code
count:      address 0x0020, data
```

After linking:
```
main:       address 0x0000, code  (unchanged)
add:        address 0x0008, code  (unchanged)
count:      address 0x0020, data  (updated to 0x0020 + code_size if needed)
```

## Limitations

- Only supports single ROF input (no multi-file linking yet)
- No external symbol resolution
- No relocation record processing
- No dynamic linking
- Stack size is preserved from input (not calculated)
- All addresses treated as absolute

## Error Handling

Exits with error on:
- File not found
- Invalid ROF format (no DEADFACE sync)
- ROF too short for header
- Invalid header fields
- Unsupported features (remote storage, debug info)
- Symbol table overflow
- Memory allocation failure

Warnings:
- None (errors are fatal)

## Build

```bash
gcc -O2 -Wall -Wextra -o qlv32 src/qlv32.c
```

Or use included Makefile:
```bash
make
```

## Testing

```bash
# Create test ROF
qov32 test.srv32 test.r

# Link to module
./qlv32 test.r -O=test.module

# Verify format
od -tx1 test.module | head -1    # Look for: de ad fa ce
file test.module                 # Should show binary data
wc -c test.module                # Check size matches
```

## Debugging

Enable verbose output (stderr):
```bash
./qlv32 input.r -O=output.module 2>&1 | head -20
```

Shows:
- ROF header information
- Section sizes
- Symbol count
- Final module size
- Success/error message

## Technical Details

### Symbol Table Entry
```c
struct Symbol {
    int name;       /* Offset in symPool */
    int value;      /* Final address */
    int type;       /* 0=reserved, 1=data, 4=code */
    int rofIdx;     /* Which ROF defined this */
};
```

### ROF Info Parsing
The linker extracts:
- PSECT name and type
- Code section size and offset
- Data section size and offset
- Global symbol count and location
- External symbol count and location

### Module Generation
Outputs:
1. 56-byte header (big-endian)
2. PSECT name
3. Global symbol count (32-bit)
4. Symbol entries (name + type + value)
5. Code section bytes
6. Data section bytes
7. External count (4 bytes = 0)
8. Local count (4 bytes = 0)
9. Common count (4 bytes = 0)
