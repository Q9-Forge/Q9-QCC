# QCC Stack IR — Opcode Reference

*German version: [IR_OPCODES_de.md](IR_OPCODES_de.md)*

Status: **2026-07-25**

Detailed reference document for the text IR that sits between the
generated QCC frontend parser and the backends (QCCVM, 68000, ARM64, C).
Short version embedded in the overall context: `docs/ARCHITEKTUR.md`
section 10 (10.5 shows the same opcode set more compactly).

## Model

- **Operand stack machine**, no register model. Every line is an opcode,
  optionally followed by arguments, separated by whitespace. Comment
  lines start with `;` or `#`.
- **Values and pointers are separate concepts.** A pointer is internally
  a `(block, offset)` pair (see the `Pointer` class in `tools/qccvm.py`),
  not a plain integer -- pointer arithmetic runs through its own opcodes
  (`PADD`/`IPADD`/`PSUB`/`PDIFF`), not `ADD`/`SUB`.
- **Type tags** (`<typetag>`), where relevant: `c`/`b` = 1 byte (char/bool),
  `p` = pointer width (architecture-dependent, smaller on 68k than
  ARM64), everything else = 4 bytes (int/unsigned/enum).
- **Canonical semantics source:** `tools/qccvm.py` -- every backend
  codegen (68k, ARM64, C) must produce the same result as the QCCVM
  interpreter for the same IR. When in doubt about an opcode's
  semantics, check there rather than guessing.
- Two opcode families are **backend-only** and cannot be executed by
  QCCVM: `CALLEXT`/`CALLEXTP` (real `extern` calls against Microware
  `clib.l`, 68k only) and `FUNCDECL`/`GLOBALDECL` (multi-file forward
  declarations without a body -- pure backend/linker information).

## Program structure / declarations

| Opcode | Stack effect | Description |
|---|---|---|
| `GLOBAL <name> [init]` | — | global scalar variable, optional initial value |
| `GARRAY <name> <typetag> <len>` | — | global array of fixed length |
| `GINIT <name> <idx> <value>` | — | initial value for an array element (multiple per array) |
| `FUNC <name> <nargs>` | — | start of function; slots `0..nargs-1` = parameters |
| `ENDFUNC` | — | end of function (frame size = highest slot+1, determined by the backend) |
| `LABEL <L>` | — | defines jump target `L` |
| `FUNCDECL <name> <argc>` | — | forward declaration without a body (multi-file/mutual recursion); backend-only |
| `GLOBALDECL <type> <name>` | — | `extern` variable, no allocation of its own; backend-only |

## Loading/storing values

Local (`L`) and global (`G`), each split by int/char/pointer:

| Opcode | Stack effect | Description |
|---|---|---|
| `PUSH <n>` | `→ n` | integer constant |
| `LOADL <i>` / `STOREL <i>` | `→ v` / `v →` | local int slot |
| `LOADC <i>` / `STOREC <i>` | `→ v` / `v →` | local char slot (masked to `0xff`) |
| `LOADP <i>` / `STOREP <i>` | `→ p` / `p →` | local pointer slot |
| `LOADG <name>` / `STOREG <name>` | `→ v` / `v →` | global int variable |
| `LOADGC <name>` / `STOREGC <name>` | `→ v` / `v →` | global char variable |
| `LOADGP <name>` / `STOREGP <name>` | `→ p` / `p →` | global pointer |
| `LARRAY` | — | reserve a local array |

## Addresses, arrays, pointers

| Opcode | Stack effect | Description |
|---|---|---|
| `ADDRL <i>` | `→ p` | address of a local slot |
| `ADDRG <name>` | `→ p` | address of a global variable |
| `PUSHADDR L/G/P <i>` | `→ p` | address of a local/global array, or of a pointer value itself |
| `LOADIDX L/P/G <i> <typetag>` | `idx → v` | read an array element, index from the stack |
| `STOREIDX L/P/G <i> <typetag>` | `idx, v →` | write an array element |
| `PTRINDEX <typetag>` | `p, idx → p'` | pointer+index → scaled address (real pointer, `p[i]` pattern) |
| `LOADIND <typetag>` | `p → v` | dereference through a pointer (read) |
| `STOREIND <typetag>` | `p, v →` | dereference through a pointer (write) |
| `PADD <typetag>` | `p, n → p'` | pointer + integer, scaled by the fixed type size |
| `IPADD <typetag>` | `n, p → p'` | like `PADD`, operands in the other order on the stack |
| `IPADDN <bytesize>` | `n, p → p'` | like `IPADD`, but with a **runtime** byte size instead of a fixed type-tag size (needed for `arr[i].field` on arrays of structs, since `IPADD` only knows fixed type-tag sizes) |
| `PSUB <typetag>` | `p, n → p'` | pointer − integer |
| `PDIFF <typetag>` | `p1, p2 → n` | pointer − pointer → scaled integer difference (both must belong to the same block) |

## Arithmetic/logic

| Opcode | Stack effect | Description |
|---|---|---|
| `ADD` `SUB` `MUL` `DIV` `MOD` | `a, b → r` | signed, `DIV`/`MOD` round toward 0 (C semantics, not floor) |
| `UDIV` `UMOD` | `a, b → r` | unsigned variants (32-bit) |
| `NEG` | `a → -a` | unary minus |
| `NOT` | `a → r` | logical not → 0/1 |
| `NOTBIT` | `a → r` | bitwise complement (32-bit) |
| `BAND` `BXOR` `BOR` | `a, b → r` | bitwise and/xor/or (32-bit) |
| `SHL` | `a, n → r` | shift left |
| `SHR` | `a, n → r` | shift right, signed/arithmetic |
| `USHR` | `a, n → r` | shift right, unsigned/logical |
| `NARROWC` | `a → r` | narrow to one byte (char assignment/cast) |
| `DUP` | `a → a, a` | duplicate the top stack element (value) |
| `SWAP` | `a, b → b, a` | swap the top two stack elements |
| `DUPP` | `p → p, p` | like `DUP`, for pointers (semantically identical, its own opcode for clarity in the backend) |

## Comparisons

All comparisons: `a, b → 0|1`.

| Family | Opcodes |
|---|---|
| signed int | `CMPLT` `CMPGT` `CMPLE` `CMPGE` `CMPEQ` `CMPNE` |
| unsigned int | `CMPULT` `CMPUGT` `CMPULE` `CMPUGE` |
| pointer | `PCMPEQ` `PCMPNE` `PCMPLT` `PCMPLE` `PCMPGT` `PCMPGE` (with block-identity check; `EQ`/`NE` additionally allow comparison against the null pointer) |

## Control flow / function calls

| Opcode | Stack effect | Description |
|---|---|---|
| `JMP <L>` | — | unconditional jump |
| `JZ <L>` | `a →` | jump if `a == 0` |
| `JNZ <L>` | `a →` | jump if `a != 0` |
| `CALL <name> <nargs>` | `a1..aN → r` | arguments pushed left→right, result on the stack |
| `CALLP <name> <nargs>` | `a1..aN → p` | like `CALL`, result is a pointer (pure marker for the backend) |
| `PUSHFN <name>` | `→ p` | address of a QCC function as a value (function pointer); in `-largedata` mode computed via the function indirection table, otherwise PC-relative |
| `CALLIND <nargs>` / `CALLINDP <nargs>` | `a1..aN, f → r`/`p` | indirect call; the function pointer sits ON TOP (above the arguments), below it the arguments left→right as with `CALL` |
| `RET` / `RETP` | `r →` | return value from the stack, tear down the frame, back to the caller |
| `CALLEXT <name> <argc> <...>` / `CALLEXTP ...` | `a1..aN → r`/`p` | call a real `extern` function via the Microware ABI (fixed parameters in `d0`/`d1`, only the variadic `"..."` overflow on the stack); backend-only (68k) |

## Miscellaneous

| Opcode | Stack effect | Description |
|---|---|---|
| `DROP` | `a →` | discard the top stack value (unused expression result) |
| `PRINT` | `a →` | debug output as signed int (builtin `putint`) |
| `PRINTU` | `a →` | debug output as unsigned int (builtin `putuint`) |
| `PRINTC` | `a →` | debug output as a character (builtin `putchar`) |

## See also

- `docs/ARCHITEKTUR.md` section 10 -- how the IR comes about, emission
  patterns (how parser actions produce the IR), function ABI
  (slots/frames).
- `tools/qccvm.py` -- reference interpreter, also the test oracle for
  all backends.
- `docs/SELFHOSTING_GAP_LIST.md` / `[[qcc-vollport-status]]` (memory)
  -- context for the ongoing QCC full port of `codegen.cpp`/`parsec.cpp`.
