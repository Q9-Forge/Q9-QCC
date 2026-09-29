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
- Standarddatei ist `q9makefile` ohne Endung. Ein unabhängiges `Makefile` kann
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
und unterstuetzt `-n`/`--dry-run`, `-v` und `-vv`. Regeln ohne Rezept dienen als
Sammel-/Phony-Regeln. Variablen werden als `NAME = wert` definiert und mit
`$(NAME)` in Rezepten expandiert. Environmentwerte haben Vorrang vor
Dateiwerten; `-DNAME=wert` hat die hoechste Prioritaet. `SUBDIRS = kind-a kind-b`
listet direkte Unterverzeichnisse explizit auf; diese werden rekursiv vor dem
lokalen Ziel gebaut. Ein explizit angegebenes Ziel wird an Kinder
weitergereicht, sonst waehlt jede Ebene ihre erste Regel. POSIX ist
implementiert; der Windows-CRT-Adapter muss noch unter Windows getestet werden.

Build und Smoke-Tests: `make test` in diesem Verzeichnis. Das Repository-
`Makefile` dient nur zum Bootstrap; Q9-Make selbst liest `q9makefile`.

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
liegen beispielsweise in `Q9SDK/Mac/CMDS`, `Q9SDK/Linux/CMDS` und
`Q9SDK/Windows/CMDS`. Q9-Zielprofile und Zielprogramme liegen unter
`Q9SDK/Q9/68k/SYS` beziehungsweise `Q9SDK/Q9/68k/CMDS`. Der Q9-Emulatoradapter
fehlt noch; sein vereinbarter Konfigurationsordner ist `/dd/SYS`.

Je Host/Compiler/Target-Kombination wird in **einer** `qmake.conf` ein benannter
Abschnitt angelegt, z. B. `[mac-clang]`, `[mac-xcc-68k]` oder `[q9-qcc-68k]`.
Gemeinsame Toolchainwerte stehen in `[global]`; jeder Konfigurationsabschnitt
enthaelt seine eigenen Compiler-/Target-Werte. Das Projekt-`q9makefile` hat
dieselben Abschnittsnamen fuer seine Regeln. Ist `TARGET_ARCH` gesetzt, sucht
qmake `qmake.conf` unter `$Q9SDK/Q9/<arch>/SYS` (sonst `$HOME/Q9SDK/...`), mit
`-C /dd/SYS` direkt unter `/dd/SYS/qmake.conf`; ohne `-P` baut qmake ein Ziel in
allen Abschnitten, die dieses Ziel definieren. So kann `all` mehrfach vorkommen,
waehrend etwa `all_q9` nur in Q9-Abschnitten steht. `-P <name>` waehlt einen
einzelnen Abschnitt, `--list-configs` zeigt die Namen. Die Variablen bleiben je
Aufruf getrennt; Ausgabepfade sollten daher pro Konfiguration eindeutig sein.
Environment und `-DNAME=value` haben weiterhin Vorrang vor Projekt-/Profilwerten.
Linkrezepte bleiben explizit. OS-9-Adapter und Image-Deployment fehlen noch.
Details in [`STATUS.md`](STATUS.md).

Beispiel: dieselben Labelnamen duerfen pro Konfiguration eigene Regeln haben;
die Ausgabepfade sind absichtlich verschieden:

```text
[global]
DEFS = /dd/DEFS/Q9

[mac-clang]
all:
	clang -c src.c -o build/mac-clang/src.o
all_mac:
	qmake -P mac-clang all

[q9-qcc-68k]
all:
	qcc --no-optimizer -c -o build/q9-qcc-68k/src.r src.c
all_q9:
	qmake -P q9-qcc-68k all
```

`qmake all` baut beide `all`-Regeln; `qmake all_q9` nur die Q9-Regel.

Das gemeinsame Profil liegt unter [`toolchains/qmake.conf`](toolchains/qmake.conf)
und enthaelt die Abschnitte `[mac-clang]`, `[linux-gcc]`, `[windows-clang]`
und `[q9-qcc-68k]`. Die XCC-Profile folgen, sobald Programmname und
Aufrufsyntax pro Host verifiziert sind. Dieselbe Datei liegt auf dem Q9-System
unter `/dd/SYS/qmake.conf`; dort waehlt `qmake -C /dd/SYS -P q9-qcc-68k` den
Q9-Abschnitt. Es gibt keine einzelne Profil-Datei pro Kombination.

Ohne Zielargument nimmt `qmake` in jedem Konfigurationsabschnitt dessen erste
lokale Regel. Ein erstes `help:`-Ziel kann also je Konfiguration eine Hilfe
ausgeben. Erzeugt das Rezept keine gleichnamige Datei, wird es bei jedem Aufruf
ausgefuehrt. Fuer abgeschlossene Buildschritte koennen Rezepte eine Markerdatei
(z. B. mit `touch .built`) erstellen; deren Zeitstempel wird bei Folgelaeufen
fuer die Aktualitaetspruefung verwendet.
