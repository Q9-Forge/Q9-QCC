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
