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
  example `mac-clang`, `mac-xcc-68k`, `q9-qcc-68k`) with its own external
  profile and isolated makefile section.
- Use an extensionless `q9makefile` by default. A directory's unrelated
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
Q9SDK/Mac/CMDS/        # tools that run on macOS
Q9SDK/Linux/CMDS/      # tools that run on Linux
Q9SDK/Windows/CMDS/    # tools that run on Windows
Q9SDK/Q9/68k/SYS/      # configuration for the Q9/68k target
Q9SDK/Q9/68k/CMDS/     # binaries that run on Q9/68k
```

Host tools belong below the matching host directory; target profiles and target
binaries belong below `Q9/<architecture>`. All toolchain profiles live in one
sectioned `qmake.conf`: the `[global]` section holds shared defaults and a
section named `NAME` holds that compiler/target combination. qmake loads
`toolchains/qmake.conf` locally, or `$Q9SDK/Q9/<TARGET_ARCH>/SYS/qmake.conf`
(falling back to `$HOME/Q9SDK/...`) when the selected section sets
`TARGET_ARCH`. `-C DIR` loads `DIR/qmake.conf`; `TOOLCHAIN_FILE` is available
only when a project needs to point at a nonstandard profile file. The Q9
emulator adapter is not implemented yet; its agreed configuration directory is
`/dd/SYS`, without host-SDK discovery.

Use one named section per host/compiler/target combination. Shared variables
belong in `[global]`; each build configuration gets its own section and may
define the same goals independently. For example:

```text
[global]
DEFS = /dd/DEFS/Q9

[mac-clang]
HOST_CC = clang
all:
	clang -c src.c -o build/mac-clang/src.o
all_mac:
	qmake -P mac-clang all

[q9-qcc-68k]
TARGET_CC = qcc
TARGET_CFLAGS = --no-optimizer
all:
	qcc -c -o build/q9-qcc-68k/src.r src.c
all_q9:
	qmake -P q9-qcc-68k all
```

With no `-P`, qmake builds the requested target in every configuration section
that defines it; thus `qmake all` builds each section's `all`, while
`qmake all_q9` only builds sections that define `all_q9`. Use `-P NAME` to run
one configuration, or `--list-configs` to list available names. Each section
is loaded with fresh variables, so compiler settings cannot leak between
configurations. Give each section
distinct output paths when the same source is built by multiple toolchains.

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
[`toolchains/qmake.conf`](toolchains/qmake.conf) contains the initial native
sections `[mac-clang]`, `[linux-gcc]`, `[windows-clang]`, and `[q9-qcc-68k]`.
The XCC sections await verification of the host executable and invocation.
Install the shared file as `/dd/SYS/qmake.conf` and select the Q9 profile with
`qmake -C /dd/SYS -P q9-qcc-68k`.

## Initial platform scope

- Native qmake binaries: macOS, Linux, Windows.
- First Q9 target binary: OS-9/68k.
- Later targets may add their own platform adapter and toolchain profile.

## First prototype

The first C89 host prototype now parses simple `target: prerequisites` rules
and indented shell recipes from `q9makefile`. It builds prerequisites first,
uses modification times for incremental decisions, detects dependency cycles,
and supports `-n`/`--dry-run`, `-v`, and `-vv`. Recipe variables use
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
extensionless `q9makefile`.

This is deliberately an early prototype, not yet the agreed complete system:
variables expand only inside recipes; configurations are selected by named
sections and use fixed implicit format rules, not arbitrary format-transition
pipelines. There is no OS-9 adapter or image deployment. `SUBDIRS` entries must
be simple direct-child names, and recursion is limited to 16 levels. qmake
reads but does not modify the process environment. See
[`STATUS.md`](STATUS.md) for verified versus pending work.
