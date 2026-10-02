# Q9-Make

Q9-Make is the planned small, portable build tool for Q9 projects. Its
executable is named `qmake`. One source base and one `q9makefile` format will
serve native development hosts and Q9 target systems; each platform receives
its own binary.

## Design decisions

- Implement the portable core in the ISO C89 subset currently accepted by
  QCC. Host compilers must build that same source without requiring a separate
  host-only implementation.
- Select platform support at compile time through platform adapter sources;
  do not scatter operating-system tests through the build engine.
- Keep host and target builds separate. A selected target/toolchain profile
  supplies CPU, ABI, compiler generation, tool paths, and output directory.
- Give each host/compiler/target combination a named configuration (for
  example `ACA`, `AXQ`, `QQQ`) with its own external
  profile and isolated makefile section.
- Use an extensionless `q9makefile` by default; `-f FILE` / `--file FILE`
  selects another project description. A directory's unrelated
  `Makefile` remains available to another Make implementation.
- Traverse only explicitly listed subdirectories. Parent builds process
  child components before their own aggregate/link steps.
- Provide `-v`, `-vv`, and `-n`/`--dry-run`; dry-run must not execute recipes
  or mutate an image.

## Initial implicit rules

Host and target tools are separate. A missing `.o` target is compiled
from its matching `.c` source by `$(HOST_CC) $(HOST_CFLAGS) -c -o target.o
source.c` (defaults `HOST_CC=cc`, `HOST_CFLAGS=-std=c89`). A missing `.r`
target first looks for matching `.c` and invokes `$(TARGET_CC)
$(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -c -o target.r source.c`; if no C source exists, matching
`.a` assembler source is passed to `$(TARGET_AS) $(TARGET_ASFLAGS) source.a
target.r`. Target tools must be configured explicitly; qmake does not guess
between QCC (native on Q9) and XCC (host/Wine cross-toolchain). Thus a host
build and an OS-9 cross-build can coexist in one q9makefile, using separate
outputs such as `.o` and `.r`. Existing outputs are rebuilt only if the source
is newer. Linking `.r` files into a module remains explicit because module
metadata and entry settings are target/project-specific. Explicit rules always
override implicit rules.

## Toolchain profiles

The SDK separates development-host tools from Q9 target files:

```text
Q9SDK/macOS/ARM64/CMDS/   # Apple Silicon host tools
Q9SDK/macOS/x86_64/CMDS/  # Intel macOS host tools (Rosetta-compatible)
Q9SDK/Linux/CMDS/      # tools that run on Linux
Q9SDK/Windows/CMDS/    # tools that run on Windows
Q9SDK/Q9/68k/CMDS_XCC/ # XCC-built Q9/68k modules
Q9SDK/Q9/68k/CMDS_QCC/ # QCC-built Q9/68k modules
Q9SDK/Q9/68k/CMDS_CC/  # native Microware compiler outputs
```

Host tools belong below the matching host directory; target profiles and target
binaries belong below `Q9/<architecture>`; `CMDS_CC` is reserved for modules
built with the native Microware compiler. All toolchain profiles live in one
sectioned `qmake.conf`: the `[global]` section holds shared defaults and a
section named `NAME` holds that compiler/target combination. With `Q9SDK` set,
macOS qmake first looks for the host profile at
`$Q9SDK/macOS/ARM64/SYS/qmake.conf` or `$Q9SDK/macOS/x86_64/SYS/qmake.conf`,
selected by the architecture qmake was built for. If it is absent, qmake uses
the project-local `TOOLCHAIN_FILE` when provided; simple projects need neither
an SDK nor a profile and use the built-in `cc`/`-std=c89` defaults. For Q9
target builds, qmake also searches `$Q9SDK/Q9/<TARGET_ARCH>/SYS/qmake.conf`
(falling back to `$HOME/Q9SDK/...`). `-C DIR` explicitly loads
`DIR/qmake.conf`. The Q9
emulator adapter is not implemented yet; its agreed configuration directory is
`/dd/SYS`, without host-SDK discovery.

Use one named section per host/compiler/target combination. Shared variables
belong in `[global]`; each build configuration gets its own section and may
define the same goals independently. For example:

```text
[global]
DEFS = /dd/DEFS/Q9

[ACA]
HOST_CC = clang
all:
	clang -c src.c -o build/ACA/src.o
all_mac:
	qmake -P ACA all

[QQQ]
TARGET_CC = qcc
TARGET_CFLAGS = --no-optimizer
all:
	qcc -c -o build/QQQ/src.r src.c
all_q9:
	qmake -P QQQ all
```

With no `-P`, qmake builds the requested target in every configuration section
that defines it; thus `qmake all` builds each section's `all`, while
`qmake all_q9` only builds sections that define `all_q9`. Use `-P NAME` to run
one configuration, or `--list-configs` to list available names. Each section
is loaded with fresh variables, so compiler settings cannot leak between
configurations. Give each section
distinct output paths when the same source is built by multiple toolchains.
Set `HOST_PLATFORM` in a toolchain section (`macos`, `linux`, `windows`, or
`q9`) to restrict where that configuration runs. In a normal invocation,
incompatible sections are silently skipped; `-v` explains each skip. An
explicit incompatible `-P NAME` is an error.

The profile uses the same simple `NAME = value` syntax and is loaded before the
makefile's global and selected-section values. It can define `TARGET_CC`, `TARGET_CPPFLAGS`, `TARGET_CFLAGS`,
`TARGET_OPT`/`TARGET_OPTFLAGS`, `TARGET_AS`/`TARGET_ASFLAGS`, `TARGET_LD`,
`TARGET_LDFLAGS`, `TARGET_ARCH`, `TARGET_CPU`, `TARGET_ABI`, `DEFS`, `LIBS`,
and `STARTUP`. These values are available to explicit
recipes; the linker is intentionally still an explicit project rule because
module metadata and startup/link conventions need to be stated by the project.

Toolchains whose command-line syntax differs can override the implicit recipes
with `C_TO_R_COMMAND` and `ASM_TO_R_COMMAND`. These command templates support
`@SOURCE@` and `@TARGET@` placeholders and normal `$(VARIABLE)` expansion. For
example, an XCC profile may define `C_TO_R_COMMAND` with its own `-o` and
preprocessor/compile flags; without a template qmake uses the portable default
shown above.
Section assignments in `q9makefile` override the matching profile section;
environment variables override both, and `-DNAME=value` has highest priority.
[`toolchains/example.conf`](toolchains/example.conf) lists the available
settings without assuming unverified compiler-specific flags.
[`toolchains/qmake.conf`](toolchains/qmake.conf) contains `[ACA]`,
`[ACI]`, `[AXQ]`, `[LGL]`, `[WCW]`,
and `[QQQ]`. Use `qmake macX64` to build the host tools as Intel
x86_64 Mach-O binaries into `Q9SDK/macOS/x86_64/CMDS`; this profile
sets a macOS 10.13 deployment target. The artifacts were built and launched
under Rosetta on Apple Silicon; testing on physical Intel hardware remains
open.
Each of the eight QCC command-line components has a local `q9makefile` with
Mac-Clang and Mac/Wine-XCC sections. XCC builds were verified and stage to
`Q9SDK/Q9/68k/CMDS_XCC`; see the German README for the component invocation.
Install the shared file as `/dd/SYS/qmake.conf` and select the Q9 profile with
`qmake -C /dd/SYS -P QQQ`.

## Initial platform scope

- Native qmake binaries: macOS, Linux, Windows.
- First Q9 target binary: OS-9/68k.
- Later targets may add their own platform adapter and toolchain profile.

## First prototype

The first C89 host prototype now parses simple `target: prerequisites` rules
and indented shell recipes from `q9makefile`. It builds prerequisites first,
uses modification times for incremental decisions, detects dependency cycles,
and supports `-n`/`--dry-run`, `-v`, and `-vv`. Successful output-producing
recipes are summarized by one status line: the target name appears as the
recipe starts, followed by elapsed time and a check mark on completion. Targets
already up to date show a green circle without a duration. Recipe output is
shown on failure or with `-vv`. Recipe variables use
`NAME = value` and `$(NAME)`; environment variables override file values, and
`-DNAME=value` overrides both. `SUBDIRS = child-a child-b` explicitly lists
direct child directories; each child is built recursively before the local
target. An explicitly requested target is forwarded to children; otherwise,
each directory selects its own first rule. A rule with no recipe acts as an
aggregate rule. With no command-line target, the first rule is selected; a
first `help:` rule can therefore print a target summary. A recipe with no
output file runs on every invocation because its target remains absent. To
cache a completed step, have the recipe create a marker (for example with
`touch .built`) and make that marker depend on the step's inputs. POSIX is
implemented; the Windows CRT adapter still needs a Windows build test.

Build and run the smoke tests with `make test` in this directory. The
repository `Makefile` is only the bootstrap makefile; Q9-Make itself reads the
extensionless `q9makefile` by default; `qmake -f build.mk target` selects a
different file.

This is deliberately an early prototype, not yet the agreed complete system:
variables expand only inside recipes; configurations are selected by named
sections and use fixed implicit format rules, not arbitrary format-transition
pipelines. There is no OS-9 adapter or image deployment. `SUBDIRS` entries must
be simple direct-child names, and recursion is limited to 16 levels. qmake
reads but does not modify the process environment. See
[`STATUS.md`](STATUS.md) for verified versus pending work.

## Configuration naming scheme

Every toolchain configuration (a named section in `qmake.conf` and in each
`q9makefile`) is identified by a fixed three-letter code:
`<host system><compiler><target system>`. The first and third letters are
drawn from the same system table; host and target only differ for
cross-compiling configurations.

### System codes (1st and 3rd letter)

| Code | System                        |
|------|-------------------------------|
| `A`  | macOS arm64 (Apple Silicon)   |
| `I`  | macOS x86_64 (Intel)          |
| `Q`  | Q9 / OS-9 68K                 |
| `L`  | Linux (Debian) x86_64         |
| `R`  | Linux (Raspberry OS) arm64    |
| `W`  | Windows 11 x86_64             |

### Compiler codes (2nd letter)

| Code | Compiler                                       |
|------|-------------------------------------------------|
| `C`  | Clang                                          |
| `G`  | GNU GCC                                        |
| `Q`  | QCC (this project's own compiler)              |
| `X`  | Microware XCC (cross, via Wine)                |
| `M`  | Microware CC (native, running on OS-9 itself)  |

`X` and `M` are both Microware compilers: `X` for the Wine-hosted cross
build, `M` for the native one running on Q9 itself. The letter `Q` is reused
between the system table and the compiler table; this is unambiguous because
each letter's meaning is fixed by its position in the three-letter code, not
by the letter alone (the same principle as ISO country versus currency
codes).

### Build matrix

| Host OS               | Host arch | Compiler      | Target OS              | Target arch | Cross | Code  |
|------------------------|-----------|---------------|-------------------------|-------------|:-----:|:-----:|
| macOS                  | arm64     | Clang         | macOS                   | arm64       |   –   | `ACA` |
| macOS                  | arm64     | Clang         | macOS                   | x86_64      |   X   | `ACI` |
| macOS                  | arm64     | QCC           | Q9/OS-9                 | 68k         |   X   | `AQQ` |
| macOS                  | arm64     | XCC/Wine      | Q9/OS-9                 | 68k         |   X   | `AXQ` |
| macOS                  | x86_64    | Clang         | macOS                   | x86_64      |   –   | `ICI` |
| macOS                  | x86_64    | QCC           | Q9/OS-9                 | 68k         |   X   | `IQQ` |
| macOS                  | x86_64    | XCC/Wine      | Q9/OS-9                 | 68k         |   X   | `IXQ` |
| Q9/OS-9                | 68k       | QCC           | Q9/OS-9                 | 68k         |   –   | `QQQ` |
| Q9/OS-9                | 68k       | Microware CC  | Q9/OS-9                 | 68k         |   –   | `QMQ` |
| Linux (Debian)         | x86_64    | GCC           | Linux (Debian)          | x86_64      |   –   | `LGL` |
| Linux (Debian)         | x86_64    | QCC           | Q9/OS-9                 | 68k         |   X   | `LQQ` |
| Linux (Debian)         | x86_64    | XCC/Wine      | Q9/OS-9                 | 68k         |   X   | `LXQ` |
| Linux (Raspberry OS)   | arm64     | GCC           | Linux (Raspberry OS)    | arm64       |   –   | `RGR` |
| Linux (Raspberry OS)   | arm64     | QCC           | Q9/OS-9                 | 68k         |   X   | `RQQ` |
| Linux (Raspberry OS)   | arm64     | XCC/Wine      | Q9/OS-9                 | 68k         |   X   | `RXQ` |
| Windows 11             | x86_64    | Clang         | Windows 11              | x86_64      |   –   | `WCW` |
| Windows 11             | x86_64    | XCC/Wine      | Q9/OS-9                 | 68k         |   X   | `WXQ` |
| Windows 11             | x86_64    | QCC           | Q9/OS-9                 | 68k         |   X   | `WQQ` |

Compiler choice per host: Clang covers all three non-Linux hosts (macOS
arm64, macOS x86_64, Windows), so they share one frontend and one set of
diagnostics; GCC stays reserved for the Linux family (Debian and Raspberry
OS), where it is the pre-installed default.

As of 2026-10-02 `qmake.conf` and every `q9makefile` in this repository use
these three-letter codes; the earlier descriptive names (`mac-clang`,
`mac-clang-x86_64`, `mac-xcc-68k`, `mac-xqcc-68k`, `q9-qcc-68k`,
`q9-microware-cc-68k`, `linux-gcc`, `windows-clang`) no longer appear.
`ACA`, `AXQ`, `AQQ` and `QQQ` have been rebuilt from a clean tree and verified
end to end on this machine; `LGL`, `WCW` and `QMQ` have a `qmake.conf` section
but are still unverified (no Linux/Windows test host, and `QMQ` additionally
needs a working ISO-to-K&R converter). `ICI` (a true native build on Intel
Mac hardware, as opposed to `ACI`'s Apple-Silicon-hosted cross build) and the
remaining matrix rows (`IQQ`, `IXQ`, `LQQ`, `LXQ`, `RGR`, `RQQ`, `RXQ`,
`WXQ`, `WQQ`) are not defined as configuration sections yet.
