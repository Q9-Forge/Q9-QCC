# q9-qcir – C-Frontend

`qcir` übersetzt vorverarbeitetes C (`.i`) in Q9 Stack-IR (`.ir`).

```text
qcpp  *.c  -> *.i
qcir  *.i  -> *.ir
```

## Struktur

- `src/` – Bootstrap-Quellen des Frontends
- `data/` – Grammatik, Lexertabelle und generierter Parserstand
- `tests/` – Frontend- und Bootstrap-Tests
- `docs/` – frontendbezogene Dokumentation

## Bauen und testen

Der generierte Parser wird als eigenständiges Frontend-Binary `qcir` gebaut:

```sh
make
make test
```

`qcir` liest C- bzw. vorverarbeiteten C-Quelltext von stdin oder über eine
`@datei`-Angabe und schreibt Q9 Stack-IR (`.ir`) nach stdout. Der Name
`qcc_p.c` bleibt als historischer/generated Source-Dateiname erhalten; das
ausführbare Werkzeug heißt verbindlich `qcir`.
