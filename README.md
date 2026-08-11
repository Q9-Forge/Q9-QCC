# Q9-QCC

*German version: [README_de.md](README_de.md)*

The shared cross-project context and the binding names live in
[Q9Forge/AI_CONTEXT.md](../Q9Forge/AI_CONTEXT.md).

QCC toolchain for Q9 (C language core, IR, 68000/ARM64 backends).
Extracted from the former `ebnf` repo (2026-07-31, full history
preserved), which now lives on as [Q9-Parsec](https://github.com/Q9-Forge/Q9-Parsec).

## Dependency on Q9-Parsec

The QCC parser (`Data/qcc_p.c`, not checked in, generated) is produced
by the EBNF generator from Q9-Parsec:

```
Data/qcc.ebnf + Data/qcc.lextab  --[parsec from Q9-Parsec]-->  Data/qcc_p.c
```

`Data/qcc.ebnf`/`qcc.lextab` (the QCC language definition) therefore
live here as a copy -- also check out Q9-Parsec to build the `ebnf`
tool:

```sh
git clone git@github.com:Q9-Forge/Q9-Parsec.git ../Q9-Parsec
(cd ../Q9-Parsec && clang++ -std=c++17 -o build/parsec Source/parsec.cpp Source/codegen.cpp)
../Q9-Parsec/build/parsec Data/qcc
```

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
- `docs/` -- status, progress, IR opcodes, ISO C gap lists,
  OS-9 bootstrap, selfhosting gap list, subproject roadmap

## Known gap (as of 2026-07-31)

The full regression suite (`runtests.sh`, formerly in the shared
`ebnf` repo, tests the EBNF generator and QCC together in a
mixed 3200-line script) has **not yet been cleanly split apart** --
for now it stays only in Q9-Parsec. Building/testing standalone here
requires manually following the steps above, until Q9-QCC gets its own
`runtests.sh`.
