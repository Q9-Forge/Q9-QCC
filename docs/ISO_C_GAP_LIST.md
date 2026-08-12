# ISO C Gap List and Target Planning

*German version: [ISO_C_GAP_LIST_de.md](ISO_C_GAP_LIST_de.md)*

Status: **2026-07-22**

## Target definition

"Complete ISO compiler" first needs a fixed language version and target
system. For planning purposes we use **ISO C17** as the complete language
base for now. C23 is then treated as its own extension block afterward.

A compiler consists of at least four separate areas:

1. Translation of the C language (frontend and code generator)
2. Preprocessor and translation units
3. ABI, linker integration, and target runtime
4. C standard library

QCC currently covers only a small, executable core of area 1.

## Legend

- **done**: implemented and tested in the current QCC path
- **partial**: basic functionality present, ISO semantics still incomplete
- **open**: not yet implemented
- **separate**: primarily belongs to the preprocessor, ABI, or library

## 1. Lexing and preprocessor

| Topic | Status | Priority |
|---|---|---|
| Comments and whitespace | done | — |
| Integer, character, and bool literals | partial | high |
| String literals and escape sequences | open | high |
| Floating literals and floating-point types | open | high |
| Complete token rules/character sets | partial | high |
| `#include`, macros, conditional compilation | open | very high |
| `#define` with parameters and `##`/`#` | open | high |
| `#pragma`, predefined macros, include search | open | medium |

## 2. Types and declarators

| Topic | Status | Priority |
|---|---|---|
| `char`, signed/unsigned integers | partial | high |
| `short`, `long`, `long long` | open | high |
| `_Bool` and qualifiers | partial/open | high |
| `float`, `double`, `long double` | open | high |
| Pointers and pointer arithmetic | done | — |
| Arrays and array decay | partial | very high |
| Function pointers | open | high |
| `void` and `void *` | open | high |
| `struct`, `union`, `enum` | partial (struct with mixed scalar field types done 2026-07-24, `enum` done; `union`, array/pointer fields, and nested structs open) | very high |
| `typedef` | done (scalar/pointer aliases, `typedef struct Name Alias;`, `typedef struct { ... } Name;` inline anonymous since 2026-07-24) | — |
| Bitfields and `_Alignas`/`_Alignof` | open | medium |
| Variable length arrays | open | medium |

## 3. Expressions and operators

| Topic | Status | Priority |
|---|---|---|
| Arithmetic operators | done | — |
| Comparisons and equality | done | — |
| `&&`, `||` with short-circuit | done | — |
| Bitwise operators and shifts | done | — |
| Assignments and compound assignments | done | — |
| Pre-/post-increment and -decrement | done (simple int/unsigned/char scalars only) | — |
| Casts and implicit conversions | partial (int/unsigned/char/bool only, no pointer/typedef as cast target) | very high |
| `sizeof` and `_Alignof` | partial (`sizeof` on int/char/bool/unsigned/struct, no pointers, no `_Alignof`) | high |
| Comma operator | open | medium |
| Full constant expressions | open | high |
| Sequencing and undefined-behavior rules | open | very high |

## 4. Statements and functions

| Topic | Status | Priority |
|---|---|---|
| Expression statements, block, `if/else`, `while` | done | — |
| `for` and `do/while` | done | — |
| `switch`, `case`, `default` | partial (stacked case labels, but NO fallthrough with code between bodies) | high |
| `break` and `continue` | done | — |
| `goto` and labels | open | medium |
| `return` | partial | high |
| Function definitions and parameters | done | — |
| Prototypes and separate declarations | partial | very high |
| `inline`, `_Noreturn`, variadic functions | open | medium |

## 5. Translation units and semantics

| Topic | Status | Priority |
|---|---|---|
| Local/global objects | partial | very high |
| Storage classes `static`, `extern`, `register`, `_Thread_local` | open | high |
| Scopes and namespaces | partial | very high |
| Linkage across multiple files | open | very high |
| Initializers for aggregates | partial | high |
| Constant and non-constant global initializers | open | high |
| Diagnosis of constraint violations | partial | very high |
| Multi-phase translation | open | high |
| Defined behavior vs. implementation-defined/undefined | open | very high |

## 6. Code generator, ABI, and runtime

| Topic | Status | Priority |
|---|---|---|
| Stack IR and QCCVM | done | — |
| 68000 backend and simulator | functional | — |
| ARM64/Darwin backend | functional | — |
| Complete target ABI for an operating system | partial | very high |
| Register/stack calling convention for all C types | partial | very high |
| Struct/union return and parameters | open | very high |
| Floating-point codegen | open | high |
| Linker/object format integration | open | very high |
| Debug information | open | low |
| Optimization and code quality | open | medium |

## 7. ISO C standard library

The library is its own project package and must not be confused with
parser scope. A practically usable compiler needs at least:

- `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`, `<limits.h>`
- `<stdio.h>` and `<stdlib.h>`
- `<string.h>` and `<ctype.h>`
- `<assert.h>`, `<errno.h>`, `<locale.h>`
- `<math.h>` including floating-point runtime
- `<time.h>`
- later `<signal.h>`, `<setjmp.h>`, `<threads.h>`, and other optional parts

The exact scope depends on the target system. For Q9 a small
target-specific runtime would make sense initially, not the complete
library right away.

## Recommended stages

### Stage A: usable C subset

`for`, `do/while`, `break`, `continue`, `typedef` including
`typedef struct { ... } Name;` inline anonymous (done, 2026-07-24),
`struct` with mixed scalar field types (done, 2026-07-24; array fields,
pointer fields, and nested structs still open, see
SELFHOSTING_GAP_LIST.md), pre-/post-increment (done,
2026-07-23), `sizeof` on basic types/struct (done, 2026-07-23; on
pointers still open), still open: casts, function prototypes, `enum`
type safety (constants are done, but without their own type), and a
robust preprocessor.

### Stage B: C17 language core

All standard types, conversions, qualifiers, storage classes, multiple
translation units, complete initializers, function pointers, and a
fixed ABI including structs and floating point.

### Stage C: target system and library

Linker/object format, startup code, memory management, I/O, and step by
step the C standard library. Only here does the language compiler become
a usable development system.

### Stage D: C23

Only after C17: new C23 keywords and language rules, attributes, changed
declaration options, new library components, and the respective
implementation documentation.

## Recommendation for the project

We should not treat "complete ISO" as the next single task. The
sensible next plan is a clear **C17 core without library completeness**:

1. `for`/`do`, `break`/`continue`, increment
2. `typedef`, prototypes, `struct`/`enum`
3. Casts, conversions, `sizeof`, qualifiers
4. Preprocessor and multiple translation units
5. ABI/runtime decision for a first real target system

After each stage the scope is re-evaluated. That keeps it visible which
work is language semantics, which is backend, and which is library.
