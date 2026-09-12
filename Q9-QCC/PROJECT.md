# Q9-QCC – Treiberprojekt

Der universelle Treiber heißt `qcc`. Er ist kein C-Compiler; `qcir` ist das
C-spezifische Frontend. `qcc` verbindet Frontends, Stack-IR, Interpreter,
Backends, Optimierer, Assembler und Linker.

Die ausführliche Planung liegt in
[DRIVER_PLAN.md](docs/DRIVER_PLAN.md).

Bibliotheks- und Systemheader werden über `-Iq9-qclib/DEFS/Q9` eingebunden.
Allgemeine Header liegen direkt unter `Q9/`, Architekturheader unter
`Q9/68k/` oder `Q9/x86/`; thematische Bereiche wie `Q9/devs/`, `Q9/os9/` und
`Q9/compiler/` sind ebenfalls zulässig.
