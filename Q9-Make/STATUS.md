# Q9-Make Status

Q9-Make ist ein C89-Prototyp mit benannten Konfigurationsabschnitten und
Dreibuchstaben-Codes. Die Anleitungen beschreiben die Profile in
[Englisch](README.md) und [Deutsch](README_de.md).

| Bereich | Status | Nachweis und offene Arbeit |
|---|---|---|
| Host-Build | 🟡 | Der POSIX-Build und die Smoke-Tests bestehen mit Warnungen als Fehler. Der Windows-Build und -Lauf sind hier nicht geprüft. |
| Q9/68k | 🟡 | Ein nativer Plattformadapter ist vorhanden. Grenzfälle der Befehlsverarbeitung sind mit Host-Stubs getestet; Build und Lauf auf dem Zielsystem sind offen. |
| Projektdatei | 🟡 | Konfigurationsabschnitte, Profile, Variablen, Abhängigkeiten, Rekursion und die Auswahl per Kommandozeile sind durch Smoke-Tests abgedeckt. Ein Gastlauf steht aus. |
| Toolchains | 🟡 | Profile für mehrere Hosts und Ziele sind vorhanden. Die Prüfung aller Compiler und Laufzeitumgebungen steht aus. |
| Implizite Regeln | 🟡 | `.c` nach `.o` und `.c`/`.a` nach `.r` sind implementiert und mit Testwerkzeugen geprüft. Die Zielumgebung ist separat zu prüfen. |
| Diagnose | 🟡 | `-v`, `-vv`, `--dry-run`, `-C` und `--config-dir` sind implementiert. Weitere Konfigurationsabfragen stehen aus. |
| Arbeitsimage-Deploy | 🔴 | Ein explizites Installziel für benannte Arbeitsimageprofile steht aus. |
| Selbstbau | 🟡 | Frühere Cross-Build-Ergebnisse sind dokumentiert. Ein reproduzierbarer Selbstbau und Gastlauf von Q9-Make selbst stehen aus. |

## Bisherige Build-Ergebnisse

Am 2026-10-02 wurden laut dem bisherigen Bericht die Profile `ACA` und `AQQ`
von Grund auf gebaut und sieben QCC-Werkzeuge erzeugt. Das Cross-Build-Profil
`AXQ` wurde ebenfalls ausgeführt. Diese historischen Ergebnisse wurden bei der
aktuellen Prüfung nicht wiederholt; sie ersetzen keinen Gastlauf von Q9-Make.

## Umfang und Grenzen

- Die Projektdatei kann bis zu 96 Regeln, 12 Abhängigkeiten je Regel,
  32 Variablen und 16 Konfigurationsabschnitte enthalten. Namen und einzelne
  Rezeptzeilen unterliegen festen Längengrenzen; Rezeptzeilen werden dynamisch
  gespeichert.
- Genau ein Zielargument ist möglich. Ohne `-P` wird es in den passenden
  Abschnitten ausgeführt. Ohne Ziel wird eine Vorgaberegel gewählt.
- `[global]`-Werte und Regeln gelten zusammen mit dem gewählten Abschnitt.
  Profile stehen in `toolchains/qmake.conf` oder in einer Datei aus `-C`.
- Standardmäßig wird `q9makefile` aus dem aktuellen Verzeichnis gelesen;
  `-f DATEI` und `--file DATEI` wählen eine andere Datei.
- Host-Rezepte laufen über die Host-Shell. Der native Adapter unterstützt
  einfache Befehle und `&&`-Verkettung; die Zielausführung ist noch zu prüfen.
- Variablen werden unter anderem in Rezepten, Zielnamen, exportierten Werten
  und ausgewählten Konfigurationswerten expandiert. Die Priorität ist
  `-D`-Override, Environment, vom Elternverzeichnis mit `EXPORT` übergebener
  Wert, Projektdatei, Profil und eingebaute Vorgabe. Das Environment wird nicht
  verändert oder exportiert.
- `SUBDIRS` akzeptiert nur direkte Kindnamen ohne Pfadtrenner oder `..` und
  höchstens 16 Ebenen. Kinder werden vor dem lokalen Ziel gebaut.
- Automatische Abhängigkeitserzeugung, Patternregeln, frei definierbare
  Format-Übergangsketten und parallele Builds sind nicht implementiert.
- Dry-run führt keine Rezepte aus. Seine Entscheidungen beruhen auf dem
  vorhandenen Dateisystem.

## Weitere Prüfung

1. Die C89-Quellen mit QCC sowie den vorgesehenen Hostcompilern bauen.
2. Die native Q9/68k-Version bauen und ihre Prozess- und Dateizugriffe auf der
   Zielumgebung prüfen.
3. Einen reproduzierbaren Selbstbau prüfen; ein unveränderter Folgelauf soll
   keine Rezepte ausführen.
4. Dry-run und Diagnosen prüfen. Ein Arbeitsimage darf nur durch ein explizites
   Deployziel verändert werden.
