# Projektstatus

Stand: **2026-07-22**

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
- Pointer: Deklaration, `&`, `*`, `p[i]`, Pointerparameter/-rückgabe,
  Pointervergleich, skalierte Arithmetik und Pointerdifferenz
- TinyVM als ausführbares Testorakel
- 68000-Backend mit Simulatorprüfung
- natives ARM64/Darwin-Backend mit Runtime

## Verifikation

`./runtests.sh` meldet aktuell:

```text
34 Tiny-C-Programme korrekt
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

