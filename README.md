# Q9-QCC

Tiny-C-Compiler-Toolchain für Q9 (C-Sprachkern, IR, 68000-/ARM64-Backends).
Extrahiert aus dem ehemaligen `ebnf`-Repo (2026-07-31, volle Historie
erhalten), das jetzt als [Q9-Parsec](https://github.com/Q9-Forge/Q9-Parsec)
weiterlebt.

## Abhängigkeit zu Q9-Parsec

Der Tiny-C-Parser (`Data/tinyc_p.c`, nicht eingecheckt, generiert) wird vom
EBNF-Generator aus Q9-Parsec erzeugt:

```
Data/tinyc.ebnf + Data/tinyc.lextab  --[ebnf-Tool aus Q9-Parsec]-->  Data/tinyc_p.c
```

`Data/tinyc.ebnf`/`tinyc.lextab` (die Tiny-C-Sprachdefinition) liegen deshalb
hier als Kopie — Q9-Parsec zum Bauen des `ebnf`-Tools zusätzlich auschecken:

```sh
git clone git@github.com:Q9-Forge/Q9-Parsec.git ../Q9-Parsec
(cd ../Q9-Parsec && clang++ -std=c++17 -o build/ebnf Source/ebnf.cpp Source/codegen.cpp)
../Q9-Parsec/build/ebnf Data/tinyc
```

## Struktur

- `Source/tinyc_backend*.cpp`, `tinyc_arm64_backend*.cpp` — IR-zu-68k- bzw.
  IR-zu-ARM64-Codegenerierung
- `SourceTinyC/` — der EBNF-Generator selbst, nach Tiny-C portiert
  (Selfhosting-Nachweis: beweist, dass dieser Compiler ein echtes,
  größeres Programm übersetzen kann)
- `runtime/arm64_darwin/` — Laufzeit-Unterstützung fürs ARM64-Testbackend
- `examples/tinyc-project/` — Beispielprojekt
- `tools/tiny68sim.py`, `tinyvm.py`, `tinyc_merge.py`, `vasmm68k_mot` —
  Test-Orakel/Simulatoren + vendorter 68k-Assembler
- `docs/` — Status, Fortschritt, IR-Opcodes, ISO-C-Lückenlisten,
  OS-9-Bootstrap, Selfhosting-Lückenliste, Teilprojekt-Roadmap

## Bekannte Lücke (Stand 2026-07-31)

Die vollständige Regressionssuite (`runtests.sh`, ehemals im gemeinsamen
`ebnf`-Repo, testet EBNF-Generator und Tiny-C-Compiler in einem
gemischten 3200-Zeilen-Skript) wurde **noch nicht sauber aufgetrennt** —
bleibt vorerst nur in Q9-Parsec. Eigenständiges Bauen/Testen hier
erfordert manuell die obigen Schritte, bis ein eigenes `runtests.sh`
für Q9-QCC entsteht.
