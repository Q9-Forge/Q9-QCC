# Known C89 gaps and bugs

Status: 2026-09-10

This list separates compiler defects from language features that are not yet
implemented. Each entry should eventually have a small reproducer, a fix, and
host plus 68000 coverage.

## Reproduced

| Area | Finding | Status |
|---|---|---|
| Large functions | **Grammar `FAIL` fixed (2026-09-10).** Cause was `memberIncTarget = ident member .` in `Data/qcc.ebnf` -- covered `x.field++` but not `x.field[i]++`/`--x.field[i]`, an ordinary C89 idiom (`vm->state[i]++` in `qrun_vm_run`). Fix: `memberIncTarget = ident member [ index ] .`. Verified via `tools/bootstrap_survey.py` against preprocessed `qrun_vm.c`/`qrun_main.c`: stage 1 (grammar) now 0/56 and 0/42 rejections (previously affected: `qrun_vm_load_ir`, `qrun_push_value`, `qrun_pop_value`, `qrun_vm_run`, `qrun_load_arch_file`). Stage 2 (semantics) also clean once the measurement recipe correctly resolves `qrun_os9.h` instead of stripping all `#include` lines -- see below. Frontend + IR + 68k backend now verified clean for both files (assembly output produced, exit 0); real `r68` assembly and `l68`/Q9-Flux execution not yet verified. | frontend + backend clean, real assemble/link/run open |
| Stage-2 measurement recipe | The original survey recipe stripped ALL `#include` lines before preprocessing, so `qrun_vm.h`/`qrun_ir.h`/`qrun_os9.h` were never resolved -- the 1566/58 semantic findings this produced were mostly `unknown type`/`unknown function` artifacts, not real gaps. Fixed by preprocessing with real header resolution (`-DQRUN_OS9 -ISource`) instead. | fixed 2026-09-10 |
| Complex VM structures | Q9-Run combines structure pointers, array indexing, and large `switch` blocks. This combination is not yet covered by independent C89 minimal tests. | open, add minimal tests |
| `sizeof` pointer types | **FIXED 2026-09-15.** The size depends on the target (68k 4, ARM64 8); the frontend now emits it symbolically as `0+1P` and each backend substitutes its own `P`. See the pointer-size addendum in [ISO_C_GAP_LIST.md](ISO_C_GAP_LIST.md). | done |
| `switch` scope | A declaration directly in a `case` body is not scoped to the `switch` block. | known frontend gap |
| `switch` fallthrough | **FIXED 2026-09-15.** Previously silently wrong: the case body jumped unconditionally to the end of the switch, so `switch(1){ case 1: r=1; case 2: r=r+2; break; }` yielded 1 instead of 3, with `OK` and no diagnostic. The body now falls into the next BODY (not the next test), behind its `DROP` - on that path the switch value is already off the stack. Eight cases in `runtests.sh` with discriminating expectations (7 = 1+2+4 across three stages). | done |
| Pointer tables with string literals | **FIXED 2026-09-15.** `char *tab[] = {"a","b"}` used to fail SILENTLY in the parser (`FAIL`, 0 diagnostics). New IR opcode `GINITADDR`; on OS-9 the loader relocates the addresses through `M$IRefs`, so such globals now live in the vsect. Came with it: size from the list (`int a[] = {1,2,3}`). | done |
| Nested initializer lists | **FIXED 2026-09-15.** `int m[2][2]={{1,2},{3,4}}` used to fail SILENTLY. The array is flat internally and the braces only group it - but a short row is **padded** to the row length, because `{{1},{2}}` means `m[1][0]=2` in C, not `m[0][1]=2`. Three levels and nesting on a 1D array are reported. | done |
| Comma operator in the `for` head | **FIXED 2026-09-15**: `for(i=0,j=n; ...; i++,j--)`. Previously a silent parse failure. | done |
| Comma operator in statement position | `i = 1, j = 2;` remains a silent parse failure. The change was written (action on `assignItem` rather than `assignStmt`) and left suite and host run green, **but broke self-hosting on the 68030**: the compiler built there emitted `JZ L2080374908` - a garbage label number - and dropped a `STOREP`. It does not show on the host because fresh memory happens to be zero there. Reverted; the `for` head carries the benefit (125 of 4109 Microware sources), this form barely. Root cause not found. | open, cause unknown |
| `goto` | **FIXED** (measured 2026-09-15): backward (loop) and forward (jump to end) both produce the correct value. | done |

## Boundary

The successful QCC bootstrap does not disprove these findings. The bootstrap
uses prepared source inside the supported subset. Q9-Run is now an independent
larger regression workload for C89 compatibility.

A fix is complete only after this chain passes:

```text
QCC frontend -> IR -> qccvm.py/native VM -> 68k backend -> Q9-Flux
```

## Work order

1. ~~Reduce the Q9-Run failures to minimal tests.~~ Not needed for the
   grammar gap: `tools/bootstrap_survey.py` against the preprocessed files
   was enough.
2. ~~Separate parser failures from semantic failures.~~ Done (2026-09-10):
   stage 1 and stage 2 both clean once the measurement recipe was fixed.
3. Extend the frontend for missing C89 constructs. -- done for the grammar
   gap (`memberIncTarget` + index).
4. Change the backend only when new IR semantics are required.
5. Keep Q9-Run as a permanent regression test.
