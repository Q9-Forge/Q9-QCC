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
`Q9SDK/Q9/68k/SYS` beziehungsweise `Q9SDK/Q9/68k/CMDS`. Ist `TARGET_ARCH`
gesetzt (im `q9makefile` oder Environment) und kein `TOOLCHAIN_FILE` angegeben,
sucht der Host-Prototyp
`$Q9SDK/Q9/<TARGET_ARCH>/SYS/qmake.conf`, sonst `$HOME/Q9SDK/...` (Windows:
`USERPROFILE`, dann `HOME`). `-C`/`--config-dir` ueberschreibt den Suchordner.
Der Q9-Emulatoradapter fehlt noch; dessen vereinbarter Standard ist `/dd/SYS`
ohne Suche im Host-SDK.

Ein Projekt kann mit `TOOLCHAIN_FILE = q9-68k.conf` ein externes Profil
auswaehlen; alternativ laedt `qmake -C /pfad/zum/SYS` dort `qmake.conf`.
Dieses verwendet dieselbe `NAME = value` Syntax und wird vor
dem Projekt-`q9makefile` geladen. Compiler, Praeprozessor-Flags, Assembler,
Linker, CPU, `DEFS`, `LIBS` und `STARTUP` koennen dort zentral stehen; Werte im
Projekt ueberschreiben Profilwerte. Environment und `-DNAME=value` haben
weiterhin Vorrang. Linkrezepte bleiben explizit und koennen diese Variablen
verwenden. Profile sind derzeit Variablen-Dateien, noch keine benannten
Abschnitte oder frei definierbaren Format-Uebergangsketten. OS-9-Adapter und
Image-Deployment fehlen noch. Details in [`STATUS.md`](STATUS.md).

Ohne Zielargument nimmt `qmake` die erste Regel. Ein erstes `help:`-Ziel kann
also eine Hilfe ausgeben. Erzeugt sein Rezept keine gleichnamige Datei, wird es
bei jedem Aufruf ausgefuehrt. Fuer abgeschlossene Buildschritte koennen Rezepte
eine Markerdatei (z. B. mit `touch .built`) erstellen; deren Zeitstempel wird
bei Folgelaeufen fuer die Aktualitaetspruefung verwendet.
