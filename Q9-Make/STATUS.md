# Q9-Make Status

**Phase:** C89-Prototyp mit benannten Host/Compiler/Target-Konfigurationsabschnitten implementiert; QCC-qmake-Build und Zielsystemnachweis offen.

| Bereich | Status | Nächster Nachweis |
|---|---|---|
| C-Sprachbaseline | 🟡 | Host-Build mit strengem C89 erfolgreich; QCC-Probe/Binary fehlt in diesem Checkout |
| Makefile-Syntax | ✅ | `[global]` und benannte Host/Compiler/Target-Abschnitte, wiederholte Ziele, `-P`-Auswahl, SDK-/`-C`-Profile und `--list-configs` per Smoke-Test abgedeckt; Gastprüfung separat offen |
| Buildgraph | ✅ | Abhängigkeiten, Zyklen, Zeitstempel/`touch`-Marker, Kind-vor-Eltern-Rekursion und Erstziel-ohne-Argument getestet |
| Native Hosts | 🟡 | POSIX/macOS-Adapter samt Q9SDK/HOME-Profilauflösung getestet; Windows-Adapter geschrieben, Windows-Test offen |
| OS-9/68k | 🔴 | Plattformadapter fehlt; `/dd/SYS`-Standard, Prozessstart und Dateisystemzugriff im Emulator umsetzen/verifizieren |
| Toolchainprofile | 🟡 | Eine zentrale `toolchains/qmake.conf` mit Mac-Clang, Linux-GCC, Windows-Clang und Q9-QCC/68k-Abschnitten; XCC-Konfigurationen und Windows-Lauftest offen; Q9-SYS-Datei vorhanden, Laden durch qmake im Emulator offen |
| Implizite Regeln | 🟡 | `.c`→`.o` via HOST_CC sowie `.c`/`.a`→`.r` via TARGET_CC/TARGET_AS; Standard und XCC-artige Befehlsvorlagen mit Fake-Tools getestet; qmake im Emulator offen |
| Diagnose | 🟡 | `-v`, `-vv`, `--dry-run`, `-C`/`--config-dir` implementiert; `--show-config`/Abfragen offen |
| Arbeitsimage-Deploy | 🔴 | Explizites, nicht-destruktives Installziel für benannte Arbeitsimageprofile entwerfen |
| Selbstbau-Abnahme | 🔴 | Host-Smoke- und strikter C89-Build erfolgreich; kleiner QCC-Compilerlauf erzeugte ROF; qmake-QCC-Build, Selbstbau und OS-9-Gastlauf offen |

## Prototypumfang / bekannte Grenzen

- Feste Kapazitaeten: 32 Regeln, 12 Abhaengigkeiten und 8 Rezepte je Regel,
  32 Variablen; Namen max. 127 und Rezeptzeilen max. 255 Zeichen.
- Genau ein Zielargument; ohne `-P` wird es in allen Abschnitten ausgefuehrt,
  die es definieren. Ohne Ziel gilt die erste lokale Regel jedes Abschnitts.
- Maximal 16 Konfigurationsabschnitte pro `q9makefile`; `[global]`-Werte und
  Regeln gelten in jedem Abschnitt. Konfigurationswerte stehen im passenden
  Abschnitt von `toolchains/qmake.conf` oder einer Datei aus `-C`.
- `q9makefile` wird aus dem aktuellen Arbeitsverzeichnis gelesen.
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
