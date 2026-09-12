# Bekannte C89-Lücken und Fehler

Stand: 2026-09-10

Diese Liste trennt echte Compilerfehler von bewusst noch nicht unterstützten
Sprachmerkmalen. Jeder Eintrag soll später einen kleinen reproduzierbaren
Testfall, eine Korrektur und einen Test auf Host und 68000 erhalten.

## Reproduziert

| Bereich | Befund | Status |
|---|---|---|
| Große Funktionen | **Grammatik-`FAIL` behoben (2026-09-10).** Ursache war `memberIncTarget = ident member .` in `Data/qcc.ebnf` -- deckte `x.feld++` ab, aber nicht `x.feld[i]++`/`--x.feld[i]`, ein gewöhnliches C89-Idiom (`vm->state[i]++` in `qrun_vm_run`). Fix: `memberIncTarget = ident member [ index ] .`. Verifiziert per `tools/bootstrap_survey.py` gegen korrekt präprozessierte `qrun_vm.c`/`qrun_main.c` (echte `-DQRUN_OS9 -ISource`-Auflösung, siehe Zeile darunter): Stufe 1 UND Stufe 2 jetzt 0/56 bzw. 0/42 Beanstandungen (vorher betroffen: `qrun_vm_load_ir`, `qrun_push_value`, `qrun_pop_value`, `qrun_vm_run`, `qrun_load_arch_file`). IR -> 68k-Backend ebenfalls verifiziert grün (beide Dateien erzeugen Assembler, exit 0). Offen bleibt nur noch das echte `r68`-Assemblieren + `l68`-Link + Ausführung auf Q9-Flux. | Frontend+Backend grün, echtes Assemblieren/Linken/Ausführen offen |
| Komplexe VM-Strukturen | Pointer auf Strukturen, Arrayzugriffe und große `switch`-Blöcke treten in Q9-Run gemeinsam auf. Die Kombination ist noch nicht als unabhängige C89-Testreihe abgesichert. | offen, Minimaltests fehlen |
| Stufe-2-Messrezept | Erste Erhebung (2026-09-10) meldete 1566 bzw. 58 SEMERR -- **Messartefakt**: das Rezept strich ALLE `#include`-Zeilen vor dem Präprozessieren (auch `qrun_vm.h`/`qrun_ir.h`/`qrun_os9.h`), damit fehlten sämtliche Typ-/Funktionsdeklarationen (`unknown type name 'qrun_vm_t'`, `unknown function 'malloc'` usw.). Korrigiert: `cc -E -P -x c -DQRUN_OS9 -ISource` statt Zeilen zu streichen -- damit löst sich `qrun_os9.h` real auf. Ergebnis siehe Zeile darüber: beanstandungsfrei. | behoben 2026-09-10 |
| `sizeof` auf Pointertypen | QCC meldet `sizeof of pointer types not supported in this version`. | dokumentierte Lücke |
| `switch`-Gültigkeitsbereich | Eine Deklaration direkt in einem `case`-Rumpf wird nicht auf den `switch`-Block begrenzt. | bekannte Frontend-Lücke |
| `switch`-Fallthrough | Fallthrough mit Code zwischen zwei `case`-Rümpfen wird nicht unterstützt. | bekannte Frontend-Lücke |
| `goto` | `goto` und Labels sind noch nicht implementiert. | bekannte Frontend-Lücke |

## Abgrenzung

Der erfolgreiche Bootstrap des QCC-Parsers ist kein Gegenbeweis zu diesen
Befunden. Der Bootstrap verwendet eine vorbereitete Quelle innerhalb der
bereits unterstützten Teilmenge. Q9-Run dient jetzt als größerer, unabhängiger
Regressionsfall für C89-Kompatibilität.

Ein Fehler gilt erst als behoben, wenn diese Kette durchläuft:

```text
QCC-Frontend -> IR -> qccvm.py/native VM -> 68k-Backend -> Q9-Flux
```

## Arbeitsreihenfolge

1. ~~Q9-Run-Funktionen in kleine Minimaltests zerlegen.~~ Für die Grammatik-
   Frage erledigt: `tools/bootstrap_survey.py` direkt gegen die präprozessierten
   Q9-Run-Dateien genügte, keine manuelle Zerlegung nötig.
2. ~~Parserfehler und Semantikfehler getrennt erfassen.~~ Erledigt
   (2026-09-10): Stufe 1 UND Stufe 2 beide 0 Beanstandungen, nach Korrektur
   des Messrezepts (siehe Tabelle oben).
3. Fehlende C89-Konstrukte im Frontend ergänzen. -- für die Grammatik-Lücke
   erledigt (`memberIncTarget` + Index).
4. Nur bei neuer IR-Semantik das Backend erweitern.
5. Q9-Run als dauerhaften Regressionstest aufnehmen.
