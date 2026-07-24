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
| Tiny-C `struct` mit gemischten skalaren Feldtypen | erledigt (2026-07-24) | echtes Byte-Layout mit natürlichem Alignment; Feldzugriff nutzt PUSHADDR/IPADD/LOADIND/STOREIND (bereits vorhandene, architekturneutrale Opcodes) statt LOADIDX/STOREIDX -- kein neuer Opcode, kein Backend-Change. Bewusst noch offen: Pointer-Felder (68k 4 Byte vs. ARM64 8 Byte), verschachtelte structs (siehe SELFHOSTING_LUECKENLISTE.md) |
| Tiny-C Array-Felder in `struct` (z.B. `char name[8]`) | erledigt (2026-07-24) | `structField = type fieldName [ arraySize ] ";"` -- `arraySize` ist dieselbe seiteneffektfreie Regel wie bei globalen Arrays (kein neuer Opcode/Grammatik-Overhead). Byte-Layout: Groesse = Elementgroesse * Elementzahl, Ausrichtung nach dem ELEMENTtyp (wie ein eingebettetes `GARRAY`); `sizeof(struct X)` und lokale struct-Variablen (`LARRAY`-Groesse) profitieren automatisch, da beide auf `tcStructByteSize` aufsetzen. Zugriff: ein BARER Feldzugriff (`p.field`, ohne Index) zerfaellt zu einem Pointer auf das erste Element -- genau wie eine Array-VARIABLE (`tcPointerTo(Elementtyp)`, kein `LOADIND`) -- daher NUR ueber eine Pointer-Zwischenvariable indizierbar (`char *q = p.field; q[i] = ..;`). Direkte `p.field[i]`-Syntax braucht eine kombinierte member+index-Kette in der `varRef`/`target`-Grammatik (aktuell `[ index \| member ]` als EIN Slot, nicht verkettbar) -- eigener Folgeschritt. Zuweisung an das GANZE Array-Feld (`p.field = ..`) wird diagnostiziert (wie in echtem C nicht erlaubt). Verifiziert in TinyVM, 68000 UND ARM64 (gemischte Feldtypen inkl. Array, korrekte Offsets). |
| Tiny-C `typedef struct { ... } Name;` (anonymes struct inline) | erledigt (2026-07-24) | Zielname kommt in der Grammatik erst nach dem Feld-Body -- verzögerte Registrierung in `tc_typedefend`, typedef-Name dient als interner struct-Tag (harmlose Vereinfachung, `struct Name x;` funktioniert dadurch als Nebeneffekt mit). Kein IR-/Backend-Change, exakt derselbe Feldzugriffs-Code wie bei benannten structs. Scope: nur reine anonyme Form, kein optionaler Tag, keine Pointer-Kombination |
| Tiny-C `const`-Qualifizierer (Skalare/Arrays) | erledigt (2026-07-24) | `const` vor Typ bei globalen/lokalen Variablen und Parametern (`constKw`-Huellregel); verbietet Zuweisung/++/-- auf die qualifizierte Variable selbst ueber die bestehenden Ziel-Aufloesungspfade (`tc_target`, `tc_preincdec`/`tc_postincdec`). Reine Frontend-Pruefung, kein neuer Opcode, kein Backend-Change. Bei Pointertypen siehe eigene Zeile "Pointee-Constness" unten (jetzt ebenfalls erledigt). |
| Tiny-C Pointee-Constness (`const T*`) | erledigt (2026-07-24) | Neues Feld `TCType.pointeeConst` (ein Bit, kein vollstaendiges Mehrebenen-Constmodell -- bei `T**` wird nur die unmittelbare Dereferenzierung geschuetzt). Wird bei `const`+Pointertyp in allen vier Registrierungsstellen gesetzt (`tc_local`, `tc_param`, `tc_globalend`, `tc_staticlocal`) und reist automatisch durch `tcPointerTo`/`tcPointee` (Struct-Kopie), Zeigerarithmetik (`tc_term`s P/IPADD-Zweige pushen denselben Typ zurueck) und Parameteruebergabe mit -- KEINE Aenderung an Lesezugriffen (`tc_varref`, `tc_derefref`) noetig. Durchgesetzt an den drei Schreibstellen: `tc_indirecttarget` (`*p = ..`) sowie den beiden Pointer-Array-Index-Zweigen in `tc_target` (`p[i] = ..`, lokal UND global) -- alle drei pruefen `pointeeConst` VOR dem `tcPointee()`-Unwrap und diagnostizieren `tinyc: cannot assign through pointer to const`, OHNE die eigentliche Codeerzeugung abzubrechen (Stack bleibt balanciert). Der Pointer selbst bleibt frei zuweisbar (`p++`, `p = ...` weiterhin erlaubt) -- deckt jetzt auch semantisch, nicht nur syntaktisch, den Hauptnutzungsfall im Generator-Vorbild ab (`const char* line`-Parameter). Verifiziert: Lesen durch const-Pointer weiterhin erlaubt, Schreiben lokal/global/per Parameter diagnostiziert, `p++`-Idiom weiterhin erlaubt, normale (nicht-const) Pointer weiterhin frei beschreibbar (Regression). |
| Tiny-C `static`-Speicherklasse | erledigt (2026-07-24) | `staticKw`-Huellregel. Bei globalen Variablen/Funktionen reines No-op (interne Verlinkung ist bei einer einzigen Uebersetzungseinheit ohne Mehrdatei-Linkage bedeutungslos) -- deckt damit den GROSSTEIL der 101 realen Fundstellen in ebnf.cpp/codegen.cpp ab (fast alle sind file-scope `static` auf Tabellenpuffern, keine lokalen). Bei LOKALEN Variablen echte Semantik: eigene Grammatikregel `staticVarDecl` (kein Frame-Slot ueber `localName`/`tc_local`, sondern `tc_staticlocal` registriert die Variable direkt als GLOBAL unter ihrem Klarnamen) -- dadurch greifen tc_target/tc_varref/tcLoadTarget/tc_preincdec/tc_postincdec/addressRef/sizeof(variable) automatisch ueber den schon vorhandenen tcLookupGlobal-Pfad zu, OHNE dass an einer dieser Stellen etwas geaendert werden musste. Backend-Voraussetzung: `collectGlobals`/`collectFunctions` in `tinyc_backend_c.cpp` UND `tinyc_arm64_backend_c.cpp` verlangten bisher "GLOBAL muss genau einmal VOR allen Funktionen stehen" -- gelockert auf "vor der ersten Funktion ODER innerhalb einer offenen Funktion", da eine static-Lokale ihr GLOBAL an der Textstelle ihrer Deklaration emittiert (moeglicherweise mitten in einer FUNC-Spanne); TinyVM tolerierte das schon vorher (reiner Pre-Scan). Verifiziert in TinyVM, 68000 UND ARM64 (echte Persistenz ueber mehrere Aufrufe). Konstante Initialisierer (`static int x = 5;`, `static bool b = true;`, `static char c = 65;`, Pointer-Initialisierer nur `0`) sind seit 2026-07-24 (Nachtrag) erlaubt: Grammatik beschraenkt auf `globalValue` (Zahl/Negativ/bool-Literal, dieselbe seiteneffektfreie Regel wie bei globalen Variablen -- KEINE Aktion auf `globalValue` selbst, also kein Laufzeit-PUSH waehrend des Parsens), `tc_staticlocal` extrahiert den Wert per Rohtext-Scan ab dem `=` (genau wie `tc_globalend` es fuer echte globale Variablen tut) und baut ihn direkt in die `GLOBAL`-Zeile ein -- kein Runs-once-Guard noetig. Ein NICHT-konstanter Ausdruck (`static int x = someVar;`) bleibt bewusst ein sauberer Parse-Fehler: er wuerde ueber die normale expr-Auswertung bei JEDEM Aufruf neu berechnet/gespeichert und die Variable faelschlich zuruecksetzen. Bewusst weiterhin NICHT unterstuetzt: static-`struct`-Lokale (diagnostiziert, `struct`-Globals sind generell noch nicht unterstuetzt) und static-Arrays. Ausserdem: kein Funktions-Scoping -- der Name einer static-Lokalen landet im GLEICHEN flachen globalen Namensraum wie normale globale Variablen, zwei Funktionen duerfen also noch keine gleichnamige static-Variable haben (`tinyc: duplicate global`). |

## Selfhosting (neue Zielrichtung ab 2026-07-23)

Ziel: Generator + Tiny-C-Toolchain irgendwann in Tiny-C selbst schreib- und
übersetzbar machen. Vollständige Analyse und Reihenfolge in
`docs/SELFHOSTING_LUECKENLISTE.md`. Kurzfassung des Stands:

1. `struct`/`typedef`/`enum`/`for`/`switch` in Tiny-C: erledigt (siehe Tabelle oben)
2. `const` (inkl. Pointee-Constness) und `static`: erledigt (2026-07-24, siehe
   Tabelle oben, inkl. konstantem static-Initialisierer). Noch offen:
   mehrdimensionale Arrays
3. Mini-Runtime (String-Vergleich, formatierte Ausgabe, Datei-I/O): noch offen
4. Mehrdatei-Übersetzung: noch offen
5. `goto`/Funktionszeiger (erst wenn der generierte Parser-Zwilling selbst gehostet werden soll): noch offen
6. Speicherbedarf der statischen Puffer in `ebnf.cpp`/`codegen.cpp` fuer ein reales
   16-MB-/8-MB-Zielsystem (Q9): **erledigt (2026-07-24)**, siehe `docs/STATUS.md` --
   Host-Datensegment ~34,6 MB -> ~1 MB (ACTION/ROUTINE-Tabellen jetzt malloc/realloc-
   basiert statt fester Arrays). xcc-Build+Ausfuehrungstest auf dem echten Q9 mit
   dieser Aenderung noch nicht wiederholt (naechster Schritt laut STATUS.md).

## Bewusst offen

- Mehrebenen-Pointee-Constness bei `T**` (nur unmittelbare Dereferenzierung geschuetzt), `char* const` (Pointer selbst konstant, umgekehrter Fall zu Pointee-Constness)
- static-Lokale mit NICHT-konstantem Initialisierer (braucht Runs-once-Guard mit hidden Flag-Global), static-`struct`-Lokale, static-Arrays, Funktions-Scoping fuer static-Lokale-Namen
- direkte `p.field[i]`-Indizierung von Array-Feldern in `struct` (braucht kombinierte member+index-Kette in `varRef`/`target`; aktuell nur ueber Pointer-Zwischenvariable moeglich), Pointer-Felder in `struct`, verschachtelte structs
- `void`/`void *`
- mehrdimensionale und flexiblere Arrays
- dynamische Speicherverwaltung (als Tiny-C-Sprachmittel, d.h. `malloc`/`free` als aufrufbare Tiny-C-Funktion)
- nichtkonstante globale Initialisierer (betrifft jetzt auch static-Lokale-Initialisierer, siehe oben)
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

