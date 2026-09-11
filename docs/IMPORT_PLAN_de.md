# Q9-QCC – Importplan für Teilprojekte

Die bestehenden Teilprojekte werden schrittweise in das Sammelrepository
übernommen. Zielpfade und Git-Historie werden dabei getrennt behandelt.

## Reihenfolge

1. `Q9-qclib` → `Q9-BACKEND-68K/q9-qclib/`
2. `Q9-Run` → `Q9-RUN/`
3. `Q9-Parsec` → `Q9-PARSEC/`

## Regeln

- Importiert wird nur der geprüfte Commit-Stand des jeweiligen Quellrepos.
- Unversionierte Arbeitsdateien werden nicht übernommen.
- Die Übernahme erfolgt als Git-Subtree mit einem eigenen Import-Commit, damit
  die Herkunft und die vollständige Historie nachvollziehbar bleiben.
- Vor jedem Import wird geprüft, ob der Zielpfad frei ist und ob das Quellrepo
  einen reproduzierbaren Build-/Teststand besitzt.
- Die bestehende Arbeitsstruktur von `Q9-QCC` wird vor dem ersten Import als
  eigener Commit abgeschlossen; persönliche `AGENTS.md`-Dateien bleiben lokal.

## Bekannte Importstände

| Projekt | Branch/Commit | Arbeitsbaum | Status |
|---|---|---|---|
| `Q9-qclib` | wird vor Import ermittelt | sauber erforderlich | zuerst |
| `Q9-Run` | `HEAD` | lokale Änderungen vorhanden | später prüfen |
| `Q9-Parsec` | `df85324` / aktueller Branch | 3 unversionierte Dateien | später prüfen |
