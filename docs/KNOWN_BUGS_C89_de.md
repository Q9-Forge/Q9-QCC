# Bekannte C89-Lücken und Fehler

Stand: 2026-09-10

Diese Liste trennt echte Compilerfehler von bewusst noch nicht unterstützten
Sprachmerkmalen. Jeder Eintrag soll später einen kleinen reproduzierbaren
Testfall, eine Korrektur und einen Test auf Host und 68000 erhalten.

## Reproduziert

| Bereich | Befund | Status |
|---|---|---|
| Große Funktionen | **Grammatik-`FAIL` behoben (2026-09-10).** Ursache war `memberIncTarget = ident member .` in `Data/qcc.ebnf` -- deckte `x.feld++` ab, aber nicht `x.feld[i]++`/`--x.feld[i]`, ein gewöhnliches C89-Idiom (`vm->state[i]++` in `qrun_vm_run`). Fix: `memberIncTarget = ident member [ index ] .`. Verifiziert per `tools/bootstrap_survey.py` gegen korrekt präprozessierte `qrun_vm.c`/`qrun_main.c` (echte `-DQRUN_OS9 -ISource`-Auflösung, siehe Zeile darunter): Stufe 1 UND Stufe 2 jetzt 0/56 bzw. 0/42 Beanstandungen (vorher betroffen: `qrun_vm_load_ir`, `qrun_push_value`, `qrun_pop_value`, `qrun_vm_run`, `qrun_load_arch_file`). IR -> 68k-Backend ebenfalls verifiziert grün (beide Dateien erzeugen Assembler, exit 0). Offen bleibt nur noch das echte `r68`-Assemblieren + `l68`-Link + Ausführung auf Q9-Flux. | Frontend+Backend grün, echtes Assemblieren/Linken/Ausführen offen |
| Integer-Suffixe | `U`, `L`, `UL` und die Schreibweise `LU` werden jetzt als Bestandteil einer Zahl erkannt; der Zahlenwert wird ohne Suffix ausgewertet. Die vollständige C-Typwirkung (unsigned/Rangregeln) und weitere Basen sind noch offen. | teilweise behoben |
| Komplexe VM-Strukturen | Pointer auf Strukturen, Arrayzugriffe und große `switch`-Blöcke treten in Q9-Run gemeinsam auf. Die Kombination ist noch nicht als unabhängige C89-Testreihe abgesichert. | offen, Minimaltests fehlen |
| Stufe-2-Messrezept | Erste Erhebung (2026-09-10) meldete 1566 bzw. 58 SEMERR -- **Messartefakt**: das Rezept strich ALLE `#include`-Zeilen vor dem Präprozessieren (auch `qrun_vm.h`/`qrun_ir.h`/`qrun_os9.h`), damit fehlten sämtliche Typ-/Funktionsdeklarationen (`unknown type name 'qrun_vm_t'`, `unknown function 'malloc'` usw.). Korrigiert: `cc -E -P -x c -DQRUN_OS9 -ISource` statt Zeilen zu streichen -- damit löst sich `qrun_os9.h` real auf. Ergebnis siehe Zeile darüber: beanstandungsfrei. | behoben 2026-09-10 |
| `sizeof` auf Pointertypen | **BEHOBEN 2026-09-15.** Die Größe hängt vom Ziel ab (68k 4, ARM64 8); das Frontend gibt sie jetzt symbolisch als `0+1P` aus, jedes Backend setzt sein `P` ein. Siehe den Zeigergrößen-Nachtrag in [ISO_C_GAP_LIST_de.md](ISO_C_GAP_LIST_de.md). | erledigt |
| `switch`-Gültigkeitsbereich | Eine Deklaration direkt in einem `case`-Rumpf (`case 1: int y;`) wird nicht auf den `switch`-Block begrenzt. **Nachgemessen 2026-09-15: kein Handlungsbedarf.** Die Form kommt in den 4109 Microware-Quellen **null mal** vor und in den eigenen Quellen ebenfalls nicht — sie ist in C89 auch gar nicht zulässig (ein Label muss auf eine Anweisung zeigen, nicht auf eine Deklaration). Block-Scoping selbst ist korrekt: `{ int y; }` gefolgt von `y` wird sauber als `unknown variable` gemeldet, auch im `case`. | offen, aber ohne praktische Bedeutung |
| `switch`-Fallthrough | **BEHOBEN 2026-09-15.** Vorher still falsch: der case-Rumpf sprang unbedingt ans switch-Ende, `switch(1){ case 1: r=1; case 2: r=r+2; break; }` lieferte 1 statt 3, mit `OK` und ohne Meldung. Jetzt springt der Rumpf in den nächsten RUMPF (nicht in den nächsten Test), und zwar hinter dessen `DROP` -- auf diesem Weg ist der switch-Wert schon vom Stapel. Acht Fälle in `runtests.sh`, Sollwerte diskriminierend (7 = 1+2+4 über drei Stufen). | erledigt |
| Zeigertabellen mit String-Literalen | **BEHOBEN 2026-09-15.** `char *tab[] = {"a","b"}` brach vorher STILL im Parser ab (`FAIL`, 0 Meldungen). Neuer IR-Opcode `GINITADDR`; auf OS-9 relokiert der Lader die Adressen über `M$IRefs`, dafür liegen solche Globals jetzt im vsect. Mitgekommen: Größe aus der Liste (`int a[] = {1,2,3}`). | erledigt |
| Verschachtelte Initialisiererlisten | **BEHOBEN 2026-09-15.** `int m[2][2]={{1,2},{3,4}}` brach vorher STILL ab. Das Array ist intern flach, die Klammern gliedern nur — aber eine zu kurze Zeile wird auf die Zeilenlänge **gepolstert**, weil `{{1},{2}}` in C `m[1][0]=2` heißt und nicht `m[0][1]=2`. Drei Ebenen und Schachtelung auf einem 1D-Array werden gemeldet. | erledigt |
| Komma-Operator im `for`-Kopf | **BEHOBEN 2026-09-15**: `for(i=0,j=n; ...; i++,j--)`. Vorher stiller Parse-Abbruch. | erledigt |
| Komma-Operator in der Anweisung | `i = 1, j = 2;` bleibt ein stiller Parse-Abbruch. Die Änderung dafür war gebaut (Aktion an `assignItem` statt `assignStmt`) und liess Suite und Host-Lauf grün, **brach aber den Selbsthost auf dem 68030**: der dort gebaute Compiler erzeugte `JZ L2080374908` — einen Müllwert als Labelnummer — und liess ein `STOREP` fallen. Auf dem Host fällt das nicht auf, weil frischer Speicher dort zufällig null ist. Zurückgenommen; der `for`-Kopf trägt den Nutzen (125 von 4109 Microware-Quellen), diese Form kaum. Die Ursache ist nicht gefunden.  **EINGEGRENZT 2026-09-15.** Symptom im stage2-Lauf: von den ZWEI Labels eines Ternärs ist nur eines Müll (`JZ L2080374908` statt `JZ L38`), das zweite (`L39`) ist korrekt — obwohl `tcTernaryFalse[]` und `tcTernaryDone[]` direkt nacheinander geschrieben werden und im Speicher nebeneinander liegen. Ein Überlauf von vorne träfe beide, ein falscher Index ebenso; es sieht also nach einem Codegen-Fehler beim Zugriff auf genau dieses Array aus, ausgelöst durch die veränderte Parser-Umgebung. **Ausgeschlossen sind:** die Ternär-Routinen selbst (ihr IR ist mit und ohne die Änderung byteidentisch), die Tabellengrenzen des Backends (472/1024 Funktionen, 1792/2048 Globals), eine zu tiefe `if`-Kette (140 aufeinanderfolgende `if`-Blöcke übersetzen korrekt) und fehlende Grenzprüfungen an den Stapeln `tcLogicDepth`/`tcBitDepth`/`tcCastDepth`/`tcTernaryDepth` (alle vier haben eine). Reproduzierbar: Aktion an `assignItem` statt `assignStmt`, dann `tools/test_selfhost_68k.sh`. **Weiter eingegrenzt (zweiter Anlauf):** Das fehlerhafte `JZ` in `tcTernaryBegin` nutzt die LOKALE Variable `falseLabel` direkt, nicht `tcTernaryFalse[]` — der Müllwert entsteht also schon dort, und das Array bekommt ihn nur weitergereicht. In derselben Zeile steht `TCType condition = tcTypePop(); int falseLabel, endLabel;`: eine Struct-Rückgabe per Wert, unmittelbar gefolgt von zwei Lokalen, von denen genau die ERSTE beschädigt wird. Der naheliegende Verdacht — die Struct-Kopie schreibt über ihr Ziel hinaus — ist jedoch **widerlegt**: derselbe Aufbau als Minimalprogramm (4-Byte-Struct per Wert zurückgegeben, danach `int f, e;`) läuft auf echtem 68030 korrekt. Der Fehler braucht also mehr Kontext als diese Form allein. **DRITTER ANLAUF — die Frage hat sich umgedreht: es ist ein FRONTEND-Fehler, kein Codegen-Fehler.** Gegenüberstellung des erzeugten 68k-Assemblers von `tcTernaryBegin` mit und ohne die Änderung (beide mit demselben Ausgabenamen erzeugt, sonst unterscheiden sich die Symbole): es fehlt **genau eine Instruktion**, `move.l (a7)+,-8(a5)` — der Store des Post-Inkrement-Ergebnisses in `falseLabel`. Der Vergleich der IR derselben Funktion bestätigt es an der Wurzel: dort fehlt **genau ein `STOREL 1`**. Das Frontend erzeugt den Store also gar nicht erst; das Backend übersetzt korrekt, was es bekommt. Die offene Frage ist damit nicht mehr „welcher Codegen-Pfad schreibt daneben", sondern **„warum feuert `tc_assign` für diese eine Zuweisung nicht"** — vermutlich eine Aktion, die beim Zurückrollen eines Parse-Pfades aus dem Protokoll fällt und nicht wieder eingetragen wird. Ein Minimalprogramm reproduziert es NICHT (auch nicht mit Struct-Rückgabe, zwei Lokalen in einer Deklaration und zwei `if` davor) — es braucht den echten Parser-Kontext. **VIERTER ANLAUF — EIN-ZEILEN-REPRODUZIERER, und meine bisherige Deutung war falsch.** Mit dem richtigen Kriterium (beide Parser auf dieselbe Eingabe, IR vergleichen) schrumpft der Fall von 310 KB auf eine Zeile: `char a; char b; char c; int main(){ a = b = 0; c = 1; putint(c); }` — ohne die Änderung 3 Stores und Ausgabe 1, mit der Änderung 2 Stores und Ausgabe 0. **Eine Kettenzuweisung, gefolgt von einer einfachen Zuweisung: die zweite verliert ihr `STOREGC`.** Jede Form für sich ist korrekt. Damit ist auch klar, dass die Änderung den Fehler EINFÜHRT und nicht bloß einen latenten Defekt weckt — ohne sie ist alles richtig. Die Alternativenreihenfolge in `assignStmt` zu tauschen (Kommaliste vor `chainAssign`) behebt es NICHT. Die Ursache liegt also im Zusammenspiel von Aktionsprotokoll und Backtracking: nach einem erfolgreichen `chainAssign` feuert `tc_assign` an `assignItem` für die nächste Zuweisung nicht mehr.| offen, Ursache unklar |
| `union` | **UMGESETZT 2026-09-15.** Vorher war das Schlüsselwort unbekannt (stiller Parse-Abbruch). Eine `union` wird als `struct` REGISTRIERT, deren Felder alle auf Offset 0 liegen und deren Größe das größte Feld ist — damit erbt sie Feldzugriff, `->`, Ganzkopie, Parameter, Rückgabe und Arrays, ohne dass davon etwas neu gebaut wurde. **Die Byte-Reihenfolge bei gemischten Feldgrößen ist implementation-defined**: der 68k ist big-endian, das VM-Orakel little-endian; `u.i=5; u.c[0]` gibt dort 0 und hier 5. Fall 42 in `tools/test_struct_68k.sh` prüft das auf dem Ziel, `runtests.sh` bleibt bewusst endian-neutral. | erledigt |
| `volatile` | **AKZEPTIERT 2026-09-15** an denselben Stellen wie `const` (lokal, global, `static`, Parameter, Struct-Feld). Eine eigene Wirkung hat es nicht — und das ist **nachgerechnet, nicht angenommen**: QCC hält keine Werte über Anweisungsgrenzen in Registern, und `-peephole` faltet nur Verkehr auf dem Auswertungsstapel (A7), keine Variablenzugriffe. Die Suite zählt das am erzeugten Assembler nach (zwei Lesezugriffe bleiben zwei, auch mit `-peephole`). Wer ein Peephole-Muster ergänzt, das Variablenzugriffe zusammenfasst, lässt genau diesen Test anschlagen. | erledigt |
| Stringverkettung | **BEHOBEN 2026-09-15.** `"ab" "cd"` ist jetzt ein String (C89 3.1.4), auch über Zeilenumbrüche — der Fall, für den lange Meldungstexte in den eigenen Werkzeugen bisher einzeilig geschrieben werden mussten. Vorher ein stiller Parse-Abbruch ohne Zeilenangabe. **Der Zwischenraum musste ausdrücklich in die Regel**: `stringLit` steht in der TOKEN-Liste, und TOKEN-Regeln überspringen keinen Leerraum — ohne `stringSep` ging nur `"ab""cd"`. | erledigt |
| Explizite `enum`-Werte | **BEHOBEN 2026-09-15.** `enum E { A = 5, B }` — B ist 6. Der Wert setzt den Zähler neu, die folgenden Konstanten zählen von dort weiter; negative Werte gehen. Vorher stiller Parse-Abbruch. | erledigt |
| Anonyme `typedef enum` | **BEHOBEN 2026-09-15.** `typedef enum { A, B } Flags;` und mit Tag. Der Tag wird wie beim `struct` geparst und verworfen — als Name dient der typedef-Zielname. | erledigt |
| Funktionszeiger als lokale Variable **und als Parameter** | **BEHOBEN 2026-09-15.** `int (*fp)(int);` ohne den Umweg über ein `typedef`, auch mit `void`-Rückgabe und mehreren Parametern. Vorher stiller Parse-Abbruch. Die beiden Parameterlisten schachteln dabei ineinander; das trägt, weil die äußere über `tc_param` in `tcLocalTypes` sammelt und die innere über `tc_externparam` in `tcExternBuildParamTypes` — zwei getrennte Puffer. **Weiterhin offen** sind die zwei übrigen Stellen: als globale Variable (der Rohtext-Parser in `tcGlobalOne` kennt die Form nicht) und als Struct-Feld; dort führt der `typedef`-Weg (`typedef int (*FP)(int);`) seit jeher zum Ziel und funktioniert unverändert. | teilweise |
| Bitfelder und `long long` | **Werden seit 2026-09-15 GEMELDET statt still abzubrechen.** Umgesetzt sind beide bewusst nicht: Bitfelder bräuchten Bitpacking im Layout **und** maskierten Zugriff an jeder Feldzugriffsstelle — für 5 von 4109 Microware-Quellen; `long long` braucht 64-Bit-Hilfsroutinen für Multiplikation und Division im Backend (76 von 4109 Quellen, überwiegend Zeitstempel). Die Grammatik **liest** beide Formen jetzt, damit die Aktion sie ablehnen kann: bei einem Parse-Abbruch läuft keine Aktion und es gibt folglich keine Meldung. | gemeldet, nicht umgesetzt |
| `goto` | **BEHOBEN** (nachgemessen 2026-09-15): rückwärts (Schleife) und vorwärts (Sprung ans Ende) liefern beide den richtigen Wert. | erledigt |
| Anonyme Enums | `typedef enum { A, B } Flags;` wird noch nicht akzeptiert; benannte `enum`-Typen und Konstanten funktionieren. | bekannte Frontend-Lücke |

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

## Nachtrag 2026-09-16 — zwei stille Abbrüche, beim Gleitkomma-Konverter gefunden

Beide kamen nicht aus einer Durchsicht, sondern daraus, dass echter Code
übersetzt werden sollte: der Dezimal-nach-IEEE-754-Konverter
(`tools/dec2ieee.c`). Das ist das verlässlichere Verfahren — eine Lücke,
über die niemand stolpert, findet man durch Lesen nicht.

### behoben: Hex und Suffixe in globalen Initialisierern

`unsigned long t[] = { 0x7FF00000UL };` scheiterte **still**. Die Grammatik
hatte mit `initNumber = digit { digit }` eine eigene, ärmere Zahlregel als
der Ausdruck, und der Rohtext-Auswerter `tcInitList` las ebenfalls nur
Dezimalziffern. Beides ist jetzt an `number` angeglichen (Hex, `U`/`L`/`UL`),
mit acht Testfällen, deren Sollwerte die dezimale Lesart ausschließen —
`0x10` muss 16 ergeben, nicht 10.

Das traf nicht nur Gleitkomma: Hexkonstanten in Tabellen sind in jedem
Systemcode üblich, Masken und Bitmuster stehen praktisch nie dezimal da.

### offen: `static` an lokalen Variablen mit struct- oder Array-Typ

`static struct S s;` **in einer Funktion** scheitert still. Skalare
static-Locals gibt es seit 2026-09-14 (`tc_staticlocal`), struct und Array
fehlen dort ausdrücklich. Im Konverter war das umgehbar, indem die großen
Zwischenwerte auf Dateiebene liegen — wo sie ohnehin hingehören, denn jeder
ist gut ein Kilobyte groß und der Stack eines OS-9-Moduls ist knapp.
Umgehbar heißt aber nicht harmlos: das Scheitern ist stumm.
