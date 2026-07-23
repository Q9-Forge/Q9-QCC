# Arbeitsweise für zukünftige Sitzungen

## Einstieg

1. `docs/STATUS.md` lesen.
2. In `docs/FORTSCHRITT.md` den aktuellen Meilenstein und die offenen Punkte prüfen.
3. Nur die für die konkrete Aufgabe relevanten Quell- und Doku-Dateien öffnen.
4. Bei Architekturfragen zusätzlich den passenden Abschnitt aus
   `docs/ARCHITEKTUR.md` lesen.

`context.txt` dient nur als Verlaufshilfe. Es ersetzt keinen gepflegten Status.

## Während einer Änderung

Eine Aufgabe soll möglichst ein Thema und ein überprüfbares Ergebnis haben, zum
Beispiel „Pointervergleich im TinyVM und in beiden Backends ergänzen“. Erst wird
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

