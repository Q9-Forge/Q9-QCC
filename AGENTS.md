# Q9-QCC – Arbeitskontext

Dieses Repository ist das Sammelrepository für die Q9-Compilerwerkzeuge.
Änderungen sollen die geplante Trennung nach Werkzeuggruppen und die
einheitliche interne Projektstruktur unterstützen.

## Verbindliche Projektgruppen

- `Q9-PARSEC` – Parsergenerator, Binary `qparsec`
- `Q9-QCC/` – universeller Compiler-Treiber, Binary `qcc`
- `Q9-RUN` – Stack-IR-Interpreter, Binary `qrun`
- `Q9-FRONTEND-C` – `qcpp` und `qcir`
- `Q9-BACKEND-68K` – `qir68k`, `qo68k`, `qr68k`, `ql68k`
- `Q9-BACKEND-x86` – `qirx86`, `qox86`, `qrx86`, `qlx86`
- `Q9-BACKEND-ARM64` – zukünftiges ARM64-Backend, zunächst `qirarm64`

Die Backendgruppen enthalten außerdem die jeweils passende
`q9-qclib`-/`q9-devs`-Variante. Gemeinsame Header werden nicht dupliziert,
sondern über `DEFS/Q9` und Architekturunterverzeichnisse eingebunden.

Die Verzeichnisnamen der Projekte tragen den `q9-`-Präfix. Der Präfix wird
nicht in den Binary-Namen wiederholt.

## Interne Standardstruktur

Jedes Teilprojekt verwendet, soweit benötigt:

```text
<projekt>/
├── src/
├── include/
├── data/
├── tests/
├── tools/
├── docs/
├── examples/
└── build/       nur lokal, nicht versionieren
```

Keine wechselnden Varianten wie `Source`, `Data`, `CSRC` oder `Test`.

## Pipeline

```text
.ebnf -> qparsec -> .c -> qcpp -> .i -> qcir -> .ir
.ir   -> qrun
.ir   -> qir68k -> .s68k -> qo68k -> qr68k -> .r -> ql68k
.ir   -> qirx86 -> .sx86 -> qox86 -> qrx86 -> .o -> qlx86
```

Der gemeinsame Zwischencode heißt Q9 Stack-IR und verwendet die Endung `.ir`.

## Wichtige Dokumente

- [Verzeichnisstandard](docs/REPO_STRUCTURE_STANDARD_de.md)
- [Umstrukturierungsplan](docs/RESTRUCTURE_PLAN_de.md)
- [C89-Bekannte-Probleme](docs/KNOWN_BUGS_C89_de.md)
- [QCC-Treiberplanung](Q9-qcc/PROJECT.md)

## Arbeitsregeln

- Vor größeren Verschiebungen den aktuellen Git-Status prüfen.
- Verschiebungen mit Git durchführen, damit die Historie nachvollziehbar bleibt.
- Keine Änderungen an Kernel-Repositories außerhalb dieses Repositories.
- Build-, Test- und Bootstrap-Pfade nach jeder strukturellen Verschiebung prüfen.
- Historische Dokumente nicht rückwirkend umschreiben; neue Pfade dort nur bei
  Bedarf als aktuelle Ergänzung kennzeichnen.
