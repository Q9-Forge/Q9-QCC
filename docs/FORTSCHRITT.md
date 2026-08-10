# Fortschritt und Roadmap

## Methodischer Fund: die funktionsweise Messung hatte einen blinden Fleck

Die Messung erkannte Funktionen nur, wenn die Signaturzeile mit `{` endet --
**136 EINZEILIGE Funktionsdefinitionen in `Data/qcc_p.c` wurden dadurch nie
geprueft** (`static int tcIsBool(TCType t) { return ...; }`). Genauso
unsichtbar blieb alles ZWISCHEN den Funktionen. Das erklaert die Luecke
zwischen "fast alle Funktionen uebersetzen" und "die Datei scheitert am
Stueck". Seither wird zusaetzlich die Gesamtdatei durchgeschoben (fehlende
Funktionen durch Ruempfe ersetzt) und der erste Blocker per Bisektion
gesucht -- das hat sofort mehrere Luecken zutage gefoerdert:

- **`const` als struct-Feldtyp** (`const char* start;`) -- aktionslose Kopie
  `fieldConstKw`, damit kein `const`-Zustand in den naechsten Parameter
  durchsickert (wie bei `retConstKw`).
- **Benannter Struct im typedef** (`typedef struct TCType { ... } TCType;`).
  Der Tag wird geparst und verworfen; als interner Tag dient wie bisher der
  typedef-Name. `struct TCType` ist damit nicht als eigenstaendiger Typname
  nutzbar -- im Bootstrap-Ziel kommt das genau einmal vor und stoert nicht.
- **Mehrere Deklaratoren pro globaler Deklaration**
  (`static TCType a[512], b[512][64];`, 31 Fundstellen). `tcGlobalOne` parst
  weiterhin eine VOLLSTAENDIGE Deklaration aus dem Rohtext; der neue Wrapper
  `tc_globalend` teilt an den Kommas der obersten Ebene und setzt fuer jeden
  weiteren Deklarator den Typ-Praefix davor. Kommas in `[...]` oder `{...}`
  zaehlen dabei nicht.

## Weitere Luecken: offene Arraygroesse und Hex-/Oktal-Escapes (2026-08-10)

`static const char wsSet[] = " \\x09\\x0d\\x0a";` -- gleich drei Dinge auf
einmal, und weil die Zeile im DEKLARATIONSteil steht, blockierte sie das
Parsen der ganzen Datei am Stueck (eine einzelne Funktion konnte man
dagegen sehr wohl uebersetzen; deshalb war die Zeile in der
funktionsweisen Messung unsichtbar).

- **Arraygroesse aus dem Initialisierer** (`char x[] = "abc";`): Die
  Groessenangabe ist jetzt optional, die Laenge wird aus dem Literal
  abgeleitet (Zeichen + Nullbyte). Dabei war ein zweiter Guard noetig: der
  ganze Initialisierer-Block hing an `if (arrayLen)`, was bei offener
  Groesse nie zutraf -- jetzt merkt sich `hadBrackets`, DASS Klammern
  dastanden, unabhaengig von der noch unbekannten Laenge.
- **Hex- und Oktal-Escapes** (`\\x09`, `\\011`) in `tcDecodeStringLit`.
  Vorher zaehlten die Ziffern als einzelne Zeichen, was bei fester
  Arraygroesse als "string literal too long" scheiterte -- und bei offener
  Groesse eine falsche Laenge ergeben haette.

## ECHTER KORREKTHEITSFEHLER gefunden und behoben: Klammern bei `*` (2026-08-10)

**`x * (a + b)` rechnete `(x * a) + b`.** Live auf Q9 nachgewiesen: statt 14
kam 10 heraus. Kein Parsefehler, keine Meldung -- stiller Falschcode. Der
Fehler ist VORBESTEHEND und aelter als die Bootstrap-Arbeit; er ist nur
aufgefallen, weil verschachtelte Ternaere ihn in einer Form ausloesten, die
sofort auffiel.

**Ursache:** QCC emittiert `*`/`+` und Vergleiche nicht sofort, sondern
gemerkt (`tcPendingMul`/`tcPendingAdd`/`tcRel0`/`tcRel1`), und wendet sie in
`tc_factor` NACH JEDEM Faktor an -- also auch nach dem ersten Faktor
INNERHALB einer Klammer. Der geklammerte Ausdruck sah dadurch das noch
ausstehende `*` des UMSCHLIESSENDEN Ausdrucks. Betroffen war nur der Fall
"Klammer als RECHTER Operand": `(a + b) * x` und `x + (a * b)` waren
zufaellig korrekt.

**Fix:** eigene Regeln `parenOpen`/`parenClose` mit demselben
Rette-und-nulle-Muster, das Aufrufe (`tc_callname`) und Indizes (`tc_arg`)
laengst verwenden. Verifiziert auf Q9: alle drei Formen liefern jetzt 14.

**Folge fuer die Regression:** `SourceQCC/ebnf.tc` aendert sich dadurch an
zwei Stellen (`v = v * 8 + (s[r] - 48);` und die 10er-Variante) -- vorher
wurde `(v*8 + s[r]) - 48` gerechnet. Das war dort ZUFAELLIG gleichwertig,
weil nur Addition und Subtraktion beteiligt sind; bei einer Multiplikation
waere es falsch gewesen. Die Referenz wurde entsprechend neu gesetzt: die
Aenderung ist eine Korrektur, keine Regression.

**Gleichzeitig behoben: verschachtelte Ternaere** (`a ? 1 : (b ? 2 : 3)`,
6 Fundstellen). `tc_ternaryend` prueft jetzt mit `tcHasTopChar` statt
`tcHasChar`, ob das `?` auf DIESER Ebene steht -- bei einem verschachtelten
Ternaer enthaelt die aeussere, selbst ternaerfreie `conditionalExpr` das `?`
des geklammerten inneren Ausdrucks und feuerte ein zusaetzliches
`tcTernaryEnd` ("conditional-frame mismatch").


## Bootstrap Stufe 1 begonnen: acht Sprachluecken geschlossen (2026-08-10)

Erster echter Selbstuebersetzungsversuch: `Data/qcc_p.c` (2 `#include`
durch `extern`-Prototypen ersetzt, danach per `xcc -pp` makrofrei) durch
QCC selbst. Gemessen wird pro Top-Level-Funktion, wie viele von 338 der
Parser annimmt.

**Fortschritt: 309 -> 112 -> 64 -> 53 -> 11 -> 8 fehlerhafte Funktionen**
(330 von 338 uebersetzen).

**Erste Messung von `Source/qcc_backend_c.cpp` (2026-08-10):** Die Datei ist
praktisch reines C -- keine Templates, Klassen, `new`/`delete`. Zwei
Blocker verhinderten aber, dass ueberhaupt IRGENDETWAS davon uebersetzbar
war, weil beide in der Deklarations-Praeambel stehen und damit jede
Funktion mitreissen:

1. **Mehrere Felder gleichen Typs in einer struct-Zeile**
   (`int nargs, first, last, locals, frameBytes;`) -- behoben, die Aktion
   haengt jetzt an `structDeclarator` statt an `structField`, damit sie
   einmal pro FELD feuert statt einmal pro Zeile.
2. **Vorwaertsdeklaration zwischen globalen Variablen**
   (`static void fatal(const char* msg);` mitten im Deklarationsblock).
   `program` bestand aus ZWEI getrennten Wiederholungen -- erst alle
   Deklarationen, dann alle Funktionen -- was die C-uebliche freie
   Reihenfolge ausschloss. Jetzt eine gemeinsame Wiederholung mit
   `funcdef` zuletzt in der geordneten Auswahl.

**Pfeil-Operator `->` implementiert (207 Vorkommen im Backend, 0 in
`qcc_p.c` -- deshalb war er nie aufgefallen).** `p->f` ist in C nur
Kurzschreibweise fuer `(*p).f` und braucht deshalb KEINEN Index: die
Feldadresse ist schlicht Zeigerwert + Feldoffset. `IPADD` poppt den Zeiger
zuerst, also wird `PUSH <offset>` VOR dem Laden des Zeigers emittiert.
`tcNameEnd` haelt jetzt auch am `-` an (ein Minus im Index steht hinter
`[`, wo ohnehin abgebrochen wird); je ein Zweig in `tc_varref` (lesend)
und `tc_target` (schreibend, setzt `tcTargetIndirect`, woraus `tc_assign`
ein `STOREIND` macht). Ein Array-Feld liefert wie ueberall dessen ADRESSE
statt eines Wertes, `p->feld[i]` haengt ein zweites `IPADD` an. Live auf
Q9 verifiziert (Lesen und Schreiben ueber `->`, Ausgabe `summe=42`).

**Zweidimensionale struct-Felder** (`char args[6][64];`) ergaenzt -- der
letzte Praeambel-Blocker des Backends. `arrayLen` haelt jetzt die
GESAMTelementzahl, das neue `tcStructFieldRowLen` die Zeilenlaenge. Der
heikle Teil ist der ZUGRIFF: `x->args[i]` adressiert ZEILE i und liefert
einen ZEIGER auf deren erstes Element (`IPADDN` mit der Zeilengroesse in
Bytes), NICHT ein einzelnes Element. Wer das uebersieht, rechnet
`base + i*1` -- das kompiliert sauber, laeuft und liefert Muell. Deshalb
diagnostizieren alle uebrigen indizierten Zweige einen 2D-Feldzugriff
ausdruecklich, statt still mit falscher Schrittweite zu rechnen (echte
`x[i][j]`-Zugriffe kommen im Bootstrap-Ziel nicht vor). Live auf Q9
verifiziert: zwei Zeilenzeiger liegen exakt 16 Byte auseinander
(`diff=16`), Inhalte korrekt.

Damit parst die Backend-Praeambel ohne jede Behelfsaenderung.

**Neuer IR-Opcode `SWAP`** (`a, b -> b, a`) sowie `(*p)++` / `++(*p)`.
Mehrere Restkonstrukte scheiterten an derselben Wurzel: der Stack liess
sich nicht umordnen, es gab nur `DUP`. Der 68k-Codegen dafuer ist trivial
(zwei Pops, zwei Pushes). `(*p)++` selbst braucht ihn am Ende gar nicht --
Adresse und Wert werden schlicht mehrfach geladen, was seiteneffektfrei
ist, weil der Zeiger in einer Variablen steht; Postfix laesst den ALTEN
Wert ganz unten liegen, Praefix laedt den neuen nach dem Speichern neu.
`SWAP` bleibt aber die Voraussetzung fuer `a[i]++`, wo der Index nur EINMAL
ausgewertet werden darf und deshalb nicht nachgeladen werden kann. Live auf
Q9 verifiziert: `a=10 b=12 z=12` (Postfix liefert den alten, Praefix den
neuen Wert).

**Arraygroesse als konstanter Ausdruck** (`char buf[64 + 40];`, 16
Vorkommen im Backend) und der `(long)`-Cast. Der neue Helfer
`tcConstArrayLen` wertet Zahlen mit `+ - *` linksassoziativ aus dem
Rohtext aus -- die Deklarations-Aktionen lesen die Groesse ohnehin dort ab
und nicht ueber den Wertestapel, deshalb genuegte ein gemeinsamer
Auswerter an den zwei Scanner-Stellen statt einer Aenderung am
Ausdrucks-Codegen.

**`a[i]++` und die Kettenzuweisung `a = b = 0;`.** Beim Arrayelement darf
der Index nur EINMAL ausgewertet werden (Seiteneffekte), sein Wert liegt
beim Feuern der Aktion bereits auf dem Stack -- Postfix bekommt den alten
Wert per `SWAP` unter den Index, Praefix kommt mit zweimaligem `DUP` aus
und laedt den neuen Wert am Ende neu. Damit ist `SWAP` auch praktisch
belegt. Die Kettenzuweisung steht als erste Alternative in `assignStmt`;
das ist gefahrlos, weil Aktionen nicht sofort ausgefuehrt, sondern
protokolliert und beim Zurueckrollen verworfen werden (`actionLog`).
Wichtige Falle dabei: passt die innere Alternative, feuert ANSCHLIESSEND
auch die Aktion der umschliessenden Regel -- ohne Sperrflag entstand ein
ueberzaehliger Store. Beides live auf Q9 verifiziert (`a=10 b=12 t=12`
bzw. `a=7 b=7 g1=5 g2=5 c=3`).

Stand: `qcc_p.c` 4 von 338, `qcc_backend_c.cpp` 2 von 24 fehlerhaft. **Einziger verbleibender Praeambel-Blocker: ein
ZWEIDIMENSIONALES struct-Feld** (`char args[6][64];`, genau ein
Vorkommen) -- `structField` erlaubt nur eine Dimension. Solange das steht,
ist die gesamte Datei blockiert.

Vierte Runde: Hexzahlen ("0x20"). `number` ist ein LEXER-TOKEN, die Regel
bestimmt also auch, wie weit der Lexer liest -- die hex-Form muss deshalb
VOR der dezimalen stehen, sonst schluckt diese die fuehrende "0" allein
und laesst das "x" haengen.

Als Rest bleiben nur noch vier verschiedene Konstrukte: Kettenzuweisung
("a = b = 0;"), der Komma-Operator in "return p == e ? (*value = v, 1) : 0;",
"++" auf einem Arrayelement ("tcCallArgCount[tcCallDepth - 1]++;", die
Grammatik erlaubt bei preIncDec/postIncDec bisher nur einen blossen
Bezeichner) sowie drei Funktionen, deren Blocker erst am Funktionsende
sichtbar wird und noch nicht eingegrenzt ist.

Dritte Runde: die leere Anweisung ";" und -- entscheidend -- `charLit` als
LEXER-TOKEN. Zeichenliterale waren als reine Grammatikregel formuliert,
wodurch der Lexer das Leerzeichen in `' '` als Whitespace wegfrass, bevor
die Grammatik es sehen konnte; `stringLit` entgeht dem seit jeher genau
deshalb, weil es in `[LEXER]` als `TOKEN` deklariert ist. Dazu das
escapte Anfuehrungszeichen im String-Literal (`"a\"b"`) -- `character`
schloss `"` komplett aus, `strEscape` konsumiert Backslash und
Folgezeichen jetzt als Einheit; dekodieren konnte `tcDecodeStringLit` das
laengst. Die leere Anweisung war der zweite grosse Hebel: der generierte
Parser schreibt hinter JEDE Sprungmarke ein `L19: ;`.

Zweite Runde (ebenfalls 2026-08-10): globale Variablen mit Typedef-Typ,
`unsigned char`/`long` als globaler Typ sowie `++`/`--` auf Zeigern.
Die Funktionszahl blieb dabei bei 53 -- diese Funktionen haben mehrere
Blocker gleichzeitig, die einzelnen Konstrukte sind aber nachweislich
repariert (`while (...) p++;`, `d[n++] = x`, `*p++` uebersetzen jetzt).

`tc_globalend` erkannte Typen per Rohtext-Scan und kannte weder
Typedef-Namen noch `long`; ausserdem uebersprang der `unsigned`-Zweig
pauschal 12 Zeichen, was bei `unsigned char` (13) mitten in den
Bezeichner lief. Damit scheiterte JEDE globale Variable eines typedef'ten
Typs mit "bad global declaration" -- obwohl dieselbe Deklaration als
LOKALE laengst funktionierte. Nach dem Fix parst die komplette
Deklarations-Praeambel von `Data/qcc_p.c` (u.a. viele `static TCType ...`)
ohne eine einzige Meldung.

`++`/`--` auf Zeigern rechnet in ELEMENTEN: `IPADD` mit dem Tag des
Zieltyps statt eines schlichten `ADD`. Da `IPADD` den Zaehler UNTEN und
den Zeiger OBEN erwartet, wird der Zeiger beim Postfix zweimal geladen
statt per `DUP` vervielfaeltigt -- ein `DUP` laege an der falschen Stelle
relativ zum Zaehler. Live auf Q9 verifiziert (Summe ueber einen
durchlaufenen Puffer plus `p--` am Ende: `n=198 last=67`).

Geschlossene Luecken:

| Konstrukt | Vorkommen | Umsetzung |
|---|---:|---|
| `f(void)` Parameterliste | 364 | `funcParams = voidParams \| normalParams` |
| Zeichenliteral `'x'` | 603 | `charLit`, Typ int wie in C, Escapes `\n \t \r \0 \\` |
| `(void)ausdruck;` | 223 | `voidCastStmt`, emittiert `DROP` |
| `unsigned char` | 72 | auf QCCs char abgebildet (dort ohnehin nullerweitert) |
| `long` | 14 | auf int abgebildet (68k: beide 32 Bit) |
| `int a, b;` + Init je Deklarator | 11 + 9 | `varDeclarator` in der Wiederholung |
| `const` als Rueckgabetyp | 3 | aktionslose Kopie `retConstKw` |

**Wichtige Designaenderung: int/bool-Strenge auf C-Semantik gelockert.**
QCC verlangte in Bedingungen bisher einen echten `bool` und lehnte
`if (x)` mit int ab, ebenso `int x = (a == b);` und `return (a && b);`.
Das ist STRENGER als ISO C und erzwang in jedem Port Umschreibungen
(dokumentiert als Quirk 3/10 weiter unten). Da `docs/ISO_C_LUECKENLISTE.md`
ISO-C-Konformitaet als Ziel fuehrt und maschinell erzeugter C-Code wie
`Data/qcc_p.c` sonst unuebersetzbar bleibt, akzeptieren Bedingungen jetzt
jeden skalaren Typ (`tcIsTruthy`) und `bool` gilt als Ganzzahltyp. Die
Aenderung ist rein additiv -- was vorher zulaessig war, bleibt es -- und
codegen-seitig folgenlos, weil ein bool ohnehin als 0/1 im selben 32-Bit-
Slot liegt und `JZ`/`JNZ` den Wert typunabhaengig testen.

**Dabei gefundene Grammatik-Falle** (wichtig fuer kuenftige Erweiterungen):
Eine Alternation, deren zweiter Zweig mit einer OPTIONALEN Gruppe beginnt,
backtrackt an dieser Stelle NICHT. Der erste Versuch
`paramList = voidParamList | [ param {...} ]` schluckte bei `f(void* p)`
das `void` und der Parser kam nicht zurueck -- `SourceQCC/ebnf.tc` parste
danach gar nicht mehr. Funktioniert hat erst die Form mit zwei ECHTEN
NTS-Alternativen, welche die Klammern mit einschliessen (dasselbe Muster
wie `call`/`varRef` in `factor`), sodass die erste als GANZES scheitert.

**Regression:** `SourceQCC/ebnf.tc` (12317 IR-Zeilen) und `codegen.tc`
(15968) erzeugen nach allen Aenderungen bitgleich dieselbe IR wie vorher.

## Funktionszeiger implementiert (2026-08-10)

Der letzte Sprachblocker fuer den echten Compiler-Bootstrap ist beseitigt.
Umgesetzt ist genau der Satz, den `Data/qcc_p.c` fuer sein Aktions-Log
braucht: typedef, Struct-Feld, Parameter, Funktionsname als Wert,
Zuweisung und indirekter Aufruf.

**Typmodell:** neuer Basistyp `'F'` mit `pointers=1` (er IST ein Zeiger,
`tcIsPointer` liefert also korrekt 1 -- wichtig, weil Backend und IR
zwischen Zeiger- und Zahlwerten unterscheiden). `structId` traegt 1-basiert
die Signatur-Id, genau wie bei `'s'` der Struct-Index; `tcSameType`
vergleicht sie fuer `'F'` mit, zwei Funktionszeiger sind also nur bei
gleicher Signatur derselbe Typ.

**Grammatik:**
```
fnPtrTypedef = "typedef" type pointerDecl fnPtrOpen "*" fnPtrName ")" "(" externParamList ")" ";" .
indirectCall = varRef indCallOpen argList ")" .
```
`externParamList` wird bewusst wiederverwendet statt nachgebaut -- die Regel
parst bereits Typen mit optionalem `const` und optionalem Parameternamen,
und ihre Aktion sammelt die Typen im gemeinsamen Puffer. `indirectCall`
steht in `factor` NACH `call`: ein blosser Name wird von `call` abgefangen,
`actionLog[i].fn(` scheitert dort schon am `[` und faellt korrekt hierher.

**Funktionsname als Wert:** ein Name, der weder lokale/globale Variable
noch Enum-Konstante ist, aber eine bekannte Funktion benennt, wird zu
`PUSHFN` (implizites `&` wie in echtem C). Die Signatur wird bei Bedarf
angelegt, damit das auch ohne passendes typedef funktioniert und zwei
Funktionen gleicher Signatur zuweisungskompatibel sind.

**IR/Backend:** `PUSHFN <name>` und `CALLIND/CALLINDP <nargs>` (siehe
`docs/IR_OPCODES.md`). Der Funktionszeiger liegt UNTER den Argumenten --
das ergibt sich zwangslaeufig aus dem Parsen, weil bei `ausdruck(args)` der
Callee-Ausdruck vor den Argumenten ausgewertet wird und seinen Wert zuerst
ablegt (anders als beim direkten `CALL`, wo der Name gar keinen Code
erzeugt). Er wird deshalb in der Tiefe `nargs*4` gelesen statt gepoppt.

**Dabei gefundener Assemblerfehler:** `adda.l a4,d0` ist ungueltig -- ADDA
verlangt ein ADRESSregister als Ziel. Bei einem Datenregister muss es das
normale `add.l` sein; der echte `r68` weist die falsche Form korrekt ab.

**Bewusste Grenzen** (beide im Bootstrap-Ziel nicht relevant): (1) Beim
indirekten Aufruf wird nur die ARGUMENTANZAHL geprueft, nicht die
Argumenttypen -- `tc_arg` nimmt die Typen generisch vom Stapel und kann sie
mangels Funktionsnamen keiner Signatur zuordnen; der Rueckgabetyp ist
dagegen korrekt. (2) Ein Aufruf ueber eine Funktionszeiger-VARIABLE mit
blossem Namen (`f(1,2)`) geht nicht -- den faengt die vorangehende
`call`-Regel ab und meldet "unknown function". Der Weg ueber ein
Struct-Feld oder ein Array (`log[i].fn(...)`, das reale Muster) funktioniert.

**Live auf Q9 verifiziert:** zwei Testprogramme durch die volle Kette bis
zum echten `l68`-Modul. Erstes: typedef + Struct-Feld + Zuweisung +
indirekter Aufruf -> `42`. Zweites: das komplette Aktions-Log-Muster --
zwei VERSCHIEDENE Funktionen als Parameter uebergeben, gespeichert und
spaeter ueber dasselbe Zeigerfeld verteilt -> `41`.

**Regression:** `SourceQCC/ebnf.tc` (12317 IR-Zeilen) und `codegen.tc`
(15968) erzeugen bitgleich dieselbe IR wie vorher.

## `goto` + Sprungmarken implementiert (2026-08-10)

Der größte der drei Bootstrap-Blocker (786 `goto`/465 Marken in
`Data/qcc_p.c`) ist beseitigt. Neu in der Grammatik:

```
gotoStmt  = "goto" ident ";" .
labelStmt = ident ":" .
```

Beide Aktionen (`tc_goto`/`tc_label`) hängen bewusst an der
VOLLSTÄNDIGEN Regel statt an einer Namens-Unterregel: `labelStmt` beginnt
mit einem `ident` und wird deshalb bei jedem ident-Statement (`foo();`,
`x = 1;`) zuerst probiert und danach zurückgerollt -- eine Aktion an einer
Unterregel würde dort spekulativ feuern und ein `LABEL` emittieren, das
gar nicht hingehört. Den Bezeichner holt sich die Aktion selbst aus dem
erkannten Textbereich (`tcIdentFromSpan`). Verifiziert: ein Programm aus
`foo(); x = x + 1;` erzeugt 0 `LABEL`/`JMP`-Zeilen.

Sprungmarken sind funktionslokal (Reset in `tc_defname`, wie
`tcLocalCount`); Name→IR-Labelnummer über `tcGotoNames`/`tcGotoLabel`.
Vorwärtssprünge funktionieren dadurch ohne Sonderbehandlung -- die Nummer
wird beim ersten Auftreten vergeben, egal ob Sprung oder Marke zuerst kam.
`tc_funcend` meldet angesprungene, aber nie definierte Marken;
`tc_label` meldet doppelte.

**Dabei gefundene und behobene Regression:** `caseBody = { statement }` --
beim `default:` eines `switch` probiert der Parser erst `statement`, und
`labelStmt = ident ":"` passt darauf. Das `default:` wurde also als
Sprungmarke verschluckt, `defaultGroup` kam nie zum Zug (aufgefallen an
1034 abweichenden IR-Zeilen in `SourceQCC/ebnf.tc`). Da der EBNF-Dialekt
keinen Except-/Negationsoperator hat (ISO 14977's `-` ist bewusst nicht
implementiert), ist "ident außer Schlüsselwort" nicht direkt ausdrückbar --
Lösung: `statement = labelStmt | unlabeledStmt`, und `caseBody` benutzt
`{ unlabeledStmt }`. **Dokumentierte Grenze:** eine Marke direkt auf der
Ebene eines case-/default-Rumpfes ist damit nicht möglich, in einem
geschachtelten Block darin schon (`block` benutzt wieder `statement`). Im
Bootstrap-Ziel `Data/qcc_p.c` kommt der Fall nicht vor (nachgezählt: 0).

**Regressionsnachweis:** `SourceQCC/ebnf.tc` (12317 Zeilen IR) und
`codegen.tc` (15968 Zeilen IR) erzeugen mit dem neuen Parser BITGLEICH
dieselbe IR wie vorher -- beide enthalten kein `goto`, dürfen sich also
nicht ändern. Ebenso das Beispielprojekt.

**Live auf Q9 verifiziert:** ein `goto`-Schleifenprogramm (Summe 1..10)
durch die volle Kette (`qcc_p` → `qcc_backend -os9 -largedata` → echter
`r68` → echter `l68` gegen echte `clib.l`) gab auf dem Emulator korrekt
`55` aus.

Damit verbleiben für den echten Bootstrap: Funktionszeiger (1 Fundstelle)
und der Präprozessor -- letzterer ist erledigt, sobald die 2 `#include`
durch `extern`-Deklarationen ersetzt werden (etabliertes Projektmuster,
siehe `SourceQCC/ebnf.tc`) oder `xcc -pp` vorgeschaltet wird, das die 13
`#define`/2 `#include` restlos auflöst (getestet, +317 Zeilen).

## Schritt 3 ABGESCHLOSSEN: selbstgehosteter Generator läuft live auf Q9 (2026-08-10)

Der von QCC selbst übersetzte EBNF-Generator (`SourceQCC/ebnf.tc` +
`codegen.tc`) läuft auf dem echten Q9-Emulator und erzeugt eine mit der
`xcc`-gebauten Referenz **bitgleiche** Parserausgabe (`qcc_p.c`, md5
`20a990d7ed2b8e816c95163c546b667c`). Damit ist die seit 2026-07-25 als
"nächster Schritt" offene Live-Q9-Verifikation erledigt und der
Selfhosting-Kreis geschlossen.

Dafür wurde ein bis dahin unsichtbarer 68k-Backend-Bug gefunden und
behoben: **`char`-Parameter wurden auf Big-Endian am falschen Byte des
32-Bit-Slots gelesen** (Slot-Basis statt Slot+3), wodurch
`astPushRNG(char lo, char hi)` beide Zeichenbereichsgrenzen als 0 bekam --
jeder Bereich der Grammatik (`"0"~"9"`, `"A"~"F"`, `"a"~"z"`, `"A"~"Z"`)
landete als `0x00`/`0x00` im erzeugten Parser. ARM64 (Little-Endian) und
QCCVM (typisierte Slots) konnten den Fehler prinzipiell nicht zeigen.

Vollständige Beschreibung inkl. Fix, Reproduktionsweg und bewusst offener
Restlücke: `docs/STATUS.md`, Abschnitt "Selfhosting-Kreis geschlossen".
Testskript: `test/expect/test_qcc_selfhost_run.exp` (im Q9-Flux-Repo).

## Selfhosting L2 Vollport: `codegen.cpp` ABGESCHLOSSEN (2026-07-25)

**Stand 2026-07-25 abends: der komplette Vollport von `Source/codegen.cpp`
(1582 Zeilen) nach `SourceQCC/codegen.tc` ist fertig** -- AST-Aufbau,
LEXER-/CODEGEN-/ACTIONS-Konfigurationsparser, AST-Validierung, sowie C- UND
68k-Backend-Codegenerator. Sieben Vollport-Regressionstests in `runtests.sh`
verankert (jeweils echter `r68`+`l68`-Link gegen echte `clib.l`). Naechster
Schritt: `Source/parsec.cpp` (2149 Zeilen, komplett unberuehrt), danach
Live-Q9-Verifikation. Details zur Entstehung (chronologisch) unten.

Nutzerwunsch: `codegen.cpp` (kleinere der beiden Kerndateien) nach QCC
portieren -- `SourceQCC/codegen.tc` (neues Verzeichnis) enthaelt zunaechst
die AST-Aufbau-Schicht (`astReset`/`astMark`/`newNode`/`pushNode`/`astPushTS`/
`astPushRNG`/`astPushNTS`/`groupAs`/`astGroupSeq`/`astGroupAlt`/`wrapAs`/
`astWrapOpt`/`astWrapRep`/`astFinishRule`) + gemeinsame Helfer
(`sanitizeName`/`newLabel`/`ruleIndexByName`), verifiziert in QCCVM UND per
echtem `r68`-Assembler.

**2026-07-25 (spaeter, direkt im Anschluss an den -largedata-Funktionsaufruf-
Fix): naechster Ausschnitt portiert -- LEXER-Konfigurationsparser** (`lexAnyBlockNested`/
`lexUnquoteAt`/`lexUnquote`/`lexParseConfig`/`markLexicalNode`/`computeLexicalSet`,
das `[LEXER]`-Konfigurationsblock-Handling aus `Source/codegen.cpp` Zeilen 207-460).
Dabei VIER weitere, rein mechanische QCC-Abweichungen gefunden (keine davon
brauchte einen Compiler-Fix, alles Workarounds im Port selbst):
1. QCC hat keine Zeichenliterale (`'x'`) -- ueberall numerische ASCII-Codes
   verwendet (34='"', 92='\', 116='t' usw.).
2. QCC lehnt partielle Indizierung eines mehrdimensionalen Arrays ab (schon
   bekannt aus den ND-Array-Tests) -- betroffene 2D-Arrays (`lexLineComment`,
   `lexBlockOn`/`lexBlockOff`, `lexRoots`) als FLACHE 1D-Arrays mit manueller
   Offset-Berechnung `&flat[i*breite]` umgesetzt.
3. Der `const char** nextOut`-Ausgabeparameter von `lexUnquoteAt` wurde zu
   einem globalen `lexUnquoteNext` vereinfacht (gleiches Muster wie beim
   ActionRoutine-Piloten).
4. **NEU ENTDECKTER GRENZFALL:** QCC-String-Literale unterstuetzen KEIN
   escaptes Anfuehrungszeichen (`\"` bricht das Parsen mit blossem "FAIL", kein
   Diagnosetext) -- die `character`-Regel in `Data/qcc.ebnf` schliesst das
   Anfuehrungszeichen grundsaetzlich aus, auch direkt nach einem Backslash,
   obwohl `tcDecodeStringLit` die Escape-DEKODIERUNG dafuer bereits generisch
   haette (die Grammatik kommt aber nie so weit). Diagnosetexte im Port bewusst
   ohne Anfuehrungszeichen umformuliert, statt die Grammatik zu erweitern (waere
   ein eigener, riskanterer Sprachfeature-Schritt, siehe "Bewusst offen" weiter
   unten). Ausserdem gefunden: QCC unterscheidet STRENG zwischen `int` und
   `bool` -- ein `int`-Array-Element oder eine `int`-Variable direkt in einer
   `if`/`&&`/`!`-Bedingung zu verwenden (das uebliche C-Idiom "0/1 als Wahrheits-
   wert") wird als Typfehler abgelehnt, muss explizit `!= 0`/`== 0` geschrieben
   werden.

**Verifikation (dreifach, da QCCVM kein `CALLEXT`/`extern` kennt):**
(a) die ECHTE Version (mit `extern strncmp`/`strchr`/`strrchr`/`strstr`/
`memcpy`) kompiliert sauber, assembliert (echter `r68`) und linkt (echter
`l68` gegen echte `clib.l`) -- Regressionstest in `runtests.sh` verankert;
(b) eine EINMALIGE Kopie mit selbstgeschriebenen QCC-String-Helfern
(`tcStrncmp`/`tcStrchr`/... statt `extern`) lieferte ueber QCCVM UND (c)
nativ per ARM64-Backend-Kompilat exakt dieselben 19 erwarteten Werte --
inklusive der TRANSITIVEN lexikalischen Markierung (`markLexicalNode` markiert
eine per `NTS` referenzierte Regel korrekt mit) und der korrekten
Nicht-Markierung einer unreferenzierten Regel. Getestet mit einer echten
Konfiguration (WHITESPACE-Escape-Dekodierung, TOKEN-Registrierung, mehrere
COMMENT LINE/BLOCK-Marker, COMMENT BLOCK NESTED-Erkennung) analog zum
`[LEXER]`-Block in `Data/qcc.lextab` selbst. Die (b)/(c)-Verifikation ist
NICHT dauerhaft in `runtests.sh` verankert (Wartungsaufwand einer zweiten
String-Bibliothek nur fuers Testen unverhaeltnismaessig) -- nur (a) ist die
dauerhafte Regression.

**2026-07-25 (noch spaeter, direkt im Anschluss): CODEGEN-Konfigurationsparser
portiert** (`cgenWantOS9`/`cgenStartRule`/`cgenParseConfig`, das
`[CODEGEN]`-Konfigurationsblock-Handling aus `Source/codegen.cpp` Zeilen
462-535) -- strukturell fast identisch zu `lexParseConfig` (letztes Wort einer
Zeile extrahieren), aber deutlich einfacher (kein Escape-Handling noetig, das
Konfigurationsformat braucht keine Anfuehrungszeichen). Gleiche dreifache
Verifikationsmethode: (a) echte Version mit `extern strncmp`/`strlen`/`memcpy`
kompiliert, assembliert (echter `r68`) und linkt (echter `l68` gegen echte
`clib.l`) -- Regressionstest in `runtests.sh` verankert; (b)/(c) eine einmalige
Kopie mit den bereits vorhandenen QCC-String-Helfern lieferte ueber QCCVM
UND nativ per ARM64 identische Werte fuer eine Test-Konfiguration
(`M68K OS9` + `M68K PSECT = mySect` + `START myRule`): `cgenWantOS9()`=1,
`cgenPsect`="mySect"+Nullterminator, `cgenStartRule()`="myRule"+Nullterminator.

**2026-07-25 (noch spaeter, direkt im Anschluss): ActionRoutine-Verwaltung UND
ACTIONS-Konfigurationsparser portiert** (`freeRoutines`/`pushRoutineC`/
`pushRoutine68k`/`routineTextC`/`routineText68k`/`lastWord`/`growLineBuf`/
`growCollectBuf`/`actionsParseConfig`, das `[NUTZER-CODE]`-Block-Handling aus
`Source/codegen.cpp` Zeilen 580-750). `pushRoutineC`/`pushRoutine68k` sind ZWEI
fast identische Funktionen statt EINER generischen mit `ActionRoutine**`-
Parameter wie im C++-Original (QCC hat keine Generik/Funktionszeiger,
`routinesC`/`routines68k` sind wie im Original file-scope-Globale -- direktes
Mutieren ist einfacher und sicherer, gleiches Muster wie beim ActionRoutine-
Piloten Milestone B). `sizeof(struct ActionRoutine)` (statt einer
hartkodierten Bytezahl) sorgt dafuer, dass `realloc`-Aufrufe automatisch die
richtige, architekturabhaengige Struktgroesse verwenden (68k: kleinere
Pointer-Groesse, ARM64: 8-Byte-Pointer).

**Verifikation NUR strukturell (wie beim Piloten selbst):** kompiliert sauber
(keine Semantikfehler), assembliert (echter `r68`), linkt (echter `l68` gegen
echte `clib.l`) -- Regressionstest in `runtests.sh` verankert. Eine versuchte
TIEFERE Logikverifikation (QCCVM/ARM64 mit selbstgeschriebenen malloc/
realloc-Ersatzfunktionen fuer eigenstaendige Ausfuehrbarkeit ausserhalb von
clib.l) scheiterte an Grenzen der TESTUMGEBUNG, nicht des Ports: QCCVMs
Zeigermodell ist nicht byte-adressierbar und vertraegt keine malloc-Heap-
Umdeutung auf struct-Zeiger ("unaligned pointer"); eine ARM64-Reproduktion mit
selbstgebautem Bump-Allocator stuerzte ab (vermutlich ein Bug im
Test-Stub-Allocator selbst -- der geprueften Port-Code verwendet durchgaengig
dieselben, bereits mehrfach bewiesenen Muster wie `arr[i].feld` ueber
Zwischenvariable). Echte End-zu-End-Verhaltensverifikation bleibt der
Live-Q9-Ausfuehrung vorbehalten (bereits als eigener offener Schritt vermerkt,
siehe "Bewusst offen").

**2026-07-25 (noch spaeter, direkt im Anschluss): AST-Validierung portiert**
(`isWordLiteral`/`nodeNullable`/`validateRepeatProgress`/
`validateAstForCodegen`, `Source/codegen.cpp` Zeilen 750-857). Prueft VOR der
Ausgabe zwei Dinge: (1) eine Wiederholung mit nullbarem Rumpf (z.B. `{ [x] }`)
waere im erzeugten Parser eine Endlosschleife -- wird per Fixpunktanalyse
ueber gegenseitig rekursive Regeln erkannt und abgelehnt; (2) zwei Regeln, die
nach der `$`->`_`-Normalisierung denselben Namen ergeben, wuerden doppelte
C-Funktionen bzw. 68k-Labels erzeugen -- wird ebenfalls erkannt und abgelehnt.
`ruleNullable[AST_MAX_RULES]` war im Original ein LOKALES Array mit
Nullinitialisierer (`= { 0 }`) -- hier als GLOBAL angelegt und per Schleife
genullt (gleiches Muster wie `ruleIsLexical`), um die Frage nach lokalen
int-Array-Initialisierern gar nicht erst zu stellen.

**Zwei weitere Stolperfallen dabei gefunden** (beide sofort im Port behoben):
ein `return <Vergleichsausdruck>;` (z.B. `return txt[0] == 0;`) aus einer als
`int` deklarierten Funktion wird ALS RUECKGABEWERT ebenfalls als Typfehler
abgelehnt ("return expects int, got bool") -- staerker als die bereits
bekannte int/bool-Regel bei `if`/`&&`, da hier sogar der Aufruf noch als
"exit 0, OK" durchlaeuft und der Fehler nur im STDOUT auftaucht (siehe
naechster Punkt). Und: **semantische Fehler stoppen den generierten Parser
NICHT** -- `tcSemanticErrors` wird zwar hochgezaehlt, aber NIRGENDS
ausgewertet (weder in `Data/qcc.lextab` noch im `codegen.cpp`-Treiber);
der Compiler druckt die Fehlermeldung, gibt trotzdem "OK" aus und exit 0.
**Deshalb ab sofort bei jedem Kompilieren zusaetzlich `grep -n "^qcc:"`
auf STDOUT/stderr pruefen, nicht nur auf "OK"/exit-Code verlassen** (nach
diesem Fund rueckwirkend den gesamten kumulativen Dateistand erneut
geprüft -- keine versteckten Fehler in den bereits gemergten Chunks).

**Verifiziert:** einmalig per QCCVM (mit QCC-eigenen Stand-ins fuer
`strcmp`/`isalpha`/`isalnum` statt `extern`) -- alle 10 erwarteten Werte
trafen exakt zu, inklusive BEIDER Diagnosepfade (nullbare Wiederholung UND
Regelnamenkollision, mit den erwarteten Diagnose-Seiteneffekten VOR dem
jeweiligen Rueckgabewert). ARM64 hier NICHT moeglich: das kumulative
Gesamtkompilat enthaelt bereits `free`-Aufrufe aus dem ActionRoutine-Chunk,
und ARM64 lehnt `CALLEXT` GRUNDSAETZLICH ab (Ganzprogrammpruefung beim
Codegen, unabhaengig von tatsaechlicher Erreichbarkeit) -- QCCVM dagegen
meldet einen unbekannten Opcode nur bei tatsaechlicher AUSFUEHRUNG, was hier
unproblematisch war. Dauerhafte Regression wie ueblich: echter `r68`+`l68`
gegen echte `clib.l`, in `runtests.sh` verankert.

**2026-07-25 (noch spaeter, direkt im Anschluss): C-Backend-Codegenerator
portiert** (`emitCString`/`emitLongerLiteralRejectC`/`genNodeC`,
`Source/codegen.cpp` Zeilen 858-1000). Das ist die ERSTE Beruehrung mit
ECHTER Dateiausgabe im Port (`fopen`/`fprintf`/`fputc`/`fclose` statt nur
stdout-Diagnosen wie bisher) -- `FILE*` wird als `void*` gefuehrt (QCC hat
keinen `FILE`-Struct-Typ, der ABI-Aufruf braucht nur einen opaken Zeiger, den
`clib.l` selbst interpretiert). `clib.l` hat KEIN `snprintf` (nur `sprintf`,
per `strings` bestaetigt, aeltere Microware-Bibliothek) -- deshalb `sprintf`
ohne Laengenlimit verwendet (unschaedlich hier: alle Aufrufe schreiben kurze,
feste Formate in ausreichend grosse Puffer). `charComment` (nur vom 68k-Chunk
gebraucht) wurde bewusst NOCH NICHT mitportiert, da fuer den C-Backend-Chunk
selbst ungenutzt.

**NEUE, bisher unbekannte QCC-Grenze gefunden:** QCC kann KEINE EIGENEN
variadischen Funktionen definieren (nur variadische `extern`-Aufrufe wie
`printf`/`fprintf` selbst) -- ein QCC-eigener Stand-in fuer `fprintf`
(fuer eine QCCVM/ARM64-Tiefenverifikation wie bei den String-Funktionen
zuvor) ist deshalb grundsaetzlich NICHT moeglich. Verifikation bleibt daher
bei "kompiliert sauber + echter `r68`/`l68`-Link" (wie beim ActionRoutine-
Chunk) -- erfolgreich mit einem Testfall, der alle sieben `AstKind`-Faelle
(SEQ/ALT/OPT/REP/TS/RNG/NTS) durchlaeuft und in eine echte Datei schreibt.
Echte Textinhalts-Verifikation bleibt der Live-Q9-Ausfuehrung vorbehalten.

**2026-07-25 (noch spaeter, direkt im Anschluss): genParserC portiert** (der
Rest des C-Backends: Datei-Header, Lexer-Helfer `ws()`/`idch()`, Action-Log-
Runtime-Geruest, eine `p_<regel>()`-Funktion pro Regel, `main()` --
`Source/codegen.cpp` Zeilen 1002-1168). SECHS Stellen im C++-Original betten
ein Anfuehrungszeichen DIREKT in einen `fprintf`-Formatstring ein -- geht in
QCC nicht, per `fputc(34,fp)`-Aufteilung umgeschrieben (Text-vor-dem-Quote
/ `fputc(34,fp)` / Text-in-den-Quotes / `fputc(34,fp)` / Text-danach). ZWEI
dieser Stellen haben zusaetzlich ein `"%%"`-Selbstescape im Original
(literales `%` ohne eigenes Substitutionsargument) -- als `"%s"`-Argument
uebergeben statt direkt in den Formatstring geschrieben, da QCC/`clib.l`s
`fprintf` ein `%` sonst als Formatzeichen re-interpretiert haette.

**Dabei ZWEI eigenstaendige, echte Bugs gefunden und behoben (nicht im Port
selbst, sondern in der Toolchain):**

1. **Neuer QCC-PARSER-Bug:** ein QCC-String-Literal, das die
   Zeichenfolge `"&&"` oder `"||"` als reinen TEXT enthaelt (hier: generierter
   C-Code braucht selbst `&&`/`||` in `ws()`/`idch()`), loeste eine falsche
   `"qcc: logical-frame mismatch"`-Diagnose aus (das interne
   `tcLogicDepth`-Tracking fuer Kurzschluss-Operatoren wird offenbar auch
   INNERHALB von String-Literalen faelschlich angestossen). NICHT die
   Grammatik gefixt (Risiko/Aufwand vs. Nutzen zu hoch fuer diesen Zweck) --
   stattdessen jede betroffene Stelle per `fputc(38,fp)`/`fputc(124,fp)`
   umgeschrieben (`emitAndAnd`/`emitOrOr`-Helfer in `SourceQCC/codegen.tc`),
   sodass `"&&"`/`"||"` nie als zusammenhaengende Zeichenfolge in EINEM
   QCC-String-Literal auftaucht -- identischer Ausgabeninhalt, nur anders
   emittiert.
2. **Echter SKALIERUNGSBUG im 68k-Backend selbst** (`Source/qcc_backend_c.cpp`):
   das `-largedata`-Datenmodell lud JEDEN Tabelleneintrag bisher per EIGENEM
   PC-relativem Label (`"movea.l tc_ga_X(pc),reg"`) -- das brach, sobald der
   GESAMTE Funktionscode zwischen einer frueh emittierten Funktion (z.B.
   `main`, die per `-largedata`-Funktionsaufruf-Fix aus PR #43 immer zuerst
   emittiert wird) und der Tabelle selbst (die NACH ALLEN Funktionsrumpf-
   Texten lag) mehr als 32 KB umfasste -- derselbe 16-Bit-PC-relative-
   Displacement-Grenzwert wie ueberall, nur diesmal fuer den TABELLENZUGRIFF
   SELBST statt fuer die Daten dahinter. Trat zum ersten Mal beim
   Skalierungstest fuer `SourceQCC/codegen.tc` selbst auf (kumulatives
   Kompilat inzwischen weit ueber 32 KB Code) und liess dabei RUECKWIRKEND
   **ALLE fuenf bisherigen Vollport-Regressionstests** fehlschlagen (nicht nur
   den neuen) -- ein ernstes, dringendes Infrastrukturproblem, sofort behoben.
   GEFIXT nach EXAKT demselben Muster wie `tc_functab`/`a4` (siehe
   `emitCall()`-Kommentar): ein bisher freies Register (`a3`) wird EINMAL beim
   Programmstart auf die absolute Adresse EINER kombinierten Tabelle
   (`tc_gadata`, direkt nach `tc_functab`, VOR allen Funktionsrumpf-Texten)
   gesetzt (`"lea tc_gadata(pc),a3"`; die Tabelle selbst wird seit dem
   68k-Backend-Chunk unten IMMER emittiert, auch ohne echte Globale, siehe
   dort); jeder Globalzugriff wird zu
   `"move.l <gidx*4>(a3),reg"` statt einem PC-relativen Tabellen-Label-Load.
   `tools/qcc68sim.py` (Test-Simulator) musste dafuer `a3` in sein
   generisches Adressregister-Dict aufnehmen (war zuvor auf a0/a2/a4
   beschraenkt). **Verifiziert:** volle `runtests.sh` wieder komplett gruen
   (alle sechs Vollport-Tests inkl. des NEUEN, reichhaltigen genParserC-Tests
   mit SEQ/ALT/OPT/REP/NTS-Kombination -- genau der Testfall, der den Bug
   urspruenglich aufdeckte, kompiliert/assembliert/linkt jetzt fehlerfrei),
   sowie die bereits bestehenden `-largedata`-Tests (grosses struct-Array,
   150-Funktionen-Skalierungstest) weiterhin gruen.

**2026-07-25 (noch spaeter, direkt im Anschluss): 68k-Backend portiert --
DAMIT IST DER GESAMTE VOLLPORT VON `codegen.cpp` NACH QCC ABGESCHLOSSEN.**
(`charComment`/`emitConsume68k`/`emitLongerLiteralReject68k`/`genNode68k`/
`emitLexHelpers68k`/`genParser68kTo`/`genParser68k`/`genParser68kOS9`,
`Source/codegen.cpp` Zeilen 858 + 1181-1580). Erzeugt reinen 68k-
Assemblertext -- KEINE C-Operatoren (`&&`/`||`) und KEINE eingebetteten
Anfuehrungszeichen im generierten Code, daher weder der `emitAndAnd`/
`emitOrOr`- noch der `fputc(34,fp)`-Workaround aus `genParserC` hier
gebraucht.

**Drei weitere Funde/Fixes waehrend dieses Chunks:**

1. **Dieselbe `"?"`-Variante des logical-frame-Bugs** (siehe oben): ein
   String-Literal mit `"?"` als Text (`"Identifikator-Zeichen? d0.b..."`)
   loeste `"qcc: conditional-frame mismatch"` aus (`tcTernaryDepth`-
   Tracking fuer den Ternary-Operator). Gefixt per neuem `emitQMark(fp)`-
   Helfer (ein `fputc(63,fp)`), analog zu `emitAndAnd`/`emitOrOr`.
2. **Backend-eigene `MAX_GLOBALS`-Grenze (256, GETRENNT von der Frontend-
   Grenze in `Data/qcc.lextab`)** blockierte den Skalierungsnachweis:
   JEDES String-Literal im QCC-Quelltext wird zu einem anonymen
   `__strN`-Global, das kumulative Kompilat hat inzwischen weit ueber 256
   davon. Erhoeht auf 1024 (`Source/qcc_backend_c.cpp`).
3. **Ein WEITERER echter Skalierungsbug im 68k-Backend:** `tc_extcall_tmp`
   (der Scratch-Puffer fuer externe Aufrufe mit Stack-Argumenten) wurde
   bisher IMMER per PC-relativem `"lea tc_extcall_tmp(pc),a0"` DIREKT an der
   Aufrufstelle referenziert -- unabhaengig von `-largedata`. Brach aus
   demselben Grund wie der `tc_gadata`-Fund zuvor (Aufrufstelle kann
   ueberall im Programm liegen, der Puffer selbst liegt spaet). Gefixt nach
   demselben `a3`-Muster: `tc_extcall_tmp` bekommt einen ZUSAETZLICHEN
   Eintrag in `tc_gadata` (Offset `globalCount*4`, direkt nach allen echten
   Globalen); `tc_gadata` wird deshalb jetzt IMMER emittiert (nicht nur bei
   `globalCount > 0`), und `"lea tc_gadata(pc),a3"` wird jetzt IMMER gesetzt
   (wie `a4`), nicht mehr nur bei vorhandenen echten Globalen.

**Verifiziert:** volle `runtests.sh` komplett gruen (alle SIEBEN Vollport-
Tests), inklusive eines neuen 68k-Backend-Tests mit derselben reichhaltigen
SEQ/ALT/OPT/REP/NTS-AST-Kombination wie beim `genParserC`-Test -- echter
`r68` assembliert, echter `l68` linkt gegen echte `clib.l`.

**Bei diesem ersten Schritt vier eigenstaendige, bisher unbekannte
Einschraenkungen gefunden und (bis auf die letzte) behoben:**

1. **Deklarationen-vor-Funktionen-Zwang:** die QCC-Grammatik
   (`program = {globalDecl|structDecl|typedefDecl|enumDecl|externDecl} {funcdef}`)
   erlaubt KEINE Verschachtelung -- ALLE globalen Deklarationen muessen VOR
   ALLEN Funktionsdefinitionen stehen, sonst "FAIL" beim Parsen ohne
   hilfreiche Fehlermeldung. War nie aufgefallen, da jeder bisherige Test
   diese Reihenfolge zufaellig schon einhielt. **Kein Compiler-Fix noetig**
   (bewusst als Struktur-Konvention fuer alle kuenftigen `.tc`-Dateien
   uebernommen: erst ALLE Deklarationen, dann ALLE Funktionen).
2. **`arr[i].feld[j]`-Erinnerung:** `nodes[id].text[0] = 0;` (Array-Feld
   direkt indiziert) traf die seit Milestone A bekannte, bewusste
   Einschraenkung -- behoben mit der etablierten Zwischenvariable.
3. **`qcc_backend_c.cpp`/`qcc_arm64_backend_c.cpp`: `MAX_ARRAY_LEN`-Grenze
   (4096) fuer GARRAY-Laenge war zu eng fuer ein grosses, aber komplett
   nullinitialisiertes Array (der urspruengliche `nodes[8192]`-AST-Puffer,
   458 KB).** Behoben: die Grenze gilt jetzt NUR fuer tatsaechlich per GINIT
   gesetzte Indizes (neues `hasGinit`-Flag pro Global), nicht mehr fuer die
   deklarierte Array-Laenge selbst. Ein nie initialisiertes Array wird jetzt
   KOMPAKT gefuellt -- 68k: mehrere kommagetrennte Nullen pro `dc.l`/`dc.b`-
   Zeile (analog zum bestehenden `tc_extcall_tmp`-Muster, r68 kennt kein
   `ds.b`/`rmb`); ARM64: echtes `.zerofill`-BSS (kein `.byte`/`.long` pro
   Element noetig). `tools/qcc68sim.py` (Test-Simulator) musste dafuer
   kommagetrennte Mehrfachwerte pro `dc.l`/`dc.b`-Zeile lesen lernen (kannte
   bisher nur GENAU einen Wert pro Zeile).
4. **ECHTE 68000-Hardware-Grenze -- inzwischen behoben (2026-07-25, noch
   selber Tag, Nutzerwunsch "Compiler-Schalter fuer ein grosses
   Speichermodell"):** der 68k-Backend adressiert JEDES Globale
   AUSSCHLIESSLICH PC-relativ (`lea tc_g_X(pc),a0`) -- eine 16-Bit-
   Displacement-Grenze (±32 KB), die ECHTE 68000-Hardware-Eigenschaft ist,
   keine Software-Grenze. Empirisch am echten `r68`-Assembler bestaetigt:
   schon ein 57-KB-Array (`nodes[1024]`) wurde mit "value out of range"
   abgelehnt. **Fix: neuer Compiler-Schalter `-largedata`** fuer
   `qcc_backend_c.cpp` (kleines/grosses "Speichermodell", klassisches
   Far-Pointer-Prinzip alter segmentierter Architekturen auf 68k-PC-relative-
   Adressierung uebertragen) -- Standard ("small") bleibt unveraendert
   PC-relativ (schnell/kompakt, aber ±32 KB Reichweite); `-largedata`
   ("large") fuegt pro Globaler einen 4-Byte-Indirektionstabelleneintrag
   (`tc_ga_X: dc.l tc_g_X`) hinzu -- `dc.l tc_g_X` ist eine ganz normale,
   vom Linker aufgeloeste ABSOLUTE Adresse OHNE Distanzbeschraenkung; nur das
   *Laden* dieses kleinen Tabelleneintrags selbst bleibt PC-relativ (die
   Tabelle liegt bewusst DIREKT NACH dem Code, VOR den potenziell riesigen
   Daten, damit sie selbst immer erreichbar bleibt). Kostet einen
   zusaetzlichen Speicherzugriff pro Globalzugriff (`movea.l` statt `lea`/
   direktem `move.l`), deshalb bewusst nicht der Standard. Zusaetzlich:
   Groessen-Heuristik in `main()` warnt bereits im STANDARD-Modus, wenn die
   globalen Daten (>16000 Byte) wahrscheinlich zu gross werden -- BEVOR der
   echte `r68` mit dem kryptischen "value out of range" scheitert.
   **Empirisch verifiziert mit der ORIGINALEN Kapazitaet:** `nodes[8192]`
   (458 KB, `SourceQCC/codegen.tc` ist wieder auf die volle Original-
   Kapazitaet zurueckgesetzt) kompiliert, assembliert (echter `r68`) UND
   linkt (echter `l68` gegen `clib.l`) jetzt vollstaendig mit `-largedata`.
   `tools/qcc68sim.py` (Test-Simulator) musste dafuer `movea.l` als
   Instruktion sowie `tc_ga_`-Label/Symbolreferenzen in `dc.l`-Werten lernen.
   **Betrifft NICHT NUR diesen Port** -- jedes QCC-Programm mit viel
   globalem Zustand kann jetzt bei Bedarf `-largedata` verwenden.
5. **`-largedata` auf FUNKTIONSAUFRUFE erweitert (2026-07-25, Nutzerwunsch
   "koennen wir automatisch eine jmp table bauen, wenn die Spruenge zu gross
   werden?"):** `bsr tc_target` ist GENAUSO PC-relativ-16-Bit begrenzt wie
   die Global-Adressierung oben -- nur ueber Code- statt Datendistanz.
   `-largedata` deckt das jetzt zusaetzlich ab: eine Funktions-
   Indirektionstabelle `tc_functab` (ein `dc.l tc_<name>`-Eintrag pro
   QCC-Funktion, danach 8 feste Eintraege fuer die Laufzeit-Helfer
   `tc_mul_i32`/`tc_div_i32`/`tc_udiv_u32`/`tc_mod_i32`/`tc_umod_u32`/
   `tc_putint`/`tc_putuint`/`tc_putchar`) plus ein EINMALIG gesetztes
   Adressregister `a4` (`lea tc_functab(pc),a4`, direkt nach dem
   Tabellen-eigenen PC-relativen Ladebefehl selbst unbeschraenkt). Aufrufe
   werden zu `move.l N(a4),a2` + `jsr (a2)` statt `bsr tc_target`; `a2`
   wurde als Aufruf-Scratchregister gewaehlt, weil es an JEDER Aufrufstelle
   nachweislich frei ist (anders als `a0`/`a1`, die an manchen Stellen ueber
   den Aufruf hinweg einen Wert halten). **Dabei zwei weitere, beim
   Skalierungstest (150 generierte Funktionen, >32 KB Code) gefundene und
   behobene Fehler:**
   - Im `-os9`-Modus ist `main` selbst der Einsprungpunkt (kein separates
     `tc_start`) und fuehrt sein EIGENES `lea tc_functab(pc),a4` aus -- das
     ist selbst PC-relativ und brach, sobald `main` NICHT die erste Funktion
     im Quelltext ist (weit hinter der Tabelle liegend). **Fix:** `main`
     wird bei `-os9 -largedata` jetzt immer als allererste Funktion
     emittiert, unabhaengig von ihrer Position in der Funktionstabelle
     (deren Eintraege ueber absolute, vom Linker aufgeloeste Adressen laufen
     und daher unabhaengig von der Emissions-Reihenfolge sind).
   - Beim Versuch, einen realistisch grossen Testfall zu bauen, zusaetzlich
     DREI unabhaengige, bisher unbekannte Puffer-/Zaehl-Grenzen im Frontend
     gefunden und behoben: `Source/codegen.cpp` `ACTION_LOG_MAX` (4096,
     generierter `qcc_p.c`-Parser) verwarf weitere Action-Log-Eintraege
     STILLSCHWEIGEND (kein Fehler, IR wurde still abgeschnitten, exit 0) --
     jetzt 1048576 und ein lauter `exit(1)` statt stillem Verwerfen;
     `Data/qcc.lextab` `tcFunctionCount`/`tcGlobalCount` (je 64) meldeten
     zwar einen Fehler auf stderr, setzten aber NIE `tcSemanticErrors`
     (Programm lief trotzdem mit `exit 0` und "OK" weiter, Funktionen/
     Globale fehlten aber in der Tabelle) -- jetzt `MAX_FUNCTIONS`=512/
     `MAX_GLOBALS`=512 und `tcSemanticErrors++`; `tcLocalCount` (Parameter+
     Lokale EINER Funktion, ebenfalls 64) hatte ueberhaupt GAR KEINE
     Grenzpruefung -- ein echter, bisher unbemerkter Pufferueberlauf bei
     >64 Lokalen/Parametern in einer einzigen Funktion, jetzt mit
     `MAX_LOCALS`=256 und explizitem Check behoben. Ausserdem
     `qcc_backend_c.cpp` `MAX_IR_LINES` von 8192 auf 65536 erhoeht (war
     bereits sauber mit `fatal()` abgesichert, nur zu knapp bemessen).
   **Empirisch verifiziert bei realer Groesse:** 150 generierte Funktionen
   (>32 KB 68k-Code zwischen `tc_functab` und der letzten Funktion)
   kompilieren, assemblieren (echter `r68`) UND linken (echter `l68` gegen
   `clib.l`) korrekt mit `-largedata`; Ausfuehrung via `tools/qcc68sim.py`
   stimmt exakt mit der architekturneutralen `tools/qccvm.py`-Referenz
   ueberein. **Gegenprobe:** dasselbe Programm OHNE `-largedata` wird vom
   echten `r68` tatsaechlich mit `"branch out of range"` abgelehnt (eine
   ANDERE Fehlermeldung als `"value out of range"` bei `lea`/`movea` fuer
   Daten oben) -- belegt die Notwendigkeit auch fuer Funktionsaufrufe, nicht
   nur fuer globale Daten. Test in `runtests.sh` verankert.
   **Damit ist der zuvor bewusst offen gelassene Fall ("dasselbe Problem bei
   FUNKTIONSAUFRUFEN") jetzt geloest** -- `-largedata` deckt Daten UND
   Funktionsaufrufe vollstaendig ab.

## Selfhosting L2 Vollport: `parsec.cpp`, Schritt 2 -- LAEUFT (ab 2026-07-25 nachts)

Nach dem kompletten `codegen.cpp`-Vollport (siehe oben) folgt jetzt `Source/parsec.cpp`
(2149 Zeilen) nach `SourceQCC/ebnf.tc`, gleiche Architektur (eigene Datei, ruft
`codegen.tc`s Funktionen ueber bare Prototypen/interne bsr-ABI, siehe Mehrdatei-
Feature). Chronologischer Fortschritt und alle dabei gefundenen QCC-Sprachquirks
stehen in der Memory-Datei `[[qcc-vollport-status]]` (dort laufend aktualisiert,
nicht hier dupliziert). Bisher portiert: globale Structs/Zustandsvariablen (TEIL 1),
Zeichen-/Puffer-Helfer, Tabellendruck (`printLexTab`), Adressaufloesung
(`resolveCallAddresses`), Linksrekursions-Pruefung (komplett), Stack-Maschine
(`execFrom`), Arbeitsdatei-Helfer (`appendUserCodeLine`/`appendLexerCfgLine`/
`appendCgenCfgLine`/`tsSymbolIndexOf`).

**2026-07-26: `writeWorkfile` portiert** (`Source/parsec.cpp:1007-1130`, die komplette
Arbeitsdatei-Ausgabe: EBNF-QUELLTEXT/TS-SYMBOLTABELLE/NTS-SYMBOLTABELLE/
PARSER-TABELLE/TESTS/LEXER/CODEGEN/NUTZER-CODE-Bloecke). Neue QCC-Erkenntnisse:

- **Literale Backslash-Buchstabenfolgen als TEXT** (z. B. das Original schreibt
  `"\ooo"` oder `" \t\r\n"` als dokumentierende BEISPIEL-Konfigurationswerte in
  die generierte Datei, nicht als Escape-Sequenz) lassen sich NICHT einfach als
  QCC-String-Literal schreiben: der echte QCC-String-Literal-Dekoder
  (`tcDecodeStringLit`, `Data/qcc.lextab`) verschluckt JEDEN Backslash
  zusammen mit dem Folgezeichen (wird zu einem Escape-Byte fuer `n`/`t`/`r`/`0`,
  sonst zum blossen Folgezeichen OHNE den Backslash). Workaround: den Backslash
  und den Folgebuchstaben in ZWEI GETRENNTEN `fputc`/`fprintf`-Aufrufen ausgeben,
  damit sie nie im selben Literal aufeinandertreffen (ein einzelner, direkt vor
  dem schliessenden Anfuehrungszeichen stehender Backslash entkommt der
  Sonderbehandlung, da dann kein Folgezeichen mehr im selben Literal existiert).
  ECHTE Tabs (Einrueckung in einem Assembler-Beispieltext, einfaches `\t` im
  C++-Original) sind davon NICHT betroffen und bleiben normale `\t`-Literale --
  die normale Escape-Dekodierung liefert dort genau das gewuenschte Tab-Byte.
- **CALLEXT-Grenze "max. 8 Stack-Argumente" real getroffen:** eine `fprintf`-Zeile
  mit neun Werten nach dem Formatstring (Zeile/true/false/addr/rngLo/rngHi/
  ident/modus/symbol) musste auf zwei `fprintf`-Aufrufe aufgeteilt werden.
- Ternaere Ausdruecke als `fprintf`-Argument weiterhin bewusst vermieden (siehe
  bereits bekannte Gruende in `SourceQCC/ebnf.tc`) -- komplette if/else-Zweige
  mit dupliziertem Aufruf stattdessen.

**ZWEI live gefundene und gefixte Backend-Bugs (`Source/qcc_backend_c.cpp`),
Voraussetzung fuer den ersten ECHTEN Zwei-Datei-Link von `ebnf.tc` gegen
`codegen.tc`** (bisherige `ebnf.tc`-Chunks wurden nur ALLEIN kompiliert/
assembliert, nie gegen `codegen.tc` gelinkt): beide Male dieselbe Bugklasse wie
schon beim `-largedata`-a3-Fund (siehe oben) -- ein rein INTERNER, in JEDER
Datei wieder bei 0 startender Zaehler erzeugt einen Symbolnamen, der r68/l68
(kein Sichtbarkeitskonzept, jedes Label automatisch global sichtbar beim
Linken) als ECHTEN globalen Namen sieht:
1. `LABEL`/`JMP`/`JZ`/`JNZ` (`tc_L<n>`) UND `emitCompare()`s interne
   Sprungmarken (`tc_cmp_yes_<n>`/`tc_cmp_done_<n>`) kollidierten, sobald zwei
   getrennt kompilierte Dateien BEIDE Kontrollfluss bzw. einen Vergleichsoperator
   enthalten (praktisch immer der Fall).
2. Dieselbe Kollision fuer die `-largedata`-Indirektionstabellen `tc_functab`/
   `tc_gadata` (inkl. aller `lea ...(pc),a3`/`a4`-Referenzen darauf), sobald
   zwei Dateien beide mit `-largedata` kompiliert werden.

Beide Faelle jetzt mit `psectName`-Suffix eindeutig gemacht (exakt dasselbe
Namensverfremdungs-Muster wie bei `static`-Funktionen/-Globalen, nur OHNE die
`isStatic`-Bedingung -- diese Namen sind NIE etwas, das eine andere Datei
ansprechen koennen soll). Volle `runtests.sh`-Suite nach dem Fix weiterhin
komplett gruen (inkl. der bestehenden Mehrdatei-M2-Tests, die pruefen, dass
eine ECHTE Namenskollision -- zwei nicht-static Definitionen desselben
Symbols -- weiterhin korrekt als `"duplicate symbol"` abgelehnt wird).

**Wichtig fuer den Verifikationsmassstab:** ein VOLLER `l68`-Link von `ebnf.tc`
gegen `codegen.tc` ist weiterhin NICHT das Kriterium fuer diesen Chunk -- die
rekursive-Abstiegs-Parsergruppe (`rule`/`expression`/`term`/`factor`/.../
`ebnfSyntax`/`lexikalischeAnalyse`/`exitProgram`) hat noch keinen echten Rumpf
(siehe `[[qcc-vollport-status]]`), ein Link schlaegt daher ERWARTET mit
"unresolved symbol" fuer genau diese (hier nicht aufgerufenen) Funktionen fehl.
Verifiziert wie bei den bisherigen `ebnf.tc`-Chunks: `ebnf.tc` kompiliert sauber
(Frontend, ALLEIN -- Konkatenation mit `codegen.tc` in einer `qcc_p`-
Kompilation verletzt Quirk 8, Deklarationen-vor-Funktionen GESAMT ueber beide
Dateien) UND assembliert fehlerfrei (echter `r68`) fuer BEIDE Dateien getrennt.
Regressionstest in `runtests.sh` verankert.

**2026-07-26 (direkt im Anschluss): `rebuildFirstEdgesFromTable` portiert**
(`Source/parsec.cpp:1132-1156`, Fall B: Linksrekursions-Kanten aus einer
GELADENEN Arbeitsdatei rekonstruieren statt waehrend des normalen Parsens
ueber das `firstPos`-Flag). Kleiner, unproblematischer Chunk -- keine neuen
QCC-Grenzfaelle. Original nutzt C++s `for`/`continue`; hier wie im Rest
der Datei per `while` umgesetzt (bewusst OHNE `continue`: der Trailing-
Increment `r = r + 1` muesste sonst vor jedem `continue` wiederholt werden --
die if-umschlossene Form vermeidet dieses Fussangel-Risiko). Das lokale
`static int visited[LEXTAB_LEN]` des Originals wurde zu einem globalen
`rebuildVisited[1024]` (analog `dfsColor`, reiner Scratch-Speicher, keine
echte Persistenz noetig). Verifizierung wie bei `writeWorkfile`: kompiliert
sauber, assembliert fehlerfrei (echter `r68`, `ebnf.tc`+`codegen.tc`
getrennt) -- Regressionstest in `runtests.sh`. ZUSAETZLICH die reine
Algorithmus-Logik einmalig gegen eine native C-Uebersetzung derselben
Funktion samt Testdaten gegengeprueft (4 `lexTab`-Zeilen mit einer
Linksrekursions-Kette): beide liefern `firstEdgeCnt=2`/`ruleNameListCnt=2`.

**2026-07-26 (direkt im Anschluss): `loadWorkfileAsGrammar` portiert**
(`Source/parsec.cpp:1162-1231`, Fall B: PARSER-TABELLE/EBNF-QUELLTEXT direkt aus
einer Arbeitsdatei laden, ohne `.ebnf`). War laut Plan ein Kandidat fuer
vereinfachte Behandlung (Fall A -- Parsen aus einer frischen `.ebnf` -- ist der
eigentlich noetige Pfad fuers Selfhosting-Ziel), auf Nutzerwunsch trotzdem
regulaer portiert. Das Original nutzt EIN grosses `sscanf(...)` mit NEUN
Ausgabeparametern (sechs `int*`, zwei `char*`, ein `int*` fuer die
Consumed-Position via `%n`) -- geht hier aus zwei Gruenden nicht 1:1:
- CALLEXT erlaubt max. 8 Stack-Argumente (siehe `writeWorkfile`), neun
  ueberschreiten das.
- `int*`-Ausgabeparameter mit `*p = wert`-Schreibzugriff sind in dieser
  QCC-Version generell unerprobt (siehe `execPosResult`/`execFrom`).

Stattdessen ein Handparser (`wfParseInt`/`wfParseToken`, globale Parse-Position
`wfParsePos` statt `int*`-Out-Parameter) passend zum `writeWorkfile`-
Zeilenformat (`"%-5d ... %-16s %-4s %s\n"` -- sechs Ganzzahlen, zwei
whitespace-getrennte Tokens, Rest der Zeile ist der Symbolwert).

**ZWEI neue Grenzfaelle live gefunden:**
- `lexTab[aktTabIndex].ident[0] = 0;` (`arr[i].field[j]` als Zuweisungsziel)
  wird vom Frontend abgelehnt (`"arr[i].field[j] not supported in this
  version"` + `"unknown assignment target"`) -- durch
  `tcCopyBounded(lexTab[aktTabIndex].ident, "", 32)` ersetzt (identisch zum
  bereits vorhandenen `"-"`-Fall daneben).
- Ein `"?"` als reiner Text in einer Fehlermeldungs-Zeichenkette
  (`"...CSV-Datei?)\n"`) loeste erneut den bekannten `"conditional-frame
  mismatch"`-Bug aus (Quirk 15) -- ohne Fragezeichen umformuliert.

Verifiziert wie bei den vorigen Chunks: kompiliert + assembliert sauber
(echter `r68`, `ebnf.tc`+`codegen.tc` weiterhin getrennt) -- Regressionstest
in `runtests.sh`. ZUSAETZLICH die komplette Rundreise (`writeWorkfile`
schreibt eine Tabelle -> Tabelle "vergessen" -> `loadWorkfileAsGrammar` laedt
sie zurueck) einmalig gegen eine native C-Uebersetzung BEIDER Funktionen
gegengeprueft: alle Werte (Modi, Ident-/TS-Text, true/falseAction,
rangeLo/rangeHi, Quelltextlaenge) kommen exakt wie geschrieben zurueck.

**2026-07-26 (direkt im Anschluss): `runTests` + `loadPreservedTests` portiert**
(`Source/parsec.cpp:1233-1268` bzw. `920-993`). `runTests` jagt alle TEST-Zeilen
durch `execFrom` und vergleicht mit dem erwarteten Ergebnis; `loadPreservedTests`
rettet TESTS/NUTZER-CODE/LEXER/CODEGEN-Bloecke aus einer alten Arbeitsdatei,
bevor sie ueberschrieben wird. Original-`execFrom(0, &pos)` (Ausgabeparameter)
wird zu `execFrom(0, 0)` + anschliessendem `execPosResult`-Read (passend zur
bereits portierten Signatur, siehe dortiger Kommentar).

**ZWEI weitere Grenzfaelle:**
- Dieselbe `arr[i].field[j]`-Zuweisungsziel-Ablehnung wie bei
  `loadWorkfileAsGrammar`, diesmal fuer `testCases[i].input[n] = 0` -- Umweg
  ueber einen lokalen Puffer (`tmpInput`) plus `tcCopyBounded`.
- **Bool-Ausdruecke koennen nicht direkt einer `int`-Variable/einem
  `int`-Feld zugewiesen werden** (`inTests = (strncmp(...) == 0);`,
  `testCases[i].expectOk = (strstr(...) == 0);` -- beides vom Frontend
  abgelehnt). Dieselbe strikte int/bool-Trennung wie bei Quirk 3 (Bedingungen)
  und Quirk 10 (Rueckgabewerte), hier erstmals fuer eine normale ZUWEISUNG
  getroffen -- komplette if/else-Zweige statt `x = (a == b);` verwendet.

Ternaere OK/FAIL-Textwahl als Funktionsargument weiterhin bewusst vermieden;
Anfuehrungszeichen um Testeingabe-Text ueber das Builtin `putchar(34)`
(PRINTC) statt eines String-Literal-Workarounds, da das Ziel hier STDOUT
(`printf`) ist, nicht ein `FILE*`-Strom wie bei `writeWorkfile`. Verifiziert
wie die vorigen Chunks (kompiliert + assembliert sauber, `ebnf.tc`+
`codegen.tc` getrennt) -- Regressionstest in `runtests.sh`. ZUSAETZLICH ein
voller Rundlauf (Arbeitsdatei mit einer RNG-Regel + drei TEST-Zeilen
schreiben, per `loadWorkfileAsGrammar`+`loadPreservedTests` wieder einlesen,
`runTests` ausfuehren) einmalig gegen eine native C-Uebersetzung aller vier
beteiligten Funktionen gegengeprueft: beide liefern
`aktTabIndex=1`/`testCaseCnt=3`/`mismatches=0` (alle drei Testfaelle PASS).

**2026-07-26 (direkt im Anschluss): die rekursive-Abstiegs-Parsergruppe
portiert** (`Source/parsec.cpp:1371-1774` + Helfer `920-1330`) --
`literal`/`ident`/`block`/`repeat`/`option`/`factor`/`term`/`expression`/`rule`
PLUS `push`/`pop`/`restart`/`errorMsg`/`test`/`addIdentList`/`patchLocalTrue`/
`patchLocalFalse`. Die neun Kernfunktionen hatten bereits seit Projektbeginn
bare Prototypen in `ebnf.tc` (gegenseitige Rekursion); der FUNCDECL/FUNC-
Blocker dafuer wurde bereits am 2026-07-25 im Backend gefixt (Commit
`7102de2`, siehe `[[qcc-vollport-status]]`).

Groesster Chunk bisher, aber KEINE grundsaetzlich neuen Sprachquirks -- alle
bereits bekannten Muster kamen konzentriert zusammen zur Anwendung:
- `arr[i].field[j]`-Zuweisungsziel erneut mehrfach (`lexTab[aktTabIndex].
  ident[0]`, TS-Feldaufbau) -- durchgehend ueber lokale Puffer + `tcCopyBounded`
  geloest (Muster aus `loadWorkfileAsGrammar` fortgesetzt).
- Anfuehrungszeichen im Listing-Text (`lst`) und im TS-Feld ueber das
  bestehende `appendQuoteChar` byteweise angehaengt, TS-Feld dabei komplett in
  einem lokalen `tsBuf` aufgebaut statt direkt in der struct.
- Bool-Ausdruecke nicht direkt zugewiesen (`wasAmbig = (!committed &&
  hadSkippable);` -> if/else), int-als-Bedingung durchgehend mit `!= 0`.
- Eine `printf`-Warnmeldung mit vier Zeilen (String-Literal-Konkatenation im
  Original) ueberschritt die 256-Byte-Grenze fuer QCC-String-Literale
  (`tcDecodeStringLit`) -- auf zwei `printf`-Aufrufe aufgeteilt.
- Post-/Prae-Inkrement INNERHALB eines Ausdrucks (`restartToken[i++]`,
  `lineStack[lineStackIndex++]`, `lineStack[--lineStackIndex]`) durchgehend
  in separate Anweisungen aufgeloest (Muster: nie `++`/`--` verwendet, siehe
  Kommentar in `ebnf.tc`).
- Ein nicht verwendeter Rueckgabewert (`pop();` als blosse Anweisung in
  `expression()`) wurde vorsichtshalber einer eigenen Variable zugewiesen
  statt verworfen (unerprobtes Terrain, kein bekannter Präzedenzfall im
  Projekt).
- `switch`/`case` (in `factor()`) funktionierte direkt wie erwartet.

Kann NICHT sinnvoll ausgefuehrt/getestet werden (haengt an
`lexikalischeAnalyse()`/`getAktChar()`, beides noch nicht portiert) --
Verifikation bleibt bei kompiliert + assembliert sauber (Regressionstest in
`runtests.sh`). **Wichtiges Zwischenergebnis:** ein echter `l68`-Link von
`ebnf.tc`+`codegen.tc` zeigt nach diesem Chunk nur noch GENAU die 7 erwarteten
Lexer-Funktionen als unresolved (`getAktChar`/`getAktLine`/`put`/
`ebnfSyntax`/`semantischeAnylyse`/`lexikalischeAnalyse`/`exitProgram`) -- die
Parsergruppe selbst ist vollstaendig und korrekt verdrahtet.

**2026-07-26 (direkt im Anschluss): der Lexer portiert** (`Source/parsec.cpp:
1810-2147, 1906-1970`) -- `lexikalischeAnalyse`/`getNext`/`getAktChar`/
`comment`/`getAktLine`/`put`/`semantischeAnylyse`.

**Neu: `initLexer()`.** Das C++-Original initialisiert die Lexer-Config-
Globalen (`startLineCommentString`, `flagBlockComment`, ...) per automatischem
C++-Globalen-Initialisierer VOR `main()` -- QCC `GLOBAL`-Deklarationen
koennen das nicht fuer String-Pointer-Werte, daher eine explizite Init-
Funktion, die `main()`/`ebnfSyntax()` (naechster, letzter Schritt) EINMAL zu
Programmbeginn rufen muss.

**Wichtigster neuer Grenzfall: rohes Zeiger-Dereferenzieren** (`*p` lesen UND
`*p = wert` schreiben, nicht nur `p[i]`) wurde hier zum ERSTEN Mal im
gesamten Port gebraucht (`getNext`/`comment`). Da eine bestehende
Projektnotiz (`execPosResult`-Kommentar) genau diesen Fall als bisher
unerprobt markierte, vorab per Standalone-Test gegen QCCVM verifiziert:
Lesen UND Schreiben ueber einen Pointer auf ein globales `char`-Array liefert
exakt die erwarteten Werte -- danach bedenkenlos wie im Original eingesetzt.
Ebenfalls dabei verifiziert: ein nicht verwendeter Rueckgabewert (`comment();`
als blosse Anweisung) kompiliert und laeuft korrekt (QCCVM-Test) -- die
vorsichtshalber-Variable aus dem Parsergruppen-Chunk (`expression()`s
`poppedLine`) war also nicht zwingend noetig, bleibt aber unveraendert stehen.
`strcpy` (ohne `_s`/Laengenlimit, wie `strcat`/`sprintf`) neu extern
deklariert.

Verifiziert wie die vorigen Chunks (kompiliert + assembliert sauber) PLUS ein
echter Tokenizer-Lauf (`"rule1 = \"a\" ;"` -> IDENT/EQUAL/LITERAL/END) einmalig
gegen eine native C-Uebersetzung ALLER Lexer-Funktionen gegengeprueft: beide
liefern exakt dieselbe Tokenfolge (`138/129/139/143`) und `aktName="rule1"`.
Regressionstest in `runtests.sh`. **Wichtiges Zwischenergebnis:** ein echter
`l68`-Link von `ebnf.tc`+`codegen.tc` zeigt nach diesem Chunk nur noch GENAU
2 unresolved Symbole (`ebnfSyntax`/`exitProgram`) -- der komplette Rest der
Datei ist vollstaendig und korrekt verdrahtet.

## Selfhosting L2 Vollport `parsec.cpp`: ABGESCHLOSSEN (2026-07-26)

**Letzter Abschnitt portiert: `ebnfMain`/`ebnfSyntax`/`exitProgram`**
(`Source/parsec.cpp:170-334, 1357-1369, 322-334`).

**Wichtigste Design-Entscheidung: QCC `main()` kann keine
Kommandozeilenargumente empfangen** -- weder die Grammatik noch der Backend-
Einsprungpunkt (`tc_start`/`emitCall`) sehen einen `argc`/`argv`-Mechanismus
vor (verifiziert: `emitCall` fuer `main` pusht keinerlei Argumente). Die
komplette Original-`main(argc, argv)`-Logik lebt deshalb in
`ebnfMain(char* baseArg)`, einer regulaeren, mit dem Basisnamen
PARAMETRISIERTEN Funktion; `main()` selbst ist nur noch ein duenner Wrapper
mit fest einprogrammiertem Platzhalter-Basisnamen (`"qcc"`). Bewusst
entfallen: die `argc<2`-Usage-Meldung (kann bei einem Funktionsparameter nie
eintreten) und der optionale dritte CLI-Parameter `<teststring>` (manueller
Testlauf gegen einen Eingabestring) -- `runTests()` (der TESTS-Block der
Arbeitsdatei) deckt den eigentlichen Testmechanismus bereits ab. Fuer einen
echten, dateinamen-flexiblen OS-9-Build muesste der Basisname kuenftig ueber
einen Syscall zum Lesen der OS-9-Kommandozeile (Process-Descriptor) kommen --
nicht implementiert, dokumentierte bekannte Grenze.

`exitProgram()`: `fflush(stdout)` aus dem Original bewusst weggelassen --
`exit()` flusht/schliesst laut C-Standard ohnehin alle offenen Streams
automatisch, und ein `stdout`-`FILE*`-Handle ist ueber die Microware-ABI
nicht ohne Weiteres als einfacher QCC-Wert zu bekommen (keine simple
`stdout`-Externvariable, `clib.l` exportiert nur ein `_iob`-Array mit
unbekanntem Layout).

**MEILENSTEIN-TEST:** zum ersten Mal ein VOLLER `l68`-Link von `ebnf.tc` (mit
seiner eigenen, echten `main()`) gegen `codegen.tc`, OHNE jedes unresolved
Symbol -- der resultierende `.out` ist ein vollstaendig gelinktes,
2,1-MB-OS-9-Modul. **Der komplette QCC-Vollport von `Source/parsec.cpp` ist
damit abgeschlossen** (Schritt 2 aus dem urspruenglichen 3-Schritt-Plan,
siehe `[[qcc-vollport-status]]`) -- zusammen mit dem bereits fertigen
`codegen.cpp`-Vollport sind beide Kerndateien des EBNF-Generators als
QCC-Quelltext vorhanden.

**Kollateral-Aufwand:** da `ebnf.tc` jetzt eine ECHTE `main()`-Funktion
enthaelt, mussten die sechs AELTEREN Regressionstests in `runtests.sh`
(`writeWorkfile`/`rebuildFirstEdgesFromTable`/`loadWorkfileAsGrammar`/
`runTests+loadPreservedTests`/Parsergruppe/Lexer) umgebaut werden -- sie
haengten bisher je ein eigenes `"void main(){...}"` an `ebnf.tc` an, was ab
jetzt mit der echten `main()` kollidiert (zwei REALE Funktionskoerper
gleichen Namens, kein harmloses FUNCDECL-Rauschen mehr). Fix: Testfunktionen
umbenannt (kein `main` mehr) und Backend-Aufruf von `-part -runtime` auf
reines `-part` reduziert -- reine `r68`-Assemblierung (wie diese sechs Tests
sie pruefen) braucht keinen echten Einsprungpunkt, nur echtes Linken (`l68`)
wuerde einen brauchen, und das pruefte keiner dieser sechs Tests ohnehin.

**Noch NICHT abgedeckt (Schritt 3 des urspruenglichen Plans, separat
vermerkt):** echte Ausfuehrung/Verhalten des selbstgehosteten `ebnf_gen` auf
dem Q9-Emulator -- insbesondere Live-Verifikation von `malloc`/`realloc`/
`free` (`realloc`s Kopierverhalten ueber die Wachstumsgrenze hinweg) sowie
ein vollstaendiger End-zu-End-Vergleich mit dem bereits `xcc`-gebauten,
live-auf-Q9-bestaetigten `ebnf_gen` (siehe `docs/STATUS.md`).

## Erledigte Meilensteine

| Bereich | Status | Bemerkung |
|---|---|---|
| EBNF-Parser/Scanner-Generator | erledigt | Tabellen, Lexerblöcke und Nutzer-Code |
| QCC M1 | erledigt | Ausdrücke, Variablen, Stack-IR, QCCVM |
| QCC M2/M3 | erledigt | `if/else`, `while`, Calls, Parameter, Rekursion |
| QCC Kontrollfluss (Stufe A, Teil 1) | erledigt | `for`, `do/while`, `break`, `continue` -- kein neuer IR-Opcode, kein Backend-Change noetig |
| QCC `typedef` | erledigt | Skalar-/Pointer-Aliase, reine Grammatik-Erweiterung |
| QCC `struct` (einheitlicher Feldtyp) | erledigt | Feldzugriff nutzt LOADIDX/STOREIDX wieder -- kein Backend-Change; gemischte Feldtypen bewusst vertagt (siehe SELFHOSTING_LUECKENLISTE.md) |
| QCC `enum` | erledigt | reine benannte int-Konstanten, kein eigener Typ, kein Backend-Change (nur PUSH) |
| QCC `sizeof` | erledigt | nur int/char/bool/unsigned/struct als Argument, keine Pointer, kein Backend-Change (Konstante zur Kompilierzeit) |
| QCC Prä-/Postinkrement (`++`/`--`) | erledigt | nur einfache int/unsigned/char-Skalare (lokal/global), kein Backend-Change (LOAD/DUP/PUSH/ADD-oder-SUB/STORE) |
| QCC `switch`/`case`/`default` | teilweise | gestapelte Case-Label (case A: case B: body) unterstuetzt, KEIN Fallthrough mit Code zwischen verschiedenen Bodies (deckt das reale Nutzungsmuster in parsec.cpp/codegen.cpp ab); kein Backend-Change |
| QCC Casts | teilweise | nur `(int)`/`(unsigned int)`/`(char)`/`(bool)`, kein Pointer-/typedef-Cast-Ziel (Mehrdeutigkeits-Falle mit Klammerausdruecken bewusst vermieden); kein Backend-Change |
| QCC `sizeof(variable)` | erledigt | ergaenzt `sizeof(Typ)`: jetzt auch `sizeof(x)` auf Skalare/Arrays (lokal+global) -- deckt die reale Nutzung im Generator ab |
| QCC `enum` als Typ | erledigt | `enum Name var;` als Deklaration/Parameter/Rueckgabetyp moeglich, bleibt intern `int` |
| Arrays | erledigt | bytegenaue `char[]`/`int[]`, lokal und global |
| Datentypen | erledigt | `int`, `unsigned int`, `char`, `bool`, Nullwert |
| Operatoren | erledigt | Rechen-, Vergleichs-, Bit-, Shift-, Logik- und ternäre Operatoren |
| Zuweisungen | erledigt | einfach und kombiniert, auch für Array-/Pointerziele |
| Pointer | erledigt | Typmodell, Adressen, Dereferenzierung, Skalierung, `T**` |
| 68000-Ausgabe | funktionsfähig | vasm und `qcc68sim.py` |
| ARM64/Darwin-Ausgabe | funktionsfähig | natives Programm mit eigener Runtime |
| L3-Backends auf reines C zurückgebaut | erledigt | `qcc_backend_c.cpp`/`qcc_arm64_backend_c.cpp`, siehe `docs/SELFHOSTING_LUECKENLISTE.md` |
| Generator: C++-Templates entfernt | erledigt | `Source/msvc_compat.h`, 75 Aufrufstellen umgestellt -- Voraussetzung dafür, dass der Generator mit der echten Microware-`xcc`-Toolchain kompiliert |
| Generator kompiliert+linkt mit echter Q9-Toolchain (`xcc`) | erledigt | siehe `docs/SELFHOSTING_LUECKENLISTE.md` Abschnitt 6; Ausführung auf Q9 scheitert noch am Speicherbedarf (~34,6 MB Datensegment vs. 16 MB RAM) |
| QCC `struct` mit gemischten skalaren Feldtypen | erledigt (2026-07-24) | echtes Byte-Layout mit natürlichem Alignment; Feldzugriff nutzt PUSHADDR/IPADD/LOADIND/STOREIND (bereits vorhandene, architekturneutrale Opcodes) statt LOADIDX/STOREIDX -- kein neuer Opcode, kein Backend-Change. Bewusst noch offen: verschachtelte structs (siehe SELFHOSTING_LUECKENLISTE.md) |
| QCC Pointer-Felder in `struct` | erledigt (2026-07-25, Selfhosting L2) | `structField = type pointerDecl fieldName [ arraySize ] ";"` -- Grammatik erlaubte bisher gar keinen `*` vor dem Feldnamen. Layout (`tcRegisterStruct`): Pointer-Felder bekommen IMMER 8 Byte Größe/Ausrichtung, UNABHÄNGIG vom Ziel-Backend -- 68k-Pointer sind 4 Byte, ARM64-Pointer 8 Byte, aber dieselbe frontend-berechnete IR muss für BEIDE Architekturen gültig bleiben (ein einzelnes Offset kann nicht architekturabhängig sein). Der 68k-Backend nutzt von diesem 8-Byte-Slot nur die ersten 4 Byte -- kein Backend-Code-Change nötig, da `LOADIND`/`STOREIND`/`IPADD` den Typtag `p` bereits generisch kennen (aus der Pointer-Variablen-Unterstützung). Nur EIN Pointer-Level (kein `T**`-Feld), keine Pointer-Arrays als struct-Feld. Direkte Indizierung DURCH ein Pointer-Feld (`p.field[i]`) ist bewusst NICHT Teil dieser Version (wie zuvor bei Array-Feldern) -- Zugriff nur über eine Pointer-Zwischenvariable. Verifiziert in QCCVM, 68000 UND ARM64. **Wichtiger Fund dabei:** `arr[i].feld` (ein ARRAY von structs, indiziert, dann Feldzugriff) funktionierte zu diesem Zeitpunkt noch GAR NICHT -- siehe eigene Tabellenzeile weiter unten (repariert, gleicher Tag). |
| QCC `arr[i].feld` (Array von structs, indiziert + Feldzugriff) | erledigt (2026-07-25, Selfhosting L2) | Grammatik: `directTarget`/`varRef` von einer Alternation (`index... \| member...`) zu einer SEQUENZ (`[ index... ] [ member... ]`) umgebaut -- Index-Kette UND Member-Zugriff jetzt kombinierbar, nicht nur alternativ. Neuer IR-Opcode `IPADDN <byteGroesse>`: wie `IPADD`, aber Skalierung um eine LAUFZEIT-Byte-Größe (hier `tcStructByteSize`) statt einer festen Typtag-Größe -- `IPADD` kennt nur `i`/`c`/`p` (4/1/8 Byte), die Elementgröße eines struct-Arrays ist aber beliebig. Codegen (`tc_varref`/`tc_target`, neuer Helper `tcSkipOneIndex` für klammertiefen-bewusstes Überspringen des Index): `PUSHADDR L <slot>` + `IPADDN <structByteSize>` (Elementadresse) + `PUSH <fieldOffset>` + `PADD c` (Feldoffset, wie beim bereits vorhandenen Skalar-Feldzugriff). `arr[i].feld[j]` (Index NACH dem Feld) bleibt bewusst ausgeschlossen (wie `p.field[i]` durch ein Pointer-Feld), sauber diagnostiziert. **Vorausgesetzter, eigenständiger Bugfix:** ein erster Versuch deckte auf, dass schon die reine Speicher-Allokation für ein Array von structs kaputt war, unabhängig vom Zugriffsmuster -- `tc_localdecl`s Sonderfall für struct-typisierte Locals emittierte IMMER `LARRAY <slot> c <structByteSize>` (genau EIN Struct), ein `[N]`-Suffix wurde komplett ignoriert (`struct Rec arr[3];` reservierte nur Platz für 1 statt 3 Elemente, in QCCVM als `pointer outside object`-Crash sichtbar). Zusätzlich verwendete `tc_local` `tcLocalArrayLen` bei JEDEM struct-typisierten Local zweckentfremdet für die FELDANZAHL statt der Array-Länge. Beides repariert: `tc_local` setzt jetzt immer 0 (echte Länge kommt erst aus `tc_localdecl`), `tc_localdecl` alloziert `LARRAY <slot> c <N * structByteSize>` und setzt `tcLocalArrayLen[slot] = N` korrekt (mehrdimensionale struct-Arrays bleiben ein sauberer Parse-Fehler). `tc_sizeofvar` ebenso korrigiert: `sizeof(structArray)` lieferte vorher immer nur die Größe EINES Elements, jetzt `count * structByteSize`. Verifiziert in QCCVM, 68000 (`qcc68sim` UND echter `r68`-Assembler) UND ARM64 -- sowohl mit reinen `int`-Feldern als auch mit dem Pointer-Feld-Fall (`char name[8]; char* text;`), der den Allokations-Bug ursprünglich aufdeckte. **Zweiter, waehrend derselben Arbeit gefundener und behobener Sicherheits-Fund:** die Grammatik-Umstellung von Alternation auf Sequenz macht auch Kombinationen OHNE Codegen-Unterstuetzung neu parsebar (allen voran `ptr[i].feld` fuer eine Pointer-auf-struct-Variable, z.B. `struct Rec* p; p[i].a`) -- ohne Gegenmassnahme haetten die bestehenden Index-Zweige die "."-Fortsetzung STILLSCHWEIGEND ignoriert (stiller Fehlcode statt Fehlermeldung). Deshalb neuer, allgemeiner Guard am Anfang von `tc_varref`/`tc_target`: JEDE "indiziert-dann-Member"-Kombination wird abgelehnt AUSSER genau der einen, die tatsaechlich gebaut ist (lokales, als Array deklariertes struct) -- mit Regressionstest in runtests.sh, damit ein kuenftiger Regress hier nicht wieder in stillen Fehlcode zurueckfaellt. **Bewusst NICHT Teil dieser Version:** globale struct-Variablen/-Arrays (siehe "Bewusst offen"). `ptr[i].feld` (Pointer-auf-struct-Variable) selbst ist seit derselben Sitzung ebenfalls erledigt, siehe eigene Tabellenzeile weiter unten. |
| QCC `ptr[i].feld` (Pointer-auf-struct-Variable, indiziert + Feldzugriff) | erledigt (2026-07-25, Selfhosting L2, Milestone B) | Braucht der Selfhosting-Pilot: `routinesC` in `codegen.cpp` ist `ActionRoutine*` (ein malloc/realloc-gewachsenes Array), kein festes lokales Array wie beim bereits fertigen `arr[i].feld` oben. Codegen (`tc_varref`/`tc_target`): identisch zu `arr[i].feld`, nur `LOADP <slot>` (geladener Pointer-WERT) statt `PUSHADDR L <slot>` (Blockadresse) -- `IPADDN` skaliert genauso um die Laufzeit-Byte-Größe des Elements. Der allgemeine Diagnose-Guard aus der `arr[i].feld`-Arbeit wurde um genau diese zweite erlaubte Kombination erweitert (lokale Pointer-auf-struct-Variable zusätzlich zum festen lokalen struct-Array) -- jede andere indiziert-dann-Member-Kombination (z.B. ein indizierter Pointer auf einen NICHT-struct-Typ) bleibt sauber diagnostiziert, mit Regressionstest. `ptr[i].feld[j]` (Index nach dem Feld) bleibt bewusst ausgeschlossen, wie bei `arr[i].feld[j]`. Verifiziert in QCCVM, 68000 (`qcc68sim` UND echter `r68`-Assembler) UND ARM64 (Lesen und Schreiben, Pointer per Zuweisung auf ein bestehendes Array gesetzt). **Damals bewusst nicht Teil dieser Version, seit 2026-07-25 (später am selben Tag) erledigt:** globale Pointer-auf-struct-Variablen, siehe eigene Tabellenzeile weiter unten. |
| QCC globale `struct`-Variablen/-Arrays/-Pointer | erledigt (2026-07-25, Selfhosting L2) | `tc_globalend` konnte `struct` bis dahin überhaupt nicht als Basistyp erkennen ("bad global declaration") -- neue Erkennung (mirrort `tc_type`s struct-Behandlung) VOR den skalaren Basistyp-Checks. Allokation: eine skalare globale struct-Variable UND ein globales Array von structs brauchen beide Block-Speicher (`GARRAY <name> c <N*structByteSize>`, `N=1` für den skalaren Fall) -- NIE die einzellige `GLOBAL`-Form; das funktioniert automatisch korrekt, weil `ADDRG`/`PUSHADDR G` in QCCVM (anders als bei Locals) `globals_[name]` UNABHÄNGIG von Skalar/Array immer als einheitliche Liste behandeln, keine Locals-artige Block-vs-Scalar-Unterscheidung nötig. Codegen (`tc_varref`/`tc_target`): alle drei schon lokal gebauten Muster (direkter `.field`-Zugriff, `arr[i].feld`, `ptr[i].feld`) 1:1 auf Globale übertragen -- `PUSHADDR G`/`ADDRG` statt `PUSHADDR L`, `LOADGP` statt `LOADP`, sonst strukturell identisch. Der allgemeine Diagnose-Guard (verhindert stillen Fehlcode bei nicht unterstützten indiziert-dann-Member-Kombinationen) wurde um die globalen Fälle erweitert. Bewusst diagnostiziert statt unterstützt: Initialisierer bei struct-Globalen (`struct Rec g = {...}`), mehrdimensionale globale struct-Arrays. Verifiziert in QCCVM, 68000 (`qcc68sim` UND echter `r68`) UND ARM64 -- UND strukturell mit dem ECHTEN `routinesC`/`routinesCCnt`/`routinesCCap`-Muster aus `codegen.cpp` (also mit file-scope-Globalen statt den main()-lokalen Variablen des ursprünglichen Piloten), erfolgreich gegen die echte `clib.l` gelinkt (`r68`+`l68`). Damit ist die einzige verbleibende Sprach-Blockade für den vollen Port von `codegen.cpp` beseitigt. **Wichtiger Nebenfund:** `build/parsec` (der EBNF-Generator selbst, NICHT QCC) hatte ein festes 128-KB-Puffer-Limit für den `[NUTZER-CODE]`-Block (`USER_CODE_LEN` in `Source/parsec.cpp`) -- die heutigen Erweiterungen ließen `Data/qcc.lextab` über dieses Limit wachsen, wodurch bei einer Regeneration STILLSCHWEIGEND drei `ROUTINE C`-Blöcke (die Ternary-Operator-Implementierung) aus der Datei verschwanden (nur eine leicht zu übersehende Warnzeile im stdout, die `runtests.sh` bis dahin mit `>/dev/null` verschluckte). Ein fehlschlagender Ternary-Test deckte das auf. Fix: `USER_CODE_LEN` auf 1 MB erhöht, UND `runtests.sh` prüft die `build/parsec`-Ausgabe jetzt aktiv auf "WARNUNG" und schlägt laut fehl statt es zu verschlucken. |
| QCC-Bug: indizierte Expression als RECHTER Vergleichsoperand (`nc != name[j]`) | erledigt (2026-07-25, beim Bau des Milestone-B-Piloten gefunden) | `tc_relop` setzt `tcRel0`/`tcRel1` schon beim Sehen von `!=`/`==`/etc., BEVOR die rechte Seite geparst ist. Enthält die rechte Seite selbst einen Index (`index = indexOpen expr "]"`) oder einen Funktionsaufruf (`arg = expr`), feuert deren VERSCHACHTELTE `tc_expr`-Aktion vorzeitig und wendet die noch gesetzten (aber für den äußeren, noch nicht abgeschlossenen Vergleich gedachten) `tcRel0`/`tcRel1` fälschlich auf den inneren Ausdruck an -- und nullt sie dabei, sodass der eigentliche äußere Vergleich beim späteren echten `tc_expr` gar nicht mehr feuert (sichtbar als bogus Folgefehler wie "array index expects int, got bool"). War VORHER (auch schon vor dieser Session, in `git show 692e05e` reproduziert) nie aufgefallen, da kein bestehender Test eine Indizierung/einen Aufruf als RECHTEN Vergleichsoperanden hatte (nur links, z.B. `arr[i] != 0`). Fix: `tcRel0`/`tcRel1` werden jetzt in `tc_callname`/`tc_arg`/`tc_call` exakt nach demselben Rette-und-nulle-Muster behandelt wie die schon länger bestehenden `tcPendingAdd`/`tcPendingMul` (neue Arrays `tcIndexSavedRel0/1`, `tcCallSavedRel0/1`). Verifiziert in QCCVM (Regressionstest `a[0] != b[0]` mit sich änderndem Vergleichsergebnis), alle 143 bestehenden Tests bleiben unverändert grün. |
| QCC `malloc`/`realloc`/`free` über `extern`+`CALLEXT` | erledigt (2026-07-25, Selfhosting L2, Milestone A/B) | Kein neuer Code nötig -- der bestehende `extern`-Mechanismus (siehe eigene Tabellenzeile oben) deckt `void*`-Rückgabe und `void*`-Parameter bereits generisch ab. Zwei unabhängige Verifikationsebenen: (1) **strukturell** -- ein Testprogramm mit `extern void* malloc(int)`/`realloc`/`free` wird über den echten `r68`-Assembler UND den echten `l68`-Linker gegen die ECHTE `clib.l`/`os_lib.l`/`sys.l` (aus der lokalen MWOS-Installation, kein Q9-Emulator-Zugriff nötig -- der Linker läuft unabhängig vom laufenden Emulator) gelinkt -- alle drei Symbole lösen sauber auf, exit 0, echtes `.out`-Modul entsteht; (2) **end-to-end per Mock-Stub** -- ein handgeschriebener Bump-Allokator-Mock (im selben Stil wie die bestehenden `myadd`/`foo`/`mockchr`-Mocks) läuft durch `qcc68sim`: `malloc` liefert einen echt benutzbaren Pointer (Schreiben/Lesen verifiziert), `realloc`/`free` werden mit korrekter Argumentplatzierung aufgerufen. **Bewusst NICHT Teil dieser Verifikation:** `realloc`s Kopierverhalten (alte Daten bleiben nach Wachstum erhalten) selbst -- der Mock kopiert nicht (qcc68sim modelliert nur ein generisches Adressregister `a0`, kein registerindiziertes Kopieren beliebiger Länge), das braucht echten Q9-Zugriff (siehe "Bewusst offen"). |
| Selfhosting L2 Milestone B: Pilot-Portierung `ActionRoutine`/`pushRoutine`/`freeRoutines`/`routineTextC` (`codegen.cpp:560-650`) | erledigt (2026-07-25) | Testet Pointer-struct-Felder, `ptr[i].feld` UND `malloc`/`realloc`/`free` über `extern` GLEICHZEITIG, in der Kombination, die der echte Generator braucht (`routinesC[i].name`/`.text`). **Bewusste Vereinfachungen ggü. dem C-Original** (siehe `runtests.sh`-Kommentar für Details): `pushRoutine` gibt den ggf. reallozierten Array-Pointer zurück statt ihn über einen `ActionRoutine**`-Out-Parameter zu schreiben (QCC braucht dafür keine Pointer-auf-Pointer-Indizierung -- gleiches beobachtbares Verhalten ohne dieses Sprachmittel); kein `strcpy`/`memcpy` (keine Standardbibliothek in QCC), stattdessen manuelle Byte-Kopierschleifen. Kompiliert fehlerfrei, per echtem `r68`+`l68`-Link gegen die echte `clib.l` strukturell verifiziert (siehe Zeile oben) -- als fester Regressionstest in `runtests.sh` verankert. **Bewusst NICHT Teil dieser Version:** Live-Ausführung auf Q9 (bestätigt, dass "one"/"two" nach dem Wachstum über "three" hinaus noch korrekt lesbar sind -- das exakte Kopierverhalten von `realloc`), der Rest von `codegen.cpp`/`parsec.cpp` (voller Port bleibt eigener, späterer Schritt), globale struct-Unterstützung (s.o.). |
| QCC Array-Felder in `struct` (z.B. `char name[8]`) | erledigt (2026-07-24) | `structField = type fieldName [ arraySize ] ";"` -- `arraySize` ist dieselbe seiteneffektfreie Regel wie bei globalen Arrays (kein neuer Opcode/Grammatik-Overhead). Byte-Layout: Groesse = Elementgroesse * Elementzahl, Ausrichtung nach dem ELEMENTtyp (wie ein eingebettetes `GARRAY`); `sizeof(struct X)` und lokale struct-Variablen (`LARRAY`-Groesse) profitieren automatisch, da beide auf `tcStructByteSize` aufsetzen. Zugriff: ein BARER Feldzugriff (`p.field`, ohne Index) zerfaellt zu einem Pointer auf das erste Element -- genau wie eine Array-VARIABLE (`tcPointerTo(Elementtyp)`, kein `LOADIND`) -- Zugriff ueber eine Pointer-Zwischenvariable (`char *q = p.field; q[i] = ..;`) funktionierte damit sofort; direkte `p.field[i]`-Syntax kam als eigener Folgeschritt hinzu (siehe eigene Tabellenzeile weiter unten). Zuweisung an das GANZE Array-Feld (`p.field = ..`) wird diagnostiziert (wie in echtem C nicht erlaubt). Verifiziert in QCCVM, 68000 UND ARM64 (gemischte Feldtypen inkl. Array, korrekte Offsets). |
| QCC `void`/`void *` | erledigt (2026-07-24) | `type` bekommt eine neue Alternative `"void"` (neuer Basistyp-Tag `'v'` an `TCType`), NUR als Funktions-Rueckgabetyp (`void f(){...}`, `return;` war als optionaler `retVal` schon vorher moeglich) und als generischer Pointer `void*` real nutzbar -- bare `void` (0 Pointer) wird an ALLEN anderen Registrierungsstellen explizit diagnostiziert (`tc_local`, `tc_param`, `tc_globalend`, `tc_staticlocal`, struct-Feld), da `type` global geteilt ist. `void*`-Kompatibilitaet: `tcCompatible` erlaubt jetzt BEIDE Richtungen zwischen `void*` und JEDEM ANDEREN Pointer GLEICHER Tiefe (Tiefenvergleich verhindert `void**` vs. `T*`-Verwechslung) -- deckt Zuweisung, Parameteruebergabe und Rueckgabe ab, ohne Cast. `void*` selbst ist NICHT dereferenzierbar/indizierbar/arithmetikfaehig (`*p`, `p[i]`, `p+1` etc. -- ohne diese Sperre wuerde `tcTypeTag` fuer den nicht abgedeckten Tag `'v'` still auf `'i'` zurueckfallen und eine falsche 4-Byte-Zugriffsgroesse annehmen; jetzt sauber diagnostiziert an allen betroffenen Stellen: `tc_derefref`, `tc_indirecttarget`, beide Pointer-Array-Index-Lese-/Schreibzweige in `tc_varref`/`tc_target`, `tc_term`s Zeigerarithmetik). Kein neuer IR-Opcode, kein Backend-Change -- verifiziert in QCCVM, 68000 UND ARM64. |
| QCC 68000-Backend: echtes Linken gegen `clib.l` + echte Ausfuehrung auf Q9 | erledigt (2026-07-24) | Aufbauend auf `-os9`-Ausgabemodus und `extern`/CALLEXT (siehe die beiden Zeilen unten): `putint`/`putuint`/`putchar` rufen im `-os9`-Modus jetzt `_os_write` auf (`error_code _os_write(path_id, const void*, u_int32 *count)`, laut `OS9/SRC/DEFS/modes.h` -- roher, ungepufferter Schreib-Syscall aus `clib.l`, path 1 = stdout). Ganzzahl->ASCII-Umwandlung passiert VOLLSTAENDIG in eigenem 68k-Code (analog zu `runtime/arm64_darwin/start.s`, wiederverwendet die bereits vorhandenen `tc_udiv_u32`/`tc_umod_u32`-Routinen aus `emitM68kCore` -- kein neuer Opcode) -- bewusst NICHT ueber `printf`, das zoege den kompletten Formatstring-Parser der ANSI-Stdio-Schicht in JEDES gelinkte Programm hinein, obwohl QCC noch gar keine String-Literale hat. **Drei eigenstaendige, live am echten Q9-Emulator gefundene Linker-/ABI-Bugs behoben:** (1) Das Backend nutzte `a6` als eigenen Frame-Pointer (`link a6,#N`/`unlk a6`) -- laut Register-Usage-Tabelle im Ultra-C/C++-Prozessorhandbuch (`ultrac_pg.pdf`) ist `a6` aber der STATISCHE Datenzeiger fuer die gesamte Laufzeit des gelinkten Moduls, `a5` ist das eigentliche Frame-Pointer-Register -- ueberschreiben wir `a6`, stuerzt jeder nachfolgende echte `clib`-Aufruf mit einem PMMU-Fehler ab. Fix: neuer `framePtr()`-Helfer, NUR im `-os9`-Modus `a5` statt `a6` (Default-/vasm-Modus unveraendert, keine Regression an Simulator-Tests). (2) `jsr <name>` fuer externe Aufrufe (CALLEXT/`_os_write`) wird von `r68` als ABSOLUTE Adresse kodiert (kein automatisches PC-relatives Encoding fuer externe Symbole) -- bricht sofort, sobald das Modul nicht zufaellig bei Adresse 0 laedt (per Disassemblierung des gelinkten Moduls nachgewiesen: `JSR $xxxx.L` statt einer PC-relativen Kodierung). Fix: `bsr` statt `jsr` NUR im `-os9`-Modus (inhaerent PC-relativ; bei zu grosser Distanz wandelt `l68 -a` automatisch in eine PIC-taugliche Jumptable-Indirektion um, exakt das Muster, das `clib.l` fuer seine eigenen internen Fernaufrufe verwendet). (3) Die Ausfuehrung eines gelinkten OS-9-Moduls beginnt IMMER am ALLERERSTEN Byte der ERSTEN Datei/Psect auf der `l68`-Kommandozeile -- es gibt KEINEN automatischen "cstart-zuerst"-Mechanismus. `l68 unser.r cstart.r ...` (unsere Reihenfolge beim ersten Versuch) sprang deshalb SOFORT in unser eigenes `main`, OHNE dass `cstart.r` je seine Laufzeit-Initialisierung (a6, iob-Puffer, Stack-Limits) durchlaufen konnte. Fix: `cstart.r` MUSS zuerst auf der Kommandozeile stehen (`l68 -a cstart.r unser.r -l=clib.l ...`), verifiziert per Linker-Symbolkarte (`-s=...`) UND per Disassemblierung (Exec-Offset zeigt danach korrekt auf `_cstart` statt auf unser `main`). **Bonus-Fund, unabhaengig von den drei Bugs oben:** beim Debuggen mit `tools/qcc68sim.py` (isolierter Test von `tc_umod_u32(42,10)`, unabhaengig vom Q9/Emulator reproduzierbar) einen voelligen eigenstaendigen, seit Einfuehrung von `tc_mod_i32`/`tc_umod_u32` unentdeckten Bug gefunden: nach der Division wird beim Rueckmultiplizieren `Quotient * Dividend` statt `Quotient * Divisor` gerechnet (`move.l d2,d0` statt `move.l d3,d0` vor `bsr tc_mul_i32` -- `d2` haelt noch den Dividenden, `d3` den Divisor). War unentdeckt, weil KEIN einziger 68k-Backend-Test den `%`-Operator ueber den echten 68k-Pfad ausgefuehrt hatte (nur ueber QCCVM) -- M4c-1-Test jetzt um drei `%`-Faelle erweitert. **Endverifikation:** ein Testprogramm (`int main(){ putint(42); putint(-7); putchar(88); }`) wurde mit `-os9` erzeugt, mit `r68`+`l68 -a` (cstart.r zuerst, `clib.l`/`os_lib.l`/`sys.l` als Bibliotheken) zu einem echten, ladbaren OS-9-Modul gelinkt, per ToolShed auf das Q9-Image kopiert und via Telnet auf dem LAUFENDEN Q9-Emulator ausgefuehrt -- Rohbyte-Ausgabe `34 32 0d 2d 37 0d 58` = exakt `"42\r-7\rX"`, kein Absturz, sauberer Shell-Return. |
| QCC 68000-Backend: Microware-r68-Ausgabemodus (`-os9`-Flag) | erledigt (2026-07-24) | `qcc_backend` akzeptiert ein optionales 4. Argument `-os9` (analog zum bereits vorhandenen `genParser68kTo`-Vorbild in `Source/codegen.cpp` fuer den Generator-eigenen Parser-Zwilling -- gleicher Trick, jetzt auch fuer das QCC-Backend). Umschaltung nur bei der TEXTFORM, kein IR-/Opcode-Unterschied: volle Kommentarzeilen `;` -> `*` (`fullCommentPrefix()`), Modul wird von `nam <psect>`/`psect <psect>,0,0,1,0,0` umschlossen und mit `ends` beendet (Psect-Name aus dem Ausgabedateinamen abgeleitet, `_p`-Suffix), Ausrichtung `even` -> `align 4` (`emitAlign()`) und der `tc_extcall_tmp`-Scratch-Puffer sowie unintialisierte DATA/BSS-Eintraege nutzen `dc.l 0,0,..` statt `ds.l`/`even` -- **empirisch am ECHTEN `r68.exe`** ermittelt (via Wine/MWOS): `even`/`ds.l`/`rmb`/`dsb`/`bsz`/`cnop` werden von Microwares r68 als "bad mnemonic" abgelehnt, `align N` und wiederholte `dc.l`-Konstanten dagegen akzeptiert. Default-Modus (ohne `-os9`) bleibt byte-identisch zum bisherigen vasm-Ausgabeformat (Regressionstest bestaetigt). Verifiziert: ein Testprogramm mit globalen Variablen (DATA/BSS), einem `char`-Array, einer Funktion UND einem `extern`-Aufruf (CALLEXT, inkl. `tc_extcall_tmp`) wurde mit `-os9` erzeugt und ERFOLGREICH durch den echten `r68.exe` zu einer relokierbaren `.r`-Objektdatei assembliert (kein `l68`-Linklauf, das ist der naechste, noch offene Schritt). |
| QCC `extern`-Deklarationen + Aufruf externer (Microware-clib-)Funktionen | erledigt (2026-07-24) | Neue Grammatikregel `externDecl = "extern" type pointerDecl externName "(" externParamList ")" ";"` -- nur Signatur, kein Rumpf/FUNC-Block. Parameter brauchen weder Namen noch werden sie semantisch genutzt (rein dokumentarisch, wie `int strcmp(const char *a, const char *b);`); `"..."` markiert eine variadische Funktion (wie `printf`), braucht wie in echtem ISO C mindestens einen benannten Parameter davor. Registrierung nutzt DIESELBEN Tabellen wie echte QCC-Funktionen (`tcFunctionNames`/`tcFunctionReturnTypes`/`tcFunctionParamTypes`/`tcFunctionNargs`, neu: `tcFunctionIsExternal`/`tcFunctionIsVariadic`) -- Aufrufpruefung (`tcLookupFunction`, Argumentanzahl/-typen in `tc_arg`/`tc_call`) funktioniert dadurch unveraendert fuer beide gleichermassen; Namenskollision zwischen `extern`-Deklaration und echter QCC-Definition wird ganz nebenbei als "duplicate function" erkannt. Bei einem Aufruf einer externen Funktion emittiert `tc_call` `CALLEXT`/`CALLEXTP` statt `CALL`/`CALLP` (dritter IR-Parameter: variadisch ja/nein). **Motivation** (Nutzeranstoss 2026-07-24): statt eine eigene QCC-Mini-Runtime (String-Vergleich, `printf`, Datei-I/O) nachzubauen, kann man die ECHTEN OS-9/Microware-`clib.l`-Funktionen direkt aufrufen, wenn man deren Aufrufkonvention trifft -- das ist im Ultra-C/C++-Prozessorhandbuch dokumentiert (`DOC/PDF/ultrac_pg.pdf`, Kapitel "Passing Arguments to Functions") und WEICHT von der internen QCC-Aufrufkonvention (rein stapelbasiert, `bsr tc_<name>`) ab: 1./2. Argument -> `d0`/`d1`, alle weiteren Argumente auf den Stack in UMGEKEHRTER Erscheinungsreihenfolge gepusht; bei einer VARIADISCHEN Funktion (wie `printf`) dagegen ALLE Argumente auf dem Stack, keine Register. `clib.l` exportiert die Symbole klarnamig ohne Underscore-Praefix (`strcmp`, `printf`, `malloc`, `strlen`, `fopen`, per `strings` bestaetigt). 68k-Backend-Codegen (NUR `qcc_backend_c.cpp`, ARM64 bewusst nicht betroffen, siehe unten): da unser eigener Stack-IR alle Argumente bereits in Erscheinungsreihenfolge auf `a7` liefert (1. Argument am weitesten unten), werden die "restlichen" (3.+) Argumente zuerst in ein festes Scratch-Feld (`tc_extcall_tmp`, PC-relativ adressiert wie die uebrigen `tc_g_<name>(pc)`-Zugriffe im PIC-Stil des Backends, max. 8 Eintraege) ausgelagert, damit sie nicht verloren gehen wenn darunter noch `d0`/`d1` herausgeholt werden muessen, und anschliessend in DERSELBEN (bereits passenden) Reihenfolge zurueckgepusht -- das ergibt exakt die von der ABI geforderte umgekehrte Reihenfolge. Aufruf selbst per `jsr <rohername>` (kein `tc_`-Praefix wie bei internen Aufrufen). Kein neuer IR-Opcode fuer die Argumentplatzierung noetig (nur vorhandene `move.l`/`lea`/`jsr`/`lea N(a7),a7`), nur `CALLEXT`/`CALLEXTP` selbst sind neue IR-Mnemonics. **Bewusst NICHT Teil dieses Schritts:** das eigentliche Linken gegen die reale `clib.l` (externe Symbolaufloesung ueber den Microware-Linker `l68`, relokierbares Objektformat statt der aktuellen `vasm -Fbin`-Direktassemblierung) -- ohne echten Q9-Zugriff in dieser Umgebung nicht sinnvoll aufsetzbar; ebenso fehlen QCC noch String-Literale (ohne die ist ein echter `printf("...", ...)`-Aufruf mit Formatstring nicht schreibbar, nur mit rein numerischen/Pointer-Argumenten). **Verifikation:** QCCVM UND ARM64 kennen die Microware-ABI bewusst NICHT und lehnen `CALLEXT` ueber ihren bestehenden "unbekannter Opcode"-Fallback sauber ab (kein stilles Falschverhalten). Die eigentliche Argumentplatzierung wurde end-to-end gegen HANDGESCHRIEBENE Mock-Stubs verifiziert (da kein echter `clib.l`-Zugriff moeglich ist) -- `tools/qcc68sim.py` (Test-Simulator, NICHT Teil der ausgelieferten Toolchain) wurde dafuer um `jsr <label>` (bisher nur `bsr` bekannt) sowie `N(a7)`/`N(a0)`-Adressierung und das Erkennen des `tc_extcall_tmp`-Scratch-Felds erweitert; drei Faelle bestaetigt: 2 Argumente (beide Register), 4 Argumente (2 Register + 2 Stack in umgekehrter Reihenfolge), 3 Argumente variadisch (alles Stack). |
| QCC zweidimensionale Arrays (z.B. `int m[2][3]`, `char names[3][4]`) | erledigt (2026-07-24) | Grammatik: optionale zweite `arraySize` NUR bei `globalDecl`/`localDecl` (`arraySize2`, eigene Regel ohne Aktion) -- bewusst NICHT bei `structField` (bleibt einstufig, ein 2D-struct-Feld ist damit ein sauberer Parse-Fehler statt eines still falsch geschnittenen Feldes) und NICHT bei `paramArray` (2D-Array-Parameter sind ebenfalls ein Parse-Fehler). `varRef`/`directTarget` erlauben jetzt `index [ index ]` -- der ZWEITE Bezug auf dieselbe `index`-Regel loest sich ueber die bereits bestehende, stapelbasierte `tcIndexDepth`-Buchfuehrung in `tc_callname`/`tc_arg` von selbst, kein neuer Grammatik-/Aktions-Mechanismus noetig. Speichermodell: `tcLocalArrayLen`/`tcGlobalArrayLen` speichern weiterhin die GESAMTE (flache) Elementzahl (dim1*dim2, row-major wie in echtem C) -- `sizeof`/`LARRAY`/`GARRAY` funktionieren dadurch automatisch unveraendert weiter; NEU: `tcLocalArrayDim2`/`tcGlobalArrayDim2` (0 = 1D) speichern zusaetzlich die innere Dimension. Zusammenfuehrung von `arr[i][j]` zu einem flachen Index `i*dim2+j`: da der Laufzeit-Operandenstack an dieser Stelle bereits `[i, j]` traegt (`i` unten, `j` oben, durch die beiden bereits geparsten Index-Ausdruecke) und ein direktes `MUL` faelschlich `j*dim2` statt `i*dim2` berechnen wuerde, wird `j` kurz in ein verstecktes globales Scratch-Feld (`__idx2d_j`, einmalig lazy per `GLOBAL`-Zeile deklariert) ausgelagert, `i` mit `dim2` skaliert, `j` wieder geladen und addiert (`tcEmit2DCombine`: nur vorhandene `STOREG`/`PUSH`/`MUL`/`LOADG`/`ADD`-Opcodes, kein neuer Opcode, kein Backend-Change). Erkennung 1 vs. 2 Indizes per Rohtext-Zaehlung der TOP-LEVEL-`[`-Vorkommen (`tcCountTopIndexes`, verschachtelte Indizes wie `arr[b[k]]` zaehlen bewusst NICHT als zweite Dimension). Flache Initialisierer (`int m[2][3] = {1,2,3,4,5,6};`) funktionieren automatisch mit, da sie einfach die bestehende flache `initList`/`GINIT`-Maschinerie wiederverwenden -- NUR verschachtelte `{{1,2,3},{4,5,6}}`-Initialisierer sind NICHT unterstuetzt (kein Nested-Brace in `initList`). Bewusst diagnostiziert statt stillschweigend falsch: `arr[i]` (nur EIN Index) auf ein 2D-Array ("partial indexing... not supported"), zwei Indizes auf ein 1D-Array ("array is not two-dimensional"), 3+ Indizes (Grenze war zunaechst 2D, seit 2026-07-24 auf beliebig viele Dimensionen erweitert, siehe eigene Tabellenzeile weiter unten). Bewusst NICHT unterstuetzt: Bounds-Checking der Einzeldimensionen bei Literal-Indizes (die bestehende `tcCheckConstIndex`-Diagnose greift nur bei genau EINEM Index und no-opt bei Mehrdim-Arrays lautlos), struct-Feld-/Parameter-Mehrdim-Arrays (s.o.). Verifiziert in QCCVM, 68000 UND ARM64 (Schleifen-basierte Zeilen/Spalten-Fuellung + Auslesen). |
| QCC `typedef struct { ... } Name;` (anonymes struct inline) | erledigt (2026-07-24) | Zielname kommt in der Grammatik erst nach dem Feld-Body -- verzögerte Registrierung in `tc_typedefend`, typedef-Name dient als interner struct-Tag (harmlose Vereinfachung, `struct Name x;` funktioniert dadurch als Nebeneffekt mit). Kein IR-/Backend-Change, exakt derselbe Feldzugriffs-Code wie bei benannten structs. Scope: nur reine anonyme Form, kein optionaler Tag, keine Pointer-Kombination |
| QCC `const`-Qualifizierer (Skalare/Arrays) | erledigt (2026-07-24) | `const` vor Typ bei globalen/lokalen Variablen und Parametern (`constKw`-Huellregel); verbietet Zuweisung/++/-- auf die qualifizierte Variable selbst ueber die bestehenden Ziel-Aufloesungspfade (`tc_target`, `tc_preincdec`/`tc_postincdec`). Reine Frontend-Pruefung, kein neuer Opcode, kein Backend-Change. Bei Pointertypen siehe eigene Zeile "Pointee-Constness" unten (jetzt ebenfalls erledigt). |
| QCC Pointee-Constness (`const T*`) | erledigt (2026-07-24) | Neues Feld `TCType.pointeeConst` (ein Bit, kein vollstaendiges Mehrebenen-Constmodell -- bei `T**` wird nur die unmittelbare Dereferenzierung geschuetzt). Wird bei `const`+Pointertyp in allen vier Registrierungsstellen gesetzt (`tc_local`, `tc_param`, `tc_globalend`, `tc_staticlocal`) und reist automatisch durch `tcPointerTo`/`tcPointee` (Struct-Kopie), Zeigerarithmetik (`tc_term`s P/IPADD-Zweige pushen denselben Typ zurueck) und Parameteruebergabe mit -- KEINE Aenderung an Lesezugriffen (`tc_varref`, `tc_derefref`) noetig. Durchgesetzt an den drei Schreibstellen: `tc_indirecttarget` (`*p = ..`) sowie den beiden Pointer-Array-Index-Zweigen in `tc_target` (`p[i] = ..`, lokal UND global) -- alle drei pruefen `pointeeConst` VOR dem `tcPointee()`-Unwrap und diagnostizieren `qcc: cannot assign through pointer to const`, OHNE die eigentliche Codeerzeugung abzubrechen (Stack bleibt balanciert). Der Pointer selbst bleibt frei zuweisbar (`p++`, `p = ...` weiterhin erlaubt) -- deckt jetzt auch semantisch, nicht nur syntaktisch, den Hauptnutzungsfall im Generator-Vorbild ab (`const char* line`-Parameter). Verifiziert: Lesen durch const-Pointer weiterhin erlaubt, Schreiben lokal/global/per Parameter diagnostiziert, `p++`-Idiom weiterhin erlaubt, normale (nicht-const) Pointer weiterhin frei beschreibbar (Regression). |
| QCC String-Literale (`char*`) | erledigt (2026-07-24) | Grammatik: neue `factor`-Alternative `stringLit = "\042" { character } "\042" .` mit `character = " "~"!" \| "#"~"~" .` (Vorbild `Data/oberon07.ebnf`, `\042` ist der Oktal-Escape des EBNF-Tools fuer ein woertliches Anfuehrungszeichen -- kein ebnf-Tool-Change, reine Grammatik-Ebene wie schon bei 2D-Arrays). `tc_string` (neue `ACTION AFTER stringLit`) dekodiert Escapes (`\n \t \r \0 \\ \"`, unbekannte `\x` woertlich) und emittiert einen anonymen globalen char-Array-Konstanten `__strN` (`GARRAY`/`GINIT` je Byte + Nullterminator) plus `ADDRG __strN` -- EXAKT dieselben, bereits vorhandenen IR-Opcodes wie ein initialisiertes globales `char`-Array; kein neuer Opcode, keine Backend-Aenderung noetig (QCCVM/68000/ARM64 kennen `GARRAY`/`GINIT`/`ADDRG` bereits aus Array-Initialisierern). Ergebnistyp `char*` (nicht pointeeConst) ist gegenueber einem `const char*`-Parameter kompatibel, da `tcCompatible`/`tcSameType` `pointeeConst` bewusst ignorieren (nur Basistyp+Zeigertiefe zaehlen) -- ein String-Literal kann also direkt an ein `extern`-`printf`-artiges `const char* fmt` uebergeben werden. **End-to-End verifiziert:** QCCVM (Literal+Indizierung, Literal als Funktionsargument), 68000 UND ARM64 (`qcc68sim`/natives Programm, `char* s = "AB"; s[0]/s[1]/s[2]` -> `AB0`), sowie -- das eigentliche Ziel dieses Schritts -- ein `extern`-Aufruf mit `const char*`-Parameter ueber CALLEXT gegen einen Mock-Stub, der das erste Byte an der uebergebenen Adresse zurueckliest (`extern int mockchr(const char* s); mockchr("Hi")` -> `72`, bestaetigt dass die PC-relative String-Adresse unveraendert bis `d0` durchgereicht wird). Bewusst NICHT Teil dieser Version: String-Literale als Array-Initialisierer (`char msg[6] = "hallo";`), String-Vergleich/-Verkettung als eigene Operatoren, direkte Indizierung/`sizeof` auf einem Literal ohne Zwischenvariable (`"AB"[0]` ist kein `factor` mit `[index]`-Suffix). |
| QCC extern-ABI-Bugfix: variadische Aufrufe (Formatstring-Parameter nach d0/d1) | erledigt (2026-07-24) | **Am echten Q9 gefundener und behobener Bug:** Die urspruengliche Annahme "bei einer variadischen Funktion gehen AUSNAHMSLOS alle Argumente auf den Stack, 0 Register" (siehe PR #23 oben) war NIE gegen echten, Compiler-erzeugten Code verifiziert worden -- nur gegen selbstgeschriebene Mock-Stubs, die dieselbe (falsche) Annahme teilten. Erster echter Test von `extern int printf(const char* fmt, ...); printf("value: %d\n", x);` gegen die REALE Microware-`clib.l` auf dem laufenden Q9-Emulator stuerzte diesen mit einem PMMU-Fehler ab (`680x0 PMMU: Unhandled Table B mode 0`). Diagnose (u.a. Q9s eigener Emulator-Quellcode `third_party/musashi/m68kmmu.h` temporaer um einen Register-/Opcode-Dump vor dem Absturz erweitert, siehe [[q9-xcc-toolchain-milestone]]): `d0` enthielt beim Absturz die Zahl `1` statt einer Formatstring-Adresse -- ein `capstone`-Disassembler-Vergleich des ECHTEN, von `xcc` erzeugten Aufrufcodes (`printf("value: %d\n", x)`) zeigte die tatsaechliche Microware-ABI-Regel: NUR die FEST deklarierten Parameter (bei `printf` genau einer: der Formatstring) gehen nach `d0`/`d1`, GENAU wie bei einem nicht-variadischen Aufruf -- ausschliesslich der variadische `"..."`-UEBERSCHUSS darueber hinaus geht auf den Stack. Fix: IR-Feld 3 von `CALLEXT`/`CALLEXTP` bedeutet jetzt `tcFunctionNargs[f]` (Anzahl fest deklarierter Parameter) statt eines reinen variadic-Bools; Backend (`qcc_backend_c.cpp`) berechnet `hasD0`/`hasD1` daraus (`fixedCount >= 1` bzw. `>= 2`) statt sie bei Variadic pauschal auf 0 zu setzen -- deckt nicht-variadische UND variadische Aufrufe einheitlich ab. Bestehender Mock-Test ("Fall 3", extern-ABI) korrigiert: `myprintf(1, x, 7)` erwartet jetzt `d0`=1 (fmt), `4(a7)`=x=42, `8(a7)`=7 (vorher faelschlich alle drei auf dem Stack). **Endverifikation:** derselbe `printf("value: %d\n", x)`-Aufruf lief nach dem Fix fehlerfrei durch, echte Ausgabe `value: 42` auf dem laufenden Q9-Emulator -- der urspruengliche Nutzerwunsch "echtes printf-Formatstring-Parsing" ist damit tatsaechlich erreicht (nicht nur die Adresse durchreichen, sondern `%d` etc. wird von der echten `clib.l` korrekt interpretiert). |
| QCC String-Literale als Array-Initialisierer (`char msg[6] = "hallo";`) | erledigt (2026-07-24) | Grammatik: `varInit = arrayStringInit \| expr \| initList .` mit `arrayStringInit = stringLit .` (MUSS vor `expr` probiert werden, sonst wuerde `expr` das Literal zuerst konsumieren) sowie global `globalInit = globalValue \| initList \| globalStringInit .` mit `globalStringInit = "\042" { character } "\042" .` (bewusst eine AKTIONSLOSE Kopie von `stringLit`, NICHT direkt referenziert -- `tc_globalend` liest die gesamte Deklaration ohnehin als Rohtext wie bei Zahlen/Bools, ein direkter Verweis wuerde nur unbenutzte `tc_string`-Nebenwirkungen erzeugen). Lokal: `arrayStringInit` referenziert `stringLit` bewusst DIREKT, weil dessen Aktion `tc_string` (anonyme `GARRAY`-Konstante + `char*`-Adresse auf dem Stack) fuer den weiterhin unveraendert funktionierenden SKALAREN Fall (`char* p = "..";`) sowieso noetig ist -- die neue Aktion `tc_arrayinitstring` verwirft die Adresse per `DROP` nur, wenn das Ziel tatsaechlich ein Array ist, und kopiert die zuvor mit einer neuen gemeinsamen Hilfsfunktion `tcDecodeStringLit` (aus `tc_string` herausgezogen) dekodierten Bytes direkt per `PUSH`/`PUSH`/`STOREIDX` in die lokalen Array-Slots -- exakt das bereits vorhandene Muster des numerischen `initList`-Zweigs, kein neuer Opcode. Global entsprechend per `GINIT`. Wie in echtem C ist ein EXAKT passendes Array ohne Platz fuer den Nullterminator erlaubt (`char m[5] = "hallo";`); zu lange Literale sowie Literale auf einem Nicht-`char`-Array werden diagnostiziert. Verifiziert in QCCVM, 68000 UND ARM64 (lokal exakt passend + global mit Nullterminator kombiniert in einem Testfall). |
| QCC `static`-Speicherklasse | erledigt (2026-07-24) | `staticKw`-Huellregel. Bei globalen Variablen/Funktionen reines No-op (interne Verlinkung ist bei einer einzigen Uebersetzungseinheit ohne Mehrdatei-Linkage bedeutungslos) -- deckt damit den GROSSTEIL der 101 realen Fundstellen in parsec.cpp/codegen.cpp ab (fast alle sind file-scope `static` auf Tabellenpuffern, keine lokalen). Bei LOKALEN Variablen echte Semantik: eigene Grammatikregel `staticVarDecl` (kein Frame-Slot ueber `localName`/`tc_local`, sondern `tc_staticlocal` registriert die Variable direkt als GLOBAL unter ihrem Klarnamen) -- dadurch greifen tc_target/tc_varref/tcLoadTarget/tc_preincdec/tc_postincdec/addressRef/sizeof(variable) automatisch ueber den schon vorhandenen tcLookupGlobal-Pfad zu, OHNE dass an einer dieser Stellen etwas geaendert werden musste. Backend-Voraussetzung: `collectGlobals`/`collectFunctions` in `qcc_backend_c.cpp` UND `qcc_arm64_backend_c.cpp` verlangten bisher "GLOBAL muss genau einmal VOR allen Funktionen stehen" -- gelockert auf "vor der ersten Funktion ODER innerhalb einer offenen Funktion", da eine static-Lokale ihr GLOBAL an der Textstelle ihrer Deklaration emittiert (moeglicherweise mitten in einer FUNC-Spanne); QCCVM tolerierte das schon vorher (reiner Pre-Scan). Verifiziert in QCCVM, 68000 UND ARM64 (echte Persistenz ueber mehrere Aufrufe). Konstante Initialisierer (`static int x = 5;`, `static bool b = true;`, `static char c = 65;`, Pointer-Initialisierer nur `0`) sind seit 2026-07-24 (Nachtrag) erlaubt: Grammatik beschraenkt auf `globalValue` (Zahl/Negativ/bool-Literal, dieselbe seiteneffektfreie Regel wie bei globalen Variablen -- KEINE Aktion auf `globalValue` selbst, also kein Laufzeit-PUSH waehrend des Parsens), `tc_staticlocal` extrahiert den Wert per Rohtext-Scan ab dem `=` (genau wie `tc_globalend` es fuer echte globale Variablen tut) und baut ihn direkt in die `GLOBAL`-Zeile ein -- kein Runs-once-Guard noetig. Ein NICHT-konstanter Ausdruck (`static int x = someVar;`) war zunaechst bewusst ein sauberer Parse-Fehler (seit 2026-07-24 spaeter erlaubt, siehe eigene Tabellenzeile weiter unten). Bewusst weiterhin NICHT unterstuetzt: static-`struct`-Lokale (diagnostiziert, `struct`-Globals sind generell noch nicht unterstuetzt) und static-Arrays. Ausserdem: kein Funktions-Scoping -- der Name einer static-Lokalen landet im GLEICHEN flachen globalen Namensraum wie normale globale Variablen, zwei Funktionen duerfen also noch keine gleichnamige static-Variable haben (`qcc: duplicate global`). |
| QCC nicht-konstanter static-Initialisierer (Runs-once-Guard) | erledigt (2026-07-24) | `staticInit = globalValue \| staticRuntimeInit .` mit `staticRuntimeInit = expr .` -- eine GEORDNETE Alternation: `globalValue` (der bisherige Fastpath, siehe Zeile oben) wird ZUERST versucht, `staticRuntimeInit` erst bei Nichtuebereinstimmung (identisches Backtracking-Prinzip wie "`call` vor `varRef`" in `factor`) -- `static int x = 5;` bleibt dadurch WEITERHIN ohne jeden Laufzeit-Code (kein Runs-once-Guard fuer den Konstantenfall noetig). Die neue `ACTION AFTER staticRuntimeInit CALL tc_staticruntimeinit` feuert NUR, wenn tatsaechlich der `expr`-Zweig gematcht hat (Signal fuer `tc_staticlocal`, da `globalValue` selbst weiterhin keine Aktion hat) -- prueft Typkompatibilitaet (`tcCompatible`) und setzt ein Pending-Flag. `tc_staticlocal` ueberspringt in diesem Fall seinen bisherigen Rohtext-Ziffern-Scan (der Ausdruck ist keine reine Ziffernfolge), registriert die Variable mit Startwert 0 und emittiert zusaetzlich einen VERSTECKTEN bool-Flag-Global (`__static_init_<name>`) sowie die Guard-Sequenz `LOADGC <flag>` / `JZ Ldo` / `DROP` / `JMP Lafter` / `LABEL Ldo` / `STOREG(C\|P) <name>` / `PUSH 1` / `STOREGC <flag>` / `LABEL Lafter` -- EXAKT dieselben Opcodes (`JZ`/`JMP`/`LABEL`), die `if`/`while` bereits verwenden (`tcNextLabel` als gemeinsamer Label-Zaehler), kein neuer Opcode, kein Backend-Change. **Bewusste Vereinfachung:** der Initialisierer-Ausdruck wird bei JEDEM Funktionsaufruf neu ausgewertet (sein PUSH-Code steht bereits VOR der Guard-Sequenz im IR-Strom, da `expr`s Teilaktionen waehrend des Parsens eager feuern, lange bevor `tc_staticlocal` am Ende der Regel selbst zum Zug kommt) -- NUR das tatsaechliche SPEICHERN in die static-Variable wird vom Guard geschuetzt, nicht die Berechnung selbst. Bei einem seiteneffektfreien Ausdruck (der weit ueberwiegende Regelfall, z.B. `static int x = n * 2;`) ist das BEOBACHTBARE Verhalten identisch zu echtem ISO C; bei einem Ausdruck mit Seiteneffekten (z.B. einem Funktionsaufruf, der selbst einen globalen Zustand veraendert) weicht es ab, da der Aufruf bei JEDEM Funktionseintritt erneut ausgefuehrt wird, nicht nur beim ersten. `const` in Kombination mit einem Laufzeit-Initialisierer funktioniert unveraendert (die bestehende `tcGlobalConst`-Durchsetzung in `tc_target` ist von der internen Guard-STOREG-Emission unberuehrt, da diese nicht ueber den normalen Zuweisungspfad laeuft). Verifiziert in QCCVM, 68000 UND ARM64: Initialisierer haengt von einem Funktionsaufruf (`base() + 5`), einer globalen Variablen und einem Funktionsparameter ab; `const` + Laufzeit-Initialisierer mit anschliessend verbotener Zuweisung; Typinkompatibilitaets-Diagnose (`static int x = mk();` bei `char* mk()`). |
| QCC direkte Indizierung ohne Zwischenvariable (`func()[i]`, `"text"[i]`) | erledigt (2026-07-24) | Neue Grammatikregel `postfixIndex = index .` -- eine reine Huellregel um die bereits bestehende `index`-Regel (KEIN neues Klammer-Handling, KEIN neuer Opcode). `factor` erlaubt jetzt `call [ postfixIndex ]` und `stringLit [ postfixIndex ]`. Dadurch feuern `indexOpen`/`tc_callname` (rettet/setzt `tcPendingAdd`/`tcPendingMul` zurueck, damit ein aeusseres `+`/`*` nicht mit dem inneren Indexausdruck kollidiert) und die bestehende `ACTION AFTER index CALL tc_arg` (prueft den Indextyp, stellt `tcPendingAdd`/`tcPendingMul` wieder her) GENAUSO wie bei `arr[i]` -- nur die neue `ACTION AFTER postfixIndex CALL tc_postfixindex` kommt hinzu. **Kernerkenntnis bei der Codeemission:** die Laufzeitreihenfolge auf dem Operandenstack ist hier `[Pointer, Indexwert]` (Pointer zuerst gepusht von `call`/`stringLit`, Indexwert danach vom inneren `index`-Ausdruck) -- das ist die GLEICHE Reihenfolge wie bei `p + n` in `tc_term` (linker Operand zuerst), NICHT die Reihenfolge, die `PTRINDEX`/`IPADD` erwarten (die den Pointer ZULETZT/obenauf verlangen, wie bei `LOADP <slot>\nPTRINDEX ..` in `tc_varref`, wo der Index schon vorher ausgewertet wurde). Deshalb emittiert `tc_postfixindex` `PADD <typ>\nLOADIND <typ>` (denselben Opcode wie Pointer-Addition, gefolgt von einer Dereferenzierung) statt `PTRINDEX`/`IPADD` -- kein neuer Opcode, keine Backend-Aenderung noetig, QCCVM/68000/ARM64 kennen `PADD`/`LOADIND` bereits aus der Pointer-Arithmetik. Diagnostiziert: Indizierung eines nicht-Pointer-Rueckgabewerts ("index expects ..."), Indizierung von `void*` ("cannot index void*"). Verifiziert in QCCVM (`"hallo"[0]`, `mkstr()[i]` mit Laufzeit-Index, gemischt mit umgebender Arithmetik `1 + mkstr()[0]` als Regressionstest fuer die `tcPendingAdd`-Rettung), 68000 (`qcc68sim`) UND ARM64 (natives Mach-O-Programm), jeweils `mkstr()[0]`/`mkstr()[4]` + `"world"[0]`/`"world"[4]` kombiniert zu `how100`. Bewusst NICHT Teil dieser Version: Indizierung als Zuweisungsziel (`foo()[0] = 5;`, bleibt saubererer Parse-Fehler), verkettete Postfix-Indizierung (`f()[0][1]`), Indizierung auf `"(" expr ")"`. |
| QCC direkte `p.field[i]`-Indizierung von Array-Feldern in `struct` | erledigt (2026-07-24) | `member` bekommt einen eigenen optionalen `index`-Anschluss (`member [ index ]`, KEIN zweiter, ineinander verschachtelter -- Array-Felder in `struct` bleiben wie `arraySize` bewusst einstufig). Die bestehende `ACTION AFTER index CALL tc_arg` feuert fuer diesen Index GENAUSO wie bei `arr[i]` (rettet/restauriert `tcPendingAdd`/`tcPendingMul`, prueft den Indextyp) -- `tc_varref`/`tc_target` erkennen per Rohtext-Scan (erstes Zeichen nach dem Feldnamen `== '['`) ob ein Index folgt und haengen dann ein ZWEITES `IPADD`+`LOADIND` (lesend) bzw. `IPADD` (schreibend, `tcTargetIndirect=1` wie bei einer echten Pointer-Dereferenz) an das bereits vorhandene Feldadress-`IPADD` an. **Kernerkenntnis:** der Operandenstack traegt an dieser Stelle bereits `[Indexwert, Feldadresse]` (Feldadresse ZULETZT gepusht, da `tc_varref`/`tc_target` erst am Ende der gesamten `varRef`/`directTarget`-Regel feuern, NACHDEM der verschachtelte Index-Ausdruck seinen Wert schon gepusht hat) -- exakt dieselbe Reihenfolge, die das bestehende `IPADD` fuer die Feldadresse selbst schon erwartet; kein neuer Opcode, kein Backend-Change. Nur LOKALE struct-Variablen (globale `struct`-Variablen sind generell noch nicht unterstuetzt, siehe SELFHOSTING_LUECKENLISTE.md). Diagnostiziert: Indizierung eines SKALAREN struct-Felds (`qcc: scalar struct field cannot be indexed`), konstante Index-Bereichsverletzung (wiederverwendet `tcCheckConstIndex`). Zuweisung an das GANZE Array-Feld (`p.buf = ..`, ohne Index) bleibt wie bisher diagnostiziert -- nur die NEUE indizierte Form (`p.buf[i] = ..`) wird jetzt akzeptiert. Verifiziert in QCCVM (Lesen/Schreiben, `int`- und `char`-Array-Felder, Laufzeit-Index, kombinierte Zuweisung `+=`, gemischt mit umgebender Arithmetik als Regressionstest fuer die `tcPendingAdd`-Rettung, Pointer-Zwischenvariable weiterhin moeglich), 68000 (`qcc68sim`) UND ARM64 (natives Mach-O-Programm). |
| QCC mehr als 2 Array-Dimensionen (z.B. `int m[2][3][4]`) | erledigt (2026-07-24) | Generalisierung der urspruenglichen 2D-Loesung: `arraySize2` (feste zweite Dimension) wird zu `arraySizeN` mit `{ arraySizeN }` (beliebig viele Wiederholungen in der Grammatik, `TC_MAXDIMS=6` begrenzt N zur Uebersetzungszeit -- eine grosszuegige, aber endliche Konstante, analog zu anderen festen Tabellengroessen im Projekt). `varRef`/`directTarget` erlauben jetzt `index { index }` statt `index [ index ]` -- ACTION AFTER index feuert einfach N-mal in Folge, keine neue Grammatik-/Aktions-Maschinerie noetig. Speichermodell: `tcLocalArrayDim2`/`tcGlobalArrayDim2` (ein `int`) werden zu `tcLocalArrayNDims`/`tcGlobalArrayNDims` (Dimensionszahl) + `tcLocalArrayDims`/`tcGlobalArrayDims` (Array der TRAILING-Dimensionen dim2..dimN); `tcLocalArrayLen`/`tcGlobalArrayLen` bleiben unveraendert die GESAMTE flache Elementzahl (Produkt aller Dimensionen). **Kombinationslogik generalisiert** (`tcEmit2DCombine` -> `tcEmitNDCombine`): der Laufzeit-Operandenstack traegt nach dem Parsen von `arr[i1]..[iN]` bereits `[i1, i2, ..., iN]` (i1 unten, iN oben) -- i2..iN werden der Reihe nach (von OBEN, also erst iN, dann i(N-1), ... zuletzt i2) in je ein eigenes, lazy deklariertes Scratch-Global (`__idxNd_2`..`__idxNd_N`) ausgelagert, bis nur noch i1 auf dem Stack liegt; danach kombiniert ein Horner-Schema (`result=i1; fuer lvl=2..N: result=result*dim[lvl]+scratch[lvl]`) den flachen row-major-Index -- fuer N=2 IDENTISCH zur bisherigen Loesung (nur ein Scratch-Feld), kein neuer Opcode, kein Backend-Change (nur vorhandene `STOREG`/`PUSH`/`MUL`/`LOADG`/`ADD`, jetzt in einer Schleife statt zweimal ausgeschrieben). Die zwei urspruenglichen 2D-spezifischen Fehlermeldungen ("array is not two-dimensional" bei 2 Indizes auf ein 1D-Array, "partial indexing of a 2D array is not supported" bei 1 Index auf ein 2D-Array) bleiben WORTGLEICH bestehen (bestehende Regressionstests pruefen exakt diese Substrings); fuer N>2 gibt es generalisierte Entsprechungen ("partial indexing of a %dD array...", "array has %d dimension(s), but %d index(es) were given"). Flache Initialisierer (`int m[2][2][2] = {1,...,8};`) funktionieren automatisch mit (dieselbe dimensionsunabhaengige `initList`/`GINIT`-Maschinerie wie bei 2D). Bewusst weiterhin NICHT unterstuetzt: struct-Feld-/Parameter-Mehrdim-Arrays, verschachtelte Brace-Initialisierer, Bounds-Checking der Einzeldimensionen bei Literal-Indizes (unveraendert gegenueber der 2D-Version). Verifiziert in QCCVM, 68000 UND ARM64 mit einem echten 3D-Array (`int m[2][3][4]`, verschachtelte Schleifenfuellung + Auslesen), inkl. Regressionstest fuer gemischt umgebende Arithmetik (`1 + m[0][0][0] + m[1][1][1]`, prueft die `tcPendingAdd`-Rettung ueber DREI verschachtelte Indexklammern). |

## Selfhosting (neue Zielrichtung ab 2026-07-23)

Ziel: Generator + QCC-Toolchain irgendwann in QCC selbst schreib- und
übersetzbar machen. Vollständige Analyse und Reihenfolge in
`docs/SELFHOSTING_LUECKENLISTE.md`. Kurzfassung des Stands:

1. `struct`/`typedef`/`enum`/`for`/`switch` in QCC: erledigt (siehe Tabelle oben)
2. `const` (inkl. Pointee-Constness), `static` (inkl. konstantem Initialisierer),
   `void`/`void *` und zweidimensionale Arrays: erledigt (2026-07-24, siehe
   Tabelle oben)
3. Mini-Runtime (String-Vergleich, formatierte Ausgabe, Datei-I/O): Strategiewechsel
   (2026-07-24) -- statt sie in QCC nachzubauen, `extern`-Deklarationen +
   CALLEXT/CALLEXTP (Microware-ABI-Aufrufkonvention) erledigt, siehe Tabelle oben.
   Ausgabemodus fuer den ECHTEN Microware-Assembler `r68` (`-os9`-Flag am
   68000-Backend, siehe Tabelle oben) ebenfalls erledigt (2026-07-24). **Echtes
   Linken gegen `clib.l` via `l68` UND echte Ausfuehrung auf dem echten Q9:
   erledigt (2026-07-24)**, siehe eigene Tabellenzeile "QCC 68000-Backend:
   echtes Linken gegen `clib.l`" oben -- `putint`/`putuint`/`putchar` rufen
   jetzt `_os_write` (roher, ungepufferter Schreib-Syscall aus `clib.l`) direkt
   auf, ein Testprogramm lief ECHT auf dem Q9-Emulator und gab die korrekten
   Werte aus. Drei eigenstaendige Bugs dabei gefunden und behoben (siehe dort:
   falsches Frame-Register a6 statt a5, `jsr` statt `bsr` fuer externe Rufe,
   falsche `l68`-Dateireihenfolge) plus ein voellig unabhaengiger, vorher nie
   entdeckter Bug in `tc_mod_i32`/`tc_umod_u32` (der `%`-Operator im
   68000-Backend rechnete Quotient*Dividend statt Quotient*Divisor).
   String-Literale (fuer `printf`-Formatstrings mit variablen Werten): seit
   2026-07-24 **erledigt** (siehe eigene Tabellenzeile "QCC String-Literale"
   oben) -- `_os_write` selbst war bereits vorher echt nutzbar und unabhaengig
   davon
4. Mehrdatei-Übersetzung: **erledigt (2026-07-25)** -- bare Funktionsprototyp
   ohne Rumpf (normale interne Verlinkung, getrennt vom Microware-ABI-`extern`)
   + `extern <typ> <name>;` bei globalen Variablen, `static` bekommt echte
   Bedeutung, neue IR-Pseudo-Opcodes `FUNCDECL`/`GLOBALDECL`. Live gegen echte
   Toolchains verifiziert: 68k/OS-9 ueber echten `r68`+`l68`-Link (inkl. neuem
   `-runtime`-Flag fuer den gemeinsamen 68k-Core-Anker UND Namensverfremdung
   fuer `static`, da `r68`/`l68` KEIN Sichtbarkeitskonzept kennen -- kein
   `xdef`/`xref`, empirisch widerlegt), ARM64 ueber echte getrennte
   `.o`-Kompilate + `clang`/`ld`-Link (hier reicht Weglassen von `.globl`,
   Mach-O unterstuetzt ECHTE lokale Symbole, keine Namensverfremdung noetig).
   Details siehe `docs/STATUS.md` Abschnitt "Mehrdatei-Übersetzung".
5. `goto`/Funktionszeiger (erst wenn der generierte Parser-Zwilling selbst gehostet werden soll): noch offen
6. Speicherbedarf der statischen Puffer in `parsec.cpp`/`codegen.cpp` fuer ein reales
   16-MB-/8-MB-Zielsystem (Q9): **erledigt (2026-07-24)**, siehe `docs/STATUS.md` --
   Host-Datensegment ~34,6 MB -> ~1 MB (ACTION/ROUTINE-Tabellen jetzt malloc/realloc-
   basiert statt fester Arrays). xcc-Build+Ausfuehrungstest auf dem echten Q9 mit
   dieser Aenderung noch nicht wiederholt (naechster Schritt laut STATUS.md).

## Bewusst offen

- String-Vergleich/-Verkettung als eigene Operatoren, `sizeof` auf einem Literal ohne Zwischenvariable (String-Literale selbst, String-Literale als Array-Initialisierer UND direkte `[index]`-Indizierung ohne Zwischenvariable sind seit 2026-07-24 erledigt, siehe Tabelle oben)
- Mehrebenen-Pointee-Constness bei `T**` (nur unmittelbare Dereferenzierung geschuetzt), `char* const` (Pointer selbst konstant, umgekehrter Fall zu Pointee-Constness)
- static-`struct`-Lokale, static-Arrays, Funktions-Scoping fuer static-Lokale-Namen (nicht-konstante static-Initialisierer selbst sind seit 2026-07-24 erledigt, siehe Tabelle oben)
- verschachtelte structs (Pointer-Felder in `struct` sowie direkte `p.field[i]`-Indizierung von Array-Feldern sind erledigt, siehe Tabelle oben)
- Live-Ausfuehrung von `malloc`/`realloc`/`free` (und des kompletten ActionRoutine-Piloten) auf dem ECHTEN Q9-Emulator -- der Mechanismus selbst ist jetzt sowohl strukturell (echter `r68`+`l68`-Link gegen die echte `clib.l`) als auch end-to-end per Mock-Stub-Ausfuehrung (`qcc68sim`) verifiziert, siehe eigene Tabellenzeile weiter unten. Was NUR echter Q9-Zugriff noch bestaetigen kann: dass `realloc`s Kopierverhalten (alte Eintraege bleiben nach Wachstum korrekt lesbar) mit der ECHTEN `clib.l` genauso funktioniert wie mit dem vereinfachten Mock (der nicht kopiert) -- naechster Schritt, sobald der Emulator gestoppt werden kann.
- Zeigerarithmetik auf `void*` (bewusst wie echtes ISO C behandelt: nicht erlaubt, im Unterschied zur GCC-Erweiterung mit Groesse 1)
- verschachtelte Brace-Initialisierer fuer Mehrdim-Arrays (`{{1,2},{3,4}}`), Bounds-Checking der Einzeldimensionen bei Mehrdim-Literal-Indizes, Mehrdim-Arrays als struct-Feld/Parameter (mehr als 2 Array-Dimensionen selbst sind seit 2026-07-24 erledigt, siehe Tabelle oben)
- flexiblere (nicht fest dimensionierte) Arrays allgemein
- nichtkonstante globale Initialisierer (betrifft jetzt auch static-Lokale-Initialisierer, siehe oben)
- endgültige Q9-Start-, Modul- und Systemcall-Runtime
- zusätzliche Architekturen und Optimierungen
- Mehrdatei-Übersetzung (seit 2026-07-25 erledigt, siehe oben) hat bewusste Grenzen:
  kein `#include`-Mechanismus/keine gemeinsame Header-Datei -- Nutzer müssen
  Funktionssignaturen/Global-Typen von Hand in beiden Dateien konsistent halten;
  nur `tools/qcc_merge.py` (QCCVM) prüft Signaturkonsistenz als Bonus, die
  echten Linker (`l68`, `ld`) kennen nur Namen, keine Typen

## Arbeitsreihenfolge für neue Features

1. Sprachregeln in `Data/qcc.ebnf` festlegen.
2. Aktionen in `Data/qcc.lextab` ergänzen bzw. regenerieren.
3. Typ- und Semantikprüfung im Frontend erweitern.
4. Neuen oder angepassten IR-Befehl definieren.
5. QCCVM implementieren und dort zuerst testen.
6. 68000- und ARM64-Backend ergänzen.
7. Kleinen gezielten Regressionstest in `runtests.sh` aufnehmen.
8. `./runtests.sh` vollständig ausführen und diese Datei aktualisieren.

