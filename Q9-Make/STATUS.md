# Q9-Make Status

**Phase:** C89-Prototyp mit benannten Host/Compiler/Target-Konfigurationsabschnitten implementiert; Konfigurationen tragen seit 2026-10-02 feste Dreibuchstaben-Codes (siehe `README.md`/`README_de.md`, Abschnitt „Configuration naming scheme"). Host-Build und Selfhost-Cross-Build sind Ende-zu-Ende von Null verifiziert; qmake-Build für das Q9-Zielsystem selbst und Emulator-Gastlauf bleiben offen.

| Bereich | Status | Nächster Nachweis |
|---|---|---|
| C-Sprachbaseline | 🟡 | Host-Build mit strengem C89 erfolgreich; QCC-Probe/Binary fehlt in diesem Checkout |
| Makefile-Syntax | ✅ | `[global]` und benannte Host/Compiler/Target-Abschnitte, wiederholte Ziele, `-f`/`--file`, `-P`-Auswahl, SDK-/`-C`-Profile und `--list-configs` per Smoke-Test abgedeckt; Gastprüfung separat offen |
| Buildgraph | ✅ | Abhängigkeiten, Zyklen, Zeitstempel/`touch`-Marker, Kind-vor-Eltern-Rekursion und Erstziel-ohne-Argument getestet |
| Native Hosts | 🟡 | macOS-Profil wird per Compilezeit-Architektur aus `Q9SDK/macOS/{ARM64,x86_64}/SYS/qmake.conf` geladen; POSIX/Q9SDK-/HOME-Auflösung getestet; Windows-Adapter geschrieben, Windows-Test offen |
| OS-9/68k | 🔴 | Plattformadapter fehlt; `/dd/SYS`-Standard, Prozessstart und Dateisystemzugriff im Emulator umsetzen/verifizieren |
| Toolchainprofile | 🟡 | Zentrale Profile für macOS arm64/Host, macOS x86_64, Mac-XCC/68k, Linux-GCC, Windows-Clang und Q9-QCC/68k; x86_64-Hosttools gebaut und unter Rosetta gestartet (Intel-Hardwaretest offen); Windows-Lauftest und Q9-SYS-Profilladen im Emulator offen |
| Implizite Regeln | 🟡 | `.c`→`.o` via HOST_CC sowie `.c`/`.a`→`.r` via TARGET_CC/TARGET_AS; Standard und XCC-artige Befehlsvorlagen mit Fake-Tools getestet; qmake im Emulator offen |
| Diagnose | 🟡 | `-v`, `-vv`, `--dry-run`, `-C`/`--config-dir` implementiert; `--show-config`/Abfragen offen |
| Arbeitsimage-Deploy | 🔴 | Explizites, nicht-destruktives Installziel für benannte Arbeitsimageprofile entwerfen |
| Selbstbau-Abnahme | 🟡 | 2026-10-02: `ACA` (Host-Build, clang/ARM64) und `AQQ` (Cross-Build mit dem per `ACA` selbstgebauten QCC nach Q9/68K) jeweils von Null neu gebaut und verifiziert -- alle sieben QCC-Werkzeuge entstehen als gültige OS9/68K-Module; `AXQ` (Microware-XCC/Wine) ebenfalls durchgelaufen. Offen: qmake baut seine eigene OS-9/68k-Binary noch nicht selbst, und kein Schritt lief bisher im echten Q9-Emulator |

## Prototypumfang / bekannte Grenzen

- Feste Kapazitaeten: 32 Regeln, 12 Abhaengigkeiten und 32 Variablen; Namen
  maximal 127 Zeichen, einzelne Rezeptzeilen maximal 1023 Zeichen. Die Zahl
  der Rezeptzeilen ist dynamisch und durch den verfuegbaren Speicher begrenzt.
- Genau ein Zielargument; ohne `-P` wird es in allen Abschnitten ausgefuehrt,
  die es definieren. Ohne Ziel gilt die erste lokale Regel jedes Abschnitts.
- Maximal 16 Konfigurationsabschnitte pro `q9makefile`; `[global]`-Werte und
  Regeln gelten in jedem Abschnitt. Konfigurationswerte stehen im passenden
  Abschnitt von `toolchains/qmake.conf` oder einer Datei aus `-C`.
- Standardmaessig wird `q9makefile` aus dem aktuellen Arbeitsverzeichnis
  gelesen; `-f DATEI`/`--file DATEI` waehlt eine alternative Datei.
- Rezepte laufen ueber die Shell des Hostsystems; portable Rezeptsyntax ist
  damit noch nicht definiert.
- Variablen werden nur in Rezeptzeilen expandiert. Profil (falls ausgewaehlt)
  wird vor `q9makefile` geladen. Prioritaet: `-D`-Override, Environment,
  Projektdatei, Profil, eingebaute Vorgabe. Environment wird nicht veraendert/
  exportiert.
- `SUBDIRS` akzeptiert nur direkte Kindnamen (keine Pfade, `/` oder `..`),
  maximal 16 Ebenen; Builds gehen rekursiv zuerst in die Kinder.
- Keine automatische Abhaengigkeitserzeugung, Patternregeln, frei definierbaren
  Format-Uebergangsketten oder parallelen Builds. Die derzeitigen impliziten
  `.c`/`.a`-Regeln sind fest in qmake und verwenden konfigurierbare Variablen.
- Dry-run fuehrt keine Rezepte aus; seine Statusentscheidungen beruhen auf
  demselben vorhandenen Dateisystem wie ein normaler Lauf.

## Abnahmereihenfolge

1. Übersetzen derselben C89-Quellen mit QCC, Clang/GCC und dem ausgewählten
   Windows-Hostcompiler.
2. Host-`qmake` baut die OS-9/68k-`qmake`-Binary mit dem gewählten
   Targetcompiler.
3. `qmake` baut seine eigene Komponente reproduzierbar; ein unveränderter
   Folgelauf führt keine Rezepte aus.
4. OS-9-`qmake` führt denselben komponentenlokalen Build im Emulator aus.
5. Dry-run und Diagnosemodi weisen nach, dass kein Image ohne explizites
   Deployziel verändert wird.
