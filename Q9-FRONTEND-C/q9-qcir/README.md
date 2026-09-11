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
