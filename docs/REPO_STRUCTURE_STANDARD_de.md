# Q9-QCC – Verzeichnisstandard

Dieser Standard gilt innerhalb des Repositories `Q9-QCC`. Andere Q9-
Repositories behalten ihre eigene, bereits veröffentlichte Struktur.

## Verbindliche Namen

| Verzeichnis | Inhalt |
|---|---|
| `src/` | produktive Quelltexte und eng zugehörige Quelltextvarianten |
| `include/` | öffentliche und projektweite Header |
| `data/` | Grammatiken, Tabellen und andere versionierte Eingabedaten |
| `tests/` | reproduzierbare Testfälle und Testdaten |
| `tools/` | Entwicklungs-, Bootstrap- und Testhilfsprogramme |
| `docs/` | technische Dokumentation und Statusberichte |
| `examples/` | veröffentlichte Beispielprojekte |
| `runtime/` | Laufzeitkomponenten, nach Zielarchitektur untergliedert |
| `build/` | ausschließlich lokale Buildausgaben; nicht versionieren |

## Regeln

- Verzeichnisnamen werden kleingeschrieben und verwenden keine Varianten wie
  `Source`, `Data`, `CSRC` oder `Test`.
- Es gibt im Repository genau ein zentrales Testverzeichnis: `tests/`.
- Unterprojekte erhalten innerhalb dieser Struktur einen eigenen, klar
  benannten Unterordner; sie führen keine abweichenden Oberbegriffe ein.
- Generierte Dateien bleiben nur dann versioniert, wenn sie für Bootstrap oder
  Reproduzierbarkeit ausdrücklich benötigt werden.
- Historische Pfade in Status- und Übergabedokumenten dürfen erhalten bleiben,
  müssen aber als historische Pfade erkennbar sein.

## Q9-Header und OS-9-`DEFS`

OS-9-kompatible Include-Aufrufe verwenden weiterhin `DEFS` als Include-
Verzeichnis. Die Q9-eigenen Header liegen darunter in einem eigenen `Q9`-
Verzeichnis:

```text
q9-qclib/
└── DEFS/
    └── Q9/
        ├── stdio.h
        ├── stdlib.h
        ├── string.h
        ├── 68k/
        │   └── regs.h
        └── x86/
            └── regs.h
```

Der Include-Pfad lautet:

```text
-Iq9-qclib/DEFS/Q9
```

Damit sind folgende Aufrufe möglich:

```c
#include <stdio.h>
#include <68k/regs.h>
#include <x86/regs.h>
```

Allgemeine Header liegen direkt unter `Q9/`; architekturabhängige Header
werden über `68k/` oder `x86/` ausgewählt. Spezielle Themenbereiche erhalten
eigene Unterverzeichnisse, beispielsweise `devs/`, `os9/` oder `compiler/`.
Das erlaubt Cross-Compilation von jedem unterstützten Host aus.

## Geplante Zuordnung im Sammelrepository Q9-QCC

| Heute | Ziel | Bemerkung |
|---|---|---|
| `Source/` | jeweiliges Teilprojekt unter `Q9-*/src/` | Frontends und Backends |
| `SourceQCC/` | `Q9-FRONTEND-C/q9-qcir/src/bootstrap/` | QCC-Bootstrapzwillinge |
| `Data/` | `data/` | Grammatik und generierter Frontend-Stand |
| `test/` + `tests/` | `tests/` | leeres historisches `test/` entfällt |
| `q9-cpp/` | `src/qcpp/` | Präprozessor als Q9-QCC-Unterprojekt |
| `Q9-qcc/` | `Q9-QCC/src/` | universeller Treiber; bisherige Planung wird überführt |
| `qo68/` | `Q9-BACKEND-68K/q9-qo68k/src/` | 68k-Peephole-Optimierer |
| `qcc_arm64_backend*.cpp` | `Q9-BACKEND-ARM64/q9-qirarm64/src/` | ARM64-Backend |

Die Zuordnung wird erst nach einer vollständigen Referenz- und Bootstrap-
Prüfung physisch umgesetzt. Bis dahin bleiben die aktuellen Pfade gültig.
