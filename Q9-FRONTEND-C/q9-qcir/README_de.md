# q9-qcir – C-Frontend

[English](README.md)

`qcir` übersetzt C-Quelltext, einschließlich vorverarbeitetem C, in Q9
Stack-IR (`.ir`). Eine typische Vorverarbeitungskette ist:

```text
qcpp  *.c  -> *.i
qcir  *.i  -> *.ir
```

## Struktur

- `src/` – Bootstrap-Quellen des Frontends
- `data/` – Grammatik, Lexertabelle und generierter Parser
- `tools/` – Hilfswerkzeuge für das Frontend
- `Makefile` – Host-Build und Smoke-Tests

## Bauen und testen

Der generierte Parser wird als eigenständiges Programm `qcir` gebaut:

```sh
make
make test
```

Der Quelltext wird als erstes Kommandozeilenargument übergeben; mit `@datei`
liest das Programm ihn aus einer Datei. Die Stack-IR wird auf die
Standardausgabe geschrieben. Die generierte Quelldatei behält den historischen
Namen `qcc_p.c`; das ausführbare Werkzeug heißt `qcir`.

Der generierte Parser besteht derzeit einen Clang-Build mit
`-Wall -Wextra -Werror` wegen Warnungen im generierten Code und in eingebetteten Aktionen
nicht. Generator und Grammatik müssen korrigiert und der Parser neu erzeugt
werden, bevor dieser strenge Build standardmäßig aktiviert wird.
