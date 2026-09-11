# Q9-QCC – universeller Q9-Treiber

Dieses Teilprojekt enthält den universellen Treiber `qcc` für die Q9-
Werkzeugkette. Der Treiber wählt abhängig von Eingabe, Ziel und Optionen das
passende Frontend sowie die passende Backend-Kette aus.

Das C-Frontend liegt separat unter `Q9-FRONTEND-C/`. Der gemeinsame
Zwischencode heißt Q9 Stack-IR und verwendet die Endung `.ir`.

## Struktur

```text
Q9-QCC/
├── src/
├── include/
├── tests/
├── tools/
├── docs/
└── config/qcc.conf
```

## Aktueller Stand

Der portable Kommandozeilenkern ist angelegt und unterstützt zunächst
`--help`, `--version`, Konfigurationsladen, `--dry-run` und die Zielauswahl. Die Konfiguration verwendet
Sektionen wie `[global]` und `[target.OS9-68K]`; weitere Target-Profile können
ergänzt werden. Die eigentliche Pipeline wird
schrittweise ergänzt; ein Aufruf mit Eingabedatei meldet bis dahin bewusst
„Pipeline noch nicht implementiert“, statt einen unvollständigen Build
vorzutäuschen.

```sh
make
make test
```
