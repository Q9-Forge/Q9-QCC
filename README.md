# Q9-Parsec

EBNF-Parser-/Scanner-Generator: liest eine EBNF-Grammatik und erzeugt daraus
Parserquelltext (C-Referenzimplementierung + 68k-Assembler).

Der darauf aufbauende Tiny-C-Compiler (Sprachkern, IR, 68000-/ARM64-Backends,
der seinen eigenen Parser mit diesem Tool aus `Data/tinyc.ebnf` erzeugt) lebt
seit 2026-07-31 als eigenständiges Repo:
[Q9-QCC](https://github.com/Q9-Forge/Q9-QCC) (volle Historie extrahiert).

Die Dokumentation ist nach Zweck getrennt:

- [Benutzerhandbuch](docs/BENUTZERHANDBUCH.md)
- [Entwicklerhandbuch](docs/ENTWICKLERHANDBUCH.md)
- [Ausführliche Architektur](docs/ARCHITEKTUR.md)

Tiny-C-spezifischer Status/Fortschritt/ISO-C-Lückenliste etc. steht jetzt in
[Q9-QCC](https://github.com/Q9-Forge/Q9-QCC).
