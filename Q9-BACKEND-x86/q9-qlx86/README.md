# qlx86 – x86-32 OS-9000 Linker

**Status:** Prototype – read-only inspection supports the observed image profile;
linking remains unverified

The reader recognizes the private `qox86` prototype profile, the legacy
16-bit-count image profile, and the observed 60-byte/32-bit-count profile
found in the v4.7 SDK. `--inspect` parsed all 114 legacy `.r` files and all 38
v4.7 `.r` files (80 PSECTs). Both Microware profiles remain inspection-only;
link mode rejects them because their full relocation and module-output paths
have not been implemented. The private QCC link path was smoke-tested with a
one-byte object only. No output has been compared with Microware `l386`.

Normal link mode currently stops without creating an output. The explicit
`--emit-module-candidate` path now serializes the observed 88-byte OS-9000 x86
header, name/code alignment, initialized-data descriptor, empty relocation
lists, header parity, and module CRC. It accepts only private QCC objects with
no global symbols, relocation sites, remote sections, or debug data. This
limited output path has not been checked with the OS-9000 loader, so it remains
a candidate format path and normal link mode remains disabled. Mapping ROF
symbols, relocation expressions, and full Microware PSECT semantics are still
outstanding. The candidate path now lays out multiple concatenated private QCC
PSECTs with per-area alignment padding. It remains limited to objects without
symbols, relocations, remote sections, or debug data. The alignment choice
defaults to 2 bytes and accepts `-b=2`, `-b=4`, `-b=8`, or `-b=16`, based on
the static `l386` option and layout analysis documented in
`FORMAT-RESEARCH.md`. This candidate layout has not yet been built or validated
against a loader; initialized data must also total a multiple of four bytes,
so `-b=4` may be required. Its
`m_dbias` now follows the Microware x86 module-type rule,
corroborated by all 403 parity-valid image modules; ordinary program modules
use `0x80`, while data and system-component types use zero. An absent ROF trap
is serialized as module exception offset zero, matching the static `l386`
output rule; loader validation remains pending. The candidate currently sets
`m_owner` to 1, matching the static `l386` fallback when `GRPUSER` is absent.
The private ROF has no ownership field, so the candidate cannot reflect
`GRPUSER` or Microware's `-gu` override. `--emit-prototype` remains available
for the private 56-byte diagnostic container.

The diagnostic prototype path now loads definitions from all concatenated
PSECTs and applies the private QCC profile's 32-bit external `call`/branch
PC-relative references, absolute code/data references, local absolute
code/data/BSS references, and QIRX86's EDX-relative PIC references after
laying out the sections. Duplicate definitions and unsupported reference
forms fail closed.
This is an initial private-profile linker step; its type words and addend
semantics are not validated against Microware `l386`, an OS-9000 loader, or a
known-good x86 object. Module-candidate output still rejects every symbol or
reference because its runtime relocation-list semantics are unresolved.

## Overview

`qlx86` is intended to link x86 ROF objects into OS-9000 modules. Its current
input and output formats are not yet established as Microware-compatible.

```
qcc_backend.ir → qirx86 → .a (assembly) → qox86 → .r (ROF) → qlx86 → .mod (OS-9000 Module)
```

## Architecture

### Current status
- `qlx86 --inspect <input.r>` prints the parsed PSECTs and detected profile.
- `qlx86 --inspect-library <input.l>` validates the observed `$0001` and
  `$FF01` ROF library profiles, reports indexed section boundaries, and lists
  their 62-byte or 66-byte PSECT records, including uninitialized and
  initialized static/remote data, code, debug, stack, and payload offsets. Add
  `--library-symbols` to list global definition records.
  It checks hash-chain coverage, table bounds, indexed 6-byte reference
  sites against their PSECT sections, and verifies the observed byte-sum
  modulo-377 hash for 378-slot tables. It does not resolve symbols or
  reconstruct linkable ROF objects from the payload.
  It reads local-reference counts and 6-byte records; old `$02xx` placement
  anomalies are reported as unresolved.
- The image profile uses little-endian size fields, 16-bit counts, and
  concatenated PSECTs. Its symbol and relocation type words are also
  little-endian. This is an empirical description of these image files, not a
  general ROF 9.2 specification.
- The reader accepts Edition 9.2's `0x00e9` series profile with a 60-byte
  header and 32-bit counts. The shipped `rdump.exe` maps `$E9` to Edition 9.2
  and `$F9` to 9.1. The legacy image's 16-bit counts suggest an older Edition
  9.0 profile; that is an inference from field widths.
- The `qox86` prototype profile has big-endian sizes and 32-bit counts, plus a
  zero common-block count at the end.
- Normal link mode accepts only the private QCC prototype profile and refuses
  to write. `--emit-module-candidate` is a separate restricted path for
  relocation-free, symbol-free objects; it has not been loader-validated.
  `--emit-prototype` writes the private, non-loadable diagnostic output.
- Image-profile linking stops before writing output. A general ROF-to-module
  relocation path and library symbol resolution are not implemented.
- Undefined external symbols stop linking. The private diagnostic path handles
only its current 32-bit absolute, PC-relative, and QIRX86 PIC subset; other
reference forms remain unsupported and unvalidated.
- All references in the inspected legacy image and Edition 9.2-shaped ROF
  samples are four bytes wide. `l386` has static 1- and 2-byte fixup paths,
  but no matching x86 fixture is available to validate them.
- Library symbol resolution and extraction of ROF objects remain
  unimplemented. Both library profiles are inspection-only; see
  [FORMAT-RESEARCH.md](FORMAT-RESEARCH.md) for their observed boundaries.

The OS-9000 image contains 114 `.r` files; the inspection mode parsed all 114.
14 files contain multiple concatenated PSECTs. `acstarta.r` has two PSECTs.
These counts describe the complete reference tree examined, not only x86
objects.

See [FORMAT-RESEARCH.md](FORMAT-RESEARCH.md) for field evidence, manual-backed
format facts, the `CMDS/link` distinction, and the next verification steps.

Microware's Ultra C documentation describes ROF 9.2 as the x86 assembler
format and documents header additions and wider counts. The reader profiles
above are not asserted to implement all ROF 9.x variants. `l386.exe` has not
been executed in this environment. Static analysis of the shipped `rdump`
utility shows that later ROF series have version-gated header fields and a
separate expression/reference-table path; the exact mapping of its series 12
to the manual's edition numbering is still unverified. See
[FORMAT-RESEARCH.md](FORMAT-RESEARCH.md) for the evidence IDs and details.

### Previous prototype behavior (unverified)

The remaining sections below document the existing prototype interface and
output layout; they are not claims of OS-9000 compatibility.

## Usage

```bash
qlx86 --inspect <input.r>
qlx86 --inspect-library [--library-symbols] <input.l>
qlx86 --emit-prototype <input.r> -O=<output.qmod>
qlx86 --emit-module-candidate [-b=2|4|8|16] <input.r> -O=<output.mod>
```

### Example

```bash
# Private QCC prototype flow (not verified against Microware tools)
qox86 final.a -O=final.r

# Private diagnostic container only; not a loadable OS-9000 module
qlx86 --emit-prototype final.r -O=final.qmod

# Restricted OS-9000 module candidate; no relocation records are accepted
qlx86 --emit-module-candidate flat.r -O=flat.mod

# Candidate using four-byte PSECT area alignment
qlx86 --emit-module-candidate -b=4 flat.r -O=flat.mod

# Historical README result; not reproduced in this review
# Historical private-prototype result; not reproduced in this review:
#   - 56 byte header (DEADFACE, attributes, sizes)
#   - 629 bytes code
#   - 0 bytes initialized data
```

## Output Format

The explicit prototype-output option writes a private 56-byte header with
these fields. This is not an OS-9000 module header:

```
Offset  Size  Field
------  ----  -----
0x00    4     Sync word (0xDEADFACE)
0x04    2     Type/Language
0x06    2     Attributes (0x8000 = executable)
0x08    2     Error code
0x0A    2     Reserved
0x0C    2     Reserved
0x0E    2     Edition
0x10    4     Static/BSS size
0x14    4     Initialized data size
0x18    4     Code size
0x1C    4     Stack size
0x20    4     Entry point
0x24    4     Trap entry
0x28    4     Remote data
0x2C    4     Remote initialized data
0x30    4     Debug info
0x34    0     Code section begins (little-endian x86-32)
```

## Historical test claim

The previous README claimed a test with Parsec C compiler frontend:
- Input: `qcc_final1.ir` (1.2 KB)
- After assembler: `final.r` (1.0 KB ROF module)
- After linker: `final.mod` (685 bytes executable)

The following values were listed there, but were not reproduced in this review:
- ✓ DEADFACE sync word present
- ✓ 56-byte header correct
- ✓ Attribute field: 0x8000 (executable)
- ✓ Code size: 629 bytes
- ✓ Initialized data: 0 bytes

## Known limitations in the prototype

1. **One ROF container per invocation** – Concatenated PSECTs in a container are read; separate object and library input selection is not implemented.
2. **No library linking** – Type-1 headers, PSECT indexes, and global definition records can be inspected, but hash-chain lookup, member selection, and extraction are not implemented. Undefined symbols are rejected.
3. **Relocations are unverified** – The reader records relocation data, but the linker does not yet honor relocation types or all section bases.
4. **Limited stack metadata** – Candidate output sums the per-PSECT stack
   estimates. A separate whole-program stack requirement is not represented.

## Future Work (Phase 13+)

- Validate the restricted `--emit-module-candidate` output with an OS-9000 loader or Microware linker and a real input fixture before enabling normal link mode.
- Implement ROF symbol resolution, relocation handling, library member selection, and general Microware PSECT layout before broadening the module writer's accepted inputs.
- Library linking (multiple ROF inputs, symbol resolution from libraries)
- Confirm the static `l386 -a` jump-table path against an x86 ROF fixture and
  OS-9000 runtime behavior before implementing it.
- Pointer-list generation (`m_idref`) for data relocations
- Debug symbol handling

## References

- GNU BFD i386os9k.c (os9k.h structures)
- Microware OS-9 Porting Guide v2.2 (1998)
- ql68k reference implementation (68k linker)
