# Q9-QCC

<p align="center">
  <img width="300" height="300" alt="Q9-QCC" src="https://github.com/user-attachments/assets/0f469c58-eebb-429b-91bd-9fc9ef6df503" />
</p>

Q9-QCC is the modular compiler toolchain for Q9. It combines a C frontend,
an intermediate representation (IR), architecture-specific backends and an
IR interpreter.

German version: [README_de.md](README_de.md)

## Toolchain components

```text
Q9-QCC/
├── Q9-QCC/                 universal driver: qcc
├── Q9-PARSEC/              parser generator: qparsec
├── Q9-RUN/                 IR interpreter: qrun
├── Q9-FRONTEND-C/
│   ├── q9-qcpp/            C preprocessor: qcpp
│   └── q9-qcir/            C frontend and IR generator: qcir
├── Q9-OPTIMIZER/           backend-neutral Stack-IR optimizer: qost
├── Q9-BACKEND-68K/         68k compiler toolchain
├── Q9-BACKEND-x86/         x86 toolchain area
├── Q9-BACKEND-ARM64/       ARM64 toolchain area
└── Q9-BACKEND-riscv/       RISC-V toolchain area
```

The 68k path is currently the most complete. The x86 and ARM64 areas are
under development.

The initial Stack-IR optimizer is `Q9-OPTIMIZER/q9-qost/`. It is built and
tested independently; the `qcc` driver does not invoke it yet.

## Parser generation

The C frontend keeps its generated parser source in the repository so that
the compiler can be built without regenerating it. The grammar and lexer
table are maintained beside it:

```text
qcc.ebnf + qcc.lextab  --[qparsec]-->  qcc_p.c
```

When the grammar changes, regenerate `qcc_p.c` with the in-tree `qparsec`
tool and commit the resulting source together with the grammar change.

## Building

Each component documents its own build and test commands. Local build output
belongs in `build/` and should not be committed. The top-level driver and the
individual tools can be developed and tested independently.

## Status

**Updated 2026-09-28:** the `main` branch includes the `QCC-DEFMODUL` work.
The C frontend and 68k backend now carry variadic and calling-convention
attributes through the numeric `FUNC` IR field; the 68k interrupt path has a
regression test. The standalone `qost` Stack-IR optimizer builds and passes
its tests, but is not yet part of the `qcc` driver pipeline. Native OS-9
module-header and dispatch-table handling is still incomplete; x86, ARM64 and
RISC-V remain development targets.

See [STATUS.md](STATUS.md) for the current project snapshot,
[Q9 module status](Q9-FRONTEND-C/q9-qcpp/docs/STATUS_DEFMODUL.md) for the
module roadmap, and [detailed compiler history](docs/STATUS.md) for prior
bootstrap findings and verification records.

## License and contributions

See the repository files for licensing details. Issues and pull requests are
welcome, especially for portable C improvements, backend work and test cases.
