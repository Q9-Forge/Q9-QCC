# Small ISO C Work Units

*German version: [ISO_C_WORK_UNITS_de.md](ISO_C_WORK_UNITS_de.md)*

This list breaks the large [ISO C gap list](ISO_C_GAP_LIST.md) down into
tasks that can each be completed and tested in a manageable work session.
Preprocessor, library, optimizer, and additional backends are their own
subprojects; their scope is defined in [SUBPROJECTS.md](SUBPROJECTS.md). A
line is deliberately not a promise that it will always be finished in
exactly one session; it gets split into sub-units as needed.

## ISO abbreviations

- **C90**: ISO C90/C89 language core
- **C99**: additions from C99
- **C11**: additions from C11
- **C17**: essentially bug fixes relative to C11
- **C23**: the current later language version, deliberately kept separate for now

The mapping is a planning assignment, not a complete clause reference.
The exact standard reference is added during implementation.

## Prioritized work units

| ID | Small unit | ISO | Dependencies | Status |
|---|---|---|---|---|
| L1 | Character and string literals, escape sequences | C90 | lexer, global data | open / P0 |
| L2 | Integer suffixes and full number bases | C90/C99 | type model | partial / P0 |
| L3 | Preprocessor: `#define` without parameters | C90 | translation unit | open |
| L4 | Preprocessor: `#include` and include search | C90 | L3 | open |
| L5 | Preprocessor: conditional compilation | C90 | L3 | open |
| L6 | Function-like macros, `#` and `##` | C99 | L3 | open |
| T1 | Qualifiers `const`, `volatile`, `restrict` | C90/C99 | type model | open |
| T2 | `void` and `void *` | C90 | pointer model | open |
| T3 | `short`, `long`, `long long` and rank rules | C90/C99 | integer conversions | open |
| T4 | `float`, `double`, literals, and basic conversions | C90 | T3, backend | open |
| T5 | `typedef` and name resolution | C90 | declarators | open / P0 |
| T6 | `enum` and enumeration constants | C90 | T5 | open / P0 |
| T7 | `struct` declaration and member access `.` | C90 | T5, layout | open / P0 |
| T8 | Pointer to `struct` and `->` | C90 | T7, pointers | open / P0 |
| T9 | `union` and shared memory layout | C90 | T7 | open / P0 |
| T10 | Designated initializers | C99 | T7, initializers | open / P0 |
| T11 | Bitfields | C90 | T7, layout | open / P0 |
| T12 | Variable length arrays | C99 | arrays, stack frames | open |
| E1 | Pre-/post-increment and -decrement | C90 | lvalues, assignment | open |
| E2 | Cast expressions | C90 | type conversions | open |
| E3 | Integer conversions and usual arithmetic conversions | C90 | T3, E2 | partial |
| E4 | Pointer conversions and compatibility checks | C90 | T1/T2, E2 | partial |
| E5 | `sizeof` | C90 | type sizes, arrays | open |
| E6 | Comma operator and full lvalue rules | C90 | E1/E2 | open |
| E7 | Constant expressions for initializers and `case` | C90 | E3, control flow | open / P0 |
| S1 | `for` loop | C90 | labels/jumps | open |
| S2 | `do/while` | C90 | S1 | open |
| S3 | `break` and `continue` | C90 | S1/S2 | open |
| S4 | `switch`, `case`, `default` | C90 | E7, S3 | open |
| S5 | Labels and `goto` | C90 | control-flow IR | open |
| S6 | Full return type checking | C90 | function prototypes | partial |
| F1 | Function declarations and prototypes | C90 | type model | partial |
| F2 | Argument compatibility checking | C90 | F1, E3/E4 | open |
| F3 | Function pointers and indirect calls | C90 | F1, pointer IR | open |
| F4 | Variadic functions (`stdarg`) | C90/C99 | F2, runtime | open |
| O1 | Scope and linkage of local objects | C90 | symbol table | partial |
| O2 | `static` and `extern` within a file | C90 | O1 | open |
| O3 | Multiple translation units and external linkage | C90 | O2, object format | open |
| O4 | Aggregate initialization for arrays/structs | C90 | T7, T10 | partial / P0 |
| O5 | `static` and `const` initializers of global objects | C90 | O2, O4 | open |
| D1 | Constraint diagnostics and error classes | C90 | all frontend types | partial |
| D2 | Document implementation-defined/undefined/unspecified behavior | C90 | semantics | open |
| D3 | Make multi-file translation reproducible | C90 | O3, preprocessor | open |
| A1 | Struct/union layout in the IR and QCCVM | C90 | T7/T9 | open |
| A2 | Struct/union parameters and return values in the 68000 ABI | C90 | A1, backend | open |
| A3 | Same ABI in the ARM64 backend | C90 | A1/A2 | open |
| A4 | Floating-point IR and ARM64 codegen | C90 | T4 | open |
| A5 | Linker/object format for a first target system | C90 | O3, ABI | open |
| R1 | Minimal runtime: `memcpy`, `memset`, `memcmp` | C90 | A5 | open |
| R2 | Minimal I/O runtime and `stdio` subset | C90 | R1, target system | partial |
| R3 | Memory management (`malloc`/`free`) | C90 | pointers, runtime | open |
| R4 | Core headers (`stddef`, `stdint`, `limits`, `stdbool`) | C99/C11 | type model | open |
| C11-1 | `_Static_assert` | C11 | D1 | open |
| C11-2 | `_Alignas`, `_Alignof`, `_Atomic` basic syntax | C11 | T1, layout | open |
| C11-3 | `_Generic` | C11 | E2, type checking | open |
| C11-4 | Thread storage and `<threads.h>` | C11 | O2, runtime | open |
| C23-1 | C23 keywords, attributes, and new declaration forms | C23 | C17 core | open |
| C23-2 | C23 literals and changed preprocessor rules | C23 | L1-L6 | open |
| C23-3 | C23 library and `nullptr`-related additions | C23 | C17 library | open |

## Types as their own subproject

The "types" area is too big for a single work session. The sensible
order is:

For the currently desired priority, this applies first:

`L1/L2` → `T5/T6` → `T7/T8` → `T9` → `O4/T10` → `E7`

Followed by the general type expansion stage:

`T1/T2` → `T3` → `T4` → `T11` → `T12`

After each unit, at minimum type checking, IR sizes, QCCVM, and the
affected backends must be checked. Only after `T7` does it make sense to
plan structs as a basis for ABI work.

## Definition of a completed unit

A unit is complete when grammar/frontend, IR, QCCVM, and the affected
backends are consistent, at least one positive and one negative test
exist, and `./runtests.sh` continues to run successfully. For large
units, the entry is split into sub-units like `T7a`, `T7b`, etc.
