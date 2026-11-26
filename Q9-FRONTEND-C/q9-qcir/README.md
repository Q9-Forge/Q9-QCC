# q9-qcir – C frontend

[Deutsch](README_de.md)

`qcir` translates C source, including preprocessed C, into Q9 Stack-IR (`.ir`).
A typical preprocessing pipeline is:

```text
qcpp  *.c  -> *.i
qcir  *.i  -> *.ir
```

## Structure

- `src/` – frontend bootstrap sources
- `data/` – grammar, lexer table and generated parser
- `tools/` – frontend support tools
- `Makefile` – host build and smoke tests

## Build and test

The generated parser builds as the standalone `qcir` program:

```sh
make
make test
```

Pass source text as the first command-line argument, or use `@file` to read
it from a file. The program writes Stack-IR to standard output. The generated
source retains the historical filename `qcc_p.c`; the executable is `qcir`.

The generated parser currently fails a Clang build with
`-Wall -Wextra -Werror` because of warnings in generated code and embedded actions. Correct
the generator and grammar sources and regenerate the parser before enabling
that strict build by default.
