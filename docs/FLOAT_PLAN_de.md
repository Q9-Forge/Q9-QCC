# Gleitkomma in QCC — Grundriss

Stand: 2026-09-16. Dies ist **kein** Code, sondern das Ergebnis der
Sondierung: was das Ziel kann, was das Orakel tut, und welcher Zuschnitt sich
daraus ergibt. Gebaut ist noch nichts.

## Der entscheidende Befund: keine Softfloat-Bibliothek nötig

**Gemessen, nicht angenommen:**

1. **Q9-Flux emuliert keine FPU-Hardware.** Im Emulator findet sich keine
   Spur von 68881/68882.
2. **xcc erzeugt trotzdem FPU-Befehle** — `fmove.d`, `fadd.x`, `fmul.x`,
   `fmovem.x`, Register `fp0`/`fp1` — und zwar auch mit `-tp=68000`, also
   unabhängig vom angegebenen Zielprozessor.
3. **OS-9 fängt sie ab.** Im Testabbild liegt ein `fpu`-Modul in
   `/CMDS/BOOTOBJS`; es bedient die F-Line-Ausnahme.
4. **Die Probe läuft.** Ein mit xcc übersetztes `double`-Programm
   (`3.5 * 2.0 + 1.0`) gibt auf dem echten 68030 im Q9-Flux korrekt `8.0`
   aus.

**Der Mechanismus ist ein TRAPHANDLER, keine Bibliothek** — nachgesehen im
lauffähigen Modul: die FPU-Befehle stehen unmittelbar im Programmcode
(42 Stellen mit der Coprozessor-Kodierung `$F2xx`, z. B. `f240 2008`), und
das Modul referenziert dafür nichts. Der 68030 ohne FPU löst auf jedem
dieser Befehle eine F-Line-Ausnahme aus; `fpu` (ein ausführbares Modul von
12.848 Byte in `/CMDS/BOOTOBJS`) bedient sie.

**Konsequenz:** QCC erzeugt FPU-Befehle direkt und bindet dafür **gar nichts**
— weder eine eigene Softfloat-Bibliothek noch eine fremde. Der mit Abstand
größte Brocken dieses Vorhabens entfällt damit; das war die offene Frage, an
der die Aufwandsschätzung hing.

Was qclib betrifft, bleibt allein die **Ausgabe**: `printf("%f")` und die
Umwandlung Zahl↔Text. Das ist Bibliotheksarbeit und von der Arithmetik
unabhängig — rechnen kann das Programm ohne jede Ergänzung.

**Preis und Abhängigkeit:** Jedes Gleitkomma-Programm braucht das
`fpu`-Modul im Bootabbild. Das gehört in die Voraussetzungen der Testskripte,
sonst scheitert ein Lauf mit einer Ausnahme statt mit einer Meldung.

## Zuschnitt

| Schicht | Was zu tun ist |
|---|---|
| Frontend | `float`/`double` als Basistypen (neue Typtags neben `i`/`u`/`c`/`h`), Gleitkomma-Literale (`3.5`, `1e-3`), übliche arithmetische Konversionen int↔double |
| IR | Eigene Opcodes statt Überladung der ganzzahligen: Laden/Speichern, Grundrechenarten, Vergleiche, Konversionen in beide Richtungen |
| 68k-Backend | `fmove.d`/`fadd.x`/`fsub.x`/`fmul.x`/`fdiv.x`/`fcmp`, Rettung von `fp0`-`fp7` über Aufrufe hinweg |
| ARM64-Backend | native Gleitkommabefehle, keine Emulation nötig |
| VM (`qccvm.py`) | Python-Gleitkomma als Orakel — **aber Vorsicht**: Python rechnet mit 64 Bit, die 68k-FPU intern mit 80 (`fadd.x`). Für Vergleichstests nur Werte nehmen, die in beiden exakt sind, oder auf `double` runden |
| qclib | `printf("%f")` und Konversionen, falls Ausgabe gebraucht wird |

## Reihenfolge, die sich anbietet

1. **Typ und Literale** im Frontend, zunächst nur `double` — `float` ist in C
   ohnehin der Sonderfall (übliche Konversionen ziehen auf `double` hoch).
2. **IR-Opcodes festlegen** und im VM-Orakel umsetzen; damit lässt sich alles
   Weitere auf dem Host prüfen, ohne Emulator.
3. **68k-Backend**, dann die Probe auf echter Hardware — das Muster, das sich
   heute bei den Zeigertabellen bewährt hat (erst das Orakel, dann beide
   Ketten, dann das Ziel).
4. **ARM64** zuletzt; dort ist es am einfachsten.

## Was diesen Plan noch nicht trägt

- **Genauigkeit:** `float` (32 Bit) gegen `double` (64) gegen die 80 Bit der
  68k-FPU. Solange nur `double` unterstützt wird, ist das beherrschbar; mit
  `float` kommen Rundungsfragen dazu, die einen eigenen Testaufbau brauchen.
- **Die Zeigergröße hat gezeigt**, wie man Zielunterschiede sauber trägt
  (`k+nP`, aufgelöst im Backend). Für Gleitkomma stellt sich die gleiche
  Frage bei `sizeof(double)` — auf beiden Zielen 8, hier also unkritisch.

## Stand 2026-09-16 — Schritt 1 angefangen, nicht abgeschlossen

Vom Schritt 1 („Typ und Literale“) ist der **Typ** da, die **Literale** nicht.
Was heute tatsächlich gebaut und geprüft wurde:

- `double` ist ein Basistyp im Frontend (interne Kennung `'d'`), `sizeof(double)`
  liefert 8, Variablen und Struct-Felder lassen sich deklarieren.
- **Die Ausrichtung ist gemessen, nicht angenommen.** xcc richtet `double`
  auf **2** Byte aus, nicht auf 8 — das ist die 68k-Wortausrichtung, und sie
  gilt hier für den 8-Byte-Typ genauso wie für `int`. Gegengerechnet:

  | Struct | QCC | xcc |
  |---|---|---|
  | `{ double d; int i; }` | 12 | 12 |
  | `{ int i; double d; }` | 12 | 12 |
  | `{ char a; short s; }` | 4 | 4 |
  | `{ char c; double d; }` | 12 | **10** |

  Der letzte Fall weicht ab, aber **nicht wegen `double`**: QCC rundet jede
  Structgröße auf ein Vielfaches von 4 auf. Dieselbe Abweichung zeigt schon
  `struct { char a; }` (QCC 4, xcc 1). Das ist eine ältere ABI-Abweichung mit
  eigenem Faden, kein Gleitkomma-Thema.
- Gleitkomma-**Literale** werden **gemeldet** (`floating point literals are not
  supported yet`) statt still zu einem falschen Ergebnis zu führen. Das war die
  eigentliche Gefahr: ein durchrutschendes `1.5` hätte schweigend `1` gerechnet.
  Die Grammatikregel `floatLit` steht vor `number`, ohne den Member-Zugriff
  `s.a` zu beschädigen — dafür gibt es einen eigenen Testfall.
- Neun Tests in `Q9-PARSEC/runtests.sh`; Suite 220 grün. Bootstrap über xcc
  EXIT=0, `test_struct_68k.sh` 42/42, Selbsthost auf dem 68030 wieder am
  Fixpunkt (IR byteidentisch, 104351 Zeilen).

**Ausdrücklich noch nicht da:** keine IR-Opcodes, keine Arithmetik, keine
Konversion, kein Backend-Code. Ein `double` lässt sich deklarieren und seine
Größe abfragen — mehr nicht. Wer damit rechnen will, bekommt eine Meldung.

### Die Henne-Ei-Frage ist beantwortet: Dezimal → IEEE-754 steht

Bevor ein einziges Literal gerechnet werden kann, muss der Compiler aus der
Ziffernfolge `3.14` das Bitmuster `0x40091EB851EB851F` erzeugen — **mit
Ganzzahlarithmetik**, denn QCC übersetzt sich selbst und hat dabei kein
Gleitkomma zur Verfügung. Dieser Baustein ist fertig:
`tools/dec2ieee.c`, C89, rund 300 Zeilen.

**Wie er rechnet.** Die Mantisse wird als Big-Integer aus 16-Bit-Gliedern
geführt; 16 Bit deshalb, weil ein Produkt zweier Glieder samt Übertrag
gerade noch in 32 Bit passt — auf dem 68030 ist `unsigned long` genau so
breit. Bei positivem Zehnerexponenten wird durchmultipliziert, bei negativem
der Zähler so weit hochgeschoben, dass der Quotient über 60 Bit hat, und der
Divisionsrest wird zum Sticky-Bit. Gerundet wird einmal, zur nächsten Zahl,
bei genau der Hälfte zur geraden — und für Denormale rutscht die
Rundungsstelle auf die feste kleinste Stufe 2^-1074 statt auf 53 Bit.

**Wie er geprüft ist.** Gegen ein Orakel, in drei Stufen:

| Stufe | Umfang | Ergebnis |
|---|---|---|
| Host gegen `struct.pack` | 894 Werte quer durch alle Größenordnungen | 0 Abweichungen |
| Host, Grenzfälle | 510 exakt ausgeschriebene Mittelpunkte zwischen zwei benachbarten `double` (bis 1976 Ziffern), normal wie denormal | 0 Abweichungen |
| von QCC übersetzt, VM-Orakel | 25 Fälle samt `5e-324`, `1e308`, `1e309`, 30-stelliger Ganzzahl | 0 Abweichungen |
| von QCC übersetzt, echter 68030 | dieselben 25 Fälle, `tests/dec2ieee68k.sh` | **alle 25 stimmen** |

Die exakten Mittelpunkte sind der eigentliche Prüfstein: dort entscheidet
sich Ties-to-even, und ein Konverter, der nur „ungefähr richtig“ rundet,
fällt genau da auf. Der Lauf auf echter Hardware ist der zweite: auf dem Mac
ist `long` 64 Bit breit, ein Überlauf in der Gliederarithmetik fiele dort
gar nicht auf.

Nebenbei hat der Konverter zwei stille Abbrüche im Compiler aufgedeckt, von
denen einer behoben ist (Hex in Initialisierern) — s. `KNOWN_BUGS_C89_de.md`.

### Was als Nächstes ansteht

Der Konverter liegt bewusst als eigenständige Datei vor und nicht schon in
`qcc.lextab`: er wird dort erst gebraucht, wenn feststeht, wie ein
`double`-Wert im IR aussieht. Das ist der nächste Schritt — und der größere,
denn er berührt Ablage (8 Byte statt 4), Aufrufkonvention und Typprüfung,
nicht nur ein neues Opcode-Paar.

## Der Engpass war der Assembler — er ist weg (2026-09-16)

Vor dem 68k-Backend steht eine Frage, die der Plan oben stillschweigend
übersprungen hatte: **kann qr68 überhaupt FPU-Befehle?** Gemessen: nein,
keinen einzigen. Immerhin sauber gemeldet („Befehl noch nicht kodierbar“)
statt still danebengegriffen.

Das ist jetzt erledigt. Gemessen wurde zuerst, was xcc für eine Handvoll
`double`-Funktionen erzeugt — daraus kam der Befehlssatz, den ein Compiler
wirklich braucht, und keine erfundene Wunschliste. Danach lieferte r68 zu
jeder Form die Sollbytes, und `tests/fpu.a` hält sie fest.

Ergebnis: 75 Formen, Zeile für Zeile byteidentisch zu r68, der SDK-Korpus
unverändert (290 gleich, eine bewusste Verweigerung), und die Probe wird
von qr68 **auf dem echten 68030 selbst assembliert** — ebenfalls
byteidentisch. Einzelheiten in `Q9-BACKEND-68K/q9-qr68k/README.md`.

### Was xcc über die Aufrufkonvention verrät

Aus demselben Assembler-Orakel, und wichtig für alles Weitere, weil QCC
sonst kein `printf("%f")` aufrufen kann:

| Frage | gemessen bei xcc |
|---|---|
| erstes `double`-Argument | in **d0/d1** (hi in d0) |
| weitere Argumente | auf dem Stack, `double` 8 Byte, `int` 4 |
| Rückgabe | **d0/d1** |
| gerechnet wird in | **80 Bit** (`fadd.x`), gespeichert in 64 |
| `int` → `double` | `fmove.l d0,fp0` |
| `double` → `int` | `fintrz.x` und dann `fmove.l fp0,d0`, schneidet also Richtung null ab |
| Vergleich | `fcmp.x` und ein `FBcc` mit umgekehrter Bedingung |
| `fp0`/`fp1` über Aufrufe | vom Gerufenen gerettet (`fmovem.x`) |
| Literal im Assembler | `dc.l $3ff80000,$0` — dasselbe Bitmuster, das `tools/dec2ieee.c` liefert |

Damit sind alle 68k-Fragen beantwortet, und der nächste Schritt ist wieder
der aus dem Plan: die IR. Dort ist die offene Frage nicht die
Befehlskodierung, sondern die Ablage — ein `double` braucht 8 Byte, und
die Slots der IR sind bisher einheitlich schmal.

## Der IR-Entwurf steht: `docs/FLOAT_IR_ENTWURF_de.md`

Die offene Frage aus Schritt 2 -- wie ein `double` in der IR aussieht -- ist
beantwortet und aufgeschrieben. Kurz: Die Slots sind schon heute nicht
einheitlich breit (68k 4 Byte, ARM64 16), deshalb wird ein `double` nicht
in Slots gelegt, sondern als **Block** wie eine lokale struct-Variable. Der
Mechanismus dafuer existiert bereits (`LARRAY`/`GARRAY`, `arrayOffset()`),
und die Offsetrechnung beider Backends bleibt unveraendert.

## Der Durchstich steht (2026-09-16)

Ein C-Programm mit `double`, übersetzt mit der eigenen Kette, **rechnet auf
echtem 68030 — und auf ARM64**. Damit ist die Reihenfolge aus dem Plan oben
abgearbeitet.

    double a; double b;
    a = 1.5; b = 2.5;
    if (a < b) putint((int)(a * b));      /* 3 */

`Q9-BACKEND-68K/q9-qclib/tests/double68k.sh` prüft dreizehn Fälle auf der
Hardware: die vier Grundrechenarten, beide Konversionsrichtungen, alle
Vergleichsformen, eine globale Variable, einen Wert über eine Schleife
hinweg. Jeder Sollwert ist so gewählt, dass er bei falscher Rechnung nicht
herauskommt — `7.0/2.0` muss **3** ergeben und nicht 4, denn `(int)`
schneidet ab und rundet nicht.

Was dabei aus der Kette wurde:

| Stufe | Stand |
|---|---|
| Dezimal → IEEE-754 | im Compiler, `tools/dec2ieee.c` wortgleich in `qcc.lextab` |
| Frontend | Typ, Literale, Variablen, Arithmetik, Vergleiche, Konversionen |
| IR | `PUSHD`, `LOADD`/`STORED`, `DADD`…, `DCMPxx`, `I2D`/`D2I` |
| VM-Orakel | vollständig |
| 68k-Backend | vollständig, FPU-Befehle |
| Assembler | qr68 kann die FPU, byteidentisch zu r68 |
| ARM64 | vollständig, native Gleitkommabefehle |

### Was bewusst offen blieb

- **Initialisierer an globalen `double`** (`double g = 2.5;`). Dafür fehlt
  der `GINIT`-Pfad für Bitmuster.
- **`float`** als eigener Typ. In C ziehen die üblichen Konversionen
  ohnehin auf `double` hoch; `float` bringt eigene Rundungsfragen mit.
- **`double` als Parameter und als Rückgabewert** — seit heute
  **gemeldet**, vorher still falsch (siehe unten).
- **Aufrufe mit `double`-Argumenten** gegen die Microware-`clib`
  (`printf("%f")`). QCC übergibt alles auf dem Stack, xcc das erste
  Argument in `d0/d1` — das betrifft nur den `CALLEXT`-Pfad.

### Beide Ziele, eine IR, dasselbe Ergebnis

Dasselbe Programm läuft auf beiden Zielen mit denselben zwölf Sollwerten:
auf dem 68030 rechnet ein Trap-Handler die F-Line-Befehle nach, auf ARM64
sind die Gleitkommabefehle einfach da, und ein `double` ist dort nur ein
64-Bit-Bitmuster, das zum Rechnen kurz in ein `d`-Register geht. Der
ARM64-Lauf wird zusätzlich gegen das VM-Orakel verglichen — drei
unabhängige Wege, ein Ergebnis.

Auch hier zeigte sich wieder das Geschwister-Muster: die Taglisten, die ein
neues Typkürzel zulassen müssen, liegen an **vier** Stellen (`isNumWord` in
beiden Backends, die `LARRAY`-Prüfung in ARM64, die Größentabelle im
Orakel). Wer nur eine anfasst, bekommt einen Abbruch an der nächsten.

## Gemischte Ausdrücke und Zuweisungen (2026-09-16, später am Tag)

`double` war ohne die üblichen arithmetischen Konversionen kaum benutzbar —
`a * 2` ist in echtem Code allgegenwärtig. Jetzt zieht C89 3.2.1.5 wie
vorgesehen: ist ein Operand `double`, wird der andere hochgezogen.

Die Schwierigkeit lag nicht in der Regel, sondern in der **Stapellage**.
Beim Emittieren liegt der linke Operand schon unter dem rechten. Für
`a + 1` genügt deshalb ein `I2D` auf das oberste Element, für `1 + a` nicht
— dort muss der Wert **darunter** umgewandelt werden. Statt `SWAP` (mit
zwei verschiedenen Breiten heikel) gibt es dafür `I2DUNDER`, das auf beiden
Zielen genau drei Zeilen kostet.

Geprüft wird beides getrennt, und mit nicht-kommutativen Fällen: `10 - a`
und `32 / a` fielen sofort auf, wenn die Konversion den falschen Operanden
träfe.

Bei Zuweisungen konvertiert `tcCoerceToTarget` in beide Richtungen, samt
Zielverengung (`char c = 66.9;` ergibt 66). Das läuft **bewusst nicht** über
`tcCompatible`: dieselbe Verträglichkeit gälte sonst auch für
Kettenzuweisung, Argumente und Rückgaben, und dort würde ohne Emission
still der falsche Wert landen. Lieber hier gezielt umwandeln und dort
weiter melden.

## Zwei stille Fehler, die erst beim Nachfassen auffielen

Nachdem alles lief, blieb eine Frage offen, die im Test nicht vorkam:
**Funktionen mit `double`.** Beides war still falsch:

- **Rückgabe.** Das Backend holt den Rückgabewert mit einem einzigen
  `move.l (a7)+,d0`. Bei acht Byte ist das die **obere Hälfte** — die
  untere blieb auf dem Stapel liegen. Ein Programm hätte gerechnet, nur
  eben mit der halben Zahl und einem Stapelleck dazu.
- **Parameter.** Ein Parameter liegt in einem **Slot**, und der ist auf dem
  68k vier Byte breit. Ein `double` liegt überall sonst als Block; für
  Parameter gibt es diesen Weg noch nicht. Im VM-Orakel endete das in
  einem `KeyError` — ein Absturz statt einer Diagnose.

Beides wird jetzt gemeldet. Für die Umsetzung braucht die IR ein eigenes
`RETD` samt Gegenstück beim Aufrufer, und Parameter brauchen Blockablage
oder zwei Slots. Das ist ein eigener Schritt, kein Nachtrag.

**Die Lehre daraus ist die übliche:** was nicht im Test steht, ist nicht
geprüft — und hier hätte es nicht einmal einen Fehler gegeben, sondern ein
falsches Ergebnis. Der Testsatz war von den Ausdrücken her gedacht und
hatte die Funktionsgrenze schlicht nicht überschritten.

## `++`/`--` auf `double` (2026-09-16, nach dem Nachfassen)

Der Testsatz war von den Ausdrücken her gedacht — und hatte, neben der
Funktionsgrenze, auch die **Inkrement-Operatoren** nicht überschritten.
Dort saß derselbe Fehlertyp: `'d'` fiel in den char-Auffangzweig der
Opcode-Wahl, das Inkrement blieb wirkungslos, gemeldet wurde nichts.

Umgesetzt sind jetzt die beiden Formen, die ohne neue IR auskommen:

```
  Postfix a++            Präfix ++a
  LOADD  <slot>          LOADD  <slot>
  DDUP                   PUSHD  1072693248 0     ; 1.0 als IEEE-754
  PUSHD  1072693248 0    DADD
  DADD                   DDUP
  STORED <slot>          STORED <slot>
  -> alter Wert bleibt   -> neuer Wert bleibt
```

Der Unterschied zwischen Präfix und Postfix ist allein, **ob das `DDUP` vor
oder nach der Addition steht** — deshalb prüfen vier Testfälle gezielt den
Wert des *Ausdrucks* (`b = a++` gegen `b = ++a`) und nicht nur den der
Variablen. Global läuft dasselbe über `LOADGD`/`STOREGD`.

Neue Opcodes waren nicht nötig: `DADD`, `DSUB`, `DDUP`, `DDROP`,
`LOADD`/`STORED` und `LOADGD`/`STOREGD` gab es alle schon, in allen drei
Konsumenten (68k-Backend, ARM64-Backend, VM-Orakel).

**Was noch fehlt — die drei Adressformen** (`s.d++`, `a[0]++`, `(*p)++`):
sie arbeiten über `LOADIND`/`LOADIDX` und brauchen für Postfix ein `SWAP`
zwischen Adresse und Wert. Für einen 8-Byte-Block gibt es das in der IR
nicht, und `DUP`/`DROP` sind dort ausdrücklich mehrdeutig. Bis die IR eine
Blockrotation hat, wird gemeldet. Vorher liefen alle drei still durch und
rechneten mit `PUSH 1 / ADD` ganzzahlig auf einem FPU-Bitmuster — im
VM-Orakel unsichtbar, weil Python `float + int` richtig addiert.

## Funktionsgrenzen, zusammengesetzte Zuweisung — und drei Backend-Fehler (2026-09-16)

`double` ist ab hier in gewöhnlichem Code benutzbar: als **Parameter**, als
**Rückgabewert** und mit `+=`/`-=`/`*=`/`/=`.

### Der Weg über einen Puffer, nicht über die Aufrufkonvention

Ein Parameter liegt in einem **Slot**, und der ist im Rahmen fest vier Byte
breit (68k: `8+4*(nargs-1-slot)`). Acht Byte hätten das Rahmenlayout in allen
drei Backends aufgebrochen. Stattdessen geht der Wert denselben Weg, den
`struct`-Argumente längst gehen: der Aufrufer legt ihn in einen globalen
Puffer und übergibt dessen **Adresse**.

```
  Aufrufer                      Aufgerufener (beim Eintritt)
  PUSHD <hi> <lo>               LARRAY <n> d 1
  STOREGD __dblArg_<k>          LOADP <p> / LOADIND d / STORED <n>
  ADDRG  __dblArg_<k>           -> ab jetzt eine normale lokale Variable
```

Zwei Entscheidungen tragen das Ganze:

- **Ein Puffer je AUFRUFSTELLE**, nicht je Argumentposition. Sonst
  überschriebe `f(1.0, g(2.0))` beim Auswerten von `g` das bereits abgelegte
  erste Argument — dieselbe Überlegung wie bei `__structArg_*`.
- **Der Aufgerufene kopiert beim Eintritt** in einen echten lokalen Block.
  Das ist der Grund, warum **Rekursion trägt**: der globale Puffer ist ab dem
  ersten Befehl des Rumpfs wieder frei. Und es hält den Rumpf einfach — keine
  Lesestelle muss zwischen „Slot hält Wert" und „Slot hält Adresse"
  unterscheiden, eine vergessene wäre wieder ein stiller Rechenfehler.

Der Rückgabewert läuft genauso, mit **einem** Puffer für das ganze Programm:
der Aufrufer lädt ihn unmittelbar nach dem `CALL`, er ist also frei, bevor
ihn der nächste Aufruf braucht.

Nach außen bleibt der Parametertyp `double` — `tc_funcbegin` registriert die
Signatur, bevor der Rumpf beginnt, und nur der Slot trägt intern eine Adresse.

### Drei Fehler, die erst die echte Hardware gezeigt hat

Das VM-Orakel war grün, der 68030 nicht: „zwei double-Parameter" rechnete
falsch, `f(f(x))` stürzte mit einer PMMU-Ausnahme ab. Der Assembler zeigte
es sofort:

```
    move.l  (a0),d0        ; LOADIND d lud VIER Byte
    move.l  d0,-(a7)
    fmove.d (a7)+,fp0      ; las aber ACHT
```

- **`LOADIND d`/`STOREIND d` waren im 68k-Backend nie für acht Byte
  umgesetzt** — `'d'` fiel in den generischen Zweig. Jetzt über die FPU,
  wie `LOADD`/`LOADGD`.
- **Dasselbe im ARM64-Backend**: dort ist ein `double` ein 64-Bit-Bitmuster,
  also `x0` statt `w0` — eine Zeile.
- **Globale `double` wurden auf ARM64 mit VIER Byte angelegt.** Die
  Größenformel kannte `char`, `short` und Zeiger, aber kein `double`, und sie
  stand **zweimal** da. Ein `str x0` schrieb über das Global hinaus. Das
  betraf jedes globale `double`, nicht nur die neuen Puffer; die Formel liegt
  jetzt in `globalElemSize`/`globalAlignP2`.

Alle drei waren **still falsch** und im Orakel unsichtbar, weil Python den
Typ kennt. Für Gleitkomma gilt damit verschärft: eine Probe gegen `qccvm.py`
allein beweist nichts über die Ablage.

### Was weiterhin fehlt

- **Initialisierer an globalen `double`** (`double g = 4.5;`). Das
  Global-Datenmodell der Backends hält **einen `int` je Element**
  (`globals[].init`); ein 8-Byte-Initialwert braucht dort einen eigenen
  Opcode (`GINITD <name> <idx> <hi> <lo>`) und die passende Datenausgabe in
  allen drei Konsumenten — hi/lo darf nicht das Frontend anordnen, die
  Byte-Reihenfolge ist zielabhängig. Ein eigener Schritt.
- **Exponentliterale** (`1e2`, `1.5e3`) werden **stumm** abgelehnt (Exit 1,
  leerer stderr). Die Grammatik kennt nur `digit {digit} "." {digit}`;
  nötig sind die EBNF-Regel, ihre aktionslose Kopie `globalFloat` und der
  Exponent im Umrechner.
- **`++`/`--` auf `s.d`, `a[0]`, `(*p)`** — seit `LOADIND d` stimmt, fehlt
  nur noch die Blockrotation für den Postfix-Fall (`SWAP` über acht Byte).
- `printf("%f")` (qclib) und `float` als eigener Typ.

## Initialisierer an globalen `double` (2026-09-16)

`double PI = 3.14159;` geht jetzt. Der Wert steht als **Bitmuster** in den
Daten — eine Zahl kann ihn nicht tragen, und das Global-Datenmodell der
Backends hält einen `int` je Element.

**Neuer IR-Opcode `GINITD <name> <idx> <hi> <lo>`**, zwei 32-Bit-Hälften wie
bei `PUSHD`. **Welche zuerst im Speicher landet, entscheidet das Backend** —
der 68k ist big-endian und schreibt zwei `dc.l`, hi zuerst; ARM64 ist
little-endian und schreibt ein `.quad`. Das Frontend darf die Reihenfolge
deshalb nicht festlegen, und genau dafür gibt es den eigenen Opcode statt
zweier `GINIT`.

Umgerechnet wird mit **demselben Konverter wie beim Literal im Ausdruck**
(`qccDecToDouble` aus `tc_floatlit`, per Vorwärtsdeklaration). Sonst hätte
`double g = 0.1;` ein anderes Bitmuster als `a = 0.1;` — ein Testfall prüft
gezielt, dass beide gleich sind.

Mitgekommen:

- **`globalNeg` kennt jetzt Gleitkomma** (`globalNeg = "-" ( globalFloat |
  globalNumber )`). Vorher scheiterte `double g = -2.5;` am Parser, stumm.
- **`double g = 5;`** — eine ganze Zahl ist ein gültiger Initialisierer
  (C89 3.5.7) und wird gewandelt statt in den Ganzzahlpfad zu laufen.
- **`initFloat`** als aktionslose Regel, damit `double t[3] = {1.0, 2.0};`
  überhaupt *gelesen* und dann **gemeldet** wird. Vorher scheiterte es am
  Listenparser mit „bad or oversized array initializer" — einer Meldung, die
  in die Irre führt. Umgesetzt sind Listen noch nicht.
- **68k, `!hasGinit`-Zweig:** ein uninitialisiertes globales `double` bekam
  dort `tagSuffix()`-gesteuert nur **vier** Byte je Element. Sichtbar wird
  das nur ohne `-remotedata`, weil sonst der vsect greift — trotzdem ein
  echter Fehler, jetzt zwei `dc.l`.

**`MAX_GLOBALS` von 2048 auf 3072 angehoben.** Gemessen: der Selbsthost
brauchte 2053 und riss die alte Grenze um fünf. Der Zuwachs kommt fast
ausschließlich aus String-Literalen — **jede neue Diagnose im Frontend ist
ein eigenes `__strN`-Global**, und der Gleitkomma-Ausbau hat viele gebracht.
Bewusst nicht auf den gemessenen Bedarf gesetzt; Preis sind rund 110 KB im
Backend-Modul.

### Weiterhin offen

- **Initialisiererlisten für `double`-Arrays** (`double t[3] = {1.0,2.0}`) —
  `tcInitList` liest Ganzzahlen; jeder Wert müsste als Bitmuster durch.
- **Exponentliterale** (`1e2`) — werden **stumm** abgelehnt (Exit 1, leerer
  stderr). Nötig sind die EBNF-Regel samt `globalFloat`/`initFloat`-Kopien
  und der Exponent im Umrechner.
- **`++`/`--` auf `s.d`, `a[0]`, `(*p)`** — es fehlt nur noch die
  Blockrotation für den Postfix-Fall.
- `printf("%f")` (qclib) und `float` als eigener Typ.

## Exponentschreibweise (2026-09-16) — und warum sie so schwer zu finden war

`1e2`, `1.5e3`, `2.5E-2` gehen jetzt, im Ausdruck wie im globalen
Initialisierer. Der Aufwand lag nicht dort, wo man ihn vermutet.

**Der Umrechner konnte es längst.** `qccDecToDouble` (wortgleich
`tools/dec2ieee.c`) liest `e`/`E` samt Vorzeichen seit jeher. Auch die
Grammatik zu erweitern genügte nicht.

**Die Ursache saß im LEXER.** Die `[LEXER]`-Sektion in `qcc.lextab` listet,
welche Grammatikregeln als **Token** gebildet werden — `ident`, `number`,
`stringLit`, `charLit`. `floatLit` fehlte. Damit zerfiel `1e2` in ein
`number`-Token `1` und ein `ident`-Token `e2`, und in ein Ident-Token kann
der Parser nicht mehr hineinschauen. Die Lösung ist eine Zeile:
`TOKEN floatLit`, **vor** `TOKEN number`.

**Die irreführende Spur:** `1e-2` und `5e+1` funktionierten die ganze Zeit —
das Vorzeichen trennt die Tokens, `e` bleibt für sich. Das sah nach einem
Problem mit der optionalen Vorzeichenregel aus und hat mehrere Umbauten der
Grammatik gekostet, die alle nichts änderten. **Wer hier etwas ändert, prüfe
zuerst, ob der Lexer das Literal überhaupt als ein Token bildet.**

**Zweiter Fehler, im eigenen Code:** `tcGlobalOne` grenzt den Zahltext selbst
ab — und schnitt den Exponenten ab. `double g = 1e2;` ergab **1 statt 100**,
still. Die Abgrenzung liegt jetzt in **einer** Funktion (`tcFloatLitEnd`),
die Ziffern, Punkt und Exponent gemeinsam liest; der Exponent zählt nur mit,
wenn ihm Ziffern folgen, damit `1e` ein Fehler bleibt. Hexzahlen sind
ausgenommen: `0x1E` endet auf ein `E`, das kein Exponent ist (eigener Test).

### Nebenbefund: `MAX_RULES` in parsec meldet sich nicht

`parsec` hat ein festes `MAX_RULES` (256) und überschreitet es **still**:
`if (ruleSymbolCnt < MAX_RULES)` überspringt die überzähligen Regeln ohne
ein Wort. Sichtbar wird das erst als „referenziert undefinierte Regel
'digit'" — eine Meldung, die auf eine ganz andere Fährte führt. Dasselbe
Muster bei `MAX_RULE_NAMES` (256) und `MAX_EDGES` (1024).

**Die Grammatik liegt mit 250 Regeln dicht darunter.** Deshalb teilen sich
`floatLit`, `globalFloat` und `initFloat` hier die aktionslosen Regeln
`floatTail`/`floatDotTail`/`floatExp` statt je eigene Kopien zu haben — drei
Kopien hätten die Grenze gerissen. Die Konstanten stehen außerdem **doppelt**:
in `src/parsec.cpp` und als harte Zahlen im QCC-Port `src-qcc/ebnf.tc`.
Eine Anhebung müsste beide anfassen; eine Meldung beim Überlauf wäre der
lohnendere erste Schritt — beides ist hier bewusst nicht gemacht.

## `&a` auf ein lokales `double` (2026-09-16) — ein Geschwister, das übersehen wurde

`double *p = &a;` lieferte einen Zeiger, der **ins Leere zeigte**. `*p` las
Müll, `*p = x` schrieb ins Leere, `f(&a)` übergab die falsche Adresse — alles
**ohne Meldung**.

`tc_addressref` emittiert für eine lokale Variable `ADDRL <slot>`, und
`ADDRL` geht im 68k-Backend über `slotAddress()` (die Slot-Adresse
`-4*(slot-nargs+1)`). Ein `double` liegt aber als **Block** (`LARRAY <i> d 1`)
und wird über `arrayOffset()` adressiert; richtig ist `PUSHADDR L <slot>`.

**Das Bemerkenswerte: genau dieser Fehler war schon einmal da.** Im Code
stand seit der struct-Arbeit:

```c
/* Eine skalare Struct liegt ebenfalls als Block vor (LARRAY), hat aber
   tcLocalArrayLen 0 -- ohne die zweite Bedingung liefert &s die Adresse
   eines leeren Skalarslots statt die des Objekts. */
if (tcLocalArrayLen[slot] || (valueType.base == 's' && !valueType.pointers))
```

Die Bedingung wurde damals an der Fundstelle geflickt. Als `double` als
zweiter Blocktyp dazukam, hat niemand daran gedacht. Die Regel steht deshalb
jetzt als **Funktion** da:

```c
static int tcLocalIsBlock(int slot, TCType t) {
	if (tcLocalArrayLen[slot]) return 1;
	if (t.pointers) return 0;
	return t.base == 's' || t.base == 'd';
}
```

**Der globale Fall war korrekt** und bleibt es: bei einem Global ist `ADDRG`
die Objektadresse, einen Slot/Block-Unterschied gibt es dort nicht.

Geprüft mit acht neuen Testfällen, darunter das Kernidiom, für das man einen
`double*` überhaupt braucht — der Aufgerufene schreibt durch den Zeiger
zurück — und auf echtem 68030 (`double68k.sh`, jetzt 60 Fälle), wo eine
falsche Adresse fremde Daten trifft statt einer Python-Liste.

## Bedingungen, structs und ein zurückgenommenes Feature (2026-09-16)

### `if (a)`, `!a`, `while (a)` mit `double`

C89 3.6.4.1/3.6.5: eine Bedingung darf **jeden** skalaren Typ haben und
bedeutet „ungleich 0". Für Ganzzahlen, Zeichen und Zeiger galt das in QCC
längst (`tcIsTruthy`); `double` war schlicht nie dazugekommen und wurde als
„expects bool" abgelehnt.

Der Vergleich wird **emittiert**, nicht bloß im Typstapel vermerkt: auf dem
Stapel liegen acht Byte, und `JZ`/`NOT` erwarten einen ganzzahligen Wert.
0.0 ist als IEEE-754-Bitmuster schlicht null, daher `PUSHD 0 0` + `DCMPNE`.
Eine Funktion (`tcCondValue`) für alle vier Stellen — eine vergessene ließe
acht Byte auf dem Stapel liegen.

### `struct` mit `double`-Feld: eine Modellgrenze, kein Compilerfehler

Der Absturz des VM-Orakels (`TypeError: unsupported operand for &`) sah nach
einem Fehler aus, war aber keiner. Die struct-Kopie ist **byteweise**
(`LOADIND c`/`STOREIND c`), und die VM bildet einen Block als Liste
**typisierter Zellen** ab — eine `double`-Zelle trifft sie damit als Ganzes
und scheitert an `float & 0xff`. **Auf dem 68k ist dieselbe Kopie echt
byteweise und richtig**, das Bitmuster wandert unverändert.

Die VM reicht `float`-Zellen bei byteweisem Zugriff jetzt durch. Der
**gemischte** Fall bleibt außerhalb ihrer Reichweite: in
`struct { int n; double d; }` liegt `d` bei **Offset 4**, und
`index = offset/größe` kann das nicht von einem `int` unterscheiden. Das
Layout ist dabei **korrekt** — xcc richtet `double` auf zwei Byte aus, und
beide Anordnungen ergeben in QCC wie in xcc 12 Byte. Die VM sagt das jetzt
auch, statt „unaligned pointer" zu melden.

**Bewiesen wurde es dort, wo es sich zeigen kann:** `double68k.sh` prüft
`struct{double}` beim Kopieren, als Parameter und als Rückgabewert, dazu
`{int n; double d;}` und `{double d; int n;}` — auf echtem 68030, 67 Fälle.

### Unäres Plus: versucht, gemessen, zurückgenommen

`i = +5;` scheitert weiterhin, für jeden Typ. Der Versuch, `"+"` in
`negFactor` aufzunehmen, **bricht bestehenden, korrekten Code**: mit einem
unären Plus wird `(x)+1` auch als **Cast** lesbar — `(x)` angewandt auf `+1`.
`castExpr` steht in `factor` vor `parenOpen`, greift also zuerst, und seine
Aktion meldet „unknown type name 'x'", bevor der Generator zur
Klammer-Alternative zurückkehren kann. Die Meldung ist dann schon abgesetzt.

Beim Minus fällt das nicht auf, weil `-1` schon immer ein `factor` war.
Der Nutzen wäre gering (`+x` ist im Wert ein No-op), der Preis wäre, dass
`putint((x)+1)` nicht mehr übersetzt. Auflösen ließe es sich nur, indem
`castExpr` erst meldet, wenn die Alternative endgültig gewählt ist — ein
Eingriff in den Generator, nicht in diese Grammatik. Ein Testfall sichert
`(x)+1` jetzt ab.

## Zwei ARM64-Fehler, gefunden beim Vorbereiten der Adressformen (2026-09-16)

Auf dem Weg zu `++`/`--` über Adressen fiel auf, dass die **bestehende**
Choreographie auf ARM64 gar nicht läuft — für **jeden** Typ, nicht nur
`double`. Beide Fehler waren im VM-Orakel unsichtbar und nativ ein Segfault:

**1. `SWAP` griff ins falsche Element.** Der Code tauschte `[sp]` mit
`[sp,#8]`, aber ein Stackelement ist auf diesem Ziel **sechzehn** Byte breit
(`push`/`pop`: `str x,[sp,#-16]!`). Der Tausch griff damit mitten in das
oberste Element hinein statt auf das zweite. `a[0]++` endete im Segfault.

**2. Die Adresse wurde mit `DUP` statt `DUPP` vervielfältigt.** Auf dem 68k
sind beide derselbe `move.l (a7),-(a7)`, auf ARM64 **nicht**: `DUP` lädt 32
Bit (`w0`), `DUPP` 64 (`x0`). In `tcMemberIncDec` ging damit die obere Hälfte
der Adresse verloren — `s.n++` segfaultete. Im Index-Zweig bleibt `DUP`
richtig: dort wird ein **Index** vervielfältigt, kein Zeiger.

Beides sind gewöhnliche C-Konstrukte. Dass sie durchrutschen konnten, lag
daran, dass die ARM64-Tests sie nicht abdeckten — das Orakel war grün, und
nativ lief nie jemand dagegen. **Ein eigener nativer Testfall prüft jetzt
`s.n++`, `++s.m`, `a[0]++`, `a[0]--` und `(*p)++` gegen das Orakel.**

Die Lehre ist dieselbe wie bei `LOADIND d`: ein Backend-Unterschied, den der
68k nicht kennt (dort sind `DUP`/`DUPP` identisch und Stackelemente vier
Byte), wird nur sichtbar, wenn man auf dem anderen Ziel wirklich ausführt.

## Arrays von `double` und `++`/`--` über Adressen (2026-09-16)

### Erst ein Fund: `double a[3]` war auf keinem Ziel übersetzbar

Beim Vorbereiten der Adressformen stellte sich heraus, dass
`LOADIDX`/`STOREIDX` nur `i`/`p`/`h`/`c`/`b` kannten und für `'d'` mit
**„unbekannter Arraytyp"** abbrachen — auf **beiden** Zielen. Nur das
VM-Orakel konnte den Fall, deshalb war er nie aufgefallen, obwohl
`double t[10]` Alltagscode ist.

Die Skalierung stimmte schon (`tagShift` kennt 8 → 3); es fehlten die
Typzulassung und die 8-Byte-Befehle. Auf dem 68k sind das `fmove.d`, auf
ARM64 ist `double` so breit wie ein Zeiger (`x0` statt `w0`, Skalierung
`#3`).

### Dann der neue Opcode `DSWAP`

Beim **Postfix** muss der alte Wert als Ergebnis **unter** der Adresse liegen
bleiben. `SWAP` taugt dafür nicht: es tauscht zwei Langworte und zerrisse die
acht Byte. `DSWAP` tauscht das oberste `double` mit dem 4-Byte-Wert darunter
— einer Adresse (Struct-Feld, Zeigerziel) oder einem Index (Array-Element):

| Ziel | Umsetzung |
|---|---|
| 68k | `fmove.d (a7)+,fp0` / `move.l (a7)+,d1` / beide zurück |
| ARM64 | wie `SWAP` — dort belegt ein `double` genau ein Stackelement |
| VM | wie `SWAP` — dort ist es ein Eintrag |

Der **Präfix**-Fall braucht ihn nicht; dort genügt die vorhandene
Choreographie. `(*p)++` kommt ebenfalls ohne aus, weil `tcDerefIncDec` den
Zeiger mehrfach lädt, statt ihn umzuschieben.

Damit gehen `s.d++`, `++s.d`, `a[0]++`, `a[0]--`, `(*p)++` und `++(*p)` — mit
dem richtigen Wert des *Ausdrucks*, nicht nur der Variablen. Geprüft im
Orakel, nativ auf ARM64 und auf echtem 68030 (`double68k.sh`, jetzt 78
Fälle).

## Initialisiererlisten für `double`-Arrays (2026-09-16)

`double t[3] = {1.5, -2.5, 0.25};` geht jetzt — je Element ein `GINITD`.

`tcInitList` bleibt dafür **unberührt**: er liest Ganzzahlen und scheitert an
`1.0`, und die Werte müssen hier ohnehin als **Bitmuster** in die Daten, nicht
als Zahl. Ein eigener, kurzer Listenparser in `tcGlobalOne` liest die Liste
über `tcFloatLitEnd` + `qccDecToDouble` — dieselben Bausteine wie beim
skalaren Initialisierer, also garantiert dasselbe Bitmuster.

Abgedeckt: Vorzeichen, Brüche, Exponenten, `static`, offen gelassene Größe
(`double t[] = {…}` leitet sie aus der Liste ab) und weniger Werte als
Elemente (Rest null, C89 3.5.7). **Gemeldet** statt still verschluckt werden
Verschachtelung (`{{1.0},{2.0}}` — mehrdimensionale `double`-Arrays sind
nicht vorgesehen) und zu viele Werte.

Geprüft im Orakel, auf echtem 68030 (`double68k.sh`, jetzt 80 Fälle) und im
Selbsthost.

## Stand `double` — was compilerseitig bleibt

Nichts mehr. Offen sind nur noch zwei Dinge außerhalb des Compilers:

- **`printf("%f")`** — Bibliotheksarbeit in qclib (Zahl↔Text). Rechnen kann
  ein Programm längst ohne.
- **`float`** als eigener Typ — bewusst zurückgestellt: C zieht in den
  üblichen Konversionen auf `double` hoch, und `float` brächte eigene
  Rundungsfragen (32 vs. 64 vs. die 80 Bit der 68k-FPU).
