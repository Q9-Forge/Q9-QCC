# Q9-QCC project status

**Snapshot: 2026-09-28.** This is the
current top-level summary. Older chronological compiler notes are retained in
[`docs/STATUS.md`](docs/STATUS.md) and its
[German version](docs/STATUS_de.md).

## Current state

| Area | Status | Current boundary |
|---|---|---|
| C frontend (`qcpp`/`qcir`) | 🟡 Active | C subset and Stack-IR generation are under active development; generated parser and grammar artifacts must stay synchronized. |
| Stack IR / calling-convention metadata | 🟢 Implemented for current path | `FUNC <name> <nargs> <isStatic> <attrs>` carries variadic and calling-convention bits. Legacy shorter `FUNC` records remain accepted. |
| 68k backend (`qir68k`) | 🟡 Active | Supports variadic functions and the implemented convention prolog/epilog paths; interrupt/variadic regression is covered. It does not yet turn module metadata and dispatch tables into a complete native OS-9 driver/module layout. |
| Linker (`ql68k`) | 🟡 Active | Links the project's ROF sequence and supports its current module workflow. Microware libgen archives are not yet supported. |
| IR optimizer (`qost`) | 🟡 Initial pass | Standalone constant-folding pass, not yet invoked by `qcc`; tracked regression fixtures pass. |
| Other backends (ARM64, x86, RISC-V) | 🟡/🔴 Development | Buildable components exist, but feature coverage and target-specific execution are incomplete. |
| Self-hosting / OS-9 end-to-end | 🟡 In progress | Compiler stages and large-input limits continue to be verified; a complete, repeatable self-hosted OS-9 toolchain is not yet declared done. |

## Verification recorded for this snapshot

- `Q9-PARSEC/runtests.sh`: complete suite passed, including the variadic
  interrupt regression and parser-generation consistency checks.
- `make -C Q9-FRONTEND-C/q9-qcir test`: passed.
- `make -C Q9-OPTIMIZER/q9-qost test`: passed with the tracked IR fixtures.
- 68k, ARM64, x86, and RISC-V backend build checks passed in the integration
  worktree; this is build coverage, not a claim of equal runtime support.

These checks verify compiler components and generated output. They do not
constitute a full driver load test on a live OS-9/Q9 system.

## Near-term work

1. Complete backend handling and end-to-end verification for `MODHEADER`,
   `ENTRY`, and `DISPATCHTAB` before claiming native manager/driver modules.
2. Continue syscall and inline-assembler support according to the
   [module roadmap](Q9-FRONTEND-C/q9-qcpp/docs/STATUS_DEFMODUL.md).
3. Expand qost beyond its initial local constant-folding pass, then decide
   when it is safe to add to the `qcc` pipeline.
4. Continue linker compatibility work for Microware libgen archives.
5. Re-run target-side memory and self-hosting tests before raising assembler
   limits or declaring the OS-9 pipeline complete.

## Detailed status files

- [Compiler/bootstrap history](docs/STATUS.md) ·
  [German](docs/STATUS_de.md)
- [Q9 module implementation roadmap](Q9-FRONTEND-C/q9-qcpp/docs/STATUS_DEFMODUL.md)
- [qclib symbol and behavior status](Q9-BACKEND-68K/q9-qclib/STATUS.md)
