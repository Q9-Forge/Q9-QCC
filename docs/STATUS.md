# Projektstatus

Stand: **2026-07-23**

## Wichtig für eine neue Sitzung (auch mit anderer KI)

An diesem Projekt arbeiten nicht immer dieselbe KI/Session. Deshalb **vor** dieser
Datei zusätzlich prüfen:

- `git status` und `git branch -a` im Repo-Wurzelverzeichnis -- Arbeit kann auf
  einem noch nicht gemergten Branch liegen, unabhängig davon was hier steht.
- `gh pr list` -- offene Pull Requests, die noch Review/Merge brauchen.

Aktuell (2026-07-23):
- PR #1 (https://github.com/foellmy51/ebnf/pull/1) ist OFFEN, noch nicht gemergt.
  Enthält: Pointer, ARM64/Darwin-Backend, Doku-Umstellung auf docs/.
- PR #2 (https://github.com/foellmy51/ebnf/pull/2) ist OFFEN, noch nicht gemergt.
  Branch `selfhost/plain-c-backends`. Enthält: Rückbau der zwei C++-Backends
  (`Source/tinyc_backend.cpp`, `Source/tinyc_arm64_backend.cpp`) auf reines C
  (neue Dateien `*_c.cpp`, Originale bleiben unverändert daneben liegen).
- Auf demselben lokalen Branch (`selfhost/plain-c-backends`) liegt zusätzlich,
  NOCH NICHT COMMITTET: `for`/`do-while`/`break`/`continue` in Tiny-C (siehe
  `docs/ISO_C_LUECKENLISTE.md` Abschnitt 4, Stufe A). Grammatik in
  `Data/tinyc.ebnf`, Aktionen in `Data/tinyc.lextab`; dabei ein echtes,
  eigenständiges Limit in `Source/codegen.cpp` gefunden und behoben:
  `ACTION_ROUTINE_MAX` war auf 64 `ROUTINE C`-Blöcke fest verdrahtet (jetzt 256).
  `./runtests.sh` läuft grün (40 Tiny-C-Programme, alle drei Backends
  gegengeprüft). Diese Arbeit ist inhaltlich unabhängig vom C-Rückbau -- vor
  dem Committen ggf. auf einen eigenen Branch verschieben, damit PR #2 nicht
  zwei unabhängige Themen mischt.
- Nächster geplanter Schritt: weitere Sprachmittel aus
  `docs/SELFHOSTING_LUECKENLISTE.md` Abschnitt 1 nachziehen, beginnend mit
  `struct`/`typedef`.

## Kurzfassung

Der EBNF-Generator erzeugt aus Grammatiken Parsercode. Als vollständiger
Referenzpfad ist Tiny-C verfügbar:

`Data/tinyc.ebnf` → generierter Parser → Stack-IR → TinyVM / 68000 / ARM64

Alle derzeitigen Regressionstests sind erfolgreich.

## Aktuell implementiert

- Ganzzahl-, unsigned-, char-, bool- und Nullwerte
- Funktionen mit Parametern, Rückgabewerten, Verschachtelung und Rekursion
- lokale/globale Variablen und eindimensionale Arrays
- arithmetische, relationale, logische und bitweise Operatoren
- Kurzschlussauswertung von `&&` und `||`
- ternärer Operator `?:`
- Zuweisungen und zusammengesetzte Zuweisungen
- Kontrollfluss: `if/else`, `while`, `for`, `do/while`, `break`, `continue`
- Pointer: Deklaration, `&`, `*`, `p[i]`, Pointerparameter/-rückgabe,
  Pointervergleich, skalierte Arithmetik und Pointerdifferenz
- TinyVM als ausführbares Testorakel
- 68000-Backend mit Simulatorprüfung
- natives ARM64/Darwin-Backend mit Runtime

## Verifikation

`./runtests.sh` meldet aktuell:

```text
40 Tiny-C-Programme korrekt
68000-Pointer-End-to-End-Test korrekt
ARM64/Darwin-Test korrekt
=== ALLE TESTS OK ===
```

## Nächster sinnvoller Schritt

Als nächstes sollte jeweils nur ein klar abgegrenztes Sprachfeature bearbeitet
werden. Naheliegende Kandidaten sind `const`, `void *`, weitere Arrayformen und
anschließend eine echte Target-Runtime (zuerst Q9). Vor jeder Erweiterung sind
Frontend, IR, TinyVM, 68000- und ARM64-Backend sowie ein Regressionstest zu
prüfen.

