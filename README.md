# Q9-QCC

Der gemeinsame projektübergreifende Kontext und die verbindlichen Namen
stehen in [Q9Forge/AI_CONTEXT.md](../Q9Forge/AI_CONTEXT.md).

QCC-Toolchain für Q9 (C-Sprachkern, IR, 68000-/ARM64-Backends).
Extrahiert aus dem ehemaligen `ebnf`-Repo (2026-07-31, volle Historie
erhalten), das jetzt als [Q9-Parsec](https://github.com/Q9-Forge/Q9-Parsec)
weiterlebt.

## Abhängigkeit zu Q9-Parsec

Der QCC-Parser `Data/qcc_p.c` wird vom EBNF-Generator aus Q9-Parsec erzeugt.
Er ist hier **eingecheckt** (in Q9-Parsec ist dieselbe Datei nur ein
unversioniertes Bauartefakt), damit dieses Repo ohne Generator übersetzbar
bleibt:

```
Data/qcc.ebnf + Data/qcc.lextab  --[parsec aus Q9-Parsec]-->  Data/qcc_p.c
```

**Achtung, Falle:** ohne die `.lextab` entsteht ein Parser ohne Lexer und
ohne Aktionen — die `.ebnf` allein trägt das nicht. Mit beiden Dateien ist
die Erzeugung bitgleich reproduzierbar; nach jeder Grammatikänderung
`Data/qcc_p.c` neu erzeugen und mitcommitten.

Die QCC-Sprachdefinition `Data/qcc.ebnf`/`qcc.lextab` wird **hier** gepflegt;
Q9-Parsec hält davon eine Kopie, weil seine Regressionssuite QCC mittestet
(siehe „Bekannte Lücke" unten). Q9-Parsec zum Bauen des Generators
zusätzlich auschecken:

```sh
git clone git@github.com:Q9-Forge/Q9-Parsec.git ../Q9-Parsec
(cd ../Q9-Parsec && clang++ -std=c++17 -o build/parsec Source/parsec.cpp Source/codegen.cpp)
../Q9-Parsec/build/parsec Data/qcc
```

## Struktur

- `Source/qcc_backend*.cpp`, `qcc_arm64_backend*.cpp` — IR-zu-68k- bzw.
  IR-zu-ARM64-Codegenerierung
- `SourceQCC/` — der EBNF-Generator selbst, nach QCC portiert
  (Selfhosting-Nachweis: beweist, dass dieser Compiler ein echtes,
  größeres Programm übersetzen kann)
- `runtime/arm64_darwin/` — Laufzeit-Unterstützung fürs ARM64-Testbackend
- `examples/qcc-project/` — Beispielprojekt
- `tools/qcc68sim.py`, `qccvm.py`, `qcc_merge.py`, `vasmm68k_mot` —
  Test-Orakel/Simulatoren + vendorter 68k-Assembler
- `docs/` — Status, Fortschritt, IR-Opcodes, ISO-C-Lückenlisten,
  OS-9-Bootstrap, Selfhosting-Lückenliste, Teilprojekt-Roadmap

## Bekannte Lücke: geteilte Dateien mit Q9-Parsec (Stand 2026-08-11)

Die vollständige Regressionssuite (`runtests.sh`, ehemals im gemeinsamen
`ebnf`-Repo, testet EBNF-Generator und QCC in einem gemischten
3200-Zeilen-Skript) wurde **noch nicht sauber aufgetrennt** — sie bleibt
vorerst nur in Q9-Parsec. Weil sie QCC mittestet, existieren sieben Dateien
in beiden Repos. Sie sind am 2026-08-11 zusammengeführt und **inhaltlich
deckungsgleich**; damit das so bleibt, gilt eine feste Zuständigkeit:

| Datei | gepflegt in |
|---|---|
| `Data/qcc.ebnf`, `Data/qcc.lextab` | **Q9-QCC** |
| `Source/qcc_backend_c.cpp`, `Source/qcc_arm64_backend_c.cpp` | **Q9-QCC** |
| `SourceQCC/ebnf.tc`, `SourceQCC/codegen.tc` | **Q9-Parsec** (Generator-Zwillinge) |
| `tools/qcc68sim.py` | **Q9-Parsec** (dort läuft die Suite) |

Eine Änderung an einer dieser Dateien muss ins jeweils andere Repo
mitgespielt werden. Prüfbefehl bei Nachbar-Auscheckung:

```sh
for f in Data/qcc.ebnf Data/qcc.lextab Source/qcc_backend_c.cpp \
         Source/qcc_arm64_backend_c.cpp SourceQCC/ebnf.tc \
         SourceQCC/codegen.tc tools/qcc68sim.py; do
  cmp -s "$f" "../Q9-Parsec/$f" || echo "DIVERGENT: $f"
done
```

**Warum das mühsam ist:** die Abhängigkeit ist gegenseitig — Q9-Parsec
erzeugt QCCs Parser, und QCC übersetzt Q9-Parsecs Selfhosting-Zwillinge.
Bis die Suite aufgeteilt (oder ein Submodul eingerichtet) ist, ersetzt die
Tabelle oben keine Automatik. Zur Vorgeschichte: bis zum 2026-08-11 lagen
diese Dateien **unbemerkt in fünf Fällen auseinander**, unter anderem mit
zwei konkurrierenden Behebungen desselben Big-Endian-Fehlers.
