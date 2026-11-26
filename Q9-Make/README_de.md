# Q9-Make

[English](README.md)

Q9-Make ist das geplante kleine, portable Buildwerkzeug für Q9-Projekte. Das
Programm heißt `qmake`. Eine gemeinsame Quellbasis und ein einheitliches
`q9makefile`-Format sollen Entwicklungsrechner und Q9-Zielsysteme bedienen;
für jede Plattform wird eine eigene Binary gebaut.

## Festgelegte Richtung

- Der portable Kern verwendet das ISO-C89-Subset, das QCC nachweislich
  unterstützt. Host-Compiler müssen dieselbe Quellbasis übersetzen können.
- Plattformfunktionen werden zur Compilezeit über Plattformadapter gewählt;
  Betriebssystemabfragen werden nicht über den Buildkern verstreut.
- Host- und Target-Builds bleiben getrennt. Ein ausgewähltes
  Target-/Toolchain-Profil liefert CPU, ABI, Compiler-Generation, Werkzeugpfade
  und Ausgabeverzeichnis.
- Compiler, Assembler, Optimierer und Linker bleiben über Konfiguration sowie
  Kommandozeilen- und Environment-Overrides austauschbar.
- Standarddatei ist `q9makefile` ohne Endung; `-f DATEI` bzw. `--file DATEI`
  wählt eine andere Projektdatei. Ein unabhängiges `Makefile` kann
  parallel von einer anderen Make-Implementierung verwendet werden.
- Unterverzeichnisse werden nur dann traversiert, wenn sie ausdrücklich
  eingetragen sind. Erst werden Kindkomponenten gebaut, danach lokale
  Sammel-/Linkschritte.
- Vorgesehen sind `-v`, `-vv` und `-n`/`--dry-run`. Ein Dry-run darf keine
  Rezepte ausführen und kein Image verändern.

## Geplante implizite Regeln

Als erste targetabhängige Standardregeln sind `.c` nach relocatable Output
über den ausgewählten C-Treiber und – sofern `.a` Assembler-Quelltext bedeutet
– `.a` nach relocatable Output über den ausgewählten Assembler vorgesehen.
Das Linken von `.r`-Dateien zu einem Modul bleibt explizit, da Modulmetadaten
und Einsprungpunkte vom Target und Projekt abhängen. Explizite Regeln haben
immer Vorrang.

## Erste Plattformziele

- Native `qmake`-Binaries für macOS, Linux und Windows.
- Erstes Q9-Target: Q9-System/68k.
- Weitere Targets können später eigene Plattformadapter und Toolchainprofile
  ergänzen.

## Erster Prototyp

Der erste C89-Hostprototyp liest einfache Regeln der Form `ziel: abhaengigkeiten`
und eingerückte Shell-Rezepte aus `q9makefile`. Er baut Abhängigkeiten zuerst,
entscheidet anhand von Zeitstempeln über inkrementelles Bauen, erkennt Zyklen
und unterstützt `-n`/`--dry-run`, `-v` und `-vv`. Erfolgreiche Rezepte mit
Ausgabedatei werden in einer Statuszeile zusammengefasst: Der Zielname erscheint
beim Start, nach Abschluss folgen Laufzeit und Haken. Bereits aktuelle Ziele
erhalten ohne Laufzeit einen grünen Kreis. Die Tool-Ausgabe erscheint bei
Fehlern oder mit `-vv`. Regeln ohne Rezept dienen als
Sammel-/Phony-Regeln. Variablen werden als `NAME = wert` definiert und mit
`$(NAME)` in Rezepten expandiert. Environmentwerte haben Vorrang vor
Dateiwerten; `-DNAME=wert` hat die höchste Priorität. `SUBDIRS = kind-a kind-b`
listet direkte Unterverzeichnisse explizit auf; diese werden rekursiv vor dem
lokalen Ziel gebaut. `EXPORT = NAME ANDERER` gibt die genannten Variablen,
im Elternverzeichnis expandiert, an alle Verzeichnisse darunter weiter (etwa
ein `TARGET_OUTPUT_DIR` für alle Tools); dort haben sie Vorrang vor Datei- und
Profilwerten des Kindes, Environment und `-D` gehen weiterhin vor. Ein
explizit angegebenes Ziel wird an Kinder
weitergereicht, sonst wählt jede Ebene ihre erste Regel. POSIX ist
implementiert; der Windows-CRT-Adapter muss noch unter Windows getestet werden.

Build und Smoke-Tests: `make test` in diesem Verzeichnis. Das Repository-
`Makefile` dient nur zum Bootstrap; Q9-Make selbst liest standardmäßig
`q9makefile`, alternativ z. B. mit `qmake -f build.mk all`.

Der Prototyp expandiert Variablen in Rezepten, Zielnamen und ausgewählten
Konfigurationswerten und liest Environmentwerte, ändert aber nicht die
Prozessumgebung. `SUBDIRS` akzeptiert nur direkte
Kindnamen ohne Pfadtrenner oder `..`; die Rekursion ist auf 16 Ebenen begrenzt.
Host- und Cross-Target-Werkzeuge sind getrennt. Ein fehlendes `.o`-Ziel wird
aus der gleichnamigen `.c`-Quelle mit `$(HOST_CC) $(HOST_CFLAGS) -c -o`
übersetzt (Vorgaben `HOST_CC=cc`, `HOST_CFLAGS=-std=c89`). Ein fehlendes `.r`
nimmt zuerst eine gleichnamige `.c`-Quelle und `$(TARGET_CC)
$(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -c -o; sonst wird eine `.a`-Assemblerquelle mit
`$(TARGET_AS) $(TARGET_ASFLAGS)` assembliert. Target-Werkzeuge müssen
explizit konfiguriert werden; qmake rät nicht zwischen QCC (nativ auf Q9) und
XCC (Host-/Wine-Cross-Toolchain). Damit können native Host- und Q9-System-Builds mit
getrennten Endungen/Ausgaben im selben `q9makefile` stehen. Vorhandene
Ausgaben werden nur bei neuerer Quelle neu gebaut. Das Linken von `.r` zu
Modulen bleibt wegen der Metadaten explizit.

Abweichende Compiler-Syntax kann ein Profil mit `C_TO_R_COMMAND` und
`ASM_TO_R_COMMAND` abbilden. `@SOURCE@` und `@TARGET@` werden durch Quell- und
Zieldatei ersetzt; `$(VARIABLE)` wird anschließend wie in normalen Rezepten
aufgelöst. Ohne diese Vorlagen gelten die obigen Standardbefehle.

Das SDK trennt Entwicklungsplattform und Q9-Zielarchitektur: Host-Programme
liegen beispielsweise in `Q9SDK/macOS/ARM64/CMDS`, `Q9SDK/macOS/x86_64/CMDS`, `Q9SDK/Linux/CMDS` und
`Q9SDK/Windows/CMDS`. Die mit XCC gebauten Q9/68k-Module liegen unter
`Q9SDK/Q9/68k/CMDS_XCC`; QCC-Ausgaben verbleiben unter
`Q9SDK/Q9/68k/CMDS_QCC`; `Q9SDK/Q9/68k/CMDS_CC` ist für den nativen
externen Compiler reserviert. Auf Q9 selbst ist der Konfigurationsordner
`/dd/SYS`.

Für Intel-Macs gibt es zusätzlich das Profil `[ACI]` und das
Ziel `qmake macX64`. Es erzeugt x86_64-Mach-O-Hostwerkzeuge unter
`Q9SDK/macOS/x86_64/CMDS` mit Mindestziel macOS 10.13. Die Binaries
wurden auf Apple Silicon unter Rosetta gestartet; ein Test auf echter
Intel-Hardware steht noch aus.

Je Host/Compiler/Target-Kombination wird in **einer** `qmake.conf` ein benannter
Abschnitt angelegt, z. B. `[ACA]`, `[ACI]`,
`[AXQ]` oder `[QQQ]`.
Gemeinsame Toolchainwerte stehen in `[global]`; jeder Konfigurationsabschnitt
enthält seine eigenen Compiler-/Target-Werte. Das Projekt-`q9makefile` hat
dieselben Abschnittsnamen für seine Regeln. Die gemeinsame Datei des Q9-SDK
ist `$Q9SDK/Q9/SYS/qmake.conf`, eine Datei für alle Entwicklungssysteme. Ist
`Q9SDK` gesetzt, lädt qmake auf macOS, Linux und Windows zuerst diese Datei;
auf Q9 selbst lädt es `/dd/SYS/qmake.conf`, wohin der Image-Bau dieselbe Datei
legt. Fehlt sie, sucht qmake auf macOS das ältere Hostprofil unter
`$Q9SDK/macOS/ARM64/SYS/qmake.conf` oder
`$Q9SDK/macOS/x86_64/SYS/qmake.conf`, passend zur Architektur, für die qmake
gebaut wurde. Fehlt auch das, gilt das lokale `TOOLCHAIN_FILE`, falls
vorhanden. Ohne SDK und ohne Profil funktionieren einfache Projekte mit den
eingebauten Vorgaben `cc` und `-std=c89`. Für Q9-Zielbuilds sucht qmake
zusätzlich unter `$Q9SDK/Q9/<arch>/SYS` (sonst `$HOME/Q9SDK/...`); `-C DIR`
lädt explizit `DIR/qmake.conf`. Ohne `-P` baut qmake ein Ziel in
allen Abschnitten, die dieses Ziel definieren. So kann `all` mehrfach vorkommen,
während etwa `all_q9` nur in Q9-Abschnitten steht. `-P <name>` wählt einen
einzelnen Abschnitt, `--list-configs` zeigt die Namen. Die Variablen bleiben je
Aufruf getrennt; Ausgabepfade sollten daher pro Konfiguration eindeutig sein.
Environment und `-DNAME=value` haben weiterhin Vorrang vor Projekt-/Profilwerten.
Mit `HOST_PLATFORM` (macos, linux, windows oder q9) kann ein Profil auf das
System beschränkt werden, auf dem sein Compiler läuft. Bei normalem Aufruf
werden unpassende Abschnitte still übersprungen; `-v` zeigt den Grund. Ein
explizites `-P` für ein unpassendes System ist ein Fehler.
Linkrezepte bleiben explizit. Der native Q9-System-Adapter benötigt weitere
Laufzeitprüfungen; Image-Deployment fehlt noch.
Details in [`STATUS.md`](STATUS.md).

Beispiel: dieselben Labelnamen dürfen pro Konfiguration eigene Regeln haben;
die Ausgabepfade sind absichtlich verschieden:

```text
[global]
DEFS = /dd/DEFS/Q9

[ACA]
all:
	clang -c src.c -o build/ACA/src.o
all_mac:
	qmake -P ACA all

[QQQ]
all:
	qcc --no-optimizer -c -o build/QQQ/src.r src.c
all_q9:
	qmake -P QQQ all
```

`qmake all` baut beide `all`-Regeln; `qmake all_q9` nur die Q9-Regel.

Das gemeinsame Profil liegt unter [`toolchains/qmake.conf`](toolchains/qmake.conf)
und enthält die Abschnitte `[ACA]`, `[ACI]`, `[LGL]`, `[WCW]`
und `[QQQ]` sowie `[AXQ]`. Das XCC-Profil nutzt Wine auf dem
Mac. Die acht QCC-Werkzeuge `qcc`, `qcpp`, `qcir`, `qir68k`, `qo68k`, `qost`,
`qr68k` und `ql68k` besitzen jeweils ein eigenes `q9makefile` mit Mac-Clang-
und XCC-Abschnitt; `tools/build_xcc_sdk.sh <werkzeug>` baut einzelne
Komponenten. Die XCC-Ausgaben werden lokal nach `Q9SDK/Q9/68k/CMDS_XCC` kopiert.
Dieselbe Datei liegt auf dem Q9-System
unter `/dd/SYS/qmake.conf`; qmake liest sie dort ohne weitere Option, und
`qmake -P QQQ` wählt den Q9-Abschnitt. Es gibt keine einzelne Profil-Datei pro Kombination.

Ohne Zielargument nimmt `qmake` in jedem Konfigurationsabschnitt dessen erste
lokale Regel. Ein erstes `help:`-Ziel kann also je Konfiguration eine Hilfe
ausgeben. Erzeugt das Rezept keine gleichnamige Datei, wird es bei jedem Aufruf
ausgeführt. Für abgeschlossene Buildschritte können Rezepte eine Markerdatei
(z. B. mit `touch .built`) erstellen; deren Zeitstempel wird bei Folgeläufen
für die Aktualitätsprüfung verwendet.

## Namensschema für Konfigurationen

Jede Toolchain-Konfiguration (ein benannter Abschnitt in `qmake.conf` und in
jedem `q9makefile`) wird durch einen festen Dreibuchstaben-Code identifiziert:
`<Buildsystem><Compiler><Zielsystem>`. Der erste und der dritte Buchstabe
stammen aus derselben Systemtabelle; Buildsystem und Zielsystem unterscheiden
sich nur bei Cross-Konfigurationen.

### Systemcodes (1. und 3. Stelle)

| Code | System                        |
|------|-------------------------------|
| `A`  | macOS arm64 (Apple Silicon)   |
| `I`  | macOS x86_64 (Intel)          |
| `Q`  | Q9-System 68K                 |
| `P`  | Q9 x86 (PC)                   |
| `L`  | Linux (Debian) x86_64         |
| `R`  | Linux (Raspberry OS) arm64    |
| `W`  | Windows 11 x86_64             |

### Compiler-Codes (2. Stelle)

| Code | Compiler                                         |
|------|---------------------------------------------------|
| `C`  | Clang                                            |
| `G`  | GNU GCC                                          |
| `Q`  | QCC (der eigene Compiler dieses Projekts)        |
| `X`  | XCC (Cross, via Wine)                  |
| `M`  | externer CC (nativ, läuft direkt auf Q9-System)     |

`X` und `M` sind beide externe Compiler: `X` für den Wine-gehosteten
Cross-Build, `M` für den nativen, der direkt auf Q9 läuft. Der Buchstabe
`Q` wird in der System- und in der Compiler-Tabelle wiederverwendet; das ist
eindeutig, weil die Bedeutung jedes Buchstabens allein von seiner Position
im Dreibuchstaben-Code abhängt, nicht vom Buchstaben selbst (dasselbe
Prinzip wie bei ISO-Länder- gegenüber Währungscodes).

### Build-Matrix

| Buildsystem-OS         | Arch      | Compiler      | Zielsystem-OS           | Arch        | Cross | Code  |
|-------------------------|-----------|---------------|---------------------------|-------------|:-----:|:-----:|
| macOS                  | arm64     | Clang         | macOS                   | arm64       |   –   | `ACA` |
| macOS                  | arm64     | Clang         | macOS                   | x86_64      |   X   | `ACI` |
| macOS                  | arm64     | QCC           | Q9-System                 | 68k         |   X   | `AQQ` |
| macOS                  | arm64     | XCC/Wine      | Q9-System                 | 68k         |   X   | `AXQ` |
| macOS                  | x86_64    | Clang         | macOS                   | x86_64      |   –   | `ICI` |
| macOS                  | x86_64    | QCC           | Q9-System                 | 68k         |   X   | `IQQ` |
| macOS                  | x86_64    | XCC/Wine      | Q9-System                 | 68k         |   X   | `IXQ` |
| Q9-System                | 68k       | QCC           | Q9-System                 | 68k         |   –   | `QQQ` |
| Q9-System                | 68k       | externer CC  | Q9-System                 | 68k         |   –   | `QMQ` |
| Linux (Debian)         | x86_64    | GCC           | Linux (Debian)          | x86_64      |   –   | `LGL` |
| Linux (Debian)         | x86_64    | QCC           | Q9-System                 | 68k         |   X   | `LQQ` |
| Linux (Debian)         | x86_64    | XCC/Wine      | Q9-System                 | 68k         |   X   | `LXQ` |
| Linux (Raspberry OS)   | arm64     | GCC           | Linux (Raspberry OS)    | arm64       |   –   | `RGR` |
| Linux (Raspberry OS)   | arm64     | QCC           | Q9-System                 | 68k         |   X   | `RQQ` |
| Linux (Raspberry OS)   | arm64     | XCC/Wine      | Q9-System                 | 68k         |   X   | `RXQ` |
| Windows 11             | x86_64    | Clang         | Windows 11              | x86_64      |   –   | `WCW` |
| Windows 11             | x86_64    | XCC/Wine      | Q9-System                 | 68k         |   X   | `WXQ` |
| Windows 11             | x86_64    | QCC           | Q9-System                 | 68k         |   X   | `WQQ` |
| macOS                  | arm64     | QCC           | Q9 x86                  | x86         |   X   | `AQP` |
| macOS                  | x86_64    | QCC           | Q9 x86                  | x86         |   X   | `IQP` |
| Linux (Debian)         | x86_64    | QCC           | Q9 x86                  | x86         |   X   | `LQP` |
| Linux (Raspberry OS)   | arm64     | QCC           | Q9 x86                  | x86         |   X   | `RQP` |
| Windows 11             | x86_64    | QCC           | Q9 x86                  | x86         |   X   | `WQP` |
| Q9-System                | 68k       | QCC           | Q9 x86                  | x86         |   X   | `QQP` |
| Q9 x86                 | x86       | QCC           | Q9 x86                  | x86         |   –   | `PQP` |
| Q9 x86                 | x86       | QCC           | Q9-System                 | 68k         |   X   | `PQQ` |

Begründung der Compiler-Wahl je Host: Clang deckt alle drei Nicht-Linux-Hosts
ab (macOS arm64, macOS x86_64, Windows) und teilt sich damit ein Frontend und
eine Diagnoseausgabe; GCC bleibt der Linux-Familie (Debian und Raspberry OS)
vorbehalten, wo es bereits vorinstalliert ist.

Stand 2026-10-02 verwenden `qmake.conf` und jedes `q9makefile` in diesem
Repository diese Dreibuchstaben-Codes; die ursprünglichen beschreibenden
Namen (`mac-clang`, `mac-clang-x86_64`, `mac-xcc-68k`, `mac-xqcc-68k`,
`q9-qcc-68k`, `q9-microware-cc-68k`, `linux-gcc`, `windows-clang`) kommen
nicht mehr vor. `ACA`, `AXQ`, `AQQ` und `QQQ` wurden auf diesem Rechner aus
einem sauberen Baum neu gebaut und Ende-zu-Ende verifiziert; `LGL`, `WCW`
und `QMQ` besitzen einen `qmake.conf`-Abschnitt, sind aber noch unverifiziert
(kein Linux-/Windows-Testrechner, `QMQ` braucht zusätzlich einen
funktionierenden ISO-zu-K&R-Konverter). `ICI` (ein echter nativer Build auf
Intel-Mac-Hardware, im Unterschied zum Apple-Silicon-gehosteten Cross-Build
`ACI`) und die restlichen Matrixzeilen (`IQQ`, `IXQ`, `LQQ`, `LXQ`, `RGR`,
`RQQ`, `RXQ`, `WXQ`, `WQQ`) sind noch nicht als Konfigurationsabschnitt
angelegt. Dasselbe gilt für alle Konfigurationen mit dem System `P` (Q9
x86): Das x86-Backend von QCC ist noch in Arbeit, `AQP` … `PQQ` bekommen ihre
Abschnitte, sobald seine Optionen feststehen.
