# Fortschritt und Roadmap

## Erledigte Meilensteine

| Bereich | Status | Bemerkung |
|---|---|---|
| EBNF-Parser/Scanner-Generator | erledigt | Tabellen, Lexerblöcke und Nutzer-Code |
| Tiny-C M1 | erledigt | Ausdrücke, Variablen, Stack-IR, TinyVM |
| Tiny-C M2/M3 | erledigt | `if/else`, `while`, Calls, Parameter, Rekursion |
| Arrays | erledigt | bytegenaue `char[]`/`int[]`, lokal und global |
| Datentypen | erledigt | `int`, `unsigned int`, `char`, `bool`, Nullwert |
| Operatoren | erledigt | Rechen-, Vergleichs-, Bit-, Shift-, Logik- und ternäre Operatoren |
| Zuweisungen | erledigt | einfach und kombiniert, auch für Array-/Pointerziele |
| Pointer | erledigt | Typmodell, Adressen, Dereferenzierung, Skalierung, `T**` |
| 68000-Ausgabe | funktionsfähig | vasm und `tiny68sim.py` |
| ARM64/Darwin-Ausgabe | funktionsfähig | natives Programm mit eigener Runtime |
| L3-Backends auf reines C zurückgebaut | fertig, noch nicht committet | `tinyc_backend_c.cpp`/`tinyc_arm64_backend_c.cpp`, Branch `selfhost/plain-c-backends`, siehe `docs/SELFHOSTING_LUECKENLISTE.md` |

## Selfhosting (neue Zielrichtung ab 2026-07-23)

Ziel: Generator + Tiny-C-Toolchain irgendwann in Tiny-C selbst schreib- und
übersetzbar machen. Vollständige Analyse und Reihenfolge in
`docs/SELFHOSTING_LUECKENLISTE.md`. Kurzfassung der nächsten Schritte:

1. `struct`/`typedef` in Tiny-C (Voraussetzung für fast alles Weitere)
2. `enum`, `for`, `switch`, mehrdimensionale Arrays, `static`/`const`, `sizeof`
3. Mini-Runtime (String-Vergleich, formatierte Ausgabe, Datei-I/O)
4. Mehrdatei-Übersetzung
5. `goto`/Funktionszeiger (erst wenn der generierte Parser-Zwilling selbst gehostet werden soll)

## Bewusst offen

- `const` und Qualifizierer
- `void`/`void *`
- mehrdimensionale und flexiblere Arrays
- dynamische Speicherverwaltung
- nichtkonstante globale Initialisierer
- endgültige Q9-Start-, Modul- und Systemcall-Runtime
- zusätzliche Architekturen und Optimierungen

## Arbeitsreihenfolge für neue Features

1. Sprachregeln in `Data/tinyc.ebnf` festlegen.
2. Aktionen in `Data/tinyc.lextab` ergänzen bzw. regenerieren.
3. Typ- und Semantikprüfung im Frontend erweitern.
4. Neuen oder angepassten IR-Befehl definieren.
5. TinyVM implementieren und dort zuerst testen.
6. 68000- und ARM64-Backend ergänzen.
7. Kleinen gezielten Regressionstest in `runtests.sh` aufnehmen.
8. `./runtests.sh` vollständig ausführen und diese Datei aktualisieren.

