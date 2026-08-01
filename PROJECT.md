# PROJECT.md - Q9-Parsec

## Übersicht

C++-Compiler-Baukasten rund um EBNF-Grammatiken: Parser/Scanner-Generator (`parsec`) plus ein
separates C#/.NET-Tool zur grafischen Darstellung von Syntaxdiagrammen (`ebnfVisualizer`).

Ursprünglich am 2020-04-28 begonnen (Visual Studio C++ Projekt), von
`C:\Users\foell\Desktop\2Q9\Compiler\` am 2026-07-19 hierher übernommen und als eigenes
Projekt aufgenommen.

## Struktur

```
Q9-Parsec/
├── ebnf.sln / ebnf.vcxproj(.filters)   # Visual Studio Projekt (C++), erzeugt parsec
├── runtests.sh                         # KOMPLETTE Regressionssuite (baut + testet alles)
├── context.txt                         # Arbeitsstand/Session-Log (zuerst lesen!)
├── docs/
│   └── ARCHITEKTUR.md                  # Architektur der Codegenerierung (68k zuerst)
├── Source/
│   ├── parsec.cpp                      # EBNF-Parser, Sprungtabelle, Stack-Maschine, Arbeitsdatei
│   ├── codegen.cpp/.h                  # AST + Codegenerierung: C-Zwilling + 68k-Assembler
│   ├── msvc_compat.h                   # *_s-Funktionen fuer macOS/Linux (unter Windows No-Op)
│   └── tiny-regex.cpp/.h               # Eingebettete Regex-Engine (derzeit ungenutzt)
├── tools/
│   └── s68sim.py                       # Mini-Simulator: fuehrt erzeugte .s68-Parser wirklich aus
├── Data/                               # Beispiel-Grammatiken + Arbeitsdateien + erzeugte Parser
│   ├── ebnf/java/modula2/oberon07.ebnf (+ .lexlst/.lextab)
│   └── oberon0.ebnf (+ .lextab mit [LEXER]-Block, oberon0_p.c, oberon0.s68)
├── Test/                               # Testgrammatiken (TESTS-Bloecke in den .lextab-Dateien)
└── ebnfVisualizer/
    └── Ebnf-Visualizer/                # C#/.NET WinForms Tool (fertig gebaut, .exe vorhanden)
```

## Hinweise

- Build + Gesamttest auf dem Mac: `./runtests.sh` (muss "ALLE TESTS OK" melden).
  Windows: `.sln`/MSBuild (msvc_compat.h ist dort No-Op).
- `<basis>.lextab` ist eine strukturierte ARBEITSDATEI (Bloecke EBNF-QUELLTEXT,
  TS-/NTS-SYMBOLTABELLE, PARSER-TABELLE, LEXER, TESTS, NUTZER-CODE); die Bloecke
  LEXER/TESTS/NUTZER-CODE sind nutzer-editierbar und bleiben beim Neu-Erzeugen erhalten.
- Codegenerierung: aus fehlerfreien Grammatiken entstehen `<basis>_p.c` (C-Referenz)
  und `<basis>.s68` (68k-Assembler) -- Details in docs/ARCHITEKTUR.md.
- `ebnfVisualizer`: separates C#-Projekt (WinForms), Parser/Scanner per Coco/R aus `EBNF.ATG`
  generiert. Enthält bereits eine gebaute `EBNF-Visualizer.exe`.
- Build-Artefakte (`.vs/`, `x64/`, `build/`) sind nicht Teil des Repos-Imports.
- Für Header/Versionierung/Doku-Konventionen siehe `C:\projects\PROJECT.md` (Q9-Stil).

---

**Erstellt**: 2026-07-19
**Übernommen von**: `C:\Users\foell\Desktop\2Q9\Compiler\ebnf` und `...\ebnfVisualizer`
