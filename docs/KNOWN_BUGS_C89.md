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
| `sizeof` pointer types | QCC reports `sizeof of pointer types not supported in this version`. | known gap |
| `switch` scope | A declaration directly in a `case` body is not scoped to the `switch` block. | known frontend gap |
| `switch` fallthrough | Fallthrough with code between two `case` bodies is not supported. | known frontend gap |
| `goto` | `goto` and labels are not implemented yet. | known frontend gap |

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
