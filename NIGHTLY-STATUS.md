# QCC night work checkpoint

Updated: 2026-11-18.

## Current focus

Work on the x86 assembler and linker. Treat `Q0x86` as optional and defer it.
The linker is still a prototype: Microware ROF inspection is implemented for
observed profiles, while general linking, symbol resolution, relocation, and
library extraction remain incomplete. The restricted module-candidate writer
now lays out multiple flat private-QCC PSECTs, but this new path is not
loader-validated. The separate diagnostic QCC path applies a limited set of
private-profile relocations; this does not enable references in the module
candidate. See
[`Q9-BACKEND-x86/q9-qlx86/README.md`](Q9-BACKEND-x86/q9-qlx86/README.md) and
[`Q9-BACKEND-x86/q9-qlx86/FORMAT-RESEARCH.md`](Q9-BACKEND-x86/q9-qlx86/FORMAT-RESEARCH.md).

## Next work

1. Validate the private QRX86 → QLX86 flow with assembler-produced fixtures;
   inspect symbol and relocation bytes and confirm multi-PSECT layout.
2. Resolve remaining QIRX86/QRX86 coverage gaps and the PIC convention against
   a real object or Microware tool before making compatibility claims. Keep
   Microware ROF profiles inspection-only until their file encodings and
   relocation rules are independently supported.
3. Add library member selection/extraction after symbol and relocation rules
   are represented. Keep normal link mode disabled pending independent
   format/runtime validation.
4. Continue opcode coverage as the assembler design allows; leave `Q0x86`
   until last.

## Work log

- 2026-10-16: Started the actual QRX86 assembler in its own `q9-qrx86` tree.
  Added a host Makefile and a small parser/emitter for the private QCC ROF
  profile, local branches, data directives, register/memory ModRM/SIB forms,
  common arithmetic, conditional branches and setcc. It parses PSECT metadata,
  emits explicit exported symbols and separates initialized from reserved data.
  External branches, symbolic `dc.l` references, absolute symbol addresses and
  the specific EDX/`__offset_` PIC form are represented in private ROF tables.
  The PIC addend is inferred from the call/pop/lea sequence and needs external
  validation. QIRX86 now emits the `tc_` exports and `__offset_` aliases the
  assembler expects. The private `qlx86 --emit-prototype` path now resolves
  symbols across concatenated PSECTs and applies absolute and PC-relative
  code/data references, local BSS references, and QIRX86's EDX-relative PIC
  form. Those private type/addend semantics and Microware ROF relocation rules
  remain unverified. Host builds succeed; no assembler
  execution or ROF/runtime validation has been performed. Multiple PSECTs and
  complete ISA coverage remain open. The
  mistaken edits to `qox86.c` and its README were reverted.
- 2026-10-16: Fixed QIRX86 comparison and logical-NOT lowering: use `setcc`
  then `movzx` without destroying condition flags before sampling them. This
  aligns emitted sequences with QRX86's byte-register and `setcc` support.
  Host builds only; no runtime semantic test was run.

- 2026-10-16 21:33 CEST: Implemented the recovered ordinary `l386 -b` layout
  for the restricted private-QCC candidate path: independently rounds each
  PSECT's code, initialized-data, and uninitialized-static sizes; emits zero
  padding in code/data; uses the aligned root code base for entry/trap offsets;
  and accepts `-b=2|4|8|16` (default 2). Preserved the rejection of symbols,
  relocations, remote areas, debug data, Microware profiles, and normal link
  output. Updated linker and format docs. `git diff --check` is clean; no build,
  tests, or loader validation were run, so this code path remains unverified.
  Next: map symbol type values to QCC's own ROF model before adding relocations.
- 2026-10-16 21:34 CEST: Inspected the current `qox86` and `qirx86` source
  boundary. `qox86` writes code/data global records but zero external/local
  reference counts; label operands do not produce fixups, and `dc.l` symbols
  become zero. `qirx86` emits unresolved `__offset_<name>` PIC references.
  Documented this as a private-prototype limitation, not Microware ROF
  semantics. This shows assembler fixups must be designed before enabling
  symbol-bearing qlx86 output.
- 2026-10-16 21:31 CEST: Rechecked the shipped x86 `l386.exe` statically with
  REA. Corrected the alignment description: ordinary x86 `-b` handling rounds
  each PSECT's five area sizes independently before cumulative bases are
  assigned. Recorded the internal size/base-field map, ordinary symbol type to
  base map, and the separate `-a` limitation in `FORMAT-RESEARCH.md`; added a
  summary to the linker README. No QCC implementation changes or runtime/tests
  were made. Next: map those fields to the current private ROF model and design
  a multiple-PSECT layout without broadening output acceptance yet.
- 2026-10-16: Set up a local `launchd` job to run up to three hours starting at
  23:30 Europe/Berlin. Each Codex CLI invocation is limited to one coherent
  work item. The runner preserves the existing dirty worktree, does not run
  tests/builds or create commits, writes logs under
  `~/Library/Logs/Q9-QCC-Nightly/`, and stops after an error or the pause
  marker. No overnight coding work has run yet.
- 2026-10-16: Added the first private-profile relocation pass to qlx86's
  diagnostic prototype output. It resolves concatenated-PSECT global
  definitions, rejects duplicate/undefined names, and applies external rel32
  branch/call, absolute code/data, local absolute code/data/BSS, and QIRX86's
  call/pop/LEA PIC references after layout. QRX86 uses a private PIC reference
  type to distinguish that case from branch rel32. Candidate module output
  still rejects references;
  no Microware l386 or loader semantics are claimed. qlx86 and qrx86 host builds
  complete cleanly and `git diff --check` passes. No runtime validation was
  performed. Next: validate with assembler-produced objects and inspect the
  emitted and linked bytes; the private PIC type/addend convention is inferred,
  not established as Microware-compatible.


## 2026-11-17 – Q9-Tools x86 continuation

- Complete inventory: 82 C translation units, 82 private ROF builds and 42
  diagnostic links at this checkpoint. Five directories have no C source.
  Rebuild with `sh ../Q9-Tools/tools/build_x86_objects.sh`; reports retain
  input.i/output.ir and per-source compiler/link logs.
- Fixed QIRX86 direct/external/indirect argument order and stack cleanup;
  cdecl callback entries cover static/external functions and pointer returns.
  QRX86/QLX86 resolve call/pop function-pointer references. Added byte/short
  accesses, global initialized arrays, local arrays and pointer arithmetic.
- Canonical frontend actions and generated parser agree on field addresses,
  sizeof dereferences, numeric/character case constants and standard escapes.
  Preserved the prior MAX_STRUCT_FIELDS=32 fix in qcc.lextab too.
- CLib now has reusable arena allocation, streams including seek/reopen,
  integer/string formatting, qsort/bsearch. Portable .r remains separate from
  the unvalidated edition-1 OS-9000 path/trap candidate.
- Executed existing frontend smoke checks, QEMU backend semantics and CLib
  integration, plus native stream-adapter checks: pass. No OS-9000 loader or
  guest trap validation. Native ident/fixmod fixture checks pass for both
  module byte orders, field offsets, options, header parity and CRC.
- Important: module-file fields differ: 68k exec/data/stack at 48/56/60;
  OS-9000/x86 at 36/44/48, parity at 86 in an 88-byte header. ident/fixmod use
  explicit file-format accessors in Q9-Tools/include/q9_module_file.h.
- Next: repair remaining source/header failures, finish low-level runtime
  services, then real OS-9000 startup/data relocation. High reasoning was
  recommended to the user for the startup/loader ABI integration.

## 2026-11-18 – Tools/runtime continuation

- All 82 existing C targets compile; 29 private diagnostic links passed before
  the new termination adapter. No runnable OS-9000 module claim.
- Shared frontend fixed indexed-structure member addresses. QRX86 fixed data
  labels after BSS. Existing backend, portable CLib and syscall-block QEMU
  checks pass after these fixes.
- Experimental path adapter now opens r/r+/w/w+ (binary variants), truncates
  via SS_Size and creates only after E_PNNF, with NOCREATE race protection.
- Added F$Link/F$UnLink/F$CpyMem candidate wrappers. QEMU mock verifies packed
  fields, in/out values, 32-bit process IDs and preserved outputs on errors.
- qid common/program/ROF and legacy specialized headers now use byte decoders.
  Native 68k/x86/ROF fixtures pass. x86 memory reads use the new copy wrapper.
- Added experimental process_os9000.c: F$Exit, 32 atexit slots, reverse callback
  execution and stream cleanup. Build/check currently in progress; inspect
  /private/tmp/q9-clib-build.log and /private/tmp/q9-syscall-check.log next.
- Next: finish termination validation, refresh full link report, then continue
  missing file/time/device/network services. Startup/global-data relocations
  still require the architecture work for which High was recommended.

- Latest continuation: termination adapter builds and mock checks pass, as do
  Attach/Detach packed output/error checks. Full report now 82/82 objects and
  32 successful private diagnostic links. Reproducible native qid checks are
  saved in Q9-Tools/tools/tests/check_qid_formats.py and pass.

- Added calendar.c, time_os9000.c and fd_os9000.c. Calendar and syscall mock
  checks pass; full tool report now 82/82 objects, 37 diagnostic links.
  localtime is UTC-only pending TZ/DST. Sleep seconds use 1/256-second form.
- Fixed shared frontend extern prototype/definition handling; positive QEMU
  and negative mismatch checks pass. Existing qcir smoke checks pass.
- Important ABI correction: OS-9000 modes use directory 0x8000, nonshare
  0x4000, group 0x10/0x20/0x40, public 0x100/0x200/0x400. The former copied
  legacy permission-byte constants were wrong. Header now selects x86 values.
  _os_create initial-size flag corrected from 0x20 to 0x2000; not supported.
- Added header dependencies to CLib Makefile. Next rebuild candidate, rerun
  syscall check and full tools after the permission correction. Candidate
  _os_load, stat/chmod/chown, rename, directory iteration and networking remain.

- Latest full report: 82/82 objects and 42 diagnostic links. Added F$Load and
  F$GBlkMp; metadata stat/fstat/chmod, directory creation and chown candidates.
  Syscall QEMU mock passes. Native owner parser/-z tests pass for 68k and x86.
- All unresolved symbols now enumerated by tools/build_x86_tools.py into
  unresolved.tsv and missing-symbols.tsv (private series-999 only).
- ABI issue for next work: public dir.h still describes a legacy 32-byte raw
  RBF record and MAXNAMLEN=32. OS-9000 SDK rbf.h has MAXNAME=43, DIRENTSIZE=64,
  name[44], padding[16], fd_addr at60; dir.h MAXNAMLEN=44. PCF reads DOS
  32-byte entries instead. GetStat SS_DEVTYPE 0x2f returns two ushort values,
  device type then file-manager class; DT_RBF=1, DT_PCF=10 (SDK io.h).
  Kernel ioman/stat.c confirms these output fields. Need manager-aware
  opendir/readdir and qid scanning; do not treat native directory bytes as 68k.
- Packed I$MakDir edition1 uses permissions at+18, size22 (PCF open.c), whereas
  later aligned SDK pb.h says+20. Initial-size mode is explicitly rejected.
  Source makdir had permissions in its mode argument and zero perm; corrected.
- Current F$Load candidate uses packed size34 through color+32; fields from
  svc_gen.c. Whole profile still unvalidated on a genuine OS-9000 guest.

- Added portable xdr.c and public memory-XDR declarations. QEMU CLib checks
  PASS with fixed network-byte fixtures, strings, arrays, pointer callbacks,
  freeing and malformed input. Source cites RFC4506 (primary standard checked).
  Own NULL-x_ops memory convention, not Microware x_ops binary compatibility.
- Added BSD bzero/bcopy/bcmp on existing portable memory routines; rebuild and
  rerun CLib/syscall checks next (header dependencies rebuild every object).
- Network source evidence: MWOS/SRC/DEFS/SPF/BSD/netdb.h NETDB_TRAP_NAME="netdb",
  trap #9, function selectors e.g gethostbyname9, getproto18, getserv23. Official
  network_ref.txt explains inetdb module + DNS. Do not substitute /etc parsing
  as the OS-9 resolver. User-trap assembly in kernel_asm.S lines413ff restores
  user EAX/EBP etc and supplies trap data via EBX. Exact caller encoding and
  Microware register ABI need evidence before implementing bindings.
