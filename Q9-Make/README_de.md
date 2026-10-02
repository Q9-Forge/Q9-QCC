# Q9-Make

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
  waehlt eine andere Projektdatei. Ein unabhaengiges `Makefile` kann
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
- Erstes Q9-Target: OS-9/68k.
- Weitere Targets können später eigene Plattformadapter und Toolchainprofile
  ergänzen.

## Erster Prototyp

Der erste C89-Hostprototyp liest einfache Regeln der Form `ziel: abhaengigkeiten`
und eingerueckte Shell-Rezepte aus `q9makefile`. Er baut Abhaengigkeiten zuerst,
entscheidet anhand von Zeitstempeln ueber inkrementelles Bauen, erkennt Zyklen
und unterstuetzt `-n`/`--dry-run`, `-v` und `-vv`. Erfolgreiche Rezepte mit
Ausgabedatei werden in einer Statuszeile zusammengefasst: Der Zielname erscheint
beim Start, nach Abschluss folgen Laufzeit und Haken. Bereits aktuelle Ziele
erhalten ohne Laufzeit einen gruenen Kreis. Die Tool-Ausgabe erscheint bei
Fehlern oder mit `-vv`. Regeln ohne Rezept dienen als
Sammel-/Phony-Regeln. Variablen werden als `NAME = wert` definiert und mit
`$(NAME)` in Rezepten expandiert. Environmentwerte haben Vorrang vor
Dateiwerten; `-DNAME=wert` hat die hoechste Prioritaet. `SUBDIRS = kind-a kind-b`
listet direkte Unterverzeichnisse explizit auf; diese werden rekursiv vor dem
lokalen Ziel gebaut. Ein explizit angegebenes Ziel wird an Kinder
weitergereicht, sonst waehlt jede Ebene ihre erste Regel. POSIX ist
implementiert; der Windows-CRT-Adapter muss noch unter Windows getestet werden.

Build und Smoke-Tests: `make test` in diesem Verzeichnis. Das Repository-
`Makefile` dient nur zum Bootstrap; Q9-Make selbst liest standardmaessig
`q9makefile`, alternativ z. B. mit `qmake -f build.mk all`.

Der Prototyp expandiert Variablen nur in Rezepten und liest Environmentwerte,
aendert aber nicht die Prozessumgebung. `SUBDIRS` akzeptiert nur direkte
Kindnamen ohne Pfadtrenner oder `..`; die Rekursion ist auf 16 Ebenen begrenzt.
Host- und Cross-Target-Werkzeuge sind getrennt. Ein fehlendes `.o`-Ziel wird
aus der gleichnamigen `.c`-Quelle mit `$(HOST_CC) $(HOST_CFLAGS) -c -o`
uebersetzt (Vorgaben `HOST_CC=cc`, `HOST_CFLAGS=-std=c89`). Ein fehlendes `.r`
nimmt zuerst eine gleichnamige `.c`-Quelle und `$(TARGET_CC)
$(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -c -o; sonst wird eine `.a`-Assemblerquelle mit
`$(TARGET_AS) $(TARGET_ASFLAGS)` assembliert. Target-Werkzeuge muessen
explizit konfiguriert werden; qmake rät nicht zwischen QCC (nativ auf Q9) und
XCC (Host-/Wine-Cross-Toolchain). Damit koennen native Host- und OS-9-Builds mit
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
`Q9SDK/Q9/68k/CMDS_QCC`; `Q9SDK/Q9/68k/CMDS_CC` ist fuer den nativen
Microware-Compiler reserviert. Der Q9-Emulatoradapter fehlt noch; sein vereinbarter
Konfigurationsordner ist `/dd/SYS`.

Fuer Intel-Macs gibt es zusaetzlich das Profil `[ACI]` und das
Ziel `qmake macX64`. Es erzeugt x86_64-Mach-O-Hostwerkzeuge unter
`Q9SDK/macOS/x86_64/CMDS` mit Mindestziel macOS 10.13. Die Binaries
wurden auf Apple Silicon unter Rosetta gestartet; ein Test auf echter
Intel-Hardware steht noch aus.

Je Host/Compiler/Target-Kombination wird in **einer** `qmake.conf` ein benannter
Abschnitt angelegt, z. B. `[ACA]`, `[ACI]`,
`[AXQ]` oder `[QQQ]`.
Gemeinsame Toolchainwerte stehen in `[global]`; jeder Konfigurationsabschnitt
enthaelt seine eigenen Compiler-/Target-Werte. Das Projekt-`q9makefile` hat
dieselben Abschnittsnamen fuer seine Regeln. Ist `Q9SDK` gesetzt, sucht qmake
auf macOS zuerst das Hostprofil unter
`$Q9SDK/macOS/ARM64/SYS/qmake.conf` oder
`$Q9SDK/macOS/x86_64/SYS/qmake.conf`, passend zur Architektur, fuer die qmake
gebaut wurde. Fehlt das Profil, gilt das lokale `TOOLCHAIN_FILE`, falls
vorhanden. Ohne SDK und ohne Profil funktionieren einfache Projekte mit den
eingebauten Vorgaben `cc` und `-std=c89`. Fuer Q9-Zielbuilds sucht qmake
zusaetzlich unter `$Q9SDK/Q9/<arch>/SYS` (sonst `$HOME/Q9SDK/...`); `-C DIR`
laedt explizit `DIR/qmake.conf`. Ohne `-P` baut qmake ein Ziel in
allen Abschnitten, die dieses Ziel definieren. So kann `all` mehrfach vorkommen,
waehrend etwa `all_q9` nur in Q9-Abschnitten steht. `-P <name>` waehlt einen
einzelnen Abschnitt, `--list-configs` zeigt die Namen. Die Variablen bleiben je
Aufruf getrennt; Ausgabepfade sollten daher pro Konfiguration eindeutig sein.
Environment und `-DNAME=value` haben weiterhin Vorrang vor Projekt-/Profilwerten.
Mit `HOST_PLATFORM` (macos, linux, windows oder q9) kann ein Profil auf das
System beschraenkt werden, auf dem sein Compiler laeuft. Bei normalem Aufruf
werden unpassende Abschnitte still uebersprungen; `-v` zeigt den Grund. Ein
explizites `-P` fuer ein unpassendes System ist ein Fehler.
Linkrezepte bleiben explizit. OS-9-Adapter und Image-Deployment fehlen noch.
Details in [`STATUS.md`](STATUS.md).

Beispiel: dieselben Labelnamen duerfen pro Konfiguration eigene Regeln haben;
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
und enthaelt die Abschnitte `[ACA]`, `[ACI]`, `[LGL]`, `[WCW]`
und `[QQQ]` sowie `[AXQ]`. Das XCC-Profil nutzt Wine auf dem
Mac. Die acht QCC-Werkzeuge `qcc`, `qcpp`, `qcir`, `qir68k`, `qo68k`, `qost`,
`qr68k` und `ql68k` besitzen jeweils ein eigenes `q9makefile` mit Mac-Clang-
und XCC-Abschnitt; `tools/build_xcc_sdk.sh <werkzeug>` baut einzelne
Komponenten. Die XCC-Ausgaben werden lokal nach `Q9SDK/Q9/68k/CMDS_XCC` kopiert.
Dieselbe Datei liegt auf dem Q9-System
unter `/dd/SYS/qmake.conf`; dort waehlt `qmake -C /dd/SYS -P QQQ` den
Q9-Abschnitt. Es gibt keine einzelne Profil-Datei pro Kombination.

Ohne Zielargument nimmt `qmake` in jedem Konfigurationsabschnitt dessen erste
lokale Regel. Ein erstes `help:`-Ziel kann also je Konfiguration eine Hilfe
ausgeben. Erzeugt das Rezept keine gleichnamige Datei, wird es bei jedem Aufruf
ausgefuehrt. Fuer abgeschlossene Buildschritte koennen Rezepte eine Markerdatei
(z. B. mit `touch .built`) erstellen; deren Zeitstempel wird bei Folgelaeufen
fuer die Aktualitaetspruefung verwendet.

## Namensschema fuer Konfigurationen

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
| `Q`  | Q9 / OS-9 68K                 |
| `L`  | Linux (Debian) x86_64         |
| `R`  | Linux (Raspberry OS) arm64    |
| `W`  | Windows 11 x86_64             |

### Compiler-Codes (2. Stelle)

| Code | Compiler                                         |
|------|---------------------------------------------------|
| `C`  | Clang                                            |
| `G`  | GNU GCC                                          |
| `Q`  | QCC (der eigene Compiler dieses Projekts)        |
| `X`  | Microware XCC (Cross, via Wine)                  |
| `M`  | Microware CC (nativ, laeuft direkt auf OS-9)     |

`X` und `M` sind beide Microware-Compiler: `X` fuer den Wine-gehosteten
Cross-Build, `M` fuer den nativen, der direkt auf Q9 laeuft. Der Buchstabe
`Q` wird in der System- und in der Compiler-Tabelle wiederverwendet; das ist
eindeutig, weil die Bedeutung jedes Buchstabens allein von seiner Position
im Dreibuchstaben-Code abhaengt, nicht vom Buchstaben selbst (dasselbe
Prinzip wie bei ISO-Laender- gegenueber Waehrungscodes).

### Build-Matrix

| Buildsystem-OS         | Arch      | Compiler      | Zielsystem-OS           | Arch        | Cross | Code  |
|-------------------------|-----------|---------------|---------------------------|-------------|:-----:|:-----:|
| macOS                  | arm64     | Clang         | macOS                   | arm64       |   –   | `ACA` |
| macOS                  | arm64     | Clang         | macOS                   | x86_64      |   X   | `ACI` |
| macOS                  | arm64     | QCC           | Q9/OS-9                 | 68k         |   X   | `AQQ` |
| macOS                  | arm64     | XCC/Wine      | Q9/OS-9                 | 68k         |   X   | `AXQ` |
| macOS                  | x86_64    | Clang         | macOS                   | x86_64      |   –   | `ICI` |
| macOS                  | x86_64    | QCC           | Q9/OS-9                 | 68k         |   X   | `IQQ` |
| macOS                  | x86_64    | XCC/Wine      | Q9/OS-9                 | 68k         |   X   | `IXQ` |
| Q9/OS-9                | 68k       | QCC           | Q9/OS-9                 | 68k         |   –   | `QQQ` |
| Q9/OS-9                | 68k       | Microware CC  | Q9/OS-9                 | 68k         |   –   | `QMQ` |
| Linux (Debian)         | x86_64    | GCC           | Linux (Debian)          | x86_64      |   –   | `LGL` |
| Linux (Debian)         | x86_64    | QCC           | Q9/OS-9                 | 68k         |   X   | `LQQ` |
| Linux (Debian)         | x86_64    | XCC/Wine      | Q9/OS-9                 | 68k         |   X   | `LXQ` |
| Linux (Raspberry OS)   | arm64     | GCC           | Linux (Raspberry OS)    | arm64       |   –   | `RGR` |
| Linux (Raspberry OS)   | arm64     | QCC           | Q9/OS-9                 | 68k         |   X   | `RQQ` |
| Linux (Raspberry OS)   | arm64     | XCC/Wine      | Q9/OS-9                 | 68k         |   X   | `RXQ` |
| Windows 11             | x86_64    | Clang         | Windows 11              | x86_64      |   –   | `WCW` |
| Windows 11             | x86_64    | XCC/Wine      | Q9/OS-9                 | 68k         |   X   | `WXQ` |
| Windows 11             | x86_64    | QCC           | Q9/OS-9                 | 68k         |   X   | `WQQ` |

Begruendung der Compiler-Wahl je Host: Clang deckt alle drei Nicht-Linux-Hosts
ab (macOS arm64, macOS x86_64, Windows) und teilt sich damit ein Frontend und
eine Diagnoseausgabe; GCC bleibt der Linux-Familie (Debian und Raspberry OS)
vorbehalten, wo es bereits vorinstalliert ist.

Stand 2026-10-02 verwenden `qmake.conf` und jedes `q9makefile` in diesem
Repository diese Dreibuchstaben-Codes; die urspruenglichen beschreibenden
Namen (`mac-clang`, `mac-clang-x86_64`, `mac-xcc-68k`, `mac-xqcc-68k`,
`q9-qcc-68k`, `q9-microware-cc-68k`, `linux-gcc`, `windows-clang`) kommen
nicht mehr vor. `ACA`, `AXQ`, `AQQ` und `QQQ` wurden auf diesem Rechner aus
einem sauberen Baum neu gebaut und Ende-zu-Ende verifiziert; `LGL`, `WCW`
und `QMQ` besitzen einen `qmake.conf`-Abschnitt, sind aber noch unverifiziert
(kein Linux-/Windows-Testrechner, `QMQ` braucht zusaetzlich einen
funktionierenden ISO-zu-K&R-Konverter). `ICI` (ein echter nativer Build auf
Intel-Mac-Hardware, im Unterschied zum Apple-Silicon-gehosteten Cross-Build
`ACI`) und die restlichen Matrixzeilen (`IQQ`, `IXQ`, `LQQ`, `LXQ`, `RGR`,
`RQQ`, `RXQ`, `WXQ`, `WQQ`) sind noch nicht als Konfigurationsabschnitt
angelegt.
