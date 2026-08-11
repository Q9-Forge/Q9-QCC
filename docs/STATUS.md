# Project Status

*German version: [STATUS_de.md](STATUS_de.md)*

Status: **2026-08-10 -- STEP 3 (Live Q9 Verification) COMPLETE.
The EBNF generator, translated by QCC itself, runs on real Q9 hardware and
produces parser output that is BIT-IDENTICAL to the reference built with
`xcc`. See the "Selfhosting loop closed" section directly below.**

<details>
<summary>Previous status (2026-07-26, late evening) -- partially superseded</summary>

Step 3 [Live Q9 verification] is underway -- five real `-largedata`/`-os9`
backend bugs found and fixed live on Q9, main is clean (PR #48+#49). A
sixth, new runtime error surfaced during the first complete end-to-end
test run and is currently being investigated -- details in the
Claude memory file `qcc-vollport-status.md`.

**Superseded:** The sixth bug was fixed on 2026-07-27 in commit `79024ea`
(variadic extern ABI put the wrong value into d1, "full suite green")
-- this section just wasn't updated at the time. Also corrected: per
that commit, the ABI rule is "the first TWO arguments in total (fixed +
variadic) go into d0/d1", not the version described further below in
this document dated 2026-07-24.

</details>

## Selfhosting loop closed (2026-08-10)

**The EBNF generator, translated by QCC itself, runs on real Q9 and
produces the same output as the reference built with `xcc`.**

Procedure (fully reproducible, everything against real tools):

1. `SourceQCC/ebnf.tc` + `SourceQCC/codegen.tc` compiled with `build/qcc_p`
   to stack IR, then with `qcc_backend -os9 -largedata` (codegen additionally
   `-part`) to 68k assembly.
2. Real `r68` assembles it, real `l68` links it against real
   `clib.l`/`os_lib.l`/`sys.l` (`cstart.r` MUST come first) --
   result: a valid 2.08 MB OS-9 module, data segment only 4826 bytes.
3. Module transferred via ToolShed into `PROJECTS/ebnf_gen_tc/` of the
   isolated working copy `OS9SYS.claude-work.hda`, executed via
   `test/expect/test_qcc_selfhost_run.exp` (in Q9-Flux).
4. Output `qcc_p.c` compared against the `xcc` reference `oberon0_p.c`
   (identical input grammar).

**Result:** `qcc_p.c` is BIT-IDENTICAL (md5 `20a990d7ed2b8e816c95163c546b667c`).
The likewise-generated `qcc.s68` differs in exactly ONE line -- a comment
text (`^Eingabe` vs. `Eingabezeiger`) that already differs in the source
itself (`Source/codegen.cpp:1530` vs. `SourceQCC/codegen.tc:1795`), so
it's not a translation difference.

### Backend bug found and fixed along the way: char parameter on big-endian

The first comparison showed exactly 8 differing lines -- all four
character-range checks of the grammar came out as `0x00`/`0x00` instead of
`0x30`/`0x39` etc.:

| Grammar rule | expected | before |
|---|---|---|
| `digit = "0"~"9"` | `0x30`-`0x39` | `0x00`-`0x00` |
| `hexDigit ... "A"~"F"` | `0x41`-`0x46` | `0x00`-`0x00` |
| `lowerLetter = "a"~"z"` | `0x61`-`0x7A` | `0x00`-`0x00` |
| `upperLetter = "A"~"Z"` | `0x41`-`0x5A` | `0x00`-`0x00` |

**Cause:** The caller stores each argument as a full 32-bit longword
(`move.l #value,-(a7)`). A `char` parameter therefore ends up in the
LEAST-significant byte of the slot -- on big-endian 68k that's slot+3.
`LOADC`/`STOREC`/`ADDRL` however addressed the slot BASE -- for LOCAL
slots that's correct and self-consistent (store and load use the same
address there), but for PARAMETERS it hit the most-significant byte,
which is constantly 0 for any ASCII value. Among those affected was
`astPushRNG(char lo, char hi)`, which received 0 for both range bounds.

**Why it went unnoticed until now:** ARM64 is little-endian (there the
least-significant byte happens to sit at the slot base, so the code was
accidentally correct), and QCCVM holds typed slots instead of raw stack
longwords. Neither reference path could have exposed this bug in
principle -- only real 68k big-endian is affected.

**Fix** (`Source/qcc_backend_c.cpp`, prologue emission): for every
parameter that is actually accessed byte-wise in the body (`LOADC`/`STOREC`
on its slot -- the unambiguous proof that it's a `char` parameter), the
prologue now copies the low-order byte to the slot base once:

```
	move.b	15(a5),12(a5)
```

After that, all existing byte-access paths (`LOADC`, `STOREC` as well as
`ADDRL`+`LOADIND`/`STOREIND`) line up unchanged, exactly as for local
`char` slots -- no opcode changes needed. The change is purely additive:
across the whole generator it adds exactly 3 extra assembly lines
(`tc_astPushRNG` twice, `tc_charComment` once), not a single existing
line changes. Verified via a control run that reproduces the checked-in
`build/selfhost-20260801/codegen.s68` byte-for-byte with the UNCHANGED
backend.

**Deliberately open remaining gap:** A `char` parameter whose address is
taken via `ADDRL` WITHOUT ever being touched via `LOADC`/`STOREC`
(`void f(char c){ char* p; p=&c; ... }`) is not detected. The IR
(`FUNC <name> <nargs>`, see `docs/IR_OPCODES.md`) carries no parameter
types, and `ADDRL` alone is not proof of `char` -- for an `int` parameter
the narrowing would in fact be wrong. This case does not occur in the
real generator.

## Important for a new session (even with a different AI)

Not always the same AI/session works on this project. Therefore **before**
this file, also check:

- `git status` and `git branch -a` in the repo root -- work may sit on a
  not-yet-merged branch, regardless of what's written here.
- `gh pr list` -- open pull requests still needing review/merge.

**Status 2026-07-23, end of session:** `main` is clean, all 11 PRs from this
session merged (#1--#11), no open PR, `./runtests.sh` fully green.
No intermediate state is left uncommitted anywhere -- a new session can
start directly with a new task.

### Q9/OS-9 executability (new investigation area, 2026-07-23)

In parallel to the language-feature work, it was checked whether the path
to real execution on the target platform (Q9 emulator, OS-9/68k) works:

- A trivial C program compiled with the real Microware `xcc` toolchain
  demonstrably runs on Q9 (ToolShed transfer, module format,
  execution -- all confirmed working).
- The **complete EBNF generator** (`parsec.cpp`+`codegen.cpp`) now also
  compiles and links successfully with `xcc` into a valid OS-9
  module. Prerequisite for that (permanently in the repo, PR #11): all
  C++ templates removed from `Source/msvc_compat.h` (the only place in
  the whole project using templates; `xcc`'s template engine crashed on
  them).
- Program startup on the real Q9 system initially failed: data segment
  ~34.6 MB, the system only has 16 MB RAM (14 MB free). Cause:
  deliberately only fixed global buffers (no dynamic memory management),
  but generously sized for a modern Mac.
- **Fixed (2026-07-24):** The vast majority (~32 MB) were two fixed
  tables in `codegen.cpp` for the ACTION/ROUTINE action interface
  (`routinesC`/`routines68k`, 256 slots of 64 KB text each, regardless of
  whether actions are used at all). Neither the number nor the length of
  routines can be meaningfully fixed in advance for an arbitrary grammar
  -- solved via `malloc`/`realloc` doubling (start small, grow on demand)
  instead of fixed arrays. Host data segment thus dropped from ~34.6 MB
  to ~1 MB (`size build/parsec`), now also fits an 8 MB system. Confirmed:
  the Microware `stdlib.h` provides `malloc`/`realloc`/`free`, so this
  only affects QCC as the language for later selfhosting, not the OS-9
  target platform itself (see `docs/SELFHOSTING_LUECKENLISTE.md`, new
  line "malloc/realloc/free"). `./runtests.sh` fully green after the
  change.
- **Confirmed on the real Q9 emulator (2026-07-24, addendum):** complete
  EBNF generator (source state after PR #26, including all QCC language
  features up to string literals) rebuilt with `xcc -tp=68030,ld`,
  deployed via ToolShed as `PROJECTS/ebnf_gen/ebnf_gen` and executed
  live. `ident` shows **Data size $FE7D0 = 1,042,384 bytes (~1 MB)**
  instead of the previous ~34.6 MB -- now comfortably fits within the
  16 MB RAM configuration. `./ebnf_gen seqtest` ran through without
  errors (parser table output correctly, `seqtest_p.c` +
  `seqtest.s68` + `seqtest.lextab`/`.lexlst` generated), no crash, no
  PMMU faults. The complete, reproducible xcc workflow (env setup,
  comment-form conversion, `xcc` invocation, ToolShed transfer) is
  documented in the memory file `q9-xcc-toolchain-milestone.md`. This
  thread is thus fully concluded.

This investigation is content-wise independent of the QCC language-feature
work below and concerns only the generator itself, not QCC.

### Selfhosting L2 full port: `parsec.cpp` to QCC COMPLETE (2026-07-26)

**The complete full port of `Source/parsec.cpp` (2149 lines) to
`SourceQCC/ebnf.tc` is done** -- together with the `codegen.cpp` full port
already completed on 2026-07-25 (`SourceQCC/codegen.tc`), BOTH core files
of the EBNF generator now exist as QCC source. A real `l68` link of
`ebnf.tc` (including its own `main()`) against `codegen.tc` completes for
the first time entirely WITHOUT unresolved symbols (regression test in
`runtests.sh`, last entry) -- the resulting 2.1 MB `.out` binary is a
fully linked OS-9 module.

Chronological progress, every QCC language quirk found, and the details
of each individual porting step are in `docs/FORTSCHRITT.md` (section
"Selfhosting L2 full port: `parsec.cpp`") and the memory file
`[[qcc-vollport-status]]` -- here just the short version:

- All nine core functions of the recursive-descent parser group
  (`rule`/`expression`/`term`/`factor`/`block`/`repeat`/`option`/`ident`/
  `literal`) as well as the complete lexer (`lexikalischeAnalyse`/`getNext`/
  `getAktChar`/`comment`/`getAktLine`) and the work-file I/O
  (`writeWorkfile`/`loadWorkfileAsGrammar`/`loadPreservedTests`/`runTests`)
  are ported.
- **Deliberate, documented deviation from the original:** QCC `main()`
  cannot receive command-line arguments (`argc`/`argv`) -- the entire
  original `main()` logic therefore lives in `ebnfMain(char* baseArg)`,
  and `main()` itself calls it with a fixed, hardcoded placeholder base
  name (`"qcc"`). For a real, filename-flexible OS-9 build this would
  need to be replaced by a syscall reading the OS-9 command line -- not
  implemented.
- Along the way, two previously undiscovered backend bugs were found and
  fixed (`Source/qcc_backend_c.cpp`): internal jump labels (`tc_L<n>`,
  `tc_cmp_yes/done_<n>`) and the `-largedata` tables (`tc_functab`/
  `tc_gadata`) collided on multi-file linking because their counters
  restart at 0 in every file -- fixed with the same `psectName` suffix
  pattern already used for `static` name mangling.
- Raw pointer dereferencing (reading/writing `*p`, not just `p[i]`) was
  needed for the first time in the whole project (lexer scanner) --
  verified beforehand with a standalone test against QCCVM, works
  correctly.
- **Step 3 (Live Q9 verification) IN PROGRESS (as of 2026-07-26, late
  evening):** five independent, deep bugs found+fixed in the
  `-largedata`/`-os9` 68k backend (all visible only on real hardware,
  since neither QCCVM nor `tools/qcc68sim.py` model real CPU
  flags/register conventions/module relocation): table relocation,
  a3/a4 register convention after external `clib.l` calls, `moveq`
  flag clobber between compare and conditional branch, and a3/a4
  convention after INTERNAL cross-file calls. All committed+merged
  (`Source/qcc_backend_c.cpp`, PR #48+#49). On the first complete
  end-to-end test run with the cleaned-up state, a SIXTH, new (milder,
  caught by OS-9 rather than crashing the emulator) runtime error
  surfaced -- not yet fixed, current investigation status in the Claude
  memory file `qcc-vollport-status.md` (bisection methodology + exact
  next steps documented there).

## Summary

The EBNF generator produces parser code from grammars. QCC is available
as a complete reference path:

`Data/qcc.ebnf` → generated parser → stack IR → QCCVM / 68000 / ARM64

All current regression tests pass.

## Currently implemented

- Integer, unsigned, char, bool, and null values
- Functions with parameters, return values, nesting, and recursion
- Local/global variables and one-dimensional arrays
- Arithmetic, relational, logical, and bitwise operators
- Short-circuit evaluation of `&&` and `||`
- Ternary operator `?:`
- Assignments and compound assignments
- Control flow: `if/else`, `while`, `for`, `do/while`, `break`, `continue`
- `typedef` (scalar/pointer aliases, `typedef struct Name Alias;`, as well
  as `typedef struct { ... } Name;` with an inline anonymous struct)
- `struct` with mixed scalar field types (real byte layout with natural
  alignment, field access read/write, local variables), including array
  fields (e.g. `char name[8]`, direct `p.field[i]` indexing OR via a
  pointer intermediate variable); pointer fields and nested structs
  still open, see SELFHOSTING_LUECKENLISTE.md
- `enum` (named int constants)
- `sizeof` (int/char/bool/unsigned/struct, no pointers)
- Pre-/post-increment `++`/`--` (simple int/unsigned/char scalars)
- `switch`/`case`/`default` (stacked case labels, no fallthrough with code
  between bodies)
- Casts `(int)`/`(unsigned int)`/`(char)`/`(bool)` (no pointer/typedef target)
- `sizeof(variable)` in addition to `sizeof(type)` (scalars and arrays,
  local+global)
- `enum Name` as a type (declaration, parameter, return type), stays
  `int` internally
- Pointers: declaration, `&`, `*`, `p[i]`, pointer parameters/return
  values, pointer comparison, scaled arithmetic, and pointer difference
- `const` on global/local variables, arrays, and parameters (scalar/array
  binding is protected against assignment/++/--), including pointee
  constness (`const T*`: writing through the pointer forbidden, the
  pointer itself remains freely assignable -- the `p++` idiom keeps
  working), see docs/FORTSCHRITT.md
- `static`: on global variables/functions a pure no-op (internal linkage
  is meaningless with a single translation unit); on local variables real
  cross-call persistence (registered as GLOBAL, works in QCCVM, 68000,
  and ARM64), including both a constant AND a non-constant runtime
  initializer (runs-once guard via a hidden bool flag global, see
  docs/FORTSCHRITT.md). Deliberately WITHOUT struct/array in this
  version, see docs/FORTSCHRITT.md
- `void` as a function return type and `void *` as a generic pointer,
  bidirectionally compatible with any other pointer of the same depth
  (assignment/parameter/return without a cast); `void *` itself is not
  dereferenceable/indexable/usable in arithmetic (cleanly diagnosed),
  bare `void` remains forbidden outside the return type, see
  docs/FORTSCHRITT.md
- Multi-dimensional arrays (`int m[2][3]`, `int m[2][3][4]`,
  `char names[3][4]`, up to `TC_MAXDIMS`=6 dimensions) for local/global
  variables -- `arr[i1]..[iN]` is folded into a flat index in the
  frontend via Horner's scheme (no new opcode, no backend change), flat
  initializers work along with it; deliberately NOT for struct fields/
  parameters or nested brace initializers, see docs/FORTSCHRITT.md
- `extern` declarations for functions not defined in QCC (e.g. real
  OS-9/Microware `clib` functions like `strcmp`/`printf`/`malloc`) --
  called via the documented Microware 68K ABI (`CALLEXT`/`CALLEXTP`): the
  FIXED declared parameters go into `d0`/`d1` (exactly as in a
  non-variadic call), ONLY the variadic `"..."` overflow goes on the
  stack -- corrected 2026-07-24 after a PMMU crash found on real Q9 on
  the first real `printf(fmt, ...)` call (the earlier assumption
  "variadic = everything on the stack" had never been verified against
  real compiler code), ONLY in the 68000 backend, see docs/FORTSCHRITT.md
- String literals (`char*`) -- create an anonymous global char-array
  constant (`GARRAY`/`GINIT` + null terminator) and yield its address
  (`ADDRG`), the same IR opcodes as an initialized global char array, no
  new opcode/backend change; passable as `const char*` to `extern` calls
  (e.g. a `printf` format string). Also usable as an array initializer
  (`char msg[6] = "hallo";`, local AND global) -- copies the bytes
  directly into the array slots (not just an address), an exactly fitting
  array with no room for the null terminator is allowed just like in real
  C. Deliberately NOT part of this version: string comparison/
  concatenation, `sizeof` on a literal without an intermediate variable,
  see docs/FORTSCHRITT.md
- Direct indexing without an intermediate variable (`func()[i]`,
  `"text"[i]`) -- `postfixIndex` wrapper rule around the existing `index`
  rule, `PADD`+`LOADIND` instead of `PTRINDEX` (the runtime order on the
  stack here is `[pointer, index value]` as with `p + n`, not as with a
  named pointer variable), no new opcode/backend change. Deliberately NOT
  part of this version: indexing as an assignment target, chained
  postfix indexing (`f()[0][1]`), indexing on `"(" expr ")"`, see
  docs/FORTSCHRITT.md
- 68000 backend: optional `-os9` output mode for the REAL Microware
  assembler `r68` (`nam`/`psect`/`ends` framing, `*` full-line comments,
  `align 4`/`dc.l` instead of `even`/`ds.l` -- Microware's r68 doesn't
  know the latter, determined empirically via Wine/MWOS); default mode
  (vasm) remains unchanged/byte-identical, see docs/FORTSCHRITT.md
- **Real linking against `clib.l` (Microware linker `l68`) AND real
  execution on the real Q9 emulator: done (2026-07-24).** `putint`/
  `putuint`/`putchar` now call the real, unbuffered `_os_write` syscall
  from `clib.l` in `-os9` mode (integer→ASCII conversion entirely in
  hand-written 68k code, no `printf`/no string literals needed). A test
  program was linked into a real OS-9 module with `r68`+`l68`, copied to
  the Q9 image via ToolShed, and executed on the RUNNING emulator over
  Telnet -- correct output (`42`, `-7`, `X`), no crash. In the process
  three independent linker/ABI bugs were found and fixed (wrong frame
  register `a6` instead of `a5`, `jsr` instead of `bsr` for external
  calls, wrong `l68` file order), plus an independent, previously
  undiscovered bug in the `%` operator of the 68000 backend
  (`tc_mod_i32`/`tc_umod_u32` multiplied quotient*dividend instead of
  quotient*divisor). Details in docs/FORTSCHRITT.md.
- QCCVM as an executable test oracle
- 68000 backend with simulator checking
- Native ARM64/Darwin backend with runtime
- **Multi-file compilation** (done 2026-07-25, see its own section below)
  -- bare function prototype without a body + `extern` for global
  variables for real separate compilation, `static` gets real meaning,
  verified live against real `r68`+`l68` AND `clang`/`ld` linking

## Verification

`./runtests.sh` currently reports:

```text
135 QCC programs correct
68000 pointer end-to-end test correct
ARM64/Darwin test correct
struct field access (uniform + mixed + anonymous in typedef + array field) 68000 + ARM64 correct
switch/case 68000 + ARM64 correct
static local persistence 68000 + ARM64 correct
void/void* 68000 + ARM64 correct
2D array indexing 68000 + ARM64 correct
extern call ABI (2 registers / 2 registers+2 stack / variadic / char* string literal) 68000 correct, ARM64 cleanly rejects
string literal addressing (GARRAY/GINIT/ADDRG) 68000 + ARM64 correct
-os9 output mode: DATA/BSS/scratch buffers + CALLEXT assemble without errors with the real r68 (via Wine)
68k signed/unsigned MUL/DIV/MOD + factorial correct (including the %-regression found 2026-07-24)
Multi-file M1: function call+global across file boundary, static isolation, duplicate/signature consistency check (QCCVM merge tool)
Multi-file M2: real r68+l68 link of two separately compiled files + duplicate symbol detection by l68
Multi-file M3: real separate .o compiles + clang/ld link + duplicate symbol detection by ld
=== ALL TESTS OK ===
```

Additionally (not part of `./runtests.sh`, since it requires real Q9
access): an `-os9` module with `putint`/`putchar` was linked with
`r68`+`l68 -a` (order: `cstart.r` first!) against
`clib.l`/`os_lib.l`/`sys.l`, copied to `local_images/OS9SYS.hda` via
ToolShed, and executed on the running Q9 emulator over Telnet -- output
`42\r-7\rX` confirmed byte-for-byte.

## Next reasonable step

Two independent strands are on the table:

1. **Language features:** continue one clearly scoped feature per step.
   `const` (including pointee constness), `static` (including a constant
   initializer), array fields in `struct`, `void`/`void *`,
   two-dimensional arrays, `extern` declarations (including Microware ABI
   call codegen), the `-os9` r68 output mode, real `l68` linking against
   `clib.l` (including real execution on Q9) AND string literals
   (including passing as `const char*` to an `extern` call) have been
   done since 2026-07-24 (see above). Also done since 2026-07-24: a real
   extern ABI bug (variadic calls incorrectly put ALL arguments on the
   stack instead of just the `"..."` overflow) was found and fixed on
   real Q9 -- `printf("value: %d\n", x)` against the real `clib.l` now
   correctly outputs `value: 42`, see docs/FORTSCHRITT.md. Also done
   since 2026-07-24: string literals as array initializers
   (`char msg[6] = "hallo";`, local and global, with length and type
   checking), non-constant `static` initializer (runs-once guard with a
   hidden flag global), direct indexing without an intermediate variable
   (`func()[i]`, `"text"[i]`), direct `p.field[i]` indexing of
   struct array fields AND more than 2 array dimensions (up to
   `TC_MAXDIMS`=6). This completes the original language-feature roadmap
   from the selfhosting gap list (section 1) -- multi-file compilation
   (see below) has likewise been done since 2026-07-25 -- so the
   language-feature roadmap from docs/SELFHOSTING_LUECKENLISTE.md
   section 1 is now fully worked through.
2. **Q9 executability:** the generator's memory footprint has been
   reduced AND confirmed on the real Q9 emulator since 2026-07-24 (see
   the section above) -- this strand is thus concluded. The QCC 68k
   backend runtime bridging (`putint`/`putchar` against `clib.l`) is
   already done (see above); `exit`/return-value handoff to `F$Exit`
   has not yet been separately checked.

Before every language extension, the frontend, IR, QCCVM, 68000 and
ARM64 backends, and a regression test must be checked.

## Multi-file compilation (2026-07-25, M1-M3 done)

Separately compiled QCC files can now really be compiled and linked
separately (user request: larger projects should no longer require
compiling everything in one shot). Four milestones (M0 spike + M1-M3),
all verified live against the real toolchains:

- **New declaration forms:** a bare function prototype without a body
  (`int f(int x);`) for normal internal `bsr`/`bl` linking -- deliberately
  SEPARATE from the existing `extern` (which stays fixed to the
  Microware ABI/`CALLEXT` for real `clib.l` calls). `extern <type> <name>;`
  now also declares global variables without allocating storage.
- **`static` gets real meaning for the first time** for global
  functions/variables (previously a pure no-op).
- **New IR pseudo-opcodes** `FUNCDECL`/`GLOBALDECL`: "exists, but is not
  defined here".
- **`tools/qcc_merge.py`** (new): a mini linker simulation for QCCVM,
  since QCCVM itself has no object-file/linker model -- checks for
  duplicate definitions, exactly one `main`, `static` visibility, and as
  a bonus signature consistency (parameter count) between declaration
  and definition.
- **M0 spike result (important, plan-changing finding):** `r68`/`l68`
  (68k/OS-9 target) know NO export/import directive AT ALL -- `xdef`
  (the original assumption) and eight further candidates (`global`/
  `public`/`def`/`entry`/`export`/`section`/`external`/`symbol`) were
  all rejected as "bad mnemonic", tested empirically with two
  hand-written `.a` modules against the real `r68`+`l68`. Every label
  automatically becomes visible to EVERY other linked file at link time.
  For `static` that means: NO real enforcement is possible, only name
  mangling (`tc_<name>__<psect>`) to avoid collisions -- without it,
  `static` would fail its main purpose (two files could no longer have
  a private helper of the same name, `l68` rejects that as "duplicate
  symbol", also empirically confirmed).
- **ARM64/Mach-O behaves FUNDAMENTALLY DIFFERENTLY:** `ld` supports REAL
  local symbols -- a label without `.globl` is invisible to other object
  files (empirically verified: two `.o` files each with a local symbol
  of the same name link without conflict). For `static` here, simply
  omitting `.globl` is enough, NO name mangling needed.
- **68k backend additionally:** new `-runtime` flag (only with `-part`).
  Reason (found live): the shared 68k core (`mul`/`div`) as well as
  `putint`/`putchar`/`tc_io_write` plus their scratch memory were
  previously ALWAYS emitted -- with separate compilation that would have
  guaranteed "duplicate symbol" errors (every file would have brought
  its own copy). Exactly ONE file in a multi-file program must set
  `-runtime` in addition to `-part`.
- **Verified live:** 68k/OS-9 via a real `r68`+`l68` link (two separately
  compiled files, function call + shared global variable across the file
  boundary, `static` isolation, symbol map confirmed correct
  resolution); ARM64 via real separate `.o` compiles (`clang -c`)
  + `clang`/`ld` link (same test program, `nm` confirms local vs. global
  symbols). Both targets: a second test case confirms that the
  respective real linker reliably rejects a REAL name collision (two
  non-static definitions of the same symbol) as "duplicate symbol".

`./runtests.sh`: 8 new multi-file tests (4x QCCVM/M1, 2x real 68k/`l68`
link/M2, 2x real ARM64/`clang`+`ld` link/M3), all green alongside the
135 existing QCC programs.
