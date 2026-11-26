# Q9-Parsec

[Deutsch](README_de.md)

Q9-Parsec is an EBNF parser and scanner generator. It reads a grammar and
produces parser source, including a C reference implementation and 68k
assembly output. The Q9-QCC tree includes this component alongside the
compiler, which uses generated parser source.

The following guides are available:

- [User guide](docs/BENUTZERHANDBUCH.md)
- [Developer guide](docs/ENTWICKLERHANDBUCH.md)
- [Architecture](docs/ARCHITEKTUR.md)
- [Documentation index](docs/README.md)

The previous project notes record the extraction of Q9-QCC into its own
repository on 2026-07-31.

Some guides describe earlier directory layouts or components. Check their
commands and paths against this checkout before running them. The surrounding
compiler project is [Q9-QCC](https://github.com/Q9-Forge/Q9-QCC).
