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

**Addendum 2026-09-07 — pointer arrays as struct fields now work.**
`char* args[6]` inside a struct used to be rejected ("pointer arrays as
struct field not supported in this version"). `qcc_backend_c.cpp` needs it:
its `Instr` structure holds an IR instruction's arguments that way, across
106 access sites. While that was missing, the backend was not
self-compilable and therefore **could never run on the 68030** — the only
link in the chain with that gap.

The crux was the **stride**: `IPADD` scales by the size of the type tag,
and a pointer is four bytes on the 68k — but occupies eight inside a struct
(`TC_PTR_SLOT`, so the same offset stays valid for ARM64). Pointer arrays
therefore get their step in **bytes** (`IPADDN 8`), the same device the 2D
rows already used. The rule lives in *one* function
(`tcEmitFieldIndexStep`) instead of at the six emission sites — three
reading, three writing.

**Addendum 2026-09-07, second round — indexing *through* a scalar pointer
field now works too** (`s.ptr[j]`, `sp->ptr[j]`, reading and writing, local
and global). Previously rejected at six emission sites. The crux: the
callers only have the **address of the field** on the stack, but the
pointer *inside* the field is what's needed — hence `LOADIND p` first, then
the index step. The rule lives in `tcEmitPtrFieldIndex`, one function for
all six sites.

**A seventh site stays rejected:** after a function call (`f().field[i]`) a
different stack discipline applies — `PADD` with swapped operands instead
of `IPADD` — and there is no caller for it. A second discipline alongside
would be the next source of error.

**Still rejected, with a proper diagnostic** (the chain does not need it,
and a message beats a wrong stride): `arr[i].field[j]` — struct step and
field step in *one* expression; that needs a second index scratch like
`tcEmitPointerIndexChain` and is a project of its own. Likewise
two-dimensional pointer arrays as a field (no caller).

**Addendum 2026-09-07 — a SILENT wrong-code bug in `&arr[i]`, fixed.**
`tc_addressref` emitted `PTRINDEX` with the type tag for the address of a
struct array element, and `tcTypeTag` yields `'i'` for a struct — a stride
of **four bytes** instead of the struct size. For an 80-byte struct,
`&arr[1]` pointed four bytes past `arr[0]`. It only surfaced with the
backend on the 68030, which fetches instructions via
`insP = &ir[irCount]`: they all landed on top of each other, visible as an
`op` field reading `FUNCLOADPUSHCMPLJZ` in four-character steps. The test
corpus contained **no `&arr[i]` on a struct array at all**; cases 27 and 28
in `tools/test_struct_68k.sh` now cover it, with discriminating expected
values (a wrong stride leaves them at 0).

**Addendum 2026-09-07 — a second silent wrong-code bug, fixed:** copying a
whole struct through a **pointer** (`struct S *p; v = p[i];`) emitted
`LOADP / PTRINDEX i / LOADIND i` — a four-byte stride *and* a `LOADIND`
where the address is wanted, so a data value ended up in A0 as the source
address. The same shape had been repaired for the ARRAY case
(`v = arr[i]`) on 2026-09-01; the pointer case was left behind, and it only
turned up when the neighbouring sites were checked while measuring the
`&arr[i]` bug. Now `LOADP / IPADDN <size>` without `LOADIND`: for a struct
the address **is** the value. Case 35 in `tools/test_struct_68k.sh`.

**Lesson for the next change of this kind:** a rule about strides never
applies at just *one* site. The `&arr[i]` bug had two emission sites, the
pointer arrays six, indexing through a pointer field another six — and the
pointer variant of `v = p[i]` is exactly the array variant from six days
earlier. Whoever touches such a site should hunt down its siblings.

**Addendum 2026-09-07 — `const` on a struct field was parsed and
discarded; FIXED the same day.** Four forms went through **silently**,
while the same is correctly reported for a *variable*:

| | before | now |
|---|---|---|
| `s.cp[0] = …` with `const char *cp` | silent | `cannot assign through pointer to const` |
| `p->cp[0] = …` | silent | same |
| `s.n = 1` with `const int n` | silent | `cannot assign to const struct field` |
| `p->n = 1` | silent | same |

The grammar named the reason itself: `fieldConstKw` was an **action-less**
copy of `constKw`, because referring to `constKw` triggers `tc_const`,
whose `tcPendingConst` is only consumed by the next parameter or local —
where it would wrongly enforce constness. The way around it was right; the
consequence was not.

`fieldConstKw` now has its **own** action with its **own** flag that only
concerns fields: `tc_fieldconst` sets it, the declarators of the line
consume it (`const int a, b;` covers **both**, measured), and
`tc_fieldconstend` clears it after the whole `structField` line.
`tcPendingConst` is untouched.

The distinction mirrors variables (`tc_local`): for a **pointer**, `const`
makes the *pointee* constant (`pointeeConst` in the field type, checked in
`tcEmitPtrFieldIndex` and only on writes); for anything else the **field**
itself (`tcStructFieldConst`, checked at the **seven** write sites in
`tc_target` — not three; the three with an index branch are only a subset).

**What explicitly stays legal** (all measured): `s.cp = b` — only the
pointee is const, not the pointer itself; reading a const field; reading
*through* a const pointer field; the non-const neighbouring field; and
neither the next line nor the next `struct` is infected. QCC's own parser
depends on exactly one of these forms (`actionLog[i].start = start` with
`const char* start`), which is why it was checked separately before the
self-host run.

**Addendum 2026-09-07 — a 2D array as a struct field failed SILENTLY.**
Ordinary 2D and 3D arrays work; a 2D array *inside a struct*
(`struct S { char t[4][8]; }`) behaved like this:

| Form | before | now |
|---|---|---|
| declaration, `sizeof` | works | works |
| `z = sp->t[i]` (pointer to row i) | works | works |
| `s.t[i]` via the dot | reported ("only supported via `->`") | unchanged |
| **`s.t[i][j]` / `sp->t[i][j]`** | **silent `FAIL`, no message** | **reported** |

The cause was the grammar: `member [ index ]` — exactly **one** optional
index after the field. Two indices could not be parsed, and a parse abort
carries no message. It now reads `member [ index { index } ]`, **not** in
order to support the form but in order to be able to reject it:
`chained indexing of a struct field (field[i][j]) not supported in this
version`.

**It is still not implemented** — that would need two indices in *one*
expression, i.e. a second index scratch along the lines of
`tcEmitPointerIndexChain`. That is the **same machinery** `arr[i].field[j]`
needs: one rework would close both gaps.

**Addendum 2026-09-07 — the nesting limit was four too small.**
`TC_MAX_CTRL` (previously the literal 64 at six places) is now 128.
Measured: `qcc_backend_c.cpp` needs **68** — its opcode dispatch is a long
`else if` chain, and every link is one level deeper in the model. It fails
at 67 and passes at 68. Nesting too deeply remains a real limit *with a
diagnostic*.
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

**Addendum 2026-09-07, measured.** Two things that must be kept apart:

**1. The 8-byte layout for pointer fields is DELIBERATE, not a defect.**
`Data/qcc.lextab` states the reason on the spot (2026-07-25): a pointer
field always occupies 8 bytes so that **a single offset computed by the
frontend stays valid for both backends** — 68k pointers are 4 bytes,
ARM64 pointers 8, and the same IR is consumed by both
(`Source/qcc_arm64_backend.cpp` is tracked). Consequence: a
`struct { int id; const char *start; const char *end; }` is **24** bytes,
not 12, and `sizeof` consistently reports 24. Changing *only* `sizeof` to
12 would be worse than the status quo — an `n * sizeof(entry)` would then
under-allocate.

The price is measurable: QCC's generated parser needs **6,291,456 instead
of 3,145,728 bytes** for its action log (262,144 entries). This surfaced
during the target run against `qclib`, where that request hit `E$NoRAM`.
Halving it requires making the **frontend target-aware** (an `-m32`, say),
and then the IR is no longer the same for both backends. That is an
architectural decision, not a bug fix.

**2. `sizeof` on a pointer type used to LIE SILENTLY — fixed 2026-09-07.**
`sizeof(char *)` produced `PUSH 1`, i.e. the size of `char`, with final
word `OK` and **no diagnostic at all**. The lack of support is deliberate
(see above), but `tc_sizeof` *meant* to report it — the branch was simply
unreachable: `tc_sizeof` calls `tc_type` again for the same span, and
`TC_SET_CURRENT` resets `pointers` to 0 even though the action on
`pointerDecl` had already counted them. `tc_sizeof` now counts the
pointer level itself. A `malloc(n * sizeof(char*))` previously got a
quarter of what it needed.
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

**Addendum 2026-09-07 — `(void)` as a parameter list works in a DEFINITION
but not in an `extern` declaration.** Measured:

| | |
|---|---|
| `extern int f(void); f();` | `wrong argument count (expected 1, got 0)` |
| `extern int f(); f();` | works |
| `int f(void){ … }` (definition) | works |

So the declaration reads `(void)` as **one** parameter. Noticed while
building a measurement probe (`extern int *vsectbase(void);`), and it costs
exactly as long as it takes to try the empty parentheses. At least it is
**reported** rather than silently misread. In this subset the empty
parenthesis is the correct form.

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
