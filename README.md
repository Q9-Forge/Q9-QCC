# Q9-QCC

*German version: [README_de.md](README_de.md)*

The shared cross-project context and the binding names live in
[Q9Forge/AI_CONTEXT.md](../Q9Forge/AI_CONTEXT.md).

QCC toolchain for Q9 (C language core, IR, 68000/ARM64 backends).
Extracted from the former `ebnf` repo (2026-07-31, full history
preserved), which now lives on as [Q9-Parsec](https://github.com/Q9-Forge/Q9-Parsec).

## Dependency on Q9-Parsec

The QCC parser `Data/qcc_p.c` is produced by the EBNF generator from
Q9-Parsec. It **is checked in here** (in Q9-Parsec the same file is only an
unversioned build artifact), so that this repo stays buildable without the
generator:

```
Data/qcc.ebnf + Data/qcc.lextab  --[parsec from Q9-Parsec]-->  Data/qcc_p.c
```

**Watch out:** without the `.lextab` you get a parser with no lexer and no
actions -- the `.ebnf` alone does not carry them. With both files the
generation is reproducible bit for bit; regenerate `Data/qcc_p.c` after every
grammar change and commit it along.

The QCC language definition `Data/qcc.ebnf`/`qcc.lextab` is maintained
**here**; Q9-Parsec keeps a copy of it because its regression suite tests QCC
as well (see "Known gap" below). Also check out Q9-Parsec to build the
generator:

```sh
git clone git@github.com:Q9-Forge/Q9-Parsec.git ../Q9-Parsec
(cd ../Q9-Parsec && clang++ -std=c++17 -o build/parsec Source/parsec.cpp Source/codegen.cpp)
../Q9-Parsec/build/parsec Data/qcc
```

## The three closing words of a generated parser

`OK` / `SEMERR` / `FAIL`, exit code `0` / `1` / `1`.

- `FAIL` -- the grammar did not accept the input. **Prefix-stable.**
- `SEMERR` -- accepted, but semantically rejected. Inside a *prefix* of a file
  this is entirely normal (forward references), so it is no finding there.
- `OK` -- translated without complaint.

Keeping these apart matters. `tools/bootstrap_survey.py` feeds *prefixes* of a
file through the parser and measures grammar coverage; reporting both failure
kinds under one word made that measurement worthless (346 reported gaps
instead of 5). Compare the closing word line-wise, never as a substring: the
semantic marker was first called `SEMFAIL` and thereby *contained* `FAIL`,
which fooled every caller doing a substring check.

## Structure

- `Source/qcc_backend*.cpp`, `qcc_arm64_backend*.cpp` -- IR-to-68k and
  IR-to-ARM64 code generation
- `SourceQCC/` -- the EBNF generator itself, ported to QCC
  (selfhosting proof: demonstrates that this compiler can translate a
  real, larger program)
- `runtime/arm64_darwin/` -- runtime support for the ARM64 test backend
- `examples/qcc-project/` -- example project
- `tools/qcc68sim.py`, `qccvm.py`, `qcc_merge.py`, `vasmm68k_mot` --
  test oracles/simulators + vendored 68k assembler
- `tools/bootstrap_survey.py` -- two-stage gap survey against a bootstrap
  target (stage 1 grammar, stage 2 semantics)
- `docs/` -- status, progress, IR opcodes, ISO C gap lists,
  OS-9 bootstrap, selfhosting gap list, subproject roadmap

## Known gap: files shared with Q9-Parsec (as of 2026-08-11)

The full regression suite (`runtests.sh`, formerly in the shared `ebnf` repo,
tests the EBNF generator and QCC together in a mixed 3200-line script) has
**not yet been cleanly split apart** -- for now it stays only in Q9-Parsec.
Because it tests QCC as well, twelve files exist in both repos. They were
merged on 2026-08-11 and are **identical in content**; to keep it that way, a
fixed ownership applies:

| File | maintained in |
|---|---|
| `Data/qcc.ebnf`, `Data/qcc.lextab` | **Q9-QCC** |
| `Source/qcc_backend_c.cpp`, `Source/qcc_arm64_backend_c.cpp` | **Q9-QCC** |
| `SourceQCC/ebnf.tc`, `SourceQCC/codegen.tc` | **Q9-Parsec** (generator twins) |
| `tools/qcc68sim.py`, `qccvm.py`, `qcc_merge.py`, `vasmm68k_mot` | **Q9-Parsec** (the suite runs there) |
| `runtime/arm64_darwin/start.s`, `LICENSE` | either, keep in step |

A change to one of these must be carried over into the other repo.
`runtests.sh` in Q9-Parsec enforces this automatically: it compares the
complete intersection of both repos (`git ls-files`) against `../Q9-QCC` and
fails on any divergence. `README.md`/`README_de.md` are the deliberate
exception -- each repo has its own text.

Manual check with a sibling checkout:

```sh
for f in $(git ls-files); do
  [ "$f" = "README.md" ] || [ "$f" = "README_de.md" ] && continue
  [ -f "../Q9-QCC/$f" ] && { cmp -s "$f" "../Q9-QCC/$f" || echo "DIVERGENT: $f"; }
done
```

**Why this is tedious:** the dependency is mutual -- Q9-Parsec generates QCC's
parser, and QCC compiles Q9-Parsec's selfhosting twins. Until the suite is
split (or a submodule is set up), the table above is no substitute for
automation. For the record: until 2026-08-11 these files had drifted apart
**unnoticed in five cases**, among them two competing fixes for the same
big-endian bug.
