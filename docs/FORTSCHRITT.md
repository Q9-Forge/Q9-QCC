# Fortschritt und Roadmap

## Erledigte Meilensteine

| Bereich | Status | Bemerkung |
|---|---|---|
| EBNF-Parser/Scanner-Generator | erledigt | Tabellen, Lexerblöcke und Nutzer-Code |
| Tiny-C M1 | erledigt | Ausdrücke, Variablen, Stack-IR, TinyVM |
| Tiny-C M2/M3 | erledigt | `if/else`, `while`, Calls, Parameter, Rekursion |
| Tiny-C Kontrollfluss (Stufe A, Teil 1) | erledigt | `for`, `do/while`, `break`, `continue` -- kein neuer IR-Opcode, kein Backend-Change noetig |
| Tiny-C `typedef` | erledigt | Skalar-/Pointer-Aliase, reine Grammatik-Erweiterung |
| Tiny-C `struct` (einheitlicher Feldtyp) | erledigt | Feldzugriff nutzt LOADIDX/STOREIDX wieder -- kein Backend-Change; gemischte Feldtypen bewusst vertagt (siehe SELFHOSTING_LUECKENLISTE.md) |
| Tiny-C `enum` | erledigt | reine benannte int-Konstanten, kein eigener Typ, kein Backend-Change (nur PUSH) |
| Tiny-C `sizeof` | erledigt | nur int/char/bool/unsigned/struct als Argument, keine Pointer, kein Backend-Change (Konstante zur Kompilierzeit) |
| Tiny-C Prä-/Postinkrement (`++`/`--`) | erledigt | nur einfache int/unsigned/char-Skalare (lokal/global), kein Backend-Change (LOAD/DUP/PUSH/ADD-oder-SUB/STORE) |
| Tiny-C `switch`/`case`/`default` | teilweise | gestapelte Case-Label (case A: case B: body) unterstuetzt, KEIN Fallthrough mit Code zwischen verschiedenen Bodies (deckt das reale Nutzungsmuster in ebnf.cpp/codegen.cpp ab); kein Backend-Change |
| Tiny-C Casts | teilweise | nur `(int)`/`(unsigned int)`/`(char)`/`(bool)`, kein Pointer-/typedef-Cast-Ziel (Mehrdeutigkeits-Falle mit Klammerausdruecken bewusst vermieden); kein Backend-Change |
| Tiny-C `sizeof(variable)` | erledigt | ergaenzt `sizeof(Typ)`: jetzt auch `sizeof(x)` auf Skalare/Arrays (lokal+global) -- deckt die reale Nutzung im Generator ab |
| Tiny-C `enum` als Typ | erledigt | `enum Name var;` als Deklaration/Parameter/Rueckgabetyp moeglich, bleibt intern `int` |
| Arrays | erledigt | bytegenaue `char[]`/`int[]`, lokal und global |
| Datentypen | erledigt | `int`, `unsigned int`, `char`, `bool`, Nullwert |
| Operatoren | erledigt | Rechen-, Vergleichs-, Bit-, Shift-, Logik- und ternäre Operatoren |
| Zuweisungen | erledigt | einfach und kombiniert, auch für Array-/Pointerziele |
| Pointer | erledigt | Typmodell, Adressen, Dereferenzierung, Skalierung, `T**` |
| 68000-Ausgabe | funktionsfähig | vasm und `tiny68sim.py` |
| ARM64/Darwin-Ausgabe | funktionsfähig | natives Programm mit eigener Runtime |
| L3-Backends auf reines C zurückgebaut | erledigt | `tinyc_backend_c.cpp`/`tinyc_arm64_backend_c.cpp`, siehe `docs/SELFHOSTING_LUECKENLISTE.md` |
| Generator: C++-Templates entfernt | erledigt | `Source/msvc_compat.h`, 75 Aufrufstellen umgestellt -- Voraussetzung dafür, dass der Generator mit der echten Microware-`xcc`-Toolchain kompiliert |
| Generator kompiliert+linkt mit echter Q9-Toolchain (`xcc`) | erledigt | siehe `docs/SELFHOSTING_LUECKENLISTE.md` Abschnitt 6; Ausführung auf Q9 scheitert noch am Speicherbedarf (~34,6 MB Datensegment vs. 16 MB RAM) |
| Tiny-C `struct` mit gemischten skalaren Feldtypen | erledigt (2026-07-24) | echtes Byte-Layout mit natürlichem Alignment; Feldzugriff nutzt PUSHADDR/IPADD/LOADIND/STOREIND (bereits vorhandene, architekturneutrale Opcodes) statt LOADIDX/STOREIDX -- kein neuer Opcode, kein Backend-Change. Bewusst noch offen: Array-Felder, Pointer-Felder (68k 4 Byte vs. ARM64 8 Byte), verschachtelte structs (siehe SELFHOSTING_LUECKENLISTE.md) |
| Tiny-C `typedef struct { ... } Name;` (anonymes struct inline) | erledigt (2026-07-24) | Zielname kommt in der Grammatik erst nach dem Feld-Body -- verzögerte Registrierung in `tc_typedefend`, typedef-Name dient als interner struct-Tag (harmlose Vereinfachung, `struct Name x;` funktioniert dadurch als Nebeneffekt mit). Kein IR-/Backend-Change, exakt derselbe Feldzugriffs-Code wie bei benannten structs. Scope: nur reine anonyme Form, kein optionaler Tag, keine Pointer-Kombination |

## Selfhosting (neue Zielrichtung ab 2026-07-23)

Ziel: Generator + Tiny-C-Toolchain irgendwann in Tiny-C selbst schreib- und
übersetzbar machen. Vollständige Analyse und Reihenfolge in
`docs/SELFHOSTING_LUECKENLISTE.md`. Kurzfassung des Stands:

1. `struct`/`typedef`/`enum`/`for`/`switch` in Tiny-C: erledigt (siehe Tabelle oben)
2. Mehrdimensionale Arrays, `static`/`const`: noch offen
3. Mini-Runtime (String-Vergleich, formatierte Ausgabe, Datei-I/O): noch offen
4. Mehrdatei-Übersetzung: noch offen
5. `goto`/Funktionszeiger (erst wenn der generierte Parser-Zwilling selbst gehostet werden soll): noch offen
6. **Neu erkannt:** Speicherbedarf der statischen Puffer in `ebnf.cpp`/
   `codegen.cpp` für ein reales 16-MB-Zielsystem (Q9) verkleinern -- einziger
   bekannter Blocker zwischen "kompiliert mit echter Toolchain" und "läuft
   wirklich auf Q9" (siehe `docs/SELFHOSTING_LUECKENLISTE.md` Abschnitt 6).

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

