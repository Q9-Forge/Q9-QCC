# x86 ROF and linker research

Stand: 2026-10-15. This note separates the Microware manual, observations from
the supplied OS-9000 v2.2 image, and the private QCC prototype format.

## Evidence sources

- Microware, *Using Ultra C/C++*, chapter 6, extracted locally to
  `/private/tmp/mwos_doc_0.txt`, pages 210–211 (compiler phase options),
  223–226 (assembler/linker names), 236–247 (ROF 9.1/9.2 structure and
  relocation flags), 296–297 (PSECT semantics, including stack sizing), and
  357–359 (Type-1 library index and PSECT records).
- Microware, *Using Ultra C/C++*, extracted locally to
  `/private/tmp/mwos_upg.txt`, section “ROF Edition #9”, pages 119–120.
- Microware, *Using Ultra C/C++ Processor Guide*, x86 chapter, extracted to
  `/Volumes/SSD1TB/projects/REA/REA_PROJECTS/KERNEL_X86/REFERENZ/doku/text/ultrac_pg.txt`,
  pages 109–110 (x86 ROF version and symbol-bias rules).
- Read-only files from
  `/Volumes/SSD1TB/projects/REA/REA_PROJECTS/KERNEL_X86/REFERENZ/quellen/v2.2_eval_cd`.
- Read-only x86 `.r` objects from
  `/Volumes/SSD1TB/projects/REA/REA_PROJECTS/KERNEL_X86/REFERENZ/quellen/v4.7_sdk_oem_bls`.
- Static REA analysis of the v4.7 SDK's `rdump.exe`, SHA-256
  `8b1c3891d8c13cb04289db77351fbd23912da70376e1271e4074872d6c2c56ac`.
  Its series decoder evidence is `ev_c06d3dcadf468392af4aaaedf8c84d518379e3abdfb67c5ba90d63229d3495c7`.
- Static REA analysis of `Program Files/DOS/bin/l386.exe`, SHA-256
  `799e5bb73ba5272b83593223e660369874a2d3ca0e1d74661189ca383a762f7d`.
  Evidence IDs: overview `ev_5e4cb037c8e166c6b6a7083085a4be4b1df491092e48cefe1c330eb45239aa77`,
  strings `ev_622ec8b9dd2b46bdad9b338cd9159ece6bbf734e601f8781d4c23953f42a39b7`,
  `ev_04376c52235e166383a66fa35fb9e91b3dd0484a6945e5e59a36c7379a58de87`,
  `ev_12de26fd40453e767a9f4983e667fd5cf779fe531aac10b7724748e5084f2bad`,
  and `ev_19f45adba20c4901110ab792f32c07c0fdef30ec3272c580ed1e8301cff9d36d`.
  The executable was not run. `file` identifies it as a PE32 Windows console
  executable; this host has DOSBox-X and QEMU, but no Wine/Windows runtime.
  DOSBox-X alone cannot provide the required Win32 environment.

## Status of the three library questions

1. **Index fields to PSECT bytes: mapped for Type 1.** The Microware table names
   all seven size fields, code and local-reference offsets, and the internal and
   external reference heads. The v2.2 `clib.l` range check covers all 159
   PSECTs that have payload bytes; section data and reference descriptors are
   contiguous through `localAt`. Evidence is static analysis plus inspection of
   the shipped file, not a running linker.
2. **Selection, duplicates, and empty PSECTs: static rules recovered.** The
   manual gives the unresolved-symbol selection rule; REA shows FIFO input
   handling, common/non-common duplicate branches, and common-symbol placement
   in aligned static storage. The 43-file v2.2 library scan has no zero-section
   PSECT with a global definition. Runtime diagnostics, resulting addresses,
   and uncommon cases remain unverified.
3. **Member reconstruction: extraction recipe established.** Section bytes,
   definitions, relocation chains, and local references can be rebuilt into a
   synthetic non-root ROF PSECT. A 238-byte example parses with `qlx86`; no
   Microware utility has independently accepted it. Exact original ROF header
   metadata is absent from the library index, so byte-identical recovery is
   impossible from this data alone.

## What Microware documents for x86

The Ultra C documentation identifies `a386` as the 80386/80486/Pentium
assembler and `l386` as its object linker. ROF Edition 9.1 and 9.2 share the
Edition 9 structure, with 32-bit counts. Edition 9.2 adds two 16-bit header
fields after the debug-size field: Code Information and Header Expansion.
Consequently, the PSECT name follows a 60-byte header for 9.2, while the
pre-9.2 header is 56 bytes. The manual describes a 6-byte relocation record:
16-bit type/location flags and a 32-bit offset. For x86, Ultra C says operand
order is source,destination; the linker applies -128 data bias to ordinary
program modules and other bias rules to system modules and raw output.

The upgrade guide says the 80x86 assembler supports ROF 9.2, and that the 386
linker rejects ROFs/libraries made by pre-UltraC2.4 assembler/libgen. ROF 9.2
external expressions are restricted to External+Absolute, External-Absolute,
and External-External.

## Observations from the supplied image

A read-only parser examined all 114 `.r` files in the v2.2 image tree. They
contain 135 concatenated PSECT records; 14 files contain more than one. Every
PSECT has sync `DE AD FA CE`, a 56-byte fixed prefix, a little-endian series
value 9 at offset 10, 16-bit little-endian counts, and 16 zero bytes after the
local-reference records. Global type words and relocation type/location words
are little-endian in these x86 samples. The 16-byte suffix is opaque in this
profile.

This v2.2 profile has 16-bit counts. The manual says that Edition 9.0 uses
16-bit counts and Edition 9.1+ uses 32-bit counts, so it is consistent with
an older 9.0-era profile. That edition identification is an inference from
field widths, not a decoded version field; the series value is not itself the
ROF edition number. These files may be among the old objects rejected by
`l386`.

The image parser successfully walked all PSECT boundaries, including
`acstarta.r` (two PSECTs). A full histogram of the observed LE16 global type
words is: `0x0004: 422`, `0x0002: 159`, `0x0003: 117`, `0x0000: 104`,
`0x0006: 20`, `0x0001: 14`. The LE16 external-reference type words are
`0x00b8: 1758`, `0x0038: 884`, `0x0218: 14`, and `0x0078: 2`. Local-reference
type words include `0x023c: 1510`, `0x003a: 859`, `0x003c: 359`, `0x021c: 280`,
and `0x003b: 176`. These align with the manual's 16-bit flag word and
little-endian x86 storage; frequencies count records, not unique types.
Across the same 135 PSECTs, the current inspector counted 836 global symbols,
794 external symbol names, 2,658 external relocation records, and 3,461 local
relocation records. These counts are records in the supplied image tree, not a
claim that each object has complete or linkable semantics.

The Edition 9.0-era image's 16-bit reference words decode under the manual's
location-flag masks. In external references, bits 3–4 select width, bit 5
selects code versus data, bit 6 negates the symbol value, bit 7 requests a
relative reference, and bit 9 selects remote data when the site is in data.
All 2,658 external records are four-byte references: `0x0038` (884) is an
absolute code reference, `0x00b8` (1,758) is relative code, `0x0078` (2) is
negative code, and `0x0218` (14) is remote initialized data. Decoding the
source section from those flags and checking `offset + width` against that
PSECT section size puts all 2,658 sites within their corresponding code or
remote initialized-data sections.

For local references, bits 0–2 encode the referenced definition type (data
or code); bit 8 marks a common definition. Bits 3–4 again encode width, bit 5
marks code location, bit 6 negation, bit 7 relative addressing, and bit 9 the
remote-location flag. All 3,461 local references in this image use width 4;
none set bit 7. Their observed words are `0x0038` (88), `0x0039` (14),
`0x003a` (859), `0x003b` (176), `0x003c` (359), `0x007c` (25), `0x021b`
(20), `0x021c` (280), `0x023a` (78), `0x023b` (52), and `0x023c` (1,510).
This is a direct byte-order and bit-mask decode of the old image profile, not
proof of how 9.2 expression references are represented. The `0x02xx` local
records include combinations of code-location and remote-location bits, and
some associated offsets lie beyond the object-code size in later PSECT
sections. Their exact source-area interpretation remains unresolved; do not
use them to implement relocation placement yet.

### Later SDK ROFs: x86 Edition 9.2-shaped profile

The v4.7 SDK/OEM tree contains 38 `.r` files with 80 concatenated PSECTs.
Their sync is `DE AD FA CE`, series word bytes are `E9 00`, the PSECT name
follows a 60-byte header, counts are little-endian 32-bit, and each PSECT ends
with 16 zero bytes. The two words at header offsets `0x38` and `0x3a` occupy
the documented Code Information and Header Expansion slots. `rdump.exe`
normalizes raw series words `$E9` and `$F9` to Edition 9 and derives their
minor versions as 2 and 1 respectively. This directly identifies `$E9` as
Edition 9.2 and `$F9` as 9.1; it also confirms that Code Information and
Header Expansion are read for Edition 9.2. This is static analysis of the
shipped dumper, not a runtime observation.

These newer objects provide a more direct relocation check. External flags
are `0x0038` (112 absolute code refs), `0x00b8` (337 relative code refs), and
`0x0218` (28 remote initialized-data refs). Local flags are `0x0038` (112),
`0x003a` (105), `0x003b` (28), and `0x021b` (14). All are four-byte
references. Applying the documented source-location masks puts all 477
external sites and all 259 local sites within their selected code or remote
initialized-data section. The local flag variants also decode their target
types through bits 0–2 as documented. This corroborates the ordinary
location-flag model for the later profile and avoids the anomalous `0x02xx`
combinations in the older v2.2 objects. `qlx86 --inspect` accepts Edition 9.2
(`0x00e9`) and reports its Code Information and Header Expansion words. It
parsed all 38 v4.7 `.r` files and all 80 concatenated PSECTs. Other later
series, including the separate series-12 reference-table layout recognized
by `rdump`, remain unsupported.

Neither available `.r` corpus supplies an example of the short widths handled
by `l386`: all 2,658 external and 3,461 local references in the older image
profile, and all 477 external and 259 local references in the Edition
9.2-shaped corpus, use width 4. Thus the linker's 1- and 2-byte formulas are
known only from static code, not from a Microware-produced x86 object fixture.
This is a coverage limit, not evidence that the assembler never emits short
references.

## The `CMDS/link` command

The image file `Program Files/OS9000/80386/CMDS/link` is an OS-9000 module,
not the assembler's object linker. Its embedded strings say “Function: link a
module in memory” and describe `link [<opts>] {<modname> [<opts>]}`, including
`-z` to read module names. The Ultra C documentation separately names `l386`
as the x86 ROF-to-module linker. The `link` command is therefore not evidence
that the QCC ROF linker can use its input format.

Static strings in `l386.exe` include its `-l=<path>` library option, an
unresolved-symbol diagnostic, a duplicate-symbol diagnostic, and common-block
type-conflict diagnostics. These confirm the executable contains object-linker
paths for library search and symbol checks; strings do not establish the exact
algorithms or prove that any given ROF profile is accepted.

Static REA analysis of `Program Files/DOS/bin/libgen.exe` (SHA-256
`f4033a3cc9d6bbde18d749b45ada6f112fbdfe484eb17f6fabc4d29310dc8843`)
found diagnostics for a bad/too-old ROF edition, a mainline psect in a library,
assembly errors, duplicate definitions, and common-block type conflicts.
These strings identify validation cases that a future library builder or
reader should consider; they do not prove the exact branch conditions or
runtime behavior. Evidence: string search
`ev_6708dd5a057383ddfe3d4353e3baa474913a93058e9e5e08bc6de47c53a16edd`.

Static REA analysis of `Program Files/DOS/bin/rdump.exe` (SHA-256
`976bfd146d5d326e1f2056a95bc88624246f7cd7874a9080d670c0ffa91fc9f5`)
provides additional format evidence. The utility describes itself as a dumper
for `.r` and merged ROF `.l` files; its switches expose local/reference,
external-reference, global-definition, and library-order information. Its
header reader selects field widths and presence by the ROF series, including a
special raw series value `$F9` that it normalizes to series 9, minor 1 in its
internal representation. In the reference reader, older series use 16-bit
counts (when the minor field is zero) and 16-bit offsets before series 10;
later series use 32-bit counts/offsets. Series 12 and newer take a separate
reference path with expression-tree and reference-table readers instead of
the earlier local-reference layout. It reads an expression-tree count and
later reference records containing two 16-bit values and two 32-bit values.
The header reader also conditionally reads an extra 16-bit field and a 32-bit
extension at series 12. This supports the manual's description of materially
different later layouts, but mapping `rdump` series 12 exactly to a particular
manual edition remains an inference. Evidence: header reader
`ev_98b6ca6acf3ad58b38f2f05e65e845649f56d5c3b5cd348a0785de6bb72a3baf`;
external-reference reader
`ev_1ea5ace7f5cfa21a9328ff6e3c5ae1c050a6b0730c8add155ce0ef024df5996a`;
expression-tree reader
`ev_c1ca55ee545be6d9ed57b8afec353fcd999aa4dd5da30042c33fe0a79b8b0bb8`;
reference-table reader
`ev_39f694b951cac84b4088bced108d991086af3c4daefa7ca4e3cbfd81dcfff72d`;
artifact SHA-256 above. This is static decompilation evidence, not a runtime
observation or a decoded Microware-produced 9.2 fixture.

The dumper's library-order checker rejects series below 9 as requiring
reassembly and rejects series above 15 as too new. It can compare definitions
against references across libraries and reports a definition that appears
after a reference. This confirms a specific diagnostic capability in
`rdump`; it does not establish `l386`'s complete library search or selection
algorithm.

Focused static analysis of v2.2 `l386.exe` confirms the `-l=<path>` option is
handled in the command-line parser and passed to a shared input-path routine.
That routine allocates a node, stores a value and links it into a global list
(`FUN_004108d0`, called from `FUN_00414b14`). This is consistent with deferred
processing of input/library paths, but the decompiler's register-derived
arguments are unclear and do not establish list-node fields or processing
order. The displayed help text is at `0x430d2c`; its xref points to a string
pointer table, not directly to the parser. Evidence: parser pseudocode
`ev_fca990e15bebed151121639e18fd58491213ecbd08ad3d12f54ac2a27d04584b`,
string xref `ev_a8abff0e6abd3e8323c199b109b026a004753e42fe94c38436ca3f57d3dd8672`.
This narrows the linker trace to its queued input list; the indexed-library
member lookup and PSECT payload layout are now identified. Library selection is
demand-driven, with FIFO command-line input ordering; common conflicts are
understood statically, with runtime behavior still unverified.

The same binary's generic file-object layer is now partly mapped. The path
wrapper `FUN_0041d485` parses a mode and calls `FUN_0041fa07`; the latter maps
access, creation, and attribute flags to `CreateFileA`, then stores the native
handle in a runtime file object. This proves how the Windows-hosted linker
opens files at the OS layer, but does not identify which file-object calls
consume queued `-l` entries or how those reads map to ROF structures. Evidence:
wrapper pseudocode
`ev_eaa088a50da87221a5b91ee4274f50043f5eb4acf564a07dbed377dd2d0b3aeb`,
CreateFile wrapper pseudocode
`ev_41df961089c4ed5aeb40ea23650069d31957aebb143531c909169678db1ad221`.

The next trace reached the format dispatch. `l386` reads the first 32-bit
word and compares it with `$2D00D5BB`; the v2.2 x86 `clib.l` starts with bytes
`BB D5 00 2D`, so this branch identifies the indexed type-1 library container
in the examined image. The dispatch sends this format to `FUN_004178e4`; files
without that word go through the separate ROF-object path `FUN_00417840`.
The input-processing loop calls this dispatcher for each queued input file.
The indexed-library reader consumes the hash table (1,512 bytes), global
definitions in 22-byte records, the string area, PSECT records whose stride
depends on header edition, and internal and external reference tables. These
reader strides/counts agree with the type-1 sections already measured in
`clib.l`. Independently, `FUN_00410c60` computes the symbol bucket as the sum
of name bytes modulo `$179` (377), directly confirming the formula previously
inferred from the 139 image libraries. Evidence: dispatch
`ev_56ec7f85c746982b27db41ab8b37d8c9f2f989f3c9eb1b26878feeb982e86867`,
magic read `ev_7cf7db881c4dc6e602efa7507d0c3ec4d32850a7e6d85387bbc2a31ade15f9ad`,
input loop `ev_02a92b162b499c26b8219d6d4ab6d8e31f33afdd981c8a23d74a014cc3b7cd14`,
library reader `ev_f2ee2b98e82610a96624e65346630ff857bef1ddaf6959e83319ee8b36f34214`,
hash routine `ev_3139122b53462d5040d8726a26cf37f9344a3d8d8840763dd36f80bfce1d6d99`,
section readers `ev_c195193bb13746713136c5b862950c2f05dba1c49528e178b33e2587095cfb36`,
`ev_7f9cf7ce240e1a6224b03f5d35cc0d8edc92661cb7e233d42c32148a4d12a4fb`,
`ev_38980b22736580093ea50a2c941254cd267fccf0a3e470bfdb838236a3fe6518`,
`ev_1ed1e587de5883c0a061cfb7faf23079580c22ae98fac95ddf959353b8c3b8a6`,
`ev_ab79b09473ad2b646b5f0a71366c99abb2c3e7f1a0ffa0fe6d81439c36bc3a97`,
`ev_7ff010a9a10ba80ae8e7c6cf7fef44841d152f37f02d5d04911b5cdf1dffc454`.
This is static analysis of v2.2 `l386.exe` (SHA-256
`799e5bb73ba5272b83593223e660369874a2d3ca0e1d74661189ca383a762f7d`) checked
against the actual `clib.l` header and the image-derived section sizes; it is
not yet a runtime trace. The same reader follows global-definition chains
through the hash table and inspects member/reference relationships. The
demand-driven selection condition and the later PSECT payload-copy path are
now identified. The branch conditions, FIFO input order, and full Type-1
initialized/remote payload mapping are detailed below; runtime behavior remains
unverified.

Control flow in `FUN_004178e4` narrows that condition: it walks the linker's
pending symbol list, hashes each name, follows the matching global-definition
chain, and maps a matching definition to its PSECT and its internal/external
reference chains. The PSECT is then merged into or added to the linker's
working structures, which can enqueue further references. This strongly
indicates demand-driven library-member selection from unresolved symbols. The
selected PSECT is later reread from its source file during finalization.
Conflict branches and input ordering are summarized below. The
initialized/remote payload fields are mapped in the Type-1 layout section. Evidence:
library-reader pseudocode above and input dispatch/caller `ev_02a92b162b499c26b8219d6d4ab6d8e31f33afdd981c8a23d74a014cc3b7cd14`.
The symbol lookup helper `FUN_00410c8c` uses the just-confirmed 377-bucket
hash and compares names while walking the bucket chain. The reader also has
separate diagnostics for conflicting definitions and for a replacement PSECT
whose symbol must also be defined. This confirms that replacement/duplicate
cases affect selection. The common marker is identified as bit 8 by the ROF
symbol-type table; the static both-common merge path is detailed below.
Input files are processed FIFO: the command parser appends each path to a
linked queue (`FUN_004108d0`) and the input loop pops its head
(`FUN_004108f8`) before dispatching to the ROF/library handler
(`FUN_00417e64`). Combining this with the manual rule, the first library in
that order that finds a still-unresolved symbol can select its defining PSECT.
A later library can still encounter a duplicate definition if its PSECT is
selected for another unresolved symbol; then the conflict/common branch below
applies. This ordering conclusion is static; runtime outcomes remain untested.
Evidence: CLI parser `ev_0767b40b548dbe59e13e6296a07473c8a06c3f3b02165c655eb413afc89c4ee2`,
queue append `ev_553239067167cb397e6ecbaabc883c1aca5a67a59b6896706750d816549ec181`,
queue pop `ev_f3ea1d48026ff11fac838f32921d3a1a7fa3de17221d42a314dd6090ca9674aa`,
input loop `ev_932336ef87f772ea2b33d6a1d81c9668e8212a521faef4ead7f7cf620f068150`,
format dispatcher `ev_e65f228926201817b691e1cd7c80847255139fd9995dea9e5400fc7048184fdf`.
Other evidence: lookup `ev_ba51a77b3dc50213394db5aacea719d20b6e758727ff204dee28385ecd5b9d82`,
duplicate diagnostic `ev_c70de616194a8df304119e9dcd561e8bd13d3166985a5913dd975c05c740b50a`,
replacement diagnostic `ev_7ef367cc7eb794b16e871dd8d00352f8c4c2632ba374c65ebc9a42b7e00d865e`.

The selected-PSECT finalization path is now traced. `FUN_00419590` obtains
queued PSECT nodes through `FUN_004153a8` and calls `FUN_00415e80` for each.
That routine opens or reuses the node's source path (`node+0x88`) via
`FUN_0041219c`, then calls `FUN_004133d0`. The latter seeks the source stream
to the 32-bit value at `node+0x6c`, then reads up to four separately sized
blocks from that source. Their exact lengths are `node+0x34 - node+0x54`,
`node+0x20 - node+0x5c`, `node+0x2c - node+0x64`, and `node+0x38`, in
that order. The first three blocks are copied with `FUN_00413710`; the fourth
uses `FUN_00413754`. The finalizer accumulates six PSECT
size fields (`+0x1c`, `+0x20`, `+0x28`, `+0x2c`, `+0x34`, `+0x38`) into six
global section totals, then converts them to cumulative offsets by subtracting
each original size from its new total. This establishes the exact source-read
length expressions and cumulative-layout behavior, but not every format-level
name (initialized data, remote data, etc.).
Evidence:
`FUN_004153a8` queue traversal `ev_864b33205d68b7fdc6e9aac37fcd8c976881536385a158fd3ae1741fbd5dd763`,
PSECT finalizer `ev_a67b8706622d9c1dfb6033fa7044126cedf30102e736980eae431883407fe27c`,
PSECT reader `ev_eb106588065f9795132816a72d361d154a3e1405d90134aedd8ea4742bbbc30e`,
source open `ev_48740546175520233fe59e6234d06df980f0bc821b16e03f43d9662371f7a71f`,
and output copy `ev_3741154e75e7158c2efe88652026b4b521adc2278b239ff4ad79bc74e810d6b4`.
The indexed-library reader builds a normalized 84-byte PSECT record. In the
old 62-byte profile, the parser callback sequence maps the normalized source
field at `+0x2c` to on-disk PSECT offset `+0x30`; the constructor copies it to
`node+0x6c`, which the reader seeks to. This proves that `node+0x6c` is
`codeAt` for this profile. The next normalized source field at `+0x30` maps to
on-disk `+0x34` (`localAt`) and is copied to `node+0x70`; `FUN_004136bc`
seeks there before reading local-reference records. Evidence: parser
`ev_d778791ae2897ca63ddba5f0e590893f76f7a925d44b1964e10262aba0dccd87`,
constructor `ev_05b75ce74ddcbff9e735f3d545c10fbfd312e2fed488e422794174f02e01595d`,
reader `ev_eb106588065f9795132816a72d361d154a3e1405d90134aedd8ea4742bbbc30e`,
and local-table seek `ev_f5d45645600095aebf2e190dbb3eb4f8eefaa805dda5e80f4eaa8cc31bee64ec`.
Across v2.2 `clib.l`, every non-empty code range fits within the file payload.
Zero-code records may have `codeAt` values before that payload, so their code
offset is not an actionable data bound.

The supplied image also contains the authoritative OS-9000 header definition
at `Program Files/OS9000/SRC/DEFS/module.h`, including the 88-byte `mh_com`
structure. The x86 `CMDS/link` module matches its byte layout: 16-bit sync and
system revision, 32-bit size/owner/name offset, then access, type/language,
attribute/revision, and edition words; 32-bit fields continue through
`m_init`, `m_term`, `m_dbias`, and `m_cbias`; `m_ident` and eight reserved
bytes precede `m_parity` at offset `0x56`. The fixed header and module name
begin at `0x58`. Multi-byte fields are little-endian in this x86 image.

| Offset | Field | Width |
|---:|---|---:|
| `0x00` | `m_sync` | 2 |
| `0x02` | `m_sysrev` | 2 |
| `0x04` | `m_size` | 4 |
| `0x08` | `m_owner` | 4 |
| `0x0c` | `m_name` | 4 |
| `0x10` | `m_access` | 2 |
| `0x12` | `m_tylan` | 2 |
| `0x14` | `m_attrev` | 2 |
| `0x16` | `m_edit` | 2 |
| `0x18` | `m_needs` | 4 |
| `0x1c` | `m_share` | 4 |
| `0x20` | `m_symbol` | 4 |
| `0x24` | `m_exec` | 4 |
| `0x28` | `m_excpt` | 4 |
| `0x2c` | `m_data` | 4 |
| `0x30` | `m_stack` | 4 |
| `0x34` | `m_idata` | 4 |
| `0x38` | `m_idref` | 4 |
| `0x3c` | `m_init` | 4 |
| `0x40` | `m_term` | 4 |
| `0x44` | `m_dbias` | 4 |
| `0x48` | `m_cbias` | 4 |
| `0x4c` | `m_ident` | 2 |
| `0x4e` | reserved | 8 |
| `0x56` | `m_parity` | 2 |

These offsets come directly from the shipped `mh_com` declaration and match
the 88-byte module from the same v2.2 image. Header parity and trailing module
CRC are separate checksums.

`CMDS/link` is 3,360 bytes. `M$Size` at offset 4 is `0x0d20`, `M$Name` at
`0x0c` is `0x58`, and its NUL-terminated name is `Clink`. The module header
parity at `0x56` is `0x4378`. The OS-9000 x86 kernel's loader routine starts
with `0xffff` and XORs all 44 little-endian 16-bit words of the 88-byte header;
the valid result is zero. The module ends with CRC bytes `FB 08 05`. Applying
the documented 24-bit OS-9 CRC algorithm to the preceding bytes produces those
three bytes, and including them yields residue `0x800FE3`. The file therefore
confirms both header parity and a trailing module CRC.

These facts settle header serialization and checksums. The loader and the
examples below also establish the initialized-data descriptor and relocation
lists. `qlx86 --emit-module-candidate` now writes this 88-byte header for a
restricted relocation-free private QCC object; its output has not yet been
checked with the loader. `--emit-prototype` still writes the private 56-byte
diagnostic format. The sample's
`M$Exec` is `0x60`, `M$Data` is `0x7e8`, `M$Stack` is `0x3800`, `M$IData` is
`0xcc0`, and `M$IDRef` is `0xcf8`; these offsets are observations from this
specific linked command module, not yet a general output-layout rule. In the
294 parity-valid `$0101` program modules, `m_name` is always `0x58`, and
`m_exec` is always the next 8-byte boundary after the NUL-terminated name.
All 294 have nonzero `m_idata` aligned to four bytes, nonzero even `m_idref`,
and an even total file size. This corpus supports the candidate writer's
name/code, initialized-data, relocation-list, and final-size alignment choices;
it does not prove that every module type or loader version uses those rules.

A read-only header sweep of `80386/CMDS` found 403 module candidates with
valid header parity, bounded names, and in-file sizes. Of these, 294 have the
ordinary program type/language word `$0101`. All 294 have `m_access=$0555`,
`m_dbias=$80`, and `m_cbias=0`; their
`m_excpt` values are `$70` (261 files) or `$78` (33 files). Their stack sizes
vary, so a fixed stack default is not supported by this corpus. Owner values
are split between 0 and 1, so ownership is not a safe format default. The
other 109 files contain seven type-2 modules with `m_dbias=$80`, four type-4
modules with zero bias, and 98 type-12 through type-15 modules with zero bias.
This exactly matches the Microware x86 Processor Guide: the linker applies
the -128 data-symbol bias to non-system module types, and leaves data symbols
unbiased for data, system, file-manager, driver, and device-descriptor types.
The restricted QCC candidate writer now selects `m_dbias` by module type and
converts an absent ROF trap sentinel (`$ffffffff`) to module exception offset
zero. Static analysis of the shipped x86 `l386.exe` independently confirms
this default: its `_m_excpt` helper returns zero when no trap entry is defined,
and otherwise returns the trap offset in the output module. No x86 program in
the image has a zero exception offset, so loader acceptance of this case still
needs runtime validation. Evidence: `FUN_00419e5c` in
`ev_dabd6221933978cf803fa67d7353d72edb71eb2576fb788f80dfdb744f857cab`, called
from output routine `FUN_0041afa4` in
`ev_2dcfab56ffff9f65a633122b0e719c9feea0067005ef64516d40ca42ff538416`.
The -128 data-symbol bias is a separate linker rule; the observed 32-bit
`m_dbias` field value `$80` should not be described as the two's-complement
encoding of -128.

`l386`'s output routine references `GRPUSER` and looks up its value. When the
lookup returns no value, the routine initializes the two owner words to 1 and
0, which yields the candidate's numeric `m_owner=1` in the observed header
layout. The `-gu=<group>.<user>` option parses an explicit override into those
words. The image sweep independently found program module owners split
between 0 and 1, so the owner field is not a universal constant. The private
QCC ROF has no owner field; its writer currently emits 1, matching the
no-`GRPUSER` fallback but ignoring that environment variable and `-gu`. Static
evidence:
output routine reference to `GRPUSER` and fallback initialization,
`ev_2dcfab56ffff9f65a633122b0e719c9feea0067005ef64516d40ca42ff538416` and
`ev_4b4b3c05f536a12cef707f2c4890baa12a9a4ddba1b3866915b977cc28ce64ae`;
`-gu` option text, `ev_91106e2c66981b935d81d004d102c1345e7823d4df18a6ee0509ff390e0c1e42`;
parser `FUN_0041123c`,
`ev_9956a569f23d3fae70a01710adcd5954e81cbfb790831bfd5d8253a030610d60`;
and owner override helper `FUN_0041bae4`,
`ev_80b90b0efad7522878bd9adca2442713c0138a1027639519bc87e4ed9e002432`.

The Microware PSECT directive says the linker totals the stack estimates from
all included PSECTs, then adds any whole-program stack requirement. The
candidate writer sums the private QCC ROF PSECT stack fields with an overflow
check. Its restricted candidate path now accepts multiple concatenated QCC
PSECTs and pads their code, initialized data, and uninitialized static sizes
independently according to `-b` (default 2, options 2/4/8/16). This implements
the ordinary static `l386` size-padding pattern for the areas represented by
the private profile. It does not implement Microware symbol classes, remote
areas, relocations, library linking, or the exceptional `-a` path. The new
multi-PSECT candidate change has not yet been built or checked with a loader.

### x86 initialized data and relocation tables

The OS-9000 x86 loader routine `FUN_0022246c` in the v2.2 kernel disassembly
resolves the `m_idata`/`m_idref` layout. It reads two little-endian 32-bit
length words at `m_idata`: a zero-fill length followed by an initialized-data
length. It zeroes the first range in the allocated data area, copies the
second range from `m_idata + 8`, then zeroes the remainder up to `m_data`.
For `CMDS/link` those lengths are 24 and 48 bytes, and
`m_idata + 8 + 48 == m_idref` (`0xcc0 + 8 + 0x30 == 0xcf8`).
The loader uses `m_data` as the allocated data-area length: after zeroing the
first range and copying the initialized bytes, it zeroes the remaining range
until that length is reached. This differs from the file offset `m_idata`.
In `CMDS/link`, the module body begins at `0x60`, `m_exec=0x60` is an entry
offset, code and constants continue until `m_idata=0xcc0`, and `m_data=0x7e8`
is the in-memory data-area length.

At `m_idref` the loader consumes two consecutive relocation lists. Each list
contains groups of a 16-bit high word, a 16-bit count, and `count` 16-bit
offsets; a zero count terminates each list. A relocation target is
`(high_word << 16) + offset + data_base`. The first list adds the module base
to the 32-bit value at each target; the second adds the allocated data base.
In `CMDS/link`, the first list is `(high=0, count=5, offsets=0x30, 0x34,
0x38, 0x3c, 0x40)` and the second is `(high=0, count=3, offsets=0x2c, 0x24,
0x28)`. Both end with a zero count, followed by the module's three CRC bytes.

The x86 `RBF` module independently confirms the data boundary:
`m_idata=0x9118`, initialized-data length `0x588`, and `m_idref=0x96a8`,
so `m_idata + 8 + 0x588 == m_idref`. Its first initialized-data word is
`0x10`, matching the 16-entry dispatch table documented in the x86 kernel
research notes. The loader's two relocation walks match the two lists at
`m_idref`. These examples and the loader implementation establish the
descriptor and relocation-list format for the examined OS-9000 x86 image.
The loader also handles flagged odd/aligned lengths by consuming a 16-bit
boundary word; the producer-side conditions for setting those flags remain
unknown.

A read-only sweep of the v2.2 OS-9000 tree found 2,631 header-parity-valid
module candidates whose two terminated relocation lists fit within `m_size`
and whose initialized-data end equals `m_idref`. Six other parity/name-valid
files had data descriptors that did not match this layout; they were `.stb`
files, so this count is corroboration for module files, not a claim about
every file shipped in the tree. The sweep did not validate each candidate's
trailing CRC.

This does not yet determine how ROF relocation expressions, symbol
resolution, program entry points, psect grouping, or type-specific module
layout map into these tables. The evidence supports implementing the module
container, but does not establish general `l386` compatibility.

The image also contains `LIB/clib.l` (91,921 bytes). Its first 42 bytes match
the manual's ROF Edition 9 library format type 1 header: library type 1 at
offset 4, edition 1 at offset 12, then six 32-bit little-endian section sizes
from offset 18. This confirms that x86 libraries have a separate indexed
container, even though the manual also permits libraries made by concatenating
ROFs. The header's name offset at byte 14 points to `clib.l` in the string
table. The six indexed regions are hash table (1,512 bytes), global
definitions (5,170), strings (7,879), psects (11,966), internal references
(4,718), and external references (1,792). They occupy bytes 42–33,079; the
remaining 58,842 bytes contain further library payload. The manual's type-1
psect record is 62 bytes; the observed psect section divides exactly into 193
records. Each record includes the ROF format value, memory/code/debug/stack
sizes, object and local-reference offsets, reference counts, internal/external
list heads, psect-name offset, and global-definition list head. All 193
observed records use ROF format 9. `qlx86 --inspect-library` validates the
fixed header, indexed-region bounds, bounded library and psect names, the
62-byte record count, and code ranges, then reports the psect index entries.
The remaining documented record widths also divide their observed section
sizes exactly: 378 four-byte hash slots, 235 22-byte global definitions, 337
14-byte internal-reference entries, and 128 14-byte external-reference
entries. `qlx86 --inspect-library --library-symbols` now lists the global
definition records (type, value, name, and psect index) and checks their string
and index bounds. It walks the hash chains and PSECT reference lists, verifying
that each global, internal-reference, and external-reference entry is reached
exactly once. It checks reference-record indexes, names, and file bounds. The
two images examined here use 378 hash slots, while every global definition is
in the bucket `sum(symbol-name bytes) mod 377`. This rule was checked across
all 57 `$0001` libraries in v2.2 (11,100 definitions), all 81 `$FF01`
libraries in v4.7 (10,918 definitions), and the separate v4.7 `$0001`
`pcilib.l` (14 definitions). `qlx86` now reports how many definitions match
this rule for 378-slot tables. Other table sizes are still parsed and
chain-checked, but their hash formula is not assumed. Hash checking does not resolve
symbols or parse ROFs from the payload. In the inspected v2.2 `clib.l`, all
235 global definitions, 337
internal-reference entries, and 128 external-reference entries are reachable
exactly once through those chains.

The modulus is one less than the observed slot count, so bucket 377 remains
unused in these fixtures. The check is reproducible directly from the index:
follow each slot's `nextHash` chain to recover a definition's bucket, sum the
unsigned bytes of its NUL-terminated name, then compare that bucket with the
sum modulo 377. This is a derived property of the examined shipped libraries,
not a runtime observation or a documented Microware algorithm.

The index's internal and external reference records point to 6-byte sites in
the library payload. The inspector now decodes the 16-bit flags and 32-bit
site offset for these entries, selects code, normal data, or remote data from
the documented location bits, and checks the field width and PSECT bounds.
On the `clib.l` sample this covers 969 sites: flags `$0038` (421), `$00B8`
(542), and `$0218` (6); every site fits its selected PSECT region. Across all
57 indexed `$0001` libraries in the v2.2 image, the inspector accepts all 57
and checks 43,305 sites: `$0038` (17,830), `$00B8` (22,403), `$0218` (2,727),
and `$031A` (345). These counts apply only to indexed internal/external symbol
references.

The PSECT `localAt` field points to a count followed by six-byte local-reference
records. The `$0001` profile uses a 16-bit count; `$FF01` uses a 32-bit count.
The inspector reads these tables and records their flags. Across the 57 old
libraries it found 18,424 local sites; across the 81 newer libraries it found
12,662. All newer local sites fit the section selected by the location flags.
In the old profile, 2,594 local sites are outside the section selected by the
ordinary masks. These are all flag `$023A` (17), `$023B` (14), and `$023C`
(2,563), matching the old profile's ambiguous remote-plus-code combinations.
The inspector reports this count without rejecting the otherwise structurally
valid library. Their placement remains unresolved and is not used for linking.

### Later SDK `$FF01` library variant

The v4.7 SDK's `OS9000/80386/LIB/clib.l` is 119,370 bytes and begins with the
same four-byte identification code (`BC D5 00 2D`) and a 46-byte
header, but the 16-bit format word is `$FF01` rather than `$0001`. Its header
reports region sizes 1,512 / 5,786 / 10,790 / 22,374 / 6,800 / 2,352 bytes.
The global-definition area divides into 263 records of the observed 22-byte
shape. The PSECT and internal-reference area sizes do not divide by the old
62-byte and 14-byte widths. Static analysis of the shipped v4.7 `libgen.exe`
shows it normalizes `$FF01` to library type 1 and sets an extension flag. It
reads a 46-byte header, including a four-byte expansion at offset 42, 66-byte
PSECT records, and 16-byte reference-index entries. The PSECT records add two
16-bit fields after the stack size, shifting later offsets by four bytes;
reference entries use a 32-bit count followed by the reference offset, next
index, and symbol/name index. In the examined 71-library 80386 corpus, the
first extra PSECT word is `$0000` in 2,562 records and `$0003` in 2,305;
both values occur in records with and without code. The second word is always
`$0000`. The distribution does not identify their semantics; the manual's
Type-3 target-processor field is a possible clue, not a confirmed mapping.
The header expansion is not
always zero: `mt_socket.l` stores `$00000003`, while `clib.l` stores zero.
Keep these values as raw fields until another producer or consumer confirms
their meanings. Evidence IDs for
the shipped `libgen.exe` functions are `ev_c339044eaabaeee0a777d6bab029f9b6dfcbd7d1b434965b0be7668c8cf7a95a`
(header profile), `ev_2a57f6c5907f0904dd7058e68903e7c958ee79ef05fbe4921d7f25a9350f717a`
(PSECT entry-size selection), `ev_326eca9fcf84fb3aab415ebda5e1f61e1336336ad4125f564ab7bc3bfb85c0d4`
(reference entry-size selection), and `ev_e60b58c16ec68ba5f0db79226a57a5e9e6d70597abb4de1e1fa0a42443966550`
(PSECT record reader). The artifact SHA-256 is
`7978e17c1a4b6631d2ae316a46f2754b88568ad1fb3cea0eedd999195de0b336`.
This is static analysis of a shipped executable, not a runtime observation.

`qlx86 --inspect-library` now parses this observed variant separately from
`$0001`. It inspected all 81 `$FF01` indexed libraries in the v4.7 tree,
covering 4,923 PSECTs, 10,918 definitions, 9,290 internal and 6,260 external
reference-index entries. It checked 36,766 indexed reference sites against
the selected PSECT areas: `$0038` (9,762), `$00B8` (25,783), `$0218` (923), and
`$031A` (298). Every chain, name, index, reference width, site bound, and
code range passed inspection. The inspector now bounds the full contiguous
payload span (code, initialized static, remote initialized static, and debug)
for each PSECT. A direct scan found all 4,867 PSECT spans in the 71 recursive
`80386/LIB/*.l` files within their containers. The inspector prints the two
additional PSECT words as raw `field1` and `field2` values. This establishes
the index layout for these
fixtures; it does not establish all `$FF01` producer variants or linker
semantics. The observed `$FF01` `clib.l` contains 727 local-reference sites;
the old `$0001` `clib.l` contains 653. No source runtime or linker was
executed.

The manual's library chapter says format 1 applies to Edition 9 and format 3
to Edition 15. It describes four-byte global-definition indices hashed by
symbol name but does not specify the hash function. Its Type-1 PSECT table
explicitly names each stored size and offset (Ultra C/C++ manual, pp. 357–359):
record offsets `+0x02/+0x06/+0x0a/+0x0e/+0x12/+0x16/+0x1a` hold uninitialized
static, initialized static, remote uninitialized, remote initialized, object
code, debug, and stack sizes respectively; `+0x1e` is code offset and `+0x22`
is local-reference offset. Thus the four payload areas are code, initialized
static data, remote initialized data, then debug. Uninitialized areas and
stack reserve space but contribute no payload bytes. The byte-sum modulo-377
rule is an empirical finding from the examined 378-slot Microware libraries;
it is not stated in the manual and should not be generalized to other table
sizes or unexamined formats. Demand-driven selection and FIFO input precedence
are supported by the static trace below. Runtime common-symbol behavior and
independent validation of the synthetic ROF remain open.

For v2.2 `clib.l`, the old-profile parser mapping confirms `codeAt` at PSECT
record offset `+0x30` and `localAt` at `+0x34`; the linker seeks to these
fields through its normalized node. All 153 non-empty code ranges fit within
the file payload. Zero-code records may have `codeAt` before the payload; for
example, data-only `_iob.c` has `codeAt=105` and `localAt=35,337`, so `codeAt`
is not a usable payload bound when code size is zero.

The index also gives code size, global definitions, internal/external
reference chains, and local-reference counts and records. Reference-chain
entries point to six-byte descriptors in the payload. In `_a_fcosh.c`, code
starts at file offset 33,079 and is 112 bytes; its sole internal-reference
entry points to descriptor offset 33,191, whose bytes are `38 00 64 00 00 00`
(flags `$0038`, site offset 100). `localAt=33,197` starts a 16-bit zero count.
Thus code, indexed reference descriptors, and local references can be
reconstructed as separate pieces for this PSECT. The four section names and
order now come directly from the Microware Type-1 table and ROF section
chapter. A read-only consistency pass over all 193 PSECT entries in this
`clib.l` found 159 with code, initialized data, remote initialized data, or
debug bytes. For all 159, `codeAt + code + init + remote-init + debug` ends
exactly where the linked internal/external six-byte reference descriptors
begin (or at `localAt` when there are none); those descriptors are contiguous
and end at `localAt`. This is derived from this one shipped library, not a
claim about every Type-1 producer. Uninitialized static/remote sizes and stack
need no bytes in the payload. A byte-identical original ROF still cannot be
recovered: the library entry does not retain all original ROF header metadata
(e.g. assembly date/time and root-only entry/module attributes). A synthetic
non-root ROF PSECT can be assembled from the retained fields, payload, symbols,
and relocation records, subject to confirming any required header defaults.
As a structural proof of the recipe, a synthetic 238-byte `_a_fcosh.c` ROF was
assembled in `/tmp` from the index and payload: it contains the 112-byte code,
one global, one internal reference converted to a named external reference, an
empty local table, and the 16-byte trailer. The QCC inspector parsed it as one
old-profile PSECT. This is self-validation only; Microware `rdump`/`l386` has
not independently accepted it and the file is not byte-identical to the
original object.

For selection, the manual says a library PSECT is retained only when it defines
a symbol that was unresolved before that library was searched (p. 357). Static
`FUN_004178e4` follows the pending-unresolved list into the hash bucket and
selects the defining PSECT; that PSECT's external references can add further
work. Duplicate symbols set through ROF type bit 8 (common) are accepted when
both definitions carry that bit; if either lacks it, the conflict diagnostic
branch runs. In the both-common branch, the candidate symbol value is set from
the existing common value, rounded up to an even value when odd. A completely
zero-section PSECT takes a compact symbol-only path instead of full PSECT
construction. These are static code observations. Common-size allocation beyond the observed
even-rounding branch and runtime diagnostics remain unverified because
`l386.exe` was not executable here. A scan
of 43 Type-1 libraries in the v2.2 x86 LIB directory found 547 common global
definitions and 258 symbol names defined as common in more than one library.
All 567 PSECTs with zero code/data/debug sections in that sample also have no
global-definition head; thus the compact symbol-only branch was not exercised
by a zero-size PSECT with definitions in this fixture set. The image's
`rdump.exe` has `-c` for displaying a code file offset and `-o`
for reference/local offsets. Its inspected parser is an ROF-object reader: it
checks the relocatable-module sync and series and calculates the code offset
from the ROF header section lengths. No type-1 library-magic dispatch was
observed there, so its output cannot directly validate `.l` member extraction
without first reconstructing an ordinary ROF object. Evidence: `rdump` main
path `ev_08cca69b290468a9d1b663b4a663eb6710c19c7f42dfa9aec0096e5f4ef5a719`,
code-offset option string `ev_a626b7ec8ade7b180b6acea6827040261eb6474ea9cbcd5c887d9cce01f5813a`.

Static selection details from `FUN_004178e4`:

- The PSECT reader checks six size-related fields. When their sum is zero, it
  takes a compact-symbol path (`FUN_00410c10`) instead of constructing a full
  PSECT node and walking its global-definition chain. In the v2.2 `clib.l`
  fixture, all 28 records with those six fields zero have no global-definition
  head. The exact behavior for a zero-sized PSECT that does have definitions
  is not represented in this fixture.
- For a candidate definition already present in the linker's symbol table,
  `FUN_004178e4` checks bit 0 of the symbol type byte at record offset
  `+0x15` for both definitions. If either bit is clear, it follows the
  duplicate/conflict path and can emit the “symbol ... defined by psect ...”
  diagnostic. If both bits are set, it copies the existing size field into the
  candidate and rounds an odd value up by one. The bit is consistent with the
  common-block case, but the exact meaning and eventual size aggregation still
  need confirmation from a runtime or Microware-produced fixture. Evidence:
  reader `ev_6a92c9334709f570e93d25223b6c2f3897cb590149e9af1d80e3b8f3bfff5203`,
  diagnostic `ev_433954811f913f5074ab29021b3512f33c2d8f3d577d3b320cfd1d8e588abd0a`.
- Once a PSECT is accepted, its internal and external reference heads are
  traversed through linked index records. Each entry carries a count, a file
  offset to six-byte site descriptors, a next-record index, and a target
  symbol index or string offset. Newly exposed unresolved references are fed
  back into the pending-symbol processing. This supports iterative,
  demand-driven library selection; the exact order across multiple libraries
  and the final treatment of replacement definitions remain unverified.

The general linker chapter says the first pass processes explicit ROFs in
command-line order, rejects duplicate global definitions, gathers unresolved
references, and then searches libraries in order until references are
resolved. A symbol psect contributes only definitions that resolve currently
unresolved references. The second pass lays out psects and patches references,
then writes code, initialized data, and a CRC. This is useful architectural
guidance. It does not by itself establish type-1 library member-selection or
every x86-specific relocation case.

### Additional static trace through the shipped `l386.exe`

The library reader's duplicate-definition path gives a concrete rule for the
examined executable. `FUN_004178e4` compares bit 8 of both existing and
incoming global-definition type words. If either definition is not common, it
enters the duplicate/conflict diagnostic path. If both are common, it copies
the existing definition's size into the incoming symbol record and rounds an
odd size up by one. This is observed in the static listing, not in an executed
link. Evidence: `ev_6a92c9334709f570e93d25223b6c2f3897cb590149e9af1d80e3b8f3bfff5203`.

The later layout pass `FUN_004181e8` walks accepted PSECTs, applies alignment
to initialized and uninitialized areas, calculates cumulative section bases,
and adjusts definition values by their section type. `FUN_00418110` applies
the section bases to definition records using the low three type bits. These
traces support the conclusion that common-symbol size is carried forward into
layout. The same layout routine also walks the linker's global-symbol list via
`FUN_00415e20`. For each common-flagged symbol, it saves the current ordinary
static-storage cursor as the symbol value, advances that cursor by the symbol
size, and clears bit 8 after placement. The cursor is aligned to four bytes
unless the target selector is 3 (PowerPC), where it aligns to eight. Since
`os9k_386` is selector 1, the inspected x86 path uses four-byte common
allocation alignment. This statically resolves the allocation location and
alignment in the examined linker; it does not validate the resulting module
with a running OS-9000 linker or loader. Evidence:
`ev_b43f5419480bf8cfcc6fe4b047bc9c6d93c738929590ba00018429531c9e6861` and
`ev_0a36c41f9e89e3ceb09b4b08426b37fba031faceb92896d10a3ed84897f19da5`,
`ev_2302eb29a64f2275e2eecf11d18d0038c21808b070787928c46a5b9f3282bb06`.

The `l386` option text says `-b=<n>` aligns code and data segments to 2, 4, 8,
or 16 bytes. The option parser stores the value in the low word of
`DAT_00432882`; its initialized bytes are `02 00 01 00`, consistent with a
default alignment value of 2 followed by target selector 1 (`os9k_386`).
Selector 3 (PowerPC) instead takes fixed 8-byte paths. For the ordinary x86
layout path, `FUN_004181e8` rounds each accepted PSECT's five in-memory area
sizes up to the selected alignment, then sums those padded sizes and assigns
separate cumulative bases. The affected size fields and corresponding assigned
bases are:

| In-memory size field | Area identified by its accumulation | Per-PSECT base field |
|---|---|---|
| `+0x34` | Code | `+0x84` |
| `+0x1c` | Ordinary uninitialized static data | `+0x44` |
| `+0x28` | Remote uninitialized data | `+0x4c` |
| `+0x20` | Initialized data | `+0x48` |
| `+0x2c` | Remote initialized data | `+0x50` |

The five type values used when adding a symbol's section base map to base
fields `+0x44`, `+0x48`, `+0x4c`, `+0x50`, and `+0x84` for values 0 through 4,
respectively, in the ordinary path. This supports modeling each area's
per-PSECT padding separately; it does not establish the final ordering or
loader interpretation of every emitted module region. The exceptional `-a`
path has additional code-reference and jump-table handling and must not be
folded into this ordinary mapping without its own model. Do not interpret the
default `-b=2` as proof that the first PSECT starts at absolute offset 2.

These are direct static observations of the examined executable's in-memory
layout routine, with area labels derived from which PSECT fields are accumulated
together. They are not runtime observations or a complete specification of
every PSECT kind. Evidence: option text
`ev_a8c0e7743b52057b67af3db9456ce0a24779e44cae929d0e2ef0292d6f066a78`,
option parser `ev_0ee9e37da5381ac792fba574325e9331ad499578896d2b701089bbeae66bb424`,
initialized bytes `ev_d70bdc17e56185f5e04cb8a20677d31279abfbfc0fa937d9ed2de78b17659f9b`,
and complete `FUN_004181e8` dossier
`ev_8fab50277acbcf25b36748435115caa38c9252e61d7e45f20cce80a253cff71b`.

The target selector can now be tied to x86: the `-t=os9k_386` option parser
sets selector value 1; `os9k_ppc` sets 3. This is directly visible in
`FUN_00414b14` and its referenced option strings. Evidence:
`ev_0767b40b548dbe59e13e6296a07473c8a06c3f3b02165c655eb413afc89c4ee2`.
The x86 output routine `FUN_0041afa4` walks PSECTs and calls the shared
`FUN_00415e80` processor. When its relocation-walk branch is selected, that
processor calls `FUN_00416944` for resolved symbol references and for
references without a resolved target pointer. This ties the fixup routine to
the x86 link path, although a particular flag combination still follows
mode-dependent branches.
Evidence: `ev_2dcfab56ffff9f65a633122b0e719c9feea0067005ef64516d40ca42ff538416`
and `ev_4b241d71a2c3ef31057e6466062df6c3888bb41373926f03c73937e4469cf258`.

With selector 1, `FUN_00416944` skips its selector-3-only block and executes
the ordinary fixup path. It reads a 16-bit location/type word and 32-bit site
offset, chooses the output area from code/data and remote bits, applies the
negative flag, handles 1-, 2-, and 4-byte fields, and checks signed or
unsigned ranges according to the relative flag. Out-of-range fields report
operand-size errors through `FUN_00417558`; an alignment check reports a
separate alignment error. Some calculations explicitly include selector 1.
The 9.2-shaped ROFs examined above use four-byte fields for all 736 local and
external references. For selector 1 and a four-byte field, the fixup adds the
already-computed symbol contribution to the 32-bit value at the site; the
earlier calculation applies the target section and negative/relative rules.
This matches the observed x86 four-byte reference profile. The short-field
branches contain explicit range diagnostics. A separate out-of-range 16-bit
path can create an extra table entry under additional mode and opcode
conditions. The option parser increments `DAT_004421b0` for `-a`; the special
path is guarded by that flag and by a resolved target record. It accepts three
preceding 16-bit instruction patterns (`0x6100`, masked `0x41fa`, or masked
`0x487a`). For target selectors 1 (x86) and 7 (ARM), it allocates a six-byte
record, links it into an internal list, patches the reference record and
instruction word, and tracks a per-reference count. When the count exceeds a
configured limit, it takes a separate diagnostic path (`FUN_00410224`); that
path reports “jmp total > guess in … referencing … in …”. A nonzero existing
field/addend takes the `FUN_00410088` path, whose diagnostic says “Can't make
jump table entry. An offset was supplied with label … in psect …”. This ties
that message to the nonzero-offset case. The code continues through
table-record handling after setting the error indicator, but the caller's
final output behavior was not observed. The listing does not establish that
these instruction patterns are valid x86 ROF jump-table sites, what the
generated table represents to the OS-9000 runtime, or whether normal x86
compiler output uses the option. Treat this as a linker extension candidate,
not a general x86 relocation rule. Evidence: `FUN_00410088` in
`ev_6af46bb4260336d399005335d179b8979277b1633683172bffa2cca706a9f433` and
`FUN_00410224` in `ev_5ceb3fd3657d47245de237a73e4f1f8fccb2fbd348a20cd7711adbc22df9ba65`.
The 1-byte branch also has an auxiliary overflow list for values below
`-128`. Each node stores the affected site offset and the previous list link;
the later lookup `FUN_004177d4` matches that offset, unlinks the node, and
returns a match so the stored byte is sign-extended before another relocation
contribution is applied. This is temporary linker bookkeeping observed in the
same routine, not an ROF output section. Evidence: `FUN_00416944` in
`ev_daf77db37354ec326751b357dc8382904304fa418ba23bc01d4309efc6e5099b` and
`FUN_004177d4` in `ev_45b40ff8ab32a5b1e37cf47717bc41a22c1f100f2b11456ce99a313c35985a32`.
The option parser `FUN_00414b14` is recorded in
`ev_14967213c1517758252b47a701a58a805d30e2596bbb22d1ea7abf32e18019e7`.
The size diagnostic formats signed/unsigned operand range errors, while the
separate alignment diagnostic reports the required byte alignment. Evidence:
`ev_b101ad48eabc22f1cdcca5cce4065794df6215e3f6e00ccf848d485654144706` and
`ev_f6d8fa73c026c7d663f7eaf2bc2cdc0f584089be6a408941c96b530befe71599`.
The higher-bit and output-mode branches remain incompletely mapped to Edition
9.2 ROF classes, so this is static x86-path evidence rather than a complete
executable linker specification. Evidence:
`ev_ff07c2e60c0f77352425274048f3696bca5c6fff34df7abf3ca0a190b8ad24fb`,
and `ev_850d307088f23ee49e52a949eb81e060b0f55b80fdf5323a56bd3a11d5d2c4d9`.

The short-field arithmetic has a useful static boundary map but no fixture
coverage. In the ordinary non-PowerPC path, the 1-byte branch bounds relative
results to `[-128, 127]` and non-relative results to `[-256, 255]`; the 2-byte
branch bounds relative results to `[-32768, 32767]` and non-relative results
to `[-65536, 65535]`. A two-byte relative overflow can enter the `-a`
jump-table case described above. For byte fields, values below `-128` are
tracked in the temporary overflow list so a later relocation at the same site
can recover the sign-extended value. The code can report an operand-size
diagnostic and still stores the low byte/word, while setting the linker's
error indicator; the caller's final error handling and runtime exit behavior
were not observed. Do not interpret
these bounds as complete assembler acceptance rules or as a validated module
encoding. Evidence: `FUN_00416944` and its diagnostic calls cited above.

The direct caller trace from input processing is `FUN_004110f0` →
`FUN_00417e64` → `FUN_004178e4`, confirming that the previously described
demand-driven library selection is the path used by the main input loop.
Evidence: `ev_932336ef87f772ea2b33d6a1d81c9668e8212a521faef4ead7f7cf620f068150`
and `ev_b57b98a686b046174e77b07f27d3ec042417892152f472b7350edabd4268edfe`.
The `CMDS/link` image file is an OS-9000 module rather than a host executable;
REA's native executable provider cannot open it directly. No Windows runtime
was available to execute the shipped `l386.exe`, so emitted bytes, runtime
diagnostics, and loader-visible results remain unverified.

## QCC implementation profile and current boundary

`qox86` writes a private prototype profile: 56-byte header, BE series marker
999, BE32 counts and sizes, plus three 32-bit terminal counts. This is not
asserted to be Microware ROF 9.2. `qlx86 --inspect` recognizes this private
profile and the observed image profile. It fails on unknown series values.

The current `qox86` source tracks defined labels as code or VSECT symbols and
serializes them as global records with type word `0x0004` (code) or `0x0001`
(initialized data), using section-relative values. These are facts about the
private QCC writer, not proof those type words or byte order match Microware's
x86 ROF. It writes zero external-reference, local-reference, and common-block
counts. Although its operand parser recognizes label operands, the instruction
encoders do not currently create relocation records; `dc.l` symbol operands
are emitted with value zero. The x86 IR backend emits `__offset_<name>` PIC
references, so the current IR-to-assembler path does not yet provide the
relocations a general linker needs. Until assembler symbol/fixup semantics are
defined and implemented, qlx86 must keep rejecting symbol-bearing candidate
output rather than infer full link semantics from these placeholder records.

`--inspect-library` recognizes and bounds-checks the type-1 header and PSECT
index records; `--library-symbols` also lists global definitions and checks
chain coverage. Normal link mode refuses to emit a file because a general,
verified ROF-to-module relocation path is not implemented. `--emit-prototype`
preserves the old non-loadable diagnostic container behind an explicit opt-in.
A separate `--emit-module-candidate` path serializes a restricted OS-9000
module for concatenated private QCC PSECTs only when they contain no global
symbols, relocation sites, remote sections, or debug data. It rounds code,
initialized-data, and uninitialized-static sizes to the selected alignment
and emits zero bytes for code/data padding. It emits empty terminated
relocation lists and is not loader-validated. This limited path does not
establish general Microware PSECT semantics. Image-profile linking, library
bucket hashing/member selection, and external relocations remain unsupported.

The 68k `ql68` implementation is useful as an architectural reference for
symbol ownership, multi-PSECT layout, libraries, relocation passes, and module
construction. Its measured big-endian 68k ROF encodings must not be copied as
x86 byte order or bias rules. Keep target-specific ROF fields and relocation
decoding separate.

## Next work

1. Find an edition-matched Windows execution environment or a Microware-made
   comparison output. Use it to verify common-symbol addresses, short-field
   diagnostics, the observed four-byte fixups, and the synthetic ROF candidate.
   The current host has no Wine/Windows runtime; the `os9` command is Toolshed
   2.4.2 file utilities, not an OS-9000 emulator. QEMU is installed, but no
   bootable OS-9000 x86 disk image was identified in the extracted reference
   tree; the shipped GUI `emulator.exe` requires Windows.
2. Add the Edition 15/series-12 profile only when a matching x86 fixture is
   available; `rdump` uses expression-tree and reference-table sections there,
   beyond the Edition 9.2 header handled now.
3. Map the remaining 1- and 2-byte fixup paths, higher flag bits, and `-a`
   table record to exact Edition 9.2 ROF forms and OS-9000 runtime semantics.
   Keep the current conclusions marked as static until compared with
   executable output.
4. Validate the restricted module candidate against an OS-9000 loader or
   Microware linker using a real, relocation-free fixture. In particular,
   confirm loader acceptance, the no-trap exception offset, and section
   alignment. Treat access and `m_dbias` values as observed module conventions,
   not universal rules; owner and stack values vary across the examined
   programs.
5. Extend candidate module output only after implementing and validating ROF
   symbol resolution, relocation records, general Microware PSECT layout, and
   an explicit owner policy for the ROF's missing ownership field. Keep normal
   link mode disabled until an emitted module loads and runs in a controlled
   image.
