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

### Die nächste Hürde hat einen Namen: Dezimal → IEEE-754

Bevor ein einziges Literal gerechnet werden kann, muss der Compiler aus der
Ziffernfolge `3.14` das Bitmuster `0x40091EB851EB851F` erzeugen — **mit
Ganzzahlarithmetik**, denn QCC soll sich selbst übersetzen und hat dabei kein
Gleitkomma zur Verfügung. Das ist kein Nebenschauplatz, sondern die
Henne-Ei-Frage dieses Vorhabens und der erste echte Brocken von Schritt 2.
