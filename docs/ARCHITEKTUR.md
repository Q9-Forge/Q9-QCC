# ARCHITEKTUR -- Weg zur korrekten Codeerzeugung (68k zuerst)

Stand: 2026-07-19 (Nacht-Session). Endziel laut Nutzer: **korrekte Codeerzeugung fuer die
gegebene Grammatik, zuerst 68k-Assembler.**

## 1. Ausgangslage: was die flache Tabelle kann und was nicht

Die Sprungtabelle (PARSER-TABELLE) + Stack-Maschine (`execFrom()`) funktioniert und ist
per TESTS-Bloecken abgesichert. Sie hat aber eine **inhaerente** Grenze: Ruecksetzpunkte
fuer die Eingabeposition existieren nur an NTS-Aufruf-Grenzen (der native Aufrufstack
uebernimmt das Sichern/Verwerfen). Innerhalb einer Regel gibt es keine Positionssicherung
pro Alternative. Konsequenzen (in ebnf.cpp gefixt bzw. gewarnt, Ver. 2.10):

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

Der Parser in ebnf.cpp baut waehrend des normalen Parsens **zusaetzlich** einen AST auf
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

- [x] AST-Aufbau parallel zum Parsen (Source/codegen.cpp/.h, Hooks in ebnf.cpp)
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
   (r68 via MWOS/Wine, psect-Rahmen, siehe Punkt 4).
4. ~~psect-Rahmen~~ **ERLEDIGT**: [CODEGEN]-Block mit `M68K OS9` (+ optional
   `M68K PSECT = name`) erzeugt zusaetzlich `<basis>_os9.a` im Microware-r68-Format
   (nam/psect/ends, '*'-Kommentarzeilen); runtests.sh assembliert es mit dem echten
   r68 via Wine/MWOS. Noch offen daran: l68-Link + Lauf als OS-9-Modul.
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
3. **Laufzeit-ABI**: OS-9/MWOS bevorzugt (nicht freistehender 68000-Code) --
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
  ebnf.cpp prüfte am Regelende nur die LETZTE Tabellenzeile auf offene Vorwaertsreferenzen
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

## 10. Tiny-C: der erste komplette Sprach-zu-IR-zu-68k-Weg (geplant 2026-07-20)

Neuer Projektfokus statt des oberon0-Handdurchlaufs (Abschnitt 7): eine kleine,
grammatisch EINDEUTIGE C-Teilsprache komplett bis zum lauffaehigen OS-9/68k-Code
durchziehen. Motivierender als oberon0, gleiche Komplexitaetsklasse, und sie
zwingt uns zum eigentlichen Projektziel -- der in 9.5 vertagten IR-Schicht.

### 10.1 Warum Tiny-C (Subset) und nicht "echtes C"

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

### 10.3 Grammatik: Data/tinyc.ebnf (mit Rollen- und Keyword-Huellregeln)

Vollstaendig in Data/tinyc.ebnf. Zwei Sorten trivialer Huellregeln (rein
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

Zugehoeriger [LEXER]-Block (kommt in Data/tinyc.lextab, M1):
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
  tinyc.ebnf + [LEXER] + [NUTZER-CODE]
        |  (ebnf-Tool erzeugt Frontend-Parser tinyc_p.c)
        v
  generierter Parser -- Aktionen emittieren --> Stack-IR (Textform)
        |                                          |
        +--> tools/tinyvm (Interpreter, Orakel)    +--> tools/ir2m68k --> .s68 / OS-9
```

Entscheidung zur in 9.2/9.4 offen gelassenen IR-Frage: Stack-Maschine (nicht
AST-Register-Code). Begruendung: passt nahtlos zum vorhandenen calcexpr-Werte-Stack,
trivial auf 68k abzubilden (a7 + d0..d2), und die Host-VM ist zugleich Test-Orakel
(differenziell gegen 68k, wie heute C-Zwilling vs s68sim). AST-Register-Code bleibt
Option fuer spaetere Optimierung (10.11).

### 10.5 Die Stack-IR (Opcode-Satz)

Textform, ein Opcode pro Zeile (wie ein Mini-Assembler). Operanden-Stack-Maschine.

```
; Konstanten / lokale Variablen
PUSH  <n>        ; Integer-Konstante -> Stack
LOADL <i>        ; lokalen Slot i laden -> Stack
STOREL <i>       ; Stack -> lokalen Slot i (pop)
; Arithmetik: pop2 -> push1  (NEG: pop1 -> push1)
ADD  SUB  MUL  DIV
NEG
; Vergleich: pop2 -> push 0/1
CMPLT  CMPGT  CMPLE  CMPGE  CMPEQ  CMPNE
; Kontrollfluss
LABEL <L>        ; definiert Sprungziel L
JMP   <L>
JZ    <L>        ; pop; springe wenn == 0
JNZ   <L>        ; pop; springe wenn != 0
; Funktionen
FUNC  <name> <nargs>   ; Funktionsbeginn; Slots 0..nargs-1 = Parameter
ENDFUNC                ; Funktionsende (Rahmengroesse = hoechster Slot+1, vom Backend ermittelt)
CALL  <name> <nargs>   ; nargs Werte vom Stack (links->rechts gepusht); Ergebnis auf Stack
RET                    ; pop = Rueckgabewert; Rahmen abbauen; zum Aufrufer
; Sonstiges
DROP             ; oberen Stackwert verwerfen (unbenutztes Ausdrucksergebnis)
PRINT            ; pop; als Zahl ausgeben (Builtin putint; spaeter OS-9 I$Write)
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

### 10.8 Host-VM tools/tinyvm = Interpreter UND Test-Orakel

Kleiner Interpreter (Python, Stil wie tools/s68sim.py): liest IR-Text, fuehrt ihn
auf einer Operanden-Stack-Maschine mit Aufruf-Stack (Frames) aus, LABEL vorab in
Index-Map gescannt, startet bei main. Liefert sofort Ergebnisse ohne 68k-Toolchain
UND dient als Referenz-Orakel: fuer jedes Testprogramm muss tinyvm dieselbe Ausgabe
liefern wie der aus derselben IR erzeugte 68k-Code unter s68sim/r68 -- dasselbe
differenzielle Muster, das die Suite heute fuer den Parser faehrt (C-Zwilling vs s68sim).

### 10.9 Mechanik-Erweiterung? -- vorerst KEINE noetig

Wichtiges Ergebnis dieses Entwurfs: Mit den Keyword-/Positions-Huellregeln (10.3/10.6)
liefert der VORHANDENE "eine ACTION AFTER pro Regel"-Mechanismus Hooks an jeder
noetigen Stelle, auch "vor" einem Teil. Ein ACTION-BEFORE ist damit fuer M1-M4 NICHT
erforderlich; das ebnf-Tool (ebnf.cpp/codegen.cpp) bleibt fuer die Tiny-C-Arbeit
unveraendert. (Ein echtes ACTION-BEFORE bliebe eine spaetere Bequemlichkeit, kein
Blocker.) Ebenfalls unveraendert: die 68k-Aktions-Rollback-Luecke (9.4c) ist irrelevant,
weil der Tiny-C-Frontend-Parser nur ROUTINE C nutzt (IR-Emission in C), analog calcexpr.

### 10.10 Meilensteine + Teststrategie

- M1  Ausdruecke + lokale Variablen + eine Funktion main: tinyc.lextab mit
      IR-emittierenden ROUTINE-C-Koerpern; tools/tinyvm. Ziel: putint(2+3*4);
      und Variablen rechnen ueber die IR korrekt. (Kein Tool-Change.)
- M2  Kontrollfluss if/else + while ueber Label/Sprung-IR (10.6). Ziel: Schleifen/
      Bedingungen laufen in tinyvm. (= 9.4d in echt.)
- M3  Funktionen mit int-Parametern + Rueckgabe + Rekursion. Ziel: fib/fakultaet
      laufen rekursiv in tinyvm.
- M4  Backend tools/ir2m68k: dieselbe IR -> .s68/OS-9, differenziell gegen tinyvm
      geprueft (s68sim + vasm + r68/Wine). Ziel: fib.c als echter OS-9/68k-Code.
- M5+ Typen wachsen lassen: char -> Pointer -> Arrays -> Structs; globale Variablen.

Integration in runtests.sh (M1): eigener Abschnitt, der tinyc_p auf eine Reihe
Testprogramme laufen laesst und die tinyvm-Ausgabe gegen erwartete Werte prueft
(ab M4 zusaetzlich gegen den 68k-Pfad). Neue Grammatik in die expliziten g-Listen
(Abschnitte "codegen"/"s68sim") NUR aufnehmen, soweit sinnvoll -- der Parser selbst
wird ohnehin ueber die Programm-Tests validiert.

### 10.11 Offene Punkte / bewusste Vertagungen

- Globale Variablen: erst nach M4 (LOADG/STOREG + Daten-psect).
- AST-Register-IR statt Stack-IR: Option fuer Codequalitaet, erst wenn 68k-Ausgabe
  zu schlecht ist (10.4).
- Dangling-else, return mitten im Block, Kurzschluss-&&/||: als M2/M3-Details
  benannt, Grammatik deckt if/else bereits ab; &&/|| spaeter (brauchen eigene
  Sprung-Emission).
- putint als echter OS-9-Trap (I$Write) statt PRINT: M4/M5.
- typedef/Praeprozessor/Declarator-Syntax: bewusst ausserhalb Tiny-C (10.1).
