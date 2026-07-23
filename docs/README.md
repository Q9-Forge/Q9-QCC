# Dokumentation

Diese Dateien sind bewusst nach Zweck getrennt. Für eine neue Arbeitssitzung genügt
normalerweise `STATUS.md` und der passende Abschnitt aus `FORTSCHRITT.md`.

| Datei | Zweck |
|---|---|
| `STATUS.md` | Kompakter, aktueller Arbeitsstand und nächster Schritt |
| `FORTSCHRITT.md` | Meilensteine, erledigte Funktionen und offene Roadmap |
| `ISO_C_LUECKENLISTE.md` | Fehlende ISO-C17/C23-Bereiche und Ausbauplan |
| `ISO_C_ARBEITSEINHEITEN.md` | Kleine, testbare Einheiten mit ISO-Zuordnung |
| `TEILPROJEKTE.md` | Abgrenzung von Sprachkern, Präprozessor, Bibliothek und Backends |
| `BENUTZERHANDBUCH.md` | Nutzung des Generators, Tiny-C und Testabläufe |
| `ARBEITSWEISE.md` | Wiederaufnahme, Kontextpflege und Abschlusskriterien |
| `ENTWICKLERHANDBUCH.md` | Interna, Datenfluss, IR, Backends und Änderungsregeln |
| `ARCHITEKTUR.md` | Ausführliche technische Entscheidungen und historische Begründungen |

## Dokumentationsregel

Nach jeder größeren Änderung werden `STATUS.md` und der passende Abschnitt in
`FORTSCHRITT.md` aktualisiert. Architekturentscheidungen gehören zusätzlich in
`ARCHITEKTUR.md`; Bedienhinweise gehören ausschließlich ins Benutzerhandbuch.
