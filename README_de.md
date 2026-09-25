# Q9-QCC


<img width="300" height="300" alt="image" src="https://github.com/user-attachments/assets/0f469c58-eebb-429b-91bd-9fc9ef6df503" />


*English version: [README.md](README.md)*

Der gemeinsame projektübergreifende Kontext und die verbindlichen Namen
stehen in [Q9-OS-Research/AI_CONTEXT.md](../Q9-OS-Research/from-Q9-Boot/AI_CONTEXT.md).

QCC-Toolchain für Q9 (C-Sprachkern, IR, 68000-/ARM64-Backends).
Extrahiert aus dem ehemaligen `ebnf`-Repo (2026-07-31, volle Historie
erhalten), das jetzt als [Q9-Parsec](https://github.com/Q9-Forge/Q9-Parsec)
weiterlebt.

## Abhängigkeit zu Q9-Parsec

Der C-Frontend-Parser `Q9-FRONTEND-C/q9-qcir/data/qcc_p.c` wird vom
EBNF-Generator `qparsec` erzeugt.
Er ist hier **eingecheckt** (in Q9-Parsec ist dieselbe Datei nur ein
unversioniertes Bauartefakt), damit dieses Repo ohne Generator übersetzbar
bleibt:

```
Q9-FRONTEND-C/q9-qcir/data/qcc.ebnf + qcc.lextab  --[qparsec]-->  qcc_p.c
```

**Achtung, Falle:** ohne die `.lextab` entsteht ein Parser ohne Lexer und
ohne Aktionen — die `.ebnf` allein trägt das nicht. Mit beiden Dateien ist
die Erzeugung bitgleich reproduzierbar; nach jeder Grammatikänderung
`qcc_p.c` neu erzeugen und mitcommitten.

Die QCC-Sprachdefinition `Q9-FRONTEND-C/q9-qcir/data/qcc.ebnf`/
`qcc.lextab` wird **hier** gepflegt;
Q9-Parsec hält davon eine Kopie, weil seine Regressionssuite QCC mittestet
(siehe „Bekannte Lücke" unten). Q9-Parsec zum Bauen des Generators
zusätzlich auschecken:

```sh
git clone git@github.com:Q9-Forge/Q9-Parsec.git ../Q9-Parsec
(cd ../Q9-Parsec && clang++ -std=c++17 -o build/parsec Source/parsec.cpp Source/codegen.cpp)
../Q9-Parsec/build/parsec Data/qcc
```

## Die drei Schlussworte eines erzeugten Parsers

`OK` / `SEMERR` / `FAIL`, Rückgabewert `0` / `1` / `1`.

- `FAIL` — die Grammatik hat die Eingabe nicht erkannt. **Präfixstabil.**
- `SEMERR` — erkannt, aber semantisch beanstandet. In einem *Präfix* einer
  Datei ist das völlig regulär (Vorwärtsbezüge) und dort kein Befund.
- `OK` — beanstandungsfrei übersetzt.

Diese Unterscheidung ist wesentlich. `tools/bootstrap_survey.py` schiebt
*Präfixe* einer Datei durch den Parser und misst die Grammatikabdeckung;
beide Fehlerarten unter einem Wort zu melden machte diese Messung wertlos
(346 gemeldete Lücken statt 5). Das Schlusswort immer zeilenweise
vergleichen, nie als Teilzeichenkette: der Semantik-Marker hieß zuerst
`SEMFAIL` und *enthielt* damit `FAIL`, worauf jeder Aufrufer mit
Teilstring-Prüfung hereinfiel.

## Struktur

Das Repository ist nach Werkzeuggruppen gegliedert:

```text
Q9-QCC/
├── Q9-QCC/                 universeller Treiber: qcc
├── Q9-PARSEC/              Parsergenerator: qparsec
├── Q9-RUN/                 Stack-IR-Interpreter: qrun
├── Q9-FRONTEND-C/          qcpp und qcir
├── Q9-BACKEND-68K/         qir68k, qo68k, q9-qclib, q9-devs
├── Q9-BACKEND-x86/         x86-Werkzeugkette
└── Q9-BACKEND-ARM64/       ARM64-Werkzeugkette
```

Jedes Teilprojekt verwendet bei Bedarf `src/`, `include/`, `data/`, `tests/`,
`tools/`, `docs/` und `build/`. `build/` enthält nur lokale Bauartefakte.

- `Q9-BACKEND-68K/q9-qir68k/src/` — IR-zu-68k-Backendquellen
  IR-zu-ARM64-Codegenerierung
- `Q9-FRONTEND-C/q9-qcir/src/bootstrap/` — QCC-Bootstrapquellen
  (Selfhosting-Nachweis: beweist, dass dieser Compiler ein echtes,
  größeres Programm übersetzen kann)
- `runtime/arm64_darwin/` — Laufzeit-Unterstützung fürs ARM64-Testbackend
- `examples/qcc-project/` — Beispielprojekt
- `tools/qcc68sim.py`, `qccvm.py`, `qcc_merge.py`, `vasmm68k_mot` —
  Test-Orakel/Simulatoren + vendorter 68k-Assembler
- `tools/bootstrap_survey.py` — zweistufige Lückenerhebung gegen ein
  Bootstrap-Ziel (Stufe 1 Grammatik, Stufe 2 Semantik)
- `docs/` — Status, Fortschritt, IR-Opcodes, ISO-C-Lückenlisten,
  OS-9-Bootstrap, Selfhosting-Lückenliste, Teilprojekt-Roadmap

## Bekannte Lücke: geteilte Dateien mit Q9-Parsec (Stand 2026-08-11)

Die vollständige Regressionssuite (`runtests.sh`, ehemals im gemeinsamen
`ebnf`-Repo, testet EBNF-Generator und QCC in einem gemischten
3200-Zeilen-Skript) wurde **noch nicht sauber aufgetrennt** — sie bleibt
vorerst nur in Q9-Parsec. Weil sie QCC mittestet, existieren zwölf Dateien
in beiden Repos. Sie sind am 2026-08-11 zusammengeführt und **inhaltlich
deckungsgleich**; damit das so bleibt, gilt eine feste Zuständigkeit:

| Datei | gepflegt in |
|---|---|
| `Data/qcc.ebnf`, `Data/qcc.lextab` | **Q9-QCC** |
| `Q9-BACKEND-68K/q9-qir68k/src/` | **Q9-BACKEND-68K** |
| `Q9-FRONTEND-C/q9-qcir/src/bootstrap/` | **Q9-FRONTEND-C** (Bootstrap-Zwillinge) |
| `tools/qcc68sim.py`, `qccvm.py`, `qcc_merge.py`, `vasmm68k_mot` | **Q9-Parsec** (dort läuft die Suite) |
| `runtime/arm64_darwin/start.s`, `LICENSE` | beliebig, gleich halten |

Eine Änderung an einer dieser Dateien muss ins jeweils andere Repo
mitgespielt werden. `runtests.sh` in Q9-Parsec setzt das automatisch durch:
es vergleicht die vollständige Schnittmenge beider Repos (`git ls-files`)
gegen `../Q9-QCC` und schlägt bei jeder Abweichung fehl. Bewusste Ausnahme
sind `README.md`/`README_de.md` — jedes Repo hat seinen eigenen Text.

Prüfbefehl bei Nachbar-Auscheckung:

```sh
for f in $(git ls-files); do
  [ "$f" = "README.md" ] || [ "$f" = "README_de.md" ] && continue
  [ -f "../Q9-QCC/$f" ] && { cmp -s "$f" "../Q9-QCC/$f" || echo "DIVERGENT: $f"; }
done
```

**Warum das mühsam ist:** die Abhängigkeit ist gegenseitig — Q9-Parsec
erzeugt QCCs Parser, und QCC übersetzt Q9-Parsecs Selfhosting-Zwillinge.
Bis die Suite aufgeteilt (oder ein Submodul eingerichtet) ist, ersetzt die
Tabelle oben keine Automatik. Zur Vorgeschichte: bis zum 2026-08-11 lagen
diese Dateien **unbemerkt in fünf Fällen auseinander**, unter anderem mit
zwei konkurrierenden Behebungen desselben Big-Endian-Fehlers.
