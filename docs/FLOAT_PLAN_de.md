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
