# Arbeitsweise für zukünftige Sitzungen

## Einstieg

1. `git status` und `git branch -a` im Repo prüfen, dazu `gh pr list` -- Arbeit
   kann auf einem noch nicht committeten oder noch nicht gemergten Branch
   liegen, unabhängig davon, was in den Doku-Dateien steht. An diesem Projekt
   arbeiten nicht immer dieselbe KI/Session, der Stand kann zwischen
   Sitzungen auseinanderlaufen.
2. `docs/STATUS.md` lesen.
3. In `docs/FORTSCHRITT.md` den aktuellen Meilenstein und die offenen Punkte prüfen.
4. Nur die für die konkrete Aufgabe relevanten Quell- und Doku-Dateien öffnen.
5. Bei Architekturfragen zusätzlich den passenden Abschnitt aus
   `docs/ARCHITEKTUR.md` lesen.

`context.txt` dient nur als Verlaufshilfe. Es ersetzt keinen gepflegten Status.

## Während einer Änderung

Eine Aufgabe soll möglichst ein Thema und ein überprüfbares Ergebnis haben, zum
Beispiel „Pointervergleich im QCCVM und in beiden Backends ergänzen“. Erst wird
die Semantik festgelegt, dann implementiert und anschließend getestet. Nach
jedem substanziellen Schritt wird kurz festgehalten, was geändert wurde und was
noch offen ist.

## Abschlusskriterien

Eine Änderung gilt erst als fertig, wenn:

- der relevante Code gebaut wurde,
- ein gezielter Regressionstest erfolgreich ist,
- `./runtests.sh` vollständig erfolgreich läuft,
- `STATUS.md` und gegebenenfalls `FORTSCHRITT.md` aktualisiert sind.

## Kontext klein halten

Lange Erklärungen und Zwischenprotokolle gehören in Markdown-Dateien, nicht in
jede neue Sitzung. Für die nächste Sitzung wird ein kurzer Auftrag mit Ziel,
betroffenen Dateien und Testbefehl verwendet. Dadurch bleibt das Gespräch für
Entscheidungen frei und der Projektstand bleibt trotzdem vollständig erhalten.

