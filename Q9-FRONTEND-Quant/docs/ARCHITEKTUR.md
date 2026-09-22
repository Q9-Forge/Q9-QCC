# ARCHITEKTUR -- Weg zur korrekten Codeerzeugung (68k zuerst)

Stand: 2026-07-19 (Nacht-Session). Endziel laut Nutzer: **korrekte Codeerzeugung fuer die
gegebene Grammatik, zuerst 68k-Assembler.**

## 1. Ausgangslage: was die flache Tabelle kann und was nicht

Die Sprungtabelle (PARSER-TABELLE) + Stack-Maschine (`execFrom()`) funktioniert und ist
per TESTS-Bloecken abgesichert. Sie hat aber eine **inhaerente** Grenze: Ruecksetzpunkte
fuer die Eingabeposition existieren nur an NTS-Aufruf-Grenzen (der native Aufrufstack
uebernimmt das Sichern/Verwerfen). Innerhalb einer Regel gibt es keine Positionssicherung
pro Alternative. Konsequenzen (in parsec.cpp gefixt bzw. gewarnt, Ver. 2.10):

- Fehlschlag-ohne-Konsum (STAT_FALSE) vs. committed (STAT_ERROR) wird jetzt beim
  Erzeugen positionsabhaengig verdrahtet (Bugs 5+6, siehe context.txt Abschnitt 11).
- Der Rest-Fall bleibt prinzipbedingt: konsumiert eine ueberspringbare Gruppe doch
  Eingabe und ein Folge-Faktor scheitert, springt die Maschine ohne Ruecksetzung weiter
  (`s = [ "-" ] "a" | "b" .` akzeptiert "-b"). Wird beim Erzeugen klar **gewarnt** (ambigF).
- Ausserdem ist die Tabelle "PEG-committed": nach dem ersten konsumierten Zeichen einer
  Alternative wird nie mehr eine andere Alternative versucht (`"a" "b" | "a" "c"` erkennt
  "ac" nicht).

Fazit: Die Tabelle bleibt als **ausfuehrbares Arbeits-/Debug-Artefakt** und fuer die
TESTS erhalten. Die **Codegenerierung geht einen eigenen, korrekteren Weg**.

## 2. Entscheidung: AST als zweite Datenquelle (additiv, riskolos)

Der Parser in parsec.cpp baut waehrend des normalen Parsens **zusaetzlich** einen AST auf
(Knotenarten: SEQ, ALT, OPT `[..]`, REP `{..}`, TS-Literal, RNG-Bereich, NTS-Referenz;
`(..)` ist im AST einfach der Inhalt selbst). Die bestehende Tabellen-Erzeugung bleibt
**unangetastet** -- der AST haengt sich nur mit wenigen Aufrufen (`astPush*/astGroup*`)
in die vorhandenen Parser-Funktionen ein. Kein Umbau, kein Regressionsrisiko.

Der AST ist die richtige Quelle fuer Codegenerierung, weil rekursiver Abstieg
**strukturell** ist: jede Regel wird eine Funktion/Subroutine, jede Alternative ein
"Position sichern -> versuchen -> bei Misserfolg zuruecksetzen"-Baustein. Damit
verschwindet die Tabellen-Grenze von selbst.

## 3. Semantik des erzeugten Codes (bewusst festgelegt)

Backtracking-rekursiver-Abstieg mit geordneter Auswahl:

- **Sequenz**: Faktoren nacheinander; scheitert einer, scheitert die Sequenz
  (Ruecksetzung uebernimmt der umgebende Auswahlpunkt).
- **Alternative `a | b`**: Position sichern, `a` versuchen; bei Misserfolg Position
  zuruecksetzen und `b` versuchen. -> `"a" "b" | "a" "c"` erkennt auch "ac"
  (besser als die Tabelle) und `[ "-" ] "a" | "b"` akzeptiert "-b" NICHT mehr.
- **Option `[x]`**: Position sichern, versuchen; Misserfolg = zuruecksetzen, weiter.
- **Wiederholung `{x}`**: greedy; pro Iteration Position sichern, erste fehlgeschlagene
  Iteration wird zurueckgesetzt, Schleife endet. KEIN Backtracking in fruehere
  Iterationen hinein (PEG-Verhalten, dokumentierte Entscheidung).
- **Regel (NTS)**: Funktion; Misserfolg stellt die Eingabeposition ihres Aufrufs wieder her.
- Linksrekursive Grammatiken werden weiterhin vorab erkannt und abgelehnt.

Anmerkung: Das ist "geordnete Auswahl mit lokalem Backtracking" (PEG-artig), nicht volle
CFG-Mehrdeutigkeitsaufloesung -- fuer Wirth-artige Programmiersprachen-Grammatiken die
etablierte, richtige Wahl (so arbeiten auch handgeschriebene rekursive Abstiegsparser).

## 4. Zwei Backends aus demselben AST-Walker

1. **68k-Assembler** (`<basis>.s68`) -- das eigentliche Ziel. Motorola-Syntax
   (vasm-kompatibel, keine Assembler-Spezialitaeten; OS-9/r68-`psect`-Rahmen kann
   spaeter per Option ergaenzt werden).
   Registerkonvention der erzeugten Subroutinen:
   - `a0` = Eingabezeiger (NUL-terminierte Eingabe), laeuft bei Erfolg mit
   - `d0.b` = 1 Erfolg / 0 Misserfolg (bei Misserfolg ist `a0` unveraendert)
   - `d1` = Scratch (Bereichs-/Zeichenvergleiche), Ruecksetzpunkte auf dem Stack `-(a7)`
   - pro Regel `<name>` eine Subroutine `p_<name>`; Einstieg `parse` = Startregel
   - TS-Literale vergleichen erst ALLE Zeichen (mit Offsets), konsumieren dann in einem
     Schritt -> ein TS konsumiert nie teilweise.
2. **C** (`<basis>_p.c`) -- semantischer **Zwilling zur Validierung**: gleicher Walker,
   gleiche Struktur, aber auf dem Mac sofort kompilier- und testbar. `runtests.sh`
   kompiliert den erzeugten C-Parser und jagt DIESELBEN TESTS-Bloecke hindurch --
   damit ist die Codegen-Semantik automatisch abgesichert, auch ohne 68k-Emulator.
   (Der 68k-Code entsteht aus demselben Walker; strukturgleiche Templates.)

Erzeugt wird nur im Fall A (echte .ebnf geparst) und nur bei fehlerfreier,
nicht-linksrekursiver Grammatik.

## 5. Bausteine / Code-Templates (68k)

Jeder AST-Knoten wird mit einem "Fehlschlag-Label" als Parameter generiert
(`genNode(node, failLabel)`); Erfolg faellt durch. Auswahl/Option/Wiederholung
sichern `a0` auf dem Stack und raeumen ihn auf JEDEM Pfad korrekt ab:

```
; TS "ab":                          ; RNG "a"~"z":            ; NTS ref:
    cmpi.b  #'a',(a0)                   move.b  (a0),d1           bsr     p_ref
    bne     Lfail                       cmpi.b  #'a',d1           tst.b   d0
    cmpi.b  #'b',1(a0)                  blo     Lfail             beq     Lfail
    bne     Lfail                       cmpi.b  #'z',d1
    addq.l  #2,a0                       bhi     Lfail
                                        addq.l  #1,a0

; ALT (a|b):                        ; OPT [x]:                ; REP {x}:
    move.l  a0,-(a7)                    move.l  a0,-(a7)      Lrep:
    <a, fail->L1>                       <x, fail->L1>             move.l  a0,-(a7)
    bra     Lok                         addq.l  #4,a7             <x, fail->L1>
L1: move.l  (a7),a0                     bra     L2                addq.l  #4,a7
    <b, fail->L2>                   L1: move.l  (a7)+,a0          bra     Lrep
    bra     Lok                     L2:                       L1: move.l  (a7)+,a0
L2: addq.l  #4,a7
    bra     Lfail                   ; Regel p_<name>:
Lok:addq.l  #4,a7                   ;   Position sichern, Body, bei Fail restaurieren,
                                    ;   d0=0/1, rts
```

## 6. Umsetzungsstand in dieser Session

- [x] AST-Aufbau parallel zum Parsen (Source/codegen.cpp/.h, Hooks in parsec.cpp)
- [x] C-Backend (`<basis>_p.c`) als semantischer Zwilling
- [x] 68k-Backend (`<basis>.s68`)
- [x] runtests.sh validiert den erzeugten C-Parser gegen alle TESTS-Bloecke
- [x] tools/s68sim.py: Mini-Simulator fuer die emittierte 68k-Teilmenge -- der .s68-TEXT
      wird in der Suite wirklich ausgefuehrt (gleiche TESTS wie der C-Zwilling; ein
      unbalancierter a7-Stack im generierten Code ist ein harter Simulationsfehler)
- [x] [LEXER]-Block (scannerless, Abschnitt 8): Oberon-0-Programme MIT Leerzeichen und
      Zeilenkommentaren laufen in C und simuliertem 68k identisch
- [x] Generator-Schutz: nullable Wiederholungsrumpf wird vor der Ausgabe abgelehnt
      (verhindert eine Endlosschleife in C und 68k); bereinigte Regelnamen duerfen
      nicht kollidieren.
- [x] Arbeitsdatei: ein roher, beim Neu-Erzeugen erhaltener `[NUTZER-CODE]`-Block
      ist als stabiler Ort fuer spaetere Aktionen vorbereitet (noch keine Einbindung).
- [x] Data/oberon0.ebnf vollstaendig gemacht (Codegen einer echten Sprachgrammatik)
      -- oberon0 hatte nur 4 fehlende Regeln (ident/integer/selector/ActualParameters
      referenzieren letter/digit, die auskommentiert/vorhanden sind -- siehe Quelle)
      [Status am Session-Ende ggf. in context.txt Abschnitt 12 nachlesen]

## 7. OFFEN (naechste Schritte, in sinnvoller Reihenfolge)

1. ~~Lexer~~ **ERLEDIGT** als scannerless [LEXER]-Block (siehe Abschnitt 8) -- loest
   auch ToDo 9b (Wortzwischenraum) und den Wortgrenzen-Fall. Blockkommentare:
   `COMMENT BLOCK = "(*" "*)"` (flach) bzw. `COMMENT BLOCK NESTED = ...`
   (geschachtelt, Oberon/Modula-2; Tiefenzaehler, im 68k-Code in d2 -- d2 wird
   dann zusaetzlich zerstoert). Unterminierte Kommentare laufen bis zum Eingabeende.
   ~~Mehrere gleichzeitige Kommentar-Marker~~ **ERLEDIGT** (2026-07-20, autonome
   Loop-Session): `lexParseConfig()` sammelt jetzt bis zu `LEX_MARKERS_MAX` (4)
   `COMMENT LINE`- UND `COMMENT BLOCK`-Zeilen gleichzeitig (z.B. `#` UND `//` als
   Zeilenkommentar, `/* */` UND `(* *)` als Blockkommentar in derselben Grammatik).
   `ws()` probiert in BEIDEN Backends alle konfigurierten Marker der Reihe nach:
   im C-Backend einfach mehrere `if`-Zweige im selben Schleifenrumpf; im 68k-Backend
   eine verkettete Pruef-Kaskade (jeder Marker hat einen eigenen Eintrittslabel,
   Mismatch faellt zum naechsten Marker durch, voller Match springt bei Zeilen-
   kommentaren zu einem GEMEINSAMEN "bis Zeilenende"-Rumpf, bei Blockkommentaren
   -- weil jeder Marker seine eigene Ende-Sequenz hat -- in eine eigene Schleife).
   Verifiziert mit Test/multicomment (runtests.sh Abschnitt 5c): alle vier Marker
   gleichzeitig funktionieren identisch in C-Zwilling UND simuliertem/echt
   assembliertem 68k-Code; die komplette Suite (inkl. oberon0s geschachtelten
   Blockkommentaren, weiterhin nur EIN Marker) blieb dabei durchgehend gruen.
2. **Nutzer-Code-Bloecke pro NTS -- ERSTE AUSBAUSTUFE ERLEDIGT** (2026-07-20):
   `actionsParseConfig()` (codegen.cpp) parst den [NUTZER-CODE]-Block der
   Arbeitsdatei nach dem in Abschnitt 9 festgelegten Format (`ACTION AFTER <regel>
   CALL <name>` + `ROUTINE C <name> ... END` + `ROUTINE M68K <name> ... END`).
   Beide Backends rufen die konfigurierte Routine direkt nach Erfolg der Regel auf
   (C: `entry`/`p` als start/end-Zeiger; 68k: `bsr <name>` mit a0=Ende, Routine muss
   a0 erhalten). Verifiziert mit Test/actiontest (ebnf+lextab+runtests.sh Abschnitt
   7b): C-Zwilling ruft die Routine mit korrektem Text auf, 68k-Code assembliert
   real (vasm) und laeuft im Simulator (s68sim) ohne Stack-Leck.
   Bewusste Grenzen dieser ersten Stufe (siehe Abschnitt 9 fuer die Fortsetzung):
   - Kein Wertrueckgabekanal -- eine Aktion kann (noch) kein Ergebnis an die
     aufrufende Regel zurueckgeben (z.B. einen Ausdruckswert). Nur Seiteneffekte.
   - 68k-Aufrufkonvention bekommt NUR das Regelende (a0), nicht den Regelanfang
     (anders als C mit entry+p) -- der Rueckstellpunkt liegt zwar auf `(a7)`, wird
     aber nicht in ein Register geladen (haette einen weiteren Registerbedarf
     bedeutet, siehe Diskussion in Abschnitt 9).
   - ROUTINE-Koerper werden woertlich in die generierte Datei kopiert (kein
     Linker noetig, bleibt beim bisherigen "eine Datei, echter Assembler prueft
     sie" Prinzip) -- fuer eine Sprache mit vielen Aktionen wird das irgendwann
     unhandlich; ein spaeterer Schritt koennte ROUTINE-Koerper aus separaten
     Dateien einlesen.
   - Kein IR/Stack-Maschine dahinter -- die Routinen sind aktuell rohe C-/68k-
     Fragmente, kein gemeinsamer Zwischencode (Entscheidung dazu siehe Abschnitt 9).
   WAISEN-Regel bei Regelumbenennung ist weiterhin nur BESCHLOSSEN, nicht
   implementiert (ACTION-Zeilen mit unbekannter Regel werden aktuell schlicht mit
   Warnung ignoriert, nicht als WAISE markiert und erhalten).
3. **68k-Code real assemblieren**: ERLEDIGT -- tools/vasmm68k_mot (aus den vasm-
   Originalquellen gebaut) assembliert in der Suite alle erzeugten .s68 fehlerfrei
   (Motorola-Syntax, -m68000). Zusaetzlich fuehrt tools/s68sim.py (Mini-Simulator der
   emittierten Instruktions-Teilmenge, typisierter a7-Stack) den .s68-Text end-to-end
   gegen die TESTS aus. Noch offen: Lauf auf echter Hardware bzw. im Q9-/OS-9-Umfeld
   (r68 via REF/Wine, psect-Rahmen, siehe Punkt 4).
4. ~~psect-Rahmen~~ **ERLEDIGT**: [CODEGEN]-Block mit `M68K OS9` (+ optional
   `M68K PSECT = name`) erzeugt zusaetzlich `<basis>_os9.a` im Microware-r68-Format
   (nam/psect/ends, '*'-Kommentarzeilen); runtests.sh assembliert es mit dem echten
   r68 via Wine/REF. Noch offen daran: l68-Link + Lauf als OS-9-Modul.
5. Spaeter: C-Programm-Skeleton mit main()/Testtreiber aus TESTS-Block, Web-Oberflaeche
   (bewusst zurueckgestellt).

## 8. Lexer: explizite Konfiguration ist notwendig -- UMGESETZT (scannerless)

Die urspruengliche Idee, lexikalische Regeln allein aus dem Regelgraphen zu erkennen,
reicht nicht aus. Eine Regel wie `ident = letter { letter | digit }` und eine kleine
syntaktische Regel wie `IdentList = ident { "," ident }` sehen strukturell gleich aus:
beide sind azyklisch und bestehen nur aus Terminalen/NTS. Der Generator kann daher nicht
korrekt raten, welche Regel ein Token darstellen soll.

**Umgesetztes Format** (Arbeitsdatei-Block, roh erhalten wie NUTZER-CODE, geparst von
`lexParseConfig()` in codegen.cpp):

```
[LEXER]
WHITESPACE = " \t\r\n"
TOKEN ident
TOKEN integer
COMMENT LINE = "//"
COMMENT BLOCK = "(*" "*)"
[ENDE]
```

**Umgesetztes Design: scannerless statt separatem Token-Puffer.** Es gibt keinen
eigenen Scanner-Pass; stattdessen erzeugt der Generator drei kleine Mechanismen direkt
im Parser (C und 68k strukturgleich, per Suite gegeneinander getestet):

1. `ws()`-Helfer: ueberliest WHITESPACE-Zeichen und `COMMENT LINE`-Zeilenkommentare.
   Wird vor jedem Terminal und vor jedem Aufruf einer lexikalischen Regel aus
   syntaktischem Kontext eingefuegt (und einmal am Ende fuer trailing Whitespace).
2. Der transitive Abschluss der `TOKEN`-Wurzeln ist "lexikalisch": innerhalb dieser
   Regeln wird NICHTS uebersprungen -- Token-Zeichen muessen adjazent sein.
3. Wortartige Literale (`"MODULE"`, `"IF"`, ...) erhalten im syntaktischen Kontext einen
   Wortgrenzen-Check via `idch()` (`MODULEX` ist nicht `MODULE` + `X`).

Eine KEYWORDS-Liste ist damit ueberfluessig: Schluesselwort-vs-Bezeichner loest das
Backtracking der geordneten Auswahl (probiert `assignment` faelschlich `ident="IF"`,
scheitert `:=` und die naechste Alternative `ifStatement` uebernimmt an der alten
Position). Laengere Operatoren vor kuerzeren (`"<="` vor `"<"`) bleiben Aufgabe der
Grammatik-Reihenfolge (in oberon0.ebnf so dokumentiert). Ein spaeterer echter
DFA-Scanner (laengster Treffer) bleibt als Option offen, ist aber fuer die
Wirth-Grammatiken nicht mehr noetig.

## 9. Nutzer-Code und semantische Aktionen

### 9.1 Die drei Nutzerentscheidungen (Stand 2026-07-20)

Vor der ersten Ausbaustufe (Abschnitt 7, Punkt 2) waren drei Entscheidungen noetig,
alle in der Nacht-Session vom 2026-07-20 getroffen:

1. **Kommentarregeln**: der `[LEXER]`-Block existiert bereits (Abschnitt 8).
   Erweiterungswunsch (mehrere Marker gleichzeitig, z.B. `#`/`//` und `/* */`/`(* *)`)
   ist erfasst, aber noch NICHT umgesetzt -- siehe Abschnitt 7 Punkt 1.
2. **Zwischenergebnis der Aktionen**: abgewogen wurden eine Stack-Maschine
   (PL/0-/P-Code-Stil -- entkoppelt C- und 68k-Backend sauber, beide Backends
   bleiben strukturgleich und gegeneinander testbar, kostet aber Codequalitaet/
   Registerzuteilung) gegen direkten AST-zu-Register-Code (Wirths spaeterer Stil
   mit "Item"-Deskriptoren -- effizienter, aber jedes Backend braucht eigene
   Ausdruckslogik, schwerer synchron zu halten). ENTSCHEIDUNG: noch offen,
   zunaechst zurueckgestellt zugunsten eines kleineren ersten Schritts (9.2/9.3).
   Der AST-zu-Register-Ansatz gilt als vermutlich effizienter und wahrscheinlicher
   Favorit, sobald eine echte IR-Schicht ansteht.
3. **Laufzeit-ABI**: OS-9/REF bevorzugt (nicht freistehender 68000-Code) --
   passt zum bereits vorhandenen r68/psect-Pfad (Abschnitt 7 Punkt 4). Wirkt sich
   erst aus, sobald ROUTINE M68K-Koerper echte I/O oder Speicheranforderung
   brauchen (Trap-Aufrufe wie `I$Write`, `F$SRQMEM` statt freistehendem Code).

### 9.2 Wie der nicht-automatisierbare Teil klein gehalten wird

Was eine Grammatikregel semantisch bedeutet (z.B. "Factor nach einem '*' heisst
Multiplikation"), kann das Tool nicht erraten -- das muss der Nutzer festlegen.
Um diesen Anteil klein zu halten, ist er von der Backend-Frage getrennt:

- Der Nutzer schreibt pro Regel GENAU EINE `ACTION AFTER <regel> CALL <name>`-Zeile
  (backend-neutral, kein doppeltes C-/68k-Aktionspaar wie im alten Entwurf oben).
- Was `<name>` konkret tut, steckt in einer kleinen Laufzeitbibliothek, die (anders
  als die Grammatik) nicht pro Sprache neu erfunden wird: `ROUTINE C <name>` und
  `ROUTINE M68K <name>` im selben [NUTZER-CODE]-Block.
- Wiederkehrende Muster (linksassoziative Operator-Schleifen wie
  `X = Y {("+"|"-") Y}.`, Kontrollfluss-Schleifen, die strukturell den bereits
  vorhandenen `{..}`/`[..]`-Codegen-Bausteinen entsprechen, Variablen-/Typverwaltung
  ueber eine generische Symboltabelle) sind Kandidaten fuer spaeter fertige
  Templates -- ABSICHTLICH zurueckgestellt, bis sie sich an einer echten Grammatik
  (oberon0 komplett von Hand durchgezogen) als tatsaechlich wiederkehrend
  bestaetigt haben, statt vorab zu spekulieren.

### 9.3 Umgesetztes Format (erste Ausbaustufe, siehe Abschnitt 7 Punkt 2)

```
ACTION AFTER <regel> CALL <name>

ROUTINE C <name>
void <name>(const char* start, const char* end) { /* ... */ }
END

ROUTINE M68K <name>
<name>:		; a0 = Ende des erkannten Textes, MUSS erhalten bleiben; d0/d1/d2 frei
	rts
END
```

- `ACTION AFTER <regel> CALL <name>`: loest `<name>` direkt nach Erfolg von `<regel>`
  aus, in BEIDEN Backends (fehlt eine der beiden ROUTINEn, wird nur eine Warnung
  ausgegeben, kein Abbruch -- so kann ein Backend zuerst entwickelt werden).
- `ROUTINE C`/`ROUTINE M68K`-Koerper werden WOERTLICH in die generierte Datei
  kopiert (kein Linker noetig -- passt zum bisherigen Ein-Datei-Prinzip: `cc`
  kompiliert `<basis>_p.c` weiterhin allein, `vasm`/`r68` assemblieren `<basis>.s68`
  weiterhin allein).
- C-Aufrufkonvention: `start`/`end` = Anfang/Ende des erkannten Textes (identisch zu
  `entry`/`p` in der generierten Regelfunktion).
- 68k-Aufrufkonvention (bewusst schmaler als C, siehe Abschnitt 7 Punkt 2): nur
  a0 = Ende wird uebergeben, kein Regelanfang-Register. Der Rueckstellpunkt liegt
  zwar auf `(a7)`, wird aber nicht extra in ein Adressregister geladen.
- Verifiziert an Test/actiontest (`number = digit {digit}.`, Aktion sammelt den
  erkannten Zifferntext) -- runtests.sh Abschnitt 7b (C-Zwilling: Routine wird mit
  korrektem Text aufgerufen) + Abschnitt "s68sim actiontest" (68k-Aufruf laeuft im
  Simulator ohne Stack-Leck) + vasm-Check (echter Assembler akzeptiert den `bsr`).

### 9.4 Erster echter Werttest: Test/calcexpr (2026-07-20)

Um die Template-Idee aus 9.2 ("linksassoziative Operator-Schleife") nicht laenger nur
zu behaupten, sondern zu pruefen, wurde `expr = term {addop term}. term = factor
{mulop factor}. factor = number.` (klassische Praezedenzkletterung, ABSICHTLICH ohne
Klammern/Rekursion -- siehe Grenze unten) komplett mit ACTION/ROUTINE C durchgespielt:

- Jede Regel bekommt eine `ACTION AFTER <regel> CALL ...`-Zeile; die ROUTINE-C-Koerper
  fuehren einen GLOBALEN Werte-Stack (`calcStack`/`calcSp`) plus zwei "pending operator"-
  Variablen -- der einzig verfuegbare Behelf, weil der aktuelle Mechanismus (9.3) KEINEN
  Wertrueckgabekanal hat. `number` pusht den geparsten Wert; `addop`/`mulop` merken sich
  das Operatorzeichen; `term`/`factor` verrechnen die letzten zwei Stack-Werte NUR, wenn
  ein Operator ansteht (bei der ERSTEN Iteration ist er das nicht -- eleganter Nebeneffekt:
  dieselbe Aktion dient unveraendert als No-Op fuer den allerersten Faktor/Term).
  `expr`s Aktion druckt/entnimmt das Endergebnis.
- FUNKTIONIERT: `2+3*4` = 14 (Praezedenz korrekt), `10-2-3` = 5 (linksassoziativ, nicht
  11) -- verifiziert in runtests.sh Abschnitt 7c.
- NUR C-Backend (kein ROUTINE M68K) -- bewusste Scope-Entscheidung fuer diesen ersten
  Versuch, siehe 9.3 fuer die 68k-Aufrufkonvention, die fuer echte Werte (nicht nur
  Text-Spannen) ohnehin eine Registerkonvention bräuchte, die es noch nicht gibt.
- GRENZE zum Zeitpunkt des ersten Tests, bewusst nicht mitgetestet: KEINE Klammern/
  Rekursion (`factor` rief NICHT `expr` auf). Grund: Aktionen feuerten damals SOFORT
  beim Erfolg einer Regel, aber ein Backtracking-Parser kann eine bereits erfolgreiche
  Teilregel spaeter DOCH verwerfen, wenn eine umschliessende Alternative insgesamt
  scheitert und zurueckspringt (Positions-Rollback existiert, ein Rollback fuer bereits
  ausgefuehrte Aktions-NEBENEFFEKTE existierte NICHT). **Diese Grenze ist seit dem
  gleichen Tag durch das Aktions-Log (siehe unten) behoben und auch mit Rekursion
  verifiziert -- nicht mehr aktuell, hier nur als Chronik stehen gelassen.**
- NEBENFUND: Das Testen dieser Grammatik deckte einen eigenstaendigen, vorbestehenden
  Bug im TABELLEN-Generator auf (nichts mit ACTION/Codegen zu tun) -- `rule()` in
  parsec.cpp prüfte am Regelende nur die LETZTE Tabellenzeile auf offene Vorwaertsreferenzen
  statt die ganze Regel. Bei `X = A {B A}.` als letztem Konstrukt einer Regel sitzt die
  offene Referenz auf der B-Zeile, nicht der letzten A-Zeile. Gefixt: `rule()` merkt sich
  jetzt `ruleStart` und scannt beim Regelende die GANZE Regel.

### 9.4b Aktions-Rollback bei Backtracking -- gefixt (2026-07-20, C-Backend)

Die oben beschriebene Grenze wurde noch am selben Tag behoben, nachdem eine gezielte
Reproduktion (Test/actionrollback: `stmt = tag "1" | tag "2". tag = "T".`) bestaetigte,
dass sie real und beobachtbar ist: bei Eingabe "T2" feuerte `note_tag` VOR dem Fix
zweimal (einmal fuer die verworfene erste Alternative `tag "1"`, einmal fuer die
gewinnende zweite `tag "2"`), obwohl `tag` im Endergebnis nur einmal vorkommt.

**Loesung (nur C-Backend, siehe unten fuer 68k): Aktionen protokollieren statt sofort
ausfuehren.** Statt eine Regel-Aktion sofort beim Erfolg aufzurufen, wird sie nur in
ein globales Log gepusht (`actionLogPush(fn, start, end)`); ERST nach bestaetigtem
GESAMTERFOLG des kompletten Parse-Vorgangs (voller Input erkannt) wird das Log der
Reihe nach abgespielt (`actionLogReplay()`). Damit ein abgebrochener Backtracking-Pfad
seine bereits geloggten Aktionen nicht im Endergebnis hinterlaesst, laeuft ein zweiter,
zu `sv[]` (Positions-Sicherung) paralleler Stack `svLog[]` mit: an JEDEM Punkt, an dem
`p` gesichert/zurueckgesetzt wird (ALT/OPT/REP-Einstieg bzw. -Ruecksetzung UND der
Misserfolgspfad einer ganzen Regel ueber `entryLog`), wird `actionLogLen` im Gleichschritt
mitgesichert/zurueckgesetzt. Verifiziert:
- Test/actionrollback (flache Alternative mit gemeinsamem Praefix): `note_tag` feuert
  bei "T2" jetzt korrekt nur 1x (runtests.sh Abschnitt 7d).
- Test/actionrollback2 (`expr2 = "(" expr2 ")" "A" | "(" expr2 ")" "B" | leaf.`,
  ECHTE Rekursion): `note_leaf` feuert bei "(x)B" trotz Verschachtelung korrekt nur 1x
  (Abschnitt 7e) -- bestaetigt, dass der Mechanismus durch rekursive Regelaufrufe
  hindurch korrekt funktioniert (jeder Regelaufruf, auch verschachtelt, sichert/
  restauriert seinen EIGENEN `entryLog`-Ausschnitt unabhaengig von der Verschachtelungs-
  tiefe -- keine Sonderbehandlung fuer Rekursion noetig).
- Komplette Suite (55 Checks) bleibt gruen, alle bestehenden Grammatiken/Aktionen
  (actiontest, calcexpr) unveraendert korrekt (Replay-Reihenfolge = urspruengliche
  Feuerreihenfolge fuer jeden PARSE, der nicht zurueckgesetzt wird).
- NEBENFUND beim Bau der Reproduktionen: BEIDE Testgrammatiken (`tag "1" | tag "2"`
  und `"(" expr2 ")" "A" | "(" expr2 ")" "B"`) haben ein gemeinsames Alternativen-
  Praefix -- damit ist die ZEICHENBASIERTE TABELLE (nicht der erzeugte Parser) fuer
  genau diese Faelle "PEG-committed" (siehe §1) und lehnt den zweiten, eigentlich
  gueltigen Zweig ab. Deren `[TESTS]`-Bloecke bleiben deshalb bewusst leer (wie bei
  lexcomment/multicomment), die eigentliche Pruefung laeuft gezielt gegen den
  erzeugten C-Parser (und bei actionrollback2 zusaetzlich gegen s68sim fuer die reine
  Backtracking-Syntax, ohne 68k-Aktionsaufruf).

### 9.4c Zweiter Fix: `entry` vor fuehrendem `ws()` erfasst (2026-07-20)

Beim ersten Interpreter-Versuch mit echtem `[LEXER]`-Block (siehe 9.5) fiel ein weiterer,
unabhaengiger Fehler auf: `entry = p;` (Basis fuer `start` einer ACTION) wurde am Anfang
jeder Regelfunktion erfasst, BEVOR ein eventuelles fuehrendes `ws()` dieser Regel lief --
Whitespace/Kommentare VOR dem eigentlichen Regelinhalt landeten dadurch faelschlich im
`start`/`end`-Bereich einer ACTION (z.B. ein Leerzeichen vor einem Operator-Zeichen bei
`note_addop`, das dann `*start != '+'` fehlschlagen liess und auf den `else`-Zweig
(Subtraktion) auswich -- 2+3 ergab -1 statt 5). calcexpr/actiontest hatten KEINEN
`[LEXER]`-Block und damit nie Whitespace zwischen Regelinhalt und Regelanfang -- deshalb
blieb das bis zum ersten LEXER+ACTION-Test verborgen.

**Fix**: bei jeder NICHT-lexikalischen Regel wird (falls `[LEXER]` aktiv) `ws()` VOR der
`entry`-Erfassung aufgerufen. `ws()` ist idempotent (ueberspringt 0 oder mehr Zeichen) --
ein zusaetzlicher, ggf. redundanter Aufruf aendert nichts an der Erkennungsleistung, nur
`entry` zeigt danach IMMER auf das erste "echte" Zeichen. Lexikalische (TOKEN-) Regeln
bleiben unveraendert (dort darf grundsaetzlich kein `ws()` laufen). Verifiziert: komplette
Suite (56 Checks) bleibt gruen, insbesondere die 13 oberon0-Programmtests (bereits vorher
korrekt, weil deren Assertions nicht ueber ACTION-start/end liefen) UND das neue
Test/miniOberon (siehe 9.5) rechnet danach exakt richtig.

**NICHT behoben: 68k-Backend.** Der 68k-Codegen feuert Aktionen weiterhin SOFORT
(`bsr <name>` direkt im generierten Code, siehe §9.3) -- die gleiche Rollback-Luecke
besteht dort unveraendert. Ein aequivalentes Log+Replay dort braeuchte einen ZWEITEN,
zum Positions-Stack `-(a7)` parallelen Log-Laengen-Stack (oder gepaarte Eintraege auf
demselben Stack) an JEDER Stelle, an der `a0` gesichert/restauriert wird -- bewusst
zurueckgestellt, da 68k-Aktionen ohnehin noch keine echten Werte uebergeben (nur das
Regelende in a0, siehe §9.3) und daher fuer den naechsten Schritt (oberon0-Handdurchlauf)
zunaechst nicht kritisch sind (C bleibt der semantische Referenzpfad).

### 9.4d Erster Mini-Interpreter mit Variablen: Test/miniOberon (2026-07-20)

Naechster Schritt Richtung oberon0, bewusst NICHT an der produktiven `Data/oberon0.ebnf`
(13 bestehende Programmtests, OS-9/r68-Pipeline) ausprobiert, sondern an einer neuen,
strukturell identischen, aber isolierten Testgrammatik (VAR-Deklarationen, Zuweisung,
Ausdruecke MIT Variablenreferenzen -- also: SimpleExpression/term/Factor-Praezedenz aus
9.4 PLUS eine echte Symboltabelle). Ergebnis: `VAR x; y; z; BEGIN x := 2+3*4; y := x-1;
z := x*y END` rechnet korrekt x=14, y=13, z=182 -- ueber ROUTINE-C-Code mit einem
Array-basierten Symboltabellen-Behelf (kein Wertrueckgabekanal, siehe 9.3/9.4).

**Wichtigste Erkenntnis (Template-Bestaetigung): `ident` ist in oberon0-artigen
Grammatiken hochgradig POLYMORPH** -- dieselbe Regel wird fuer Modulnamen, Deklarationen,
Zuweisungsziele UND Ausdrucks-Referenzen verwendet. Der ACTION-Mechanismus kennt aber
nur "eine Aktion pro RegelNAME", keinen Kontext ("welcher AUFRUFER hat mich gerufen").
Loesung, die sich in der Praxis bewaehrt hat: fuer jede semantische ROLLE eine eigene,
triviale Huellregel einfuehren (`target = ident.` fuer Zuweisungsziele, `varRef = ident.`
fuer Werte-Referenzen, `declName = ident.` fuer Deklarationen) -- rein syntaktisch ein
No-Op (erkennt exakt dieselbe Sprache), gibt aber jeder Rolle einen eigenen, actionable
Regelnamen. Das ist jetzt die BESTAETIGTE Antwort auf die in 9.2 offen gelassene Frage
"wie geht man mit wiederverwendeten Regeln um" -- fuer den oberon0-Durchlauf bedeutet das:
oberon0.ebnf braucht dieselbe Behandlung fuer JEDES `ident`-Vorkommen in unterschiedlicher
Rolle (Modul-/Prozedurname, IdentList-Deklaration, Zuweisungsziel, Factor-Referenz,
ProcedureCall-Ziel) BEVOR Aktionen daran gehaengt werden koennen -- ein eigener,
vorbereitender Refactoring-Schritt an der echten Grammatik (siehe naechste Schritte).

Dabei wurden bei der Fehlersuche zwei eigene Bugs entdeckt und behoben:
declare_var war zunaechst faelschlich auf `declItem` (Spanne "ident;") statt auf eine
neue `declName`-Huellregel gehaengt -- das Semikolon landete im Variablennamen (erste,
kleinere Instanz genau des obigen Polymorphie-Problems, hier durch falsche Regelwahl
selbst verursacht). Und der `entry`-vor-`ws()`-Bug aus 9.4c, der beim ersten Test mit
ECHTEM Leerzeichen-haltigem Programmtext auffiel.

**Ausdruecklich NICHT in diesem Schritt: Kontrollfluss (IF/WHILE/REPEAT).** Grund ist
grundsaetzlicher als nur "noch nicht gemacht": das aktuelle Aktionsmodell (Log + Replay
in Parse-Reihenfolge, siehe 9.4b) fuehrt JEDE protokollierte Aktion GENAU EINMAL aus, in
der Reihenfolge, in der die zugehoerige Regel beim PARSEN erfolgreich war. Eine Regel wie
`WhileStatement` wird beim Parsen aber nur EINMAL besucht, unabhaengig davon, wie oft der
Rumpf zur LAUFZEIT ausgefuehrt werden muesste (0-mal, 1-mal, 1000-mal) -- ein "waehrend
des Parsens interpretieren" kann Schleifen/Bedingungen daher grundsaetzlich nicht so
abbilden wie es bei calcexpr/miniOberon fuer geradlinige Ausdruecke/Zuweisungen
funktioniert. Der richtige Weg (und das eigentliche, urspruengliche Projektziel:
Codeerzeugung, nicht Interpretation): Aktionen fuer IF/WHILE muessen CODE/IR EMITTIEREN
(Sprungbefehle + Label), nicht Werte live berechnen -- strukturell genau das, was
genNode68k/genNodeC fuer die META-Grammatik ({..}/[..]-Konstrukte der EBNF selbst) schon
laengst tun (siehe §3/§5). Die vermeintliche "Interpreter"-Natur von calcexpr/miniOberon
ist also ein Sonderfall (Konstantenfaltung ohne Kontrollfluss), kein allgemeines Muster --
fuer IF/WHILE in oberon0 ist ein eigener Entwurf noetig, siehe naechste Schritte.

### 9.5 Empfohlene Endarchitektur (unveraendert, noch nicht erreicht)

```
EBNF + LEXER + NUTZER-CODE
          |
      Scanner (Tokens, Positionen)
          |
  generierter Parser -- Aktionen --> sprachneutrales Frontend-IR
          |                                  |
          +------ Diagnose/Tests             +--> 68k-Backend --> .s68 / OS-9-Modul
                                             +--> C-Referenzbackend (Tests)
```

Die aktuelle erste Ausbaustufe (9.3) hat noch KEIN IR dazwischen -- ROUTINE-Koerper
sind rohe Backend-Fragmente ohne Wertrueckgabekanal. Das IR (Stack-Maschine oder
AST-Register-Code, siehe 9.2 Punkt 2) ist der naechste groessere Schritt, sobald
Aktionen echte Werte (Ausdrucksergebnisse, Typen) zwischen Regeln durchreichen
sollen, nicht nur Seiteneffekte ausloesen.

Bei einer Regelumbenennung werden Aktionen nie automatisch verschoben. BESCHLOSSEN,
aber noch NICHT implementiert: nicht mehr referenzierte Aktionen sollen beim
Schreiben als `WAISE` markiert werden und erhalten bleiben, bis der Nutzer sie
bewusst umbenennt oder entfernt -- aktuell werden ACTION-Zeilen mit unbekannter
Regel beim Parsen der Konfiguration schlicht mit Warnung ignoriert (codegen.cpp,
`actionsParseConfig()`).

## 10. QCC: der erste komplette Sprach-zu-IR-zu-68k-Weg (geplant 2026-07-20)

Neuer Projektfokus statt des oberon0-Handdurchlaufs (Abschnitt 7): eine kleine,
grammatisch EINDEUTIGE C-Teilsprache komplett bis zum lauffaehigen OS-9/68k-Code
durchziehen. Motivierender als oberon0, gleiche Komplexitaetsklasse, und sie
zwingt uns zum eigentlichen Projektziel -- der in 9.5 vertagten IR-Schicht.

### 10.1 Warum QCC (Subset) und nicht "echtes C"

Echtes C ist genau dort eklig, wo es mit dem Retro-Codegen-Ziel nichts zu tun hat:
Praeprozessor (eigene Sprache), typedef-vs-identifier ("lexer hack", echt
kontextsensitiv, braucht Parser-zu-Symboltabelle-Rueckkopplung), Deklaration-vs-
Ausdruck (a * b;), Declarator-Syntax (Pointer/Arrays/Funktionszeiger). Ein
Subset OHNE diese Konstrukte ist eindeutig und mit dem vorhandenen Backtracking-
Parser sauber parsebar.

### 10.2 Sprachumfang (Meilenstein-Zielsprache)

- Einziger Typ int (spaeter char, dann Pointer/Arrays -- 10.11).
- Programm = Folge von Funktionsdefinitionen; Ausfuehrung startet bei main().
- Funktionen mit int-Parametern und int-Rueckgabe (bewusst eingeschraenkt).
- Anweisungen: lokale Deklaration (int x; / int x = e;), Zuweisung, if/else,
  while, return, Block, Ausdrucks-/Aufruf-Anweisung.
- Ausdruecke: + - * /, unaeres Minus, Klammern, Relationen (< > <= >= == !=),
  Funktionsaufrufe, Variablenreferenzen, Integer-Literale.
- Builtin putint(e) (frueh: IR-Opcode PRINT; spaeter echtes OS-9 I$Write).
- KEINE globalen Variablen in M1-M4 (bewusst, 10.11).

### 10.3 Grammatik: Data/qcc.ebnf (mit Rollen- und Keyword-Huellregeln)

Vollstaendig in Data/qcc.ebnf. Zwei Sorten trivialer Huellregeln (rein
syntaktische No-Ops, erkennen dieselbe Sprache) geben jeder SEMANTISCHEN Rolle
bzw. Position einen eigenen, actionable Regelnamen -- die verallgemeinerte
Erkenntnis aus 9.4d:

- ROLLEN-Huellen (wegen "ident ist polymorph", 9.4d):
  target = ident. (Zuweisungsziel), varRef = ident. (Werte-Referenz),
  declName = ident. (Deklaration), funcName = ident. (Aufrufziel).
- KEYWORD-Huellen als "davor"-Hook: whileKw = "while". ifKw = "if".
  elseKw = "else". -- deren AFTER-Aktion feuert direkt nach dem Schluesselwort,
  also VOR der Bedingung/dem Rumpf. Damit braucht der Kontrollfluss KEIN neues
  ACTION-BEFORE (siehe 10.6/10.9).
- weitere Positions-Huellen: funcHead (Funktionskopf vor dem Rumpf),
  varInit, retVal, arg, thenPart, elsePart, whileBody, ifCond, whileCond.

Ordered choice + Backtracking loesen die Mehrdeutigkeiten (call vs varRef,
assignStmt vs callStmt: beide starten mit ident -> laengere Alternative zuerst,
sonst Ruecksetzung). Keyword-vs-ident loest der [LEXER]-Wortgrenzen-Check (Abschnitt 8),
eine KEYWORDS-Liste ist unnoetig.

Zugehoeriger [LEXER]-Block (kommt in Data/qcc.lextab, M1):
WHITESPACE = " \t\r\n" / TOKEN ident / TOKEN number / COMMENT LINE = "//" /
COMMENT BLOCK = "/*" "*/".

### 10.4 Warum eine IR-Schicht (nicht Live-Interpretation)

9.4d hat gezeigt: geradliniger Code (Ausdruecke/Zuweisung) laesst sich per
Post-Order-Aktion live auswerten (calcexpr/miniOberon), Kontrollfluss NICHT --
eine Regel wird beim Parsen nur EINMAL besucht, muesste zur Laufzeit aber 0/1/n-mal
ausgefuehrt werden. Konsequenz (= das eigentliche Projektziel): Aktionen EMITTIEREN
Code/IR (Spruenge + Label), statt Werte live zu berechnen. Wir realisieren damit
endlich die in 9.5 skizzierte Architektur:

```
  qcc.ebnf + [LEXER] + [NUTZER-CODE]
        |  (ebnf-Tool erzeugt Frontend-Parser qcc_p.c)
        v
  generierter Parser -- Aktionen emittieren --> Stack-IR (Textform)
        |                                          |
        +--> tools/qccvm (Interpreter, Orakel)    +--> tools/ir2m68k --> .s68 / OS-9
```

Entscheidung zur in 9.2/9.4 offen gelassenen IR-Frage: Stack-Maschine (nicht
AST-Register-Code). Begruendung: passt nahtlos zum vorhandenen calcexpr-Werte-Stack,
trivial auf 68k abzubilden (a7 + d0..d2), und die Host-VM ist zugleich Test-Orakel
(differenziell gegen 68k, wie heute C-Zwilling vs s68sim). AST-Register-Code bleibt
Option fuer spaetere Optimierung (10.11).

### 10.5 Die Stack-IR (Opcode-Satz)

Textform, ein Opcode pro Zeile (wie ein Mini-Assembler). Operanden-Stack-Maschine.
Werte und Pointer sind getrennte Konzepte (ein Pointer ist intern `(Block, Offset)`,
kein simpler Integer). Der Satz unten ist der VOLLSTAENDIGE, aktuelle Stand
(2026-07-25) -- gewachsen aus dem urspruenglichen M1-Startsatz von 2026-07-20
ueber Zeiger/Array-/Char-/struct-Unterstuetzung bis zur Mehrdatei-Uebersetzung.
Kanonische Quelle fuer die Semantik ist `tools/qccvm.py` (Interpreter/Test-Orakel);
ausfuehrliche Beschreibung inkl. Stack-Effekt pro Opcode: `docs/IR_OPCODES.md`.

```
; --- Programmstruktur / Deklarationen ---
GLOBAL <name> [init]          ; globale skalare Variable
GARRAY <name> <typtag> <len>  ; globales Array
GINIT  <name> <idx> <wert>    ; Initialwert fuer ein Array-Element
FUNC   <name> <nargs>         ; Funktionsbeginn; Slots 0..nargs-1 = Parameter
ENDFUNC                       ; Funktionsende (Rahmengroesse vom Backend ermittelt)
LABEL  <L>                    ; definiert Sprungziel L
FUNCDECL   <name> <argc>      ; Vorwaertsdeklaration ohne Rumpf (Mehrdatei/geg. Rekursion)
GLOBALDECL <typ> <name>       ; "extern"-Variable, keine eigene Allokation
                               ; FUNCDECL/GLOBALDECL: nur backend-/linkerrelevant,
                               ; von QCCVM nicht interpretierbar

; --- Werte laden/speichern (lokal L / global G; int / char / pointer) ---
LOADL / STOREL <i>            ; lokaler int-Slot
LOADC / STOREC <i>            ; lokaler char-Slot (maskiert auf 0xff)
LOADP / STOREP <i>            ; lokaler Pointer-Slot
LOADG / STOREG   <name>       ; globale int-Variable
LOADGC / STOREGC <name>       ; globale char-Variable
LOADGP / STOREGP <name>       ; globaler Pointer
PUSH <n>                      ; Integer-Konstante -> Stack
LARRAY                        ; lokales Array reservieren

; --- Adressen, Arrays, Pointer ---
ADDRL / ADDRG                 ; Adresse eines lokalen/globalen Slots -> Pointer
PUSHADDR L/G/P <i>            ; Adresse eines lokalen/globalen Arrays bzw. Pointer-Werts
LOADIDX / STOREIDX L/P/G <i> <typtag>  ; Array-Element lesen/schreiben (Index vom Stack)
PTRINDEX <typtag>             ; Pointer+Index -> skalierte Adresse (echter Pointer p[i])
LOADIND / STOREIND <typtag>   ; durch Pointer dereferenzieren
PADD / IPADD <typtag>         ; Pointer +/- Ganzzahl, skaliert (feste Typgroesse)
IPADDN <bytesize>             ; wie IPADD, aber LAUFZEIT-Bytegroesse (arr[i].feld bei structs)
PSUB <typtag>                 ; Pointer - Ganzzahl
PDIFF <typtag>                ; Pointer - Pointer -> skalierte Ganzzahl-Differenz

; --- Arithmetik/Logik ---
ADD  SUB  MUL  DIV  MOD       ; signed
UDIV  UMOD                    ; unsigned
NEG                           ; unaeres Minus
NOT                           ; logisches Nicht (0/1)
NOTBIT                        ; bitweises Komplement
BAND  BXOR  BOR               ; bitweise Ops
SHL                           ; Shift links
SHR  /  USHR                  ; Shift rechts, signed(arithmetisch) / unsigned(logisch)
NARROWC                       ; auf ein Byte einschraenken (char-Zuweisung/-Cast)
DUP  /  DUPP                  ; oberstes Stackelement duplizieren (Wert / Pointer)

; --- Vergleiche: pop2 -> push 0/1 ---
CMPLT  CMPGT  CMPLE  CMPGE  CMPEQ  CMPNE          ; signed
CMPULT CMPUGT CMPULE CMPUGE                        ; unsigned
PCMPEQ PCMPNE PCMPLT PCMPLE PCMPGT PCMPGE          ; Pointer (mit Block-Identitaetspruefung)

; --- Kontrollfluss / Aufrufe ---
JMP  <L>
JZ   <L>                      ; pop; springe wenn == 0
JNZ  <L>                      ; pop; springe wenn != 0
CALL / CALLP <name> <nargs>   ; nargs Werte vom Stack (links->rechts gepusht) -> Ergebnis auf Stack
                               ; (P-Variante: Kennzeichnung Pointer-Rueckgabewert)
RET / RETP                    ; pop = Rueckgabewert; Rahmen abbauen; zum Aufrufer
CALLEXT / CALLEXTP <name> <argc> <...>  ; Aufruf einer echten extern-Funktion ueber die
                               ; Microware-ABI (feste Parameter d0/d1, nur variadischer
                               ; Ueberschuss auf den Stack); backend-only (68k), QCCVM kann das nicht

; --- Sonstiges ---
DROP             ; oberen Stackwert verwerfen (unbenutztes Ausdrucksergebnis)
PRINT / PRINTU / PRINTC   ; Debug-Ausgabe int/unsigned/char (Builtins putint/putuint/putchar)
```

### 10.6 Emissions-Muster: wie Aktionen die IR erzeugen

Kernprinzip: Der ACTION-Mechanismus protokolliert Aktionen beim Parsen und spielt
sie NUR bei Gesamterfolg, EINMAL, in Parse-Reihenfolge ab (9.4b). Ein durch
Backtracking verworfener Pfad rollt seine geloggten Aktionen automatisch zurueck
(svLog). Deshalb genuegen AFTER-Aktionen + Huellregeln; Emission laeuft in einem
einzigen linearen Replay ohne Backtracking. Ein globaler IR-Puffer, ein globaler
Label-Zaehler, ein Kontroll-Frame-Stack und ein Call-Frame-Stack leben (wie
calcexprs Werte-Stack) in den ROUTINE-C-Koerpern.

Geradliniger Code (AFTER, Post-Order -- exakt calcexpr-Muster, nur EMIT statt rechnen):
- number  AFTER -> emit PUSH <wert>
- varRef  AFTER -> Slot nachschlagen -> emit LOADL <i>
- negFactor AFTER -> emit NEG
- addop/mulop AFTER -> "pending operator" merken; term/addExpr AFTER -> falls
  pending: emit ADD/SUB bzw. MUL/DIV (No-Op bei erster Iteration, wie calcexpr)
- relop AFTER -> pending merken; expr AFTER -> falls pending: emit CMPxx
- target AFTER -> Ziel-Slot merken; assignStmt AFTER -> emit STOREL <slot>
  (Reihenfolge stimmt: target VOR expr geparst, expr-Code VOR dem STOREL emittiert)
- varDecl: declName AFTER allokiert Slot; varInit AFTER -> (Code steht) emit
  STOREL <slot>
- callStmt AFTER -> emit DROP (unbenutztes Ergebnis; bei putint/PRINT entfaellt es)
- returnStmt AFTER -> falls kein retVal: emit PUSH 0; dann emit RET

Funktionsdefinition (Keyword-/Kopf-Huelle liefert den "vor dem Rumpf"-Hook):
- funcHead AFTER -> neue Symboltabelle/Slot-Zaehler; emit FUNC <name> <nargs>
- funcdef  AFTER -> emit RET (Fallthrough-Sicherung) + ENDFUNC; Scope schliessen

Aufruf (Call-Frame-Stack traegt Name + Argumentzahl, auch geschachtelt):
- funcName AFTER -> Call-Frame pushen (Name, count=0)
- arg      AFTER -> count des obersten Call-Frames erhoehen
- call     AFTER -> emit CALL <name> <count>; Call-Frame poppen

Kontrollfluss (Keyword-Huelle = "davor"-Hook, Kontroll-Frame-Stack fuer Labels):
- whileKw   AFTER -> Ltop,Lend allokieren; Frame pushen; emit LABEL Ltop
- whileCond AFTER -> emit JZ Lend   (Lend vom obersten Frame)
- whileStmt AFTER -> emit JMP Ltop; emit LABEL Lend; Frame poppen
  => Ltop: [cond] JZ Lend [body] JMP Ltop Lend:            (korrekt)
- ifKw      AFTER -> Lelse,Lend allokieren; Frame pushen
- ifCond    AFTER -> emit JZ Lelse
- thenPart  AFTER -> emit JMP Lend; emit LABEL Lelse
- ifStmt    AFTER -> emit LABEL Lend; Frame poppen
  => [cond] JZ Lelse [then] JMP Lend Lelse: [else] Lend:   (ohne else: Lelse == Fall-through)

Backtracking-Sicherheit: Beim Parsen von factor wird call VOR varRef versucht;
scheitert call (kein "("), rollt svLog die dabei geloggte funcName-Aktion
(Call-Frame-Push) automatisch zurueck -- kein Phantom-Frame. Genau dafuer existiert
der Log+Replay-Mechanismus (9.4b).

### 10.7 Funktionen: Slots und Call/Return-ABI

- Slot-Vergabe (Codegen-Zeit): declName in param/varDecl allokiert den naechsten
  freien Slot der AKTUELLEN Funktion; varRef/target schlagen nach. Parameter
  belegen Slots 0..nargs-1 (von CALL vorbelegt), lokale Variablen die folgenden.
- IR-Semantik: CALL name n nimmt n Werte vom Operanden-Stack (links->rechts
  gepusht), legt eine Aktivierung an (locals[0..n-1] = Argumente), fuehrt bis
  RET aus; RET nimmt den obersten Stackwert als Rueckgabe und legt ihn im
  Aufrufer-Stack ab. Host-VM: locals als Dict/Array -> Rahmengroesse muss nicht
  vorab bekannt sein.
- 68k/OS-9-Skizze (M4, nicht final): Argumente auf a7; jsr p_<name>; Callee
  link a6,#-frame (frame = hoechster Slot+1, per Vorab-Scan der Funktions-IR),
  Parameter/Locals ueber a6-relative Offsets, Rueckgabe in d0, unlk a6, rts;
  Aufrufer legt d0 als Ergebnis ab. putint -> OS-9 I$Write-Trap (M4/M5).

### 10.8 Host-VM tools/qccvm = Interpreter UND Test-Orakel

Kleiner Interpreter (Python, Stil wie tools/s68sim.py): liest IR-Text, fuehrt ihn
auf einer Operanden-Stack-Maschine mit Aufruf-Stack (Frames) aus, LABEL vorab in
Index-Map gescannt, startet bei main. Liefert sofort Ergebnisse ohne 68k-Toolchain
UND dient als Referenz-Orakel: fuer jedes Testprogramm muss qccvm dieselbe Ausgabe
liefern wie der aus derselben IR erzeugte 68k-Code unter s68sim/r68 -- dasselbe
differenzielle Muster, das die Suite heute fuer den Parser faehrt (C-Zwilling vs s68sim).

### 10.9 Mechanik-Erweiterung? -- vorerst KEINE noetig

Wichtiges Ergebnis dieses Entwurfs: Mit den Keyword-/Positions-Huellregeln (10.3/10.6)
liefert der VORHANDENE "eine ACTION AFTER pro Regel"-Mechanismus Hooks an jeder
noetigen Stelle, auch "vor" einem Teil. Ein ACTION-BEFORE ist damit fuer M1-M4 NICHT
erforderlich; das ebnf-Tool (parsec.cpp/codegen.cpp) bleibt fuer die QCC-Arbeit
unveraendert. (Ein echtes ACTION-BEFORE bliebe eine spaetere Bequemlichkeit, kein
Blocker.) Ebenfalls unveraendert: die 68k-Aktions-Rollback-Luecke (9.4c) ist irrelevant,
weil der QCC-Frontend-Parser nur ROUTINE C nutzt (IR-Emission in C), analog calcexpr.

### 10.10 Meilensteine + Teststrategie

- M1  Ausdruecke + lokale Variablen + eine Funktion main: qcc.lextab mit
      IR-emittierenden ROUTINE-C-Koerpern; tools/qccvm. Ziel: putint(2+3*4);
      und Variablen rechnen ueber die IR korrekt. (Kein Tool-Change.)
- M2  Kontrollfluss if/else + while ueber Label/Sprung-IR (10.6). Ziel: Schleifen/
      Bedingungen laufen in qccvm. (= 9.4d in echt.) **FERTIG, 2026-07-20:**
      `ifKw`/`whileKw`-Aktionen legen Kontroll-Frames mit eindeutigen Labels an;
      `ifCond`/`whileCond` emittieren `JZ`, die Abschlussaktionen `JMP`/`LABEL`.
      4 zusaetzliche End-to-End-Programme testen true/false-if, while und ein
      verschachteltes if im while gegen qccvm.
- M3  Funktionen mit int-Parametern + Rueckgabe + Rekursion. Ziel: fib/fakultaet
      laufen rekursiv in qccvm. **FERTIG, 2026-07-20:** Der Frontend-Emitter
      verwaltet verschachtelbare Call-Frames (Name + Argumentzahl), damit ein
      Aufruf als Argument eines anderen Aufrufs korrekt bleibt. Die Frames sichern
      zudem ausstehende Add-/Mul-Operatoren, damit etwa `n * fact(n - 1)` nicht
      beim Parsen des Arguments voreilig multipliziert. Zwei neue Tests pruefen
      Mehrfachparameter/verschachtelten Aufruf und rekursive Fakultaet.
- M4  Backend (eigenstaendiges C++, nicht Teil von `ebnf`): dieselbe IR -> .s68/OS-9,
      differenziell gegen qccvm geprueft. **BEGONNEN, M4a 2026-07-20:**
      `Source/qcc_backend.cpp` liest die Text-IR und erzeugt PIC-faehigen
      68000-Motorola-Assembler fuer Funktionsframes, Parameter, lokale Slots,
      ADD/SUB/NEG, Vergleiche, Spruenge, CALL/RET und PRINT. `runtests.sh`
      assembliert einen Mehrparameter-/CALL-Fall mit freiem vasm. MUL/DIV und I/O
      sind noch Runtime-Stubs. **M4b 2026-07-20:** `tools/qcc68sim.py` (nur
      Test-Orakel, nicht Teil der Toolchain) fuehrt die M4a-Ausgabe aus und bildet
      die minimale Runtime nach; die rekursive 68k-Fakultaet liefert nach vasm-
      Assemblercheck ebenso `120` wie qccvm. **M4c-1 2026-07-20:** Die festen,
      PIC-faehigen 68000-Schablonen `tc_mul_i32` und `tc_div_i32` sind nun echter
      68k-Core (signed int32, nicht Runtime); der Simulator durchlaeuft sie mit
      Fakultaet, negativer Multiplikation und Division. **M4c-2 2026-07-20:**
      Globale, nullinitialisierte `int32`-Variablen sind implementiert. Der
      Frontend-Namensraum trennt lokale Slots und globale Namen; die IR verwendet
      `GLOBAL`/`LOADG`/`STOREG`. Das 68000-Backend emittiert PC-relative Loads
      sowie Stores ueber einen kurzlebigen `a0`-Adress-Temporaer in sein
      nullinitialisiertes Daten-/BSS-Aequivalent. **M4c-3 2026-07-21:**
      Globale Literale (`int limit = 10;`, auch negativ) emittieren
      `GLOBAL name wert`. Nichtnullwerte stehen im DATA-Aequivalent, Nullwerte
      im BSS-Aequivalent; ARM64/Darwin verwendet dafuer echte Mach-O-
      `__DATA,__data`- bzw. `__DATA,__bss`-Bereiche. Die Auswertung erfolgt
      erst nach der vollstaendigen globalDecl-Regel, damit Backtracking eines
      Funktionspraefixes keinen Frontend-Zustand veraendert. Echte Ziel-Runtime
      folgt. **M4c-4 2026-07-21:** Der zweite Plattform-Hook `tc_putchar`
      ist implementiert. QCC erkennt `putchar(int)`, emittiert `PRINTC` und
      das ARM64-Darwin-start.s schreibt das niederwertige Byte direkt nach
      stdout. Der 68k-Backend-Vertrag ist derselbe (`bsr tc_putchar`); bis zur
      Q9-Runtime prueft qcc68sim ihn. Der native Test gibt `OK`, `120`, `-6`
      aus.
      **T1 2026-07-21:** `int` (signed 32 Bit) und `char` (unsigned 8 Bit)
      sind nun echte Eintraege der lokalen und globalen Namensraeume. Die IR
      unterscheidet `LOADC`/`STOREC` und `LOADGC`/`STOREGC`; Zuweisungen nach
      char kuerzen auf das niederwertige Byte. 68000 emittiert `move.b`, ARM64
      `ldrb`/`strb`; globale char-Daten sind ein Byte gross. Der End-to-End-Test
      kuerzt `char mark = 335` zu `O` und prueft beide Backends.
      **Typnamen-Entscheidung 2026-07-21:** Der geplante 32-Bit-Unsigned-Typ
      heisst nach ISO C `unsigned int` (nicht `uint`). Komfortnamen wie
      `uint32_t` gehoeren spaeter in eine Bibliotheks-`typedef.h`, nicht in den
      Sprachkern.
      **T2 unsigned int 2026-07-21:** `unsigned int` ist jetzt ein 32-Bit-
      Worttyp mit demselben Speicherlayout wie `int`. Der Frontend-Typcode ist
      `u`; Addition/Subtraktion bleiben Wortoperationen, Division und die vier
      Ordnungsvergleiche werden jedoch unsigned (`UDIV`, `CMPU...`). QCCVM,
      die ARM64-Emission (`udiv`, `lo`/`hi`/`ls`/`hs`) und der 68000-Pfad
      (feste 32-Runden-`tc_udiv_u32`-Schablone, Carry-basierte Vergleiche)
      sind gegen `0xffffffff > 1` und `0xffffffff / 2` getestet. `putint`
      bleibt bewusst eine signed-Ausgabe; ein `putuint` ist ein spaeterer
      Plattform-Hook.
      **T3 putuint 2026-07-22:** Dieser Hook ist nun vorhanden: `putuint(e)`
      emittiert `PRINTU` und schreibt den Wert als unsigned 32-Bit-Dezimalzahl.
      QCCVM und qcc68sim verwenden dieselbe Darstellung; die ARM64-Darwin-
      Runtime besitzt `_tc_putuint` ohne Vorzeichenbehandlung. Der 68000-
      Backend-Vertrag ist `tc_putuint` und wartet wie `tc_putint` nur noch auf
      die spaetere Q9-Runtime.
      **Gestaltungsregel fuer den weiteren Ausbau:** Wiederkehrende Sprachkonstrukte
      bekommen je Backend eine FESTE Assembler-Schablone; eingesetzt werden nur
      Parameter wie Labels, Stack-/Frame-Offsets, Konstanten und Symbolnamen. Ein
      `if`, `while`, Call/Return oder Frame-Prolog wird also nicht jedes Mal neu
      entworfen. Diese festen Muster (Frame-Prolog/-Epilog, Push/Pop,
      Binaeroperation, Vergleich zu Bool, bedingter Sprung, Datenzugriff,
      Runtime-Aufruf) werden als benannte Emissionsfunktionen gekapselt statt als
      verstreute Textausgaben pro Opcode.
- M5+ Typen wachsen lassen: char und Arrays sind vorhanden; Pointer sind seit
      2026-07-22 implementiert. Naechster grosser Typschritt sind Structs sowie
      nichtkonstante globale Initialisierer und echte linkerfaehige Abschnitte.

Integration in runtests.sh (M1): eigener Abschnitt, der qcc_p auf eine Reihe
Testprogramme laufen laesst und die qccvm-Ausgabe gegen erwartete Werte prueft
(ab M4 zusaetzlich gegen den 68k-Pfad). Neue Grammatik in die expliziten g-Listen
(Abschnitte "codegen"/"s68sim") NUR aufnehmen, soweit sinnvoll -- der Parser selbst
wird ohnehin ueber die Programm-Tests validiert.

### 10.11 Offene Punkte / bewusste Vertagungen

- Nichtkonstante globale Initialisierer und linkerfaehige 68k-/Q9-Abschnitte im
  Modulformat: nach M4 (konstante int32-DATA/BSS-Globals sind seit M4c-3 da).

### 10.11a Array-Frame-Modell (Entwurf, 2026-07-21)

Feste, eindimensionale `int`-/`char`-Arrays werden nicht als Folge normaler
4-Byte-Lokalslots modelliert: Das waere fuer `char[]` falsch, weil dessen
Elemente byteweise zusammenhaengen muessen. Die Frontend-Symboltabelle erhaelt
deshalb pro Objekt Basistyp, Elementzahl, bytegenaue Groesse und einen logischen
Frame-/Datenoffset. Die IR wird dazu eine explizite lokale bzw. globale
Arraydeklaration sowie `LOADIDX`/`STOREIDX` erhalten; der Index liegt bereits
als int32 auf dem Operanden-Stack.

Die Backend-Schablonen sind dann fest: `char` adressiert `basis + index`, `int`
adressiert `basis + index * 4`. Lokale Arrays reservieren exakt ihre Bytegroesse
im Frame (mit mindestens 4-Byte-Ausrichtung der nachfolgenden int-Objekte);
globale Arrays liegen als zusammenhaengende DATA-/BSS-Bloecke. Parameter-Arrays
und Pointer sind inzwischen im nachfolgenden Modell vereinheitlicht;
vollstaendige dynamische Bounds-Checks bleiben ein Folgeschritt.

### 10.11b Pointer- und erweiterbares Typmodell (implementiert 2026-07-22)

Ein Typ ist im Frontend nicht mehr nur ein einzelner Buchstabe, sondern ein
Paar aus Basistyp (`int`, `unsigned int`, `char`, `bool`) und Pointertiefe.
Dadurch werden `T*`, `T**` und spaetere Erweiterungen am selben Modell
abgebildet. Arrays behalten ihre feste Objektgroesse, zerfallen in Ausdruecken
aber zu einem Pointer auf ihr erstes Element; Arrayparameter werden intern
ebenfalls als Pointer gefuehrt.

Die Stack-IR trennt Adresse und Wert explizit: `ADDRL`/`ADDRG` und `PUSHADDR`
bilden Adressen, `LOADIND`/`STOREIND` greifen indirekt zu, `PTRINDEX` adressiert
ein Element. `PADD`, `PSUB`, `IPADD` und `PDIFF` transportieren statt einer
festen Bytezahl den Elementtyp `c`, `i` oder `p`. Daher skaliert jedes Backend
selbst korrekt: `char*` mit 1, `int*` mit 4 und Pointer-auf-Pointer mit der
Pointergroesse des Ziels (68000: 4, ARM64: 8 Byte).

QCCVM verwendet dafuer abstrakte Byteadressen in Speicherbloecke. Das
68000-Backend nutzt 32-Bit-Adressen, ARM64 durchgehend 64-Bit-Adressen fuer
Pointer-Slots, Argumente und Rueckgaben. Unterstuetzt sind Adressbildung und
Dereferenzierung, indirekte Zuweisung, `p[i]`, Pointerparameter/-rueckgaben,
Pointerarrays, Pointervergleiche, Nullkonstante `0`, skalierte Addition und
Subtraktion sowie die Differenz kompatibler Pointer.
- AST-Register-IR statt Stack-IR: Option fuer Codequalitaet, erst wenn 68k-Ausgabe
  zu schlecht ist (10.4).
- Dangling-else, return mitten im Block, Kurzschluss-&&/||: als M2/M3-Details
  benannt, Grammatik deckt if/else bereits ab; &&/|| spaeter (brauchen eigene
  Sprung-Emission).
- putint als echter OS-9-Trap (I$Write) statt PRINT: M4/M5.
- typedef/Praeprozessor/Declarator-Syntax: bewusst ausserhalb QCC (10.1).

### 10.12 Zielmodularitaet: Architecture Backend + Target Runtime (Idee, vertagt 2026-07-20)

Die Stack-IR bleibt bewusst unabhaengig von CPU, Betriebssystem und konkreter
Aufrufkonvention. Kuenftige Ausgaben werden in zwei austauschbare Teile getrennt:

```
Stack-IR -> Architecture Backend -> Target Runtime -> Betriebssystem/Hardware
             (68k/i386/x86-64)    (Q9/OS-9/POSIX/DOS/Bare Metal)
```

- Das **Architecture Backend** setzt Rechenoperationen, lokale Slots,
  Kontrollfluss, Frames und den internen Funktionsaufruf in CPU-Code um.
- Die **Target Runtime** implementiert die sprachliche Aussenwelt, zuerst
  `tc_putint`, spaeter Ein-/Ausgabe, Dateien, Speicher und Programmende. Sie
  kapselt dabei Systemcalls, Startcode und das jeweilige Ziel-ABI.
- `int` der QCC-Sprache ist von Anfang an als signiertes 32-Bit-Wort zu
  definieren, unabhaengig von der Host-CPU. Der IR-Opcode-Satz bleibt ABI-neutral.

Erwuenschte Kombinationen: `68k backend + Q9 runtime`, `i386 backend +
OS-9/386 runtime` sowie `x86-64 backend + POSIX runtime` als schneller
Entwicklungs- und Testpfad. BIOS-/UEFI-Zugriff ist bei Bedarf ein weiteres
Bare-Metal-Runtime-Ziel; er gehoert nicht in die IR und nicht in den Hosted-
x86-64-Testpfad. Diese Aufteilung wird erst nach M1--M3 und vor bzw. zusammen
mit dem ersten 68k-Backend (M4) konkretisiert.

**Erster echter Hosted-Zielweg, 2026-07-21:** Auf dem ARM64-Mac existiert nun
ARM64 backend + Darwin runtime. `Source/qcc_arm64_backend.cpp` emittiert
PIC-faehigen ARM64-Programmassembler (Frames, Calls, signed int32-Arithmetik,
Kontrollfluss und Globals); `runtime/arm64_darwin/start.s` besitzt den eigenen
`_start`, `tc_putint` und `tc_exit` ueber direkte Darwin-Systemcalls. Es wird
mit `clang -nostartfiles` als Mach-O gelinkt: libSystem ist nur zum Laden des
Programms noetig, keine C-Startdatei und keine C-Runtime-Semantik. Der
Regressionstest fuehrt rekursive Fakultaet, Globals und signed Division als
natives Programm aus (120, -6). Das ist ein Testtraeger; die saubere
Architekturgrenze gilt unveraendert auch fuer x86-64/POSIX und 68k/Q9.

### 10.13 M4c: erste Runtime = Q9-ABI-Entwurf (2026-07-20)

Q9 ist der vorgesehene erste echte 68k-Zielweg. Die Q9-Dokumentation legt
bereits die Richtung fest (PIC-68k-Code, ein spaeterer TRAP-zu-`q9_syscall`-
Uebergang und ein eigenes Modulformat), aber der konkrete 68k-TRAP-Dispatcher,
der Modul-Lader und damit die endgueltigen Eintritts- und Systemcall-Details
sind im Q9-Projekt noch nicht implementiert. Das Backend darf diese Details
nicht raten und insbesondere keine Microware-ABI oder OS-9-ROF-Abhaengigkeit
einschmuggeln.

M4c teilt sich deshalb bewusst in zwei Schritte:

1. **Jetzt im EBNF-Projekt:** Die Runtime-Grenze wird als kleine,
   assembler- und linkerunabhaengige Schnittstelle festgelegt. Das 68k-Backend
   ruft nur diese Plattform-Symbole auf: zuerst `tc_putint(d0: int32)` und
   `tc_exit(d0: int32)`. Interne Funktionen bleiben PC-relativ. Die fehlenden
   68000-Operationen `tc_mul_i32(d0,d1) -> d0` und `tc_div_i32(d0,d1) -> d0`
   sind dagegen Teil des Architecture Backends: feste, PIC-faehige 68k-Core-
   Schablonen und keine Q9-Abhaengigkeit.
2. **Sobald Q9 bereit ist:** Ein separates `runtime/q9_68k` implementiert
   genau diese Symbole mit dem dann definierten Q9-Start-, Modul- und
   Syscall-ABI. Es darf den Aufruf der Anwendung, Ausgabe und Programmende
   kapseln, nicht jedoch Semantik in die Stack-IR zuruecktragen.

Der erste Schritt ist kein Platzhalter-Trick: Er ist die feste Vertragsschicht
zwischen Machine Backend und Plattform. Bis Q9 sie ausfuehren kann, bleibt
`qcc68sim.py` ausschliesslich das differenzielle Test-Orakel. Ein spaeteres
POSIX-, OS-9/386- oder Bare-Metal-Runtime-Paket liefert dieselben Symbole mit
eigener Start-/Systemschicht.

### 10.14 Umsetzungsstand Meilenstein 1 (2026-07-20, FERTIG)

M1 laeuft end-to-end: Data/qcc.ebnf -> generierter Parser (Data/qcc_p.c,
ROUTINE-C-Aktionen im [NUTZER-CODE] von Data/qcc.lextab) -> Stack-IR nach stdout
-> tools/qccvm.py fuehrt sie aus. runtests.sh Abschnitt 12 prueft 9 Programme
(Ausdruecke mit Praezedenz/Assoziativitaet, Klammern, unaeres Minus, C-Division,
lokale Variablen, Kommentare, Relationen) gegen erwartete Werte. KEIN Tool-Change
noetig (Bestaetigung von 10.9), nur Grammatik + [NUTZER-CODE] + VM.

Verfeinerung gegenueber 10.3: die generische Deklarations-Huelle declName wurde in
DREI rollenspezifische Huellen gesplittet, weil dasselbe declName sonst fuer
Funktionsname, Parameter UND lokale Variable feuert (jede mit eigener Slot-Semantik):
- defName   = ident.  Funktionsname (setzt Slot-Zaehler auf 0 zurueck, merkt Namen)
- paramName = ident.  Parameter     (belegt Slot 0..nargs-1)
- localName = ident.  lokale Var     (belegt Folge-Slots)
Das ist wieder exakt das Polymorphie-Muster aus 9.4d, nur eine Ebene feiner.

IR-Emission (bestaetigtes calcexpr-Muster, nur EMIT statt rechnen): number->PUSH,
varRef->LOADL, addop/mulop gemerkt und in term/factor als ADD/SUB/MUL/DIV emittiert,
negFactor->NEG, relop in expr als CMPxx, target-Slot gemerkt und in assignStmt als
STOREL, varInit->STOREL auf zuletzt deklarierten Slot, funcHead->FUNC name nargs,
funcdef->PUSH 0/RET/ENDFUNC (Fallthrough-Rueckgabe). putint(x) wird als PRINT
emittiert (Builtin), sonstige Aufrufe als CALL name nargs (fuer M3 vorbereitet;
Call-Frame-Stack fuer Schachtelung folgt dort).

M2 und M3 sind inzwischen ebenfalls fertig: `if/else` und `while` emittieren
ueber einen kleinen, verschachtelbaren Kontroll-Frame-Stack eindeutige
`LABEL`-/`JZ`-/`JMP`-Folgen. Ein verschachtelbarer Call-Frame-Stack ermoeglicht
Mehrfachparameter, Aufrufe als Argument und rekursive Fakultaet. Der aktuelle
M4-Backend-Prototyp uebersetzt diese IR bereits in PIC-faehigen 68000-Assembler;
offen bleibt die echte, nun als Q9-Runtime geplante Plattformanbindung (10.13).
