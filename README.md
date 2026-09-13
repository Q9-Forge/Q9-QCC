# Q9-QCC

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
├── Q9-BACKEND-68K/         68k compiler toolchain
├── Q9-BACKEND-x86/         x86 toolchain area
└── Q9-BACKEND-ARM64/       ARM64 toolchain area
```

The 68k path is currently the most complete. The x86 and ARM64 areas are
under development.

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

Q9-QCC is an active development project. The 68k compiler route and the IR
interpreter are working development components; other targets remain
experimental.

## License and contributions

See the repository files for licensing details. Issues and pull requests are
welcome, especially for portable C improvements, backend work and test cases.
