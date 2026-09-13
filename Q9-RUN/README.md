# Q9-RUN

*German version: [README_de.md](README_de.md)*

Q9-RUN is the C-based reference interpreter for Q9-QCC stack IR. It reads
textual `.ir` files produced by the compiler frontend and executes the
instruction stream on a stack machine.

## Features

- Parses textual Q9-QCC IR
- Executes the Q9-QCC IR instruction set
- Simulates local and global memory
- Supports function calls and control flow
- Provides a portable reference implementation for compiler tests

## Structure

```text
Q9-RUN/
├── Source/       C sources and public headers
├── tests/        interpreter tests
├── examples/     sample IR programs
├── docs/         documentation
├── tools/        helper scripts
└── build/        local build output
```

## Build and test

```sh
make
make test
```

## Status

Native Q9-QCC IR integration is working. The interpreter is also used as a
semantic reference for the compiler and backend test suites. Running the full
on-target chain through the 68k backend and Q9-Flux remains under active
development.

## Future work

The interpreter may later be packaged as an OS-9 module for the 68k target.
