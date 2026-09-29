# Q9-Make Status

**Phase:** Minimaler C89-Hostprototyp implementiert; QCC- und Zielsystemnachweis offen.

| Bereich | Status | Nächster Nachweis |
|---|---|---|
| C-Sprachbaseline | 🟡 | Host-Build mit strengem C89 erfolgreich; QCC-Probe/Binary fehlt in diesem Checkout |
| Makefile-Syntax | 🟡 | Regeln, Rezepte, Variablen, `SUBDIRS`, Toolchainprofile und konfigurierbare Befehlsvorlagen per Smoke-Test abgedeckt; Q9-Gastlauf offen |
| Buildgraph | ✅ | Abhängigkeiten, Zyklen, Zeitstempel/`touch`-Marker, Kind-vor-Eltern-Rekursion und Erstziel-ohne-Argument getestet |
| Native Hosts | 🟡 | POSIX/macOS-Adapter samt Q9SDK/HOME-Profilauflösung getestet; Windows-Adapter geschrieben, Windows-Test offen |
| OS-9/68k | 🔴 | Plattformadapter fehlt; `/dd/SYS`-Standard, Prozessstart und Dateisystemzugriff im Emulator umsetzen/verifizieren |
| Toolchainprofile | 🟡 | Externe Profile, `-C`, Host-SDK-Suche und eigene `.c`/`.a`-Befehlsvorlagen implementiert; qmake-Profil mit echtem XCC/QCC noch zu verifizieren |
| Implizite Regeln | 🟡 | `.c`→`.o` via HOST_CC sowie `.c`/`.a`→`.r` via TARGET_CC/TARGET_AS; Standard und XCC-artige Befehlsvorlagen mit Fake-Tools getestet; qmake im Emulator offen |
| Diagnose | 🟡 | `-v`, `-vv`, `--dry-run`, `-C`/`--config-dir` implementiert; `--show-config`/Abfragen offen |
| Arbeitsimage-Deploy | 🔴 | Explizites, nicht-destruktives Installziel für benannte Arbeitsimageprofile entwerfen |
| Selbstbau-Abnahme | 🔴 | Host-Smoke- und strikter C89-Build erfolgreich; kleiner QCC-Compilerlauf erzeugte ROF; qmake-QCC-Build, Selbstbau und OS-9-Gastlauf offen |

## Prototypumfang / bekannte Grenzen

- Feste Kapazitaeten: 32 Regeln, 12 Abhaengigkeiten und 8 Rezepte je Regel,
  32 Variablen; Namen max. 127 und Rezeptzeilen max. 255 Zeichen.
- Genau ein Zielargument; Standardziel ist die erste Regel.
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
