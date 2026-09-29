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

## Mac-SDK und Q9-Crossbuild

Der Mac-Treiber `qcc` orchestriert qcpp, qcir, qir68k, qo68k, qr68k und ql68k.
Die Host-Binaries liegen in `$Q9SDK/Mac/CMDS`; ausführbare 68k-Module werden
getrennt nach ihrer Buildkette unter `Q9/68k/CMDS_QCC` beziehungsweise
`Q9/68k/CMDS_XQCC` abgelegt. Header, `q9_cstart.r` und `qclib.l` liegen unter
`Q9/68k/DEFS` und `Q9/68k/LIBS`.

Im Repository-Root baut und staged qmake die Mac-Werkzeuge sowie die derzeit
QCC-selbsthostbaren Q9-Module:

```sh
Q9-Make/build/qmake -C Q9-Make/toolchains -P mac-clang \
  -DQ9SDK=/Volumes/SSD1TB/Q9SDK all
Q9-Make/build/qmake -C Q9-Make/toolchains -P mac-clang \
  -DQ9SDK=/Volumes/SSD1TB/Q9SDK all-qcc
Q9-Make/build/qmake -C Q9-Make/toolchains -P mac-clang \
  -DQ9SDK=/Volumes/SSD1TB/Q9SDK all-xqcc
```

`all` baut qmake, qcc, qcpp, qcir, qir68k, qo68k, qost, qr68k und ql68k als
macOS-Programme. `all-qcc` staged die Q9-Unterstützungsdateien und baut die
QCC-Zielmodule, für die die komplette Kette derzeit durchläuft. qcc findet bei
gesetztem `Q9SDK` sein Hostprofil automatisch; `Mac/CMDS` muss zusätzlich im
`PATH` stehen. Der Q9-Treiber sucht seine eigenen Stufen unter
`/dd/CMDS_QCC`. Große Programme können `--largedata --stack 512K` benötigen.

`all-qcc` baut qcpp, qcir, qir68k, qo68k, qost, qr68k und ql68k nach
`CMDS_QCC`. Es baut jetzt auch den OS-9-Treiber `qcc`: QCC erzeugt dessen
ROF, die kleine OS-9-Prozess-Bridge wird wegen ihrer Microware-ABI mit XCC
übersetzt und MWOS `l68` bindet das Ergebnis gegen die Microware-Laufzeit-
bibliotheken. Das ist ein gezielter Build-Workaround; `ql68` selbst kann
LIB-GEN-Archive weiterhin nicht lesen. `all-xqcc` baut dieselben Programme
mit Microware XCC/Wine nach `CMDS_XQCC`. qcpp aus beiden SDK-Verzeichnissen
wurde zuvor im Q9-Emulator ausgeführt; die Ausgaben waren byteidentisch zum
Mac-Lauf.

Die zuvor blockierten QCC-Selbstbuilds sind behoben: qr68s Namenspool ist auf
2 MiB vergrößert, qir68k vermeidet die nicht unterstützte verkettete
Strukturfeld-Indizierung und qo68k verwendet für seinen Zeilenend-Initializer
eine QCC-kompatible numerische Konstante. qcir meldet bei Syntaxfehlern jetzt
Zeile, Spalte und den nicht verarbeiteten Quelltext statt nur `FAIL`.

Der QCC-Build des Treibers benötigt `tools/build_qcc_selfhost.sh`. Sein
fertiges Modul wird mit `ident` auf CRC und Header-Parität geprüft. Es wurde
noch nicht im Q9-Emulator gestartet; bis zu diesem Lauf ist die
Laufzeitfunktion des selbstgebauten Treibers nicht bestätigt.

```sh
make
make test
```
