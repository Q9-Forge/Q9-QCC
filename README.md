# Q9-qr68 — der 68k-Assembler der Q9-Werkzeugkette

Ausführbares Programm: **`qr68`**

```
qr68 [Optionen] <eingabe.a> <ausgabe.r>
```

Ziel: Microwares `r68` ersetzen. Präprozessor (`qcpp`) und Compiler-Frontend
(QCC) laufen bereits auf echtem 68030 — Assembler und Binder sind die letzten
Fremdteile der Kette.

## Stand (2026-09-03)

**Die vom QCC-Backend erzeugten Quellen assembliert `qr68` byteidentisch zu
`r68`** — ganze Module, nicht Einzelfälle:

| Prüfung | Ergebnis |
|---|---|
| 10 Proben (Kopf, Globale, Externe, vsect, Padding) | **byteidentisch** |
| `test/insn.a`, `test/dir.a`, `test/mac.a`, `test/bopt.a` — jede kodierbare Form | **byteidentisch**, Zeile für Zeile |
| 10 Module aus QCCs Backend, bis 146.848 Zeilen / 1,07 MB ROF | **byteidentisch** |
| Die Assemblerquellen des **Q9-OS-Kernels** (handgeschrieben, 4.000 Zeilen) | **byteidentisch** |
| 11 **SCF-Treiber des MWOS-SDK** (Includes, Makros, bedingte Assemblierung, `-b`) | **byteidentisch** |

Der Zeitstempel ist dabei nicht ausgenommen, sondern nachgebildet (`-fdate=`).

Abgedeckt: `psect`/`vsect`/`ends`/`endsect`, `equ`/`set`, `dc.b/.w/.l` (auch
Zeichenketten), `ds.b/.w/.l`, `align`, `end`, die beschreibenden Direktiven
(`nam`/`ttl`/`page`/`opt`/`spc`), Ausdrücke mit `$`/`%`/`@`/Zeichen,
`+ - * / & ! << >>` und unär `- + ^`, Klammern, `*` als aktueller Ort und
`.` als org-Zähler, Symbole mit Vorwärtsreferenzen, globale Labels
(`name:`), externe und lokale Referenzen.

**Achtung bei Ausdrücken, an r68 gemessen:** `^` ist das **unäre Nicht**
(`^$0f` = `$f0`), *kein* XOR — `$ff^$0f` lehnt r68 ab; und `~` kennt r68
gar nicht.

**Befehle:** `move`/`movea`/`moveq`, `lea`/`pea`,
`add`/`sub`/`and`/`or`/`eor`/`cmp` mit allen Formen, die `r68` daraus macht
(Grundform, `…a`, `…i`, `…q`), `adda`/`suba`/`cmpa`, `addi`…`cmpi`,
`addq`/`subq`, `muls`/`mulu`/`divs`/`divu` (Wortform), `clr`/`neg`/`negx`/
`not`/`tst`/`tas`/`swap`/`ext`/`extb`, alle acht Schiebe- und Rotierbefehle
(Sofortwert, Register, Speicherform), `Scc` (14 Bedingungen), `bra`/`bsr`/
`Bcc` (kurz und Wort), `dbra`/`dbcc`, `jsr`/`jmp`, `link`/`unlk`,
`rts`/`rte`/`rtr`/`nop`/`trap`/`trapv`/`reset`/`stop`/`illegal`, dazu
`movem` mit Registerlisten (`d0-d7/a0-a6`, bei `-(An)` mit umgekehrter
Maske), die Bitbefehle `btst`/`bset`/`bclr`/`bchg` (statisch und dynamisch),
`ori`/`andi`/`eori` nach `ccr`/`sr`, `move` von und nach `sr`/`ccr`/`usp`,
`exg`, und vom 68010 `movec` und `moves`.

**Adressierungsarten:** alle zwölf des 68000 —  `Dn`, `An`, `(An)`, `(An)+`,
`-(An)`, `d16(An)`, `d8(An,Xn)`, `abs.w`, `abs.l`, `d16(PC)`, `d8(PC,Xn)`,
`#imm`.

Dazu die Direktiven `use` (Include, mit `-u=`-Suchliste), `org`/`do` (der
Strukturbeschreiber des SDK) und `.` als org-Zähler, die bedingte
Assemblierung (`ifeq`/`ifne`/`ifgt`/`ifge`/`iflt`/`ifle`/`ifdef`/`ifndef`/
`else`/`endc`), **Makros** (`\1`…`\9`, `\#`, `\@`) mit `rept`/`endr`, und
der Systemaufruf `os9`.

Von r68s Schaltern: `-b` (Sprungweiten selbst wählen), `-a<sym>[=<wert>]`,
`-u=<verz>`, `-q` (angenommen und ignoriert).

**Alles andere bricht mit Meldung ab.** Das ist Absicht: eine still falsche
Kodierung wäre schlimmer als eine fehlende.

## Der Prüfstein

`r68` ist byteweise reproduzierbar — bis auf **sechs Zeitstempelbytes** im
ROF-Kopf. Also:

> qr68 ist richtig, wenn seine Ausgabe zu der von `r68` byteidentisch ist,
> diese sechs Bytes ausgenommen.

```
make test                          # Proben, Befehlstabelle, use, -b
make backend                       # QCC-Backend-Quellen + Q9-OS-Kernel
make check                         # beides
./test/mwos.sh                     # die SCF-Treiber des SDK

./test/difftest.sh                 # die eingebauten Proben
./test/difftest.sh datei.a         # eine echte Quelle, ganze ROF-Datei
./test/insndiff.sh datei.a         # Quellzeile für Quellzeile
RFLAGS=-b ./test/insndiff.sh d.a   # dieselben Schalter auf beiden Seiten
```

`difftest.sh` vergleicht die ganze Datei und bleibt bei der ersten Abweichung
stehen. `insndiff.sh` nutzt zusätzlich `r68 -l`: dessen Listing nennt zu jeder
Quellzeile den Offset im Code, damit steht in der Meldung die **Zeile**, die
falsch kodiert wurde. Eine Befehlstabelle lässt sich damit in einem Durchgang
abarbeiten statt Fehler für Fehler.

Das ist ein echtes Orakel: kein selbstgeschriebener Sollwert, der erst selbst
richtig sein müsste. Der Zielkorpus sind **644 handgeschriebene `.a`-Dateien
mit 207.847 Zeilen** (MWOS-SDK + Q9-OS-Kernel).

## Das ROF-Format, am Original gemessen

Es gibt genau **eine** Beschreibung des Formats: `MWOS/APPS/src/osk-disasm`
(`rof.c`/`rof.h`), ein Disassembler. Sie stimmt in einem wesentlichen Punkt
**nicht** mit dem überein, was `r68` schreibt — und in einem weiteren ist sie
gar nicht ausgeführt. Deshalb hier das gemessene Layout.

### Kopf (56 Byte, big-endian)

| Offset | Größe | Feld | Herkunft |
|---|---|---|---|
| 0 | 4 | `sync` = `$DEADFACE` | |
| 4 | 2 | `ty_lan` | psect-Parameter 2 |
| 6 | 2 | `att_rev` | psect-Parameter 3 |
| 8 | 2 | `valid` = 0 | |
| 10 | 2 | `series` = 249 | Assemblerkennung von r68 V2.9.1 |
| 12 | 6 | `rdate` | Jahr−1900, Monat, Tag, Stunde, Minute, Sekunde |
| 18 | 2 | `edition` | psect-Parameter 4 |
| 20 | 4 | `statstorage` | reservierte Daten (`ds` im vsect) |
| 24 | 4 | `idatsz` | initialisierte Daten (`dc` im vsect) |
| 28 | 4 | `codsz` | Codegröße |
| 32 | 4 | `stksz` | psect-Parameter 5 |
| 36 | 4 | `code_begin` | psect-Parameter 6 |
| 40 | 4 | `utrap` | psect-Parameter 7, **sonst `-1`** |
| 44 | 4 | `remotestatsiz` | |
| 48 | 4 | `remoteidatsiz` | |
| 52 | 4 | `debugsiz` | |

### Danach

```
Psect-Name (NUL-terminiert)
Anzahl Globale (4)                 ← 32 Bit, NICHT 16
  je Global: Name (NUL), Typ (2), Adresse (4)
Code (codsz Byte)
Initialisierte Daten (idatsz Byte)
Anzahl externer Namen (4)
  je Name: Name (NUL), Anzahl Referenzen (4)
    je Referenz: Typ (2), Offset (4)
Anzahl lokaler Referenzen (4)
  je Referenz: Typ (2), Offset (4)
vier Langwörter (in allen gemessenen Fällen 0)
```

### Vier Befunde, die nur durch Messen kamen

1. **Die Zähler sind 32 Bit, nicht 16.** `rof.c` liest sie mit `fread_w` als
   Wort. Nachgemessen mit Proben von 0, 1, 2 und 3 Symbolen: `r68` schreibt
   Langwörter. Wer der Dokumentation folgt, verrutscht ab dem ersten Symbol.
2. **`r68` sortiert die Globalen alphabetisch**, nicht in Quellreihenfolge.
   Belegt an einer Quelle mit der Reihenfolge `wert`, `puffer`, `start` —
   ausgegeben wurde `puffer`, `start`, `wert`. Ohne dieselbe Reihenfolge gibt
   es keine Byteidentität.
3. **Der Code wird mit `NOP` (`$4E71`) auf ein Vielfaches von vier
   aufgefüllt.** Ein einzelnes `rts` ergibt `codsz` = 4.
4. **Im `vsect` haben initialisierte und reservierte Daten je einen eigenen
   Adressraum, beide ab 0.** Bei `d1 dc.l / d2 dc.l / u1 ds.b 4 / u2 ds.b 4`
   kommen die Adressen 0, 4 und 0, 4 heraus, `idatsz` = 8 **und**
   `statstorage` = 8.

### Das Typwort einer Referenz

Aus siebzehn gemessenen Fällen setzt es sich aus vier Teilen zusammen:

| Bit(s) | Bedeutung |
|---|---|
| `$80` | PC-relativ (`bsr fremd`, `lea fremd(pc),a0`) |
| `$40` | **abziehen** — der Anteil geht negativ ein (`dc.l fremd-basis`) |
| `$20` | die Referenz *liegt* im Code (ohne das Bit: in den init. Daten) |
| `$18`/`$10`/`$08` | Umfang: Langwort / Wort / Byte |
| untere Bits | **Ziel**abschnitt, wie bei den Globalen: Code 4, init. Daten 1, reservierte Daten 0, extern 0 |

Also `$3c` = Langwort im Code auf ein Codelabel, `$39` = auf initialisierte
Daten, `$38` = auf reservierte Daten oder einen externen Namen, `$19`/`$1c` =
dasselbe innerhalb des vsect, `$b0` = PC-relativ auf einen externen Namen,
`$7c` = ein abgezogenes Codelabel.

Globale: Code `$0004`, initialisierte Daten `$0001`, reservierte Daten
`$0000`. Weitere Fälle werden beim Ausbau **einzeln gemessen**, nicht
abgeleitet — bei jeder ungemessenen Kombination bricht `qr68` ab.

### Mehrere verschiebbare Anteile in einem Ausdruck

`r68` löst sie nicht auf, sondern legt **je Anteil eine Referenz auf denselben
Offset** ab. `move.b PD_PAR-PD_OPT+M$DTyp(a1),d0` (so steht es in den
SCF-Treibern, alle drei Namen extern) ergibt `$0030`, `$0070` und `$0030` auf
dem Displacementwort; `dc.l EA+EB` ergibt zwei Referenzen, `dc.l basis+basis`
ebenfalls. Nur die **Differenz zweier moduleigener Größen** rechnet r68 aus
und gibt gar keine Referenz aus — auch über Abschnittsgrenzen hinweg
(`dc.l dat-basis` mit `dat` im vsect: Wert 0, keine Referenz). Genau davon
leben die Indirektionstabellen von QCCs `-largedata`.

Multipliziert, geteilt, geschoben oder verundet werden darf nur ein Anteil
ohne Bezug — aber es zählt der **Teilausdruck**, nicht der ganze:
`(*-BaudTabl)/2` ist erlaubt (die Differenz ist eine Konstante), und
`\1+\1+\1+\1+256*4` aus `MACROS/os9svc.m` ebenfalls.

### Makroargumente werden an JEDEM Komma getrennt

Klammern zählen dabei **nicht** mit: `REGMOVE2 d0,(a0,d2.w)` sind für r68
drei Argumente (`d0`, `(a0`, `d2.w)`), und genau darauf baut
`MACROS/longio.m` — das Makro prüft `\#-3` und setzt sie mit
`move.b \1,\2,\3` wieder zusammen. Anführungszeichen zählen dagegen sehr
wohl: `#',',b` sind zwei Argumente.

### Was sonst noch nur durchs Messen kam

- **`equ` erbt einen externen Namen.** `IRQCtrl equ u_icr` (so in
  `sc68070.a`) bindet einen Namen an einen externen; jede Benutzung von
  `IRQCtrl` muss danach wieder eine Referenz auf `u_icr` erzeugen. Ohne das
  fehlen im ROF stillschweigend Referenzen — der Code ist byteidentisch, der
  Binder setzt die Adresse aber nie ein. Stehen mehrere unbekannte Namen
  rechts (`ILVLR4_default equ ILVLR4a+ILVLR4b+…`), legt auch r68 keine
  Referenz an.
- **`addi`/`subi` verkürzt r68 ebenfalls** zu `addq`/`subq` (1…8) — nicht
  nur `add`/`sub`.
- **`0x100`** ist neben `$100` ein gültiges Hexliteral (nur klein
  geschrieben; `0X10` lehnt r68 ab).
- **`pcr`** ist die zweite Schreibweise für `pc` und verhält sich genauso.
- **Ein globales `equ`** auf einen festen Wert bekommt das Typwort `$0006`,
  und in der Adresse steht der Wert selbst.

### Sofortwerte: drei gemessene Sonderfälle

- `move.b #fremd,d0` bekommt eine **Byte**referenz auf das *niederwertige*
  Byte des Erweiterungswortes (`$0028` auf Offset 3), die I-Formen dagegen
  eine Wortreferenz auf das ganze Wort: `cmpi.b #-1,d0` legt `$ffff` ab,
  `move.b #-1,d0` nur `$00ff`.
- `moveq #fremd,d1` bekommt eine Byte-Referenz auf das niederwertige Byte des
  **Befehlswortes** (`$0028` auf Offset 1). `addq`/Schiebeweiten lehnt r68
  mit einem externen Namen ab.
- `moveq` nimmt mehr als `-128..127`: `#$ff` wird `$70ff`, `#-129` wird
  `$707f`, erst ab 256 meldet r68 „value out of range".

### Reihenfolgen, ohne die es keine Byteidentität gibt

- **Globale**: alphabetisch nach Bytewerten.
- **Externe Namen**: ebenfalls alphabetisch (gemessen an einer Quelle, die
  erst `realloc` und dann `_os_write` braucht — ausgegeben wird `_os_write`
  zuerst). Die Referenzen unter einem Namen dagegen aufsteigend nach Offset.
- **Lokale Referenzen**: erst die im Code, dann die in den initialisierten
  Daten, je Gruppe mit **absteigendem** Offset.

### Auffüllen und Ausrichten

- Vor allem, was mindestens ein Wort breit ist, richtet `r68` selbst aus — und
  zwar **bevor** das Label der Zeile seinen Wert bekommt (`dc.b 1` /
  `lab: nop` ⇒ `lab` = 2).
- Ein einzelnes ungerades Byte wird mit `$00` gefüllt, danach mit `NOP`
  (`$4E71`) — im Code. Im `vsect` wird durchgehend mit `$00` gefüllt.
- Am Schluss wird der Code auf ein Vielfaches von vier gebracht, nach
  derselben Regel: erst das Nullbyte, dann NOPs.

Die vier Langwörter am Schluss sind offen: genau diesen Abschnitt hat
`osk-disasm` auskommentiert („common block variables… Do this after everything
else is done"). In allen Proben sind sie null — das ist das Messergebnis, kein
Platzhalter. Tritt ein Fall mit Inhalt auf, wird er gemessen.

## Was der Korpus verlangt

Aus 644 Dateien / 207.847 Zeilen gezählt:

**29 Direktiven:** `dc` `equ` `set` `ds` `align` `psect` `vsect` `ends` `org`
`use` `os9` `macro` `endm` `rept` `endr` `ifeq` `ifne` `ifgt` `ifge` `iflt`
`ifdef` `ifndef` `else` `endc` `fail` `opt` `nam` `ttl` `page`.
`use` (965×) ist Include, `os9` (742×) der Systemaufruf-Makro, `macro`/`endm`
(157×) heißt: **Makroverarbeitung ist Pflicht, nicht Beigabe.**

**Befehle:** 68000-Grundbestand plus 68010 (`movec` 220×, `moves` 9×),
68040-Cache/`move16` (61×), 68020-Bitfelder (`bfextu`/`bfins`/`bfffo`, 23×),
`divul` (8×) und PMMU (`pmove`, 4×). **Die FPU kommt in 207.000 Zeilen
nirgends vor** — sie bleibt draußen, mit klarem Abbruch statt stillem
Ignorieren.

## Was `r68` selbst umformt — und was nicht

Ohne diese Liste gibt es keine Byteidentität. Alles gemessen, nichts
hergeleitet:

**Umgeformt wird:**

- `add.l #4,d0` → **ADDQ**, `sub.l #4,a0` → SUBQ. Werte 1…8; außerhalb
  bleibt es ADDI/SUBI. Auch mit einem Symbol, dessen Wert erst später
  definiert wird — deshalb misst `qr68` in mehreren Durchläufen (s. u.).
- Ein Adressregister als Ziel wählt die A-Form: `add.l #9,a0` → ADDA,
  `cmp.l a1,a0` → CMPA, `move.l a5,a0` → MOVEA.
- `and.l #4,d0` → ANDI, ebenso `or`/`eor`/`cmp` — dort gibt es keine
  Kurzform.

**Nicht umgeformt wird:**

- `move.l #7,d0` bleibt MOVE (**kein** MOVEQ).
- `bra` ist **immer** die Wortform, auch wenn das Ziel in 8 Bit passt —
  `r68` warnt dann nur („destination in short branch range").
- `lea 0(a5),a0` bleibt die Displacementform; nur `lea (a5),a0` ist die
  indirekte.
- Ein nackter Ausdruck als Adresse ist **immer** absolut lang, auch
  `move.l $1000,d0`; `.w` am Operanden erzwingt die kurze Form.

**Was `r68` nicht kennt:** die Klammerform `(4,a5)` („parenthesis needed") —
nur `4(a5)`.

### Was `use`, `org`/`do` und die Makros wirklich tun

Alles an r68 gemessen, weil es sich anders liest, als man vermutet:

- **`use datei`** und **`use "datei"`** öffnen genau diesen Pfad, also
  relativ zum **Arbeitsverzeichnis** — *nicht* zum Verzeichnis der
  einschließenden Datei. **`use <datei>`** sucht in den `-u=`-Verzeichnissen
  (r68 nimmt dort zusätzlich ein festes `<MWOS>/OS9/SRC/DEFS`; qr68 liest
  keine Umgebungsvariablen, das Verzeichnis muss man ihm mit `-u=` nennen).
  Ein fehlendes `>` stört nicht: im SDK steht `use <memc040.d)` — Tippfehler
  in `systype.d` — und r68 übersetzt das anstandslos.
- **`org`** bewegt den Ort im Abschnitt **nicht**. Es setzt einen eigenen
  Zähler, auf den **`do.b/.w/.l`** Namen legt und den **`.`** liest — so
  beschreiben die SDK-Definitionsdateien ihre Strukturen (1593 `do` in 127
  Dateien). `do.w` und `do.l` richten dabei auf **gerade** aus, nicht auf
  ihre eigene Breite: `org 4 / A do.b 1 / B do.w 1` ergibt A=4, B=6.
- **Makros**: `\1`…`\9` sind die Argumente und werden **textuell** ersetzt,
  auch innerhalb von Anführungszeichen (`dc.b "\5",0` steht so im SDK);
  `\#` ist die Zahl der Argumente **zweistellig**, `\@` eine laufende
  Nummer **fünfstellig** (`lok00001`, beginnt bei 1). Ein fehlendes Argument
  wird zu nichts — es darf nicht abbrechen, denn die SDK-Makros prüfen `\#`
  und benutzen höhere Argumente nur in einem Zweig, den die bedingte
  Assemblierung dann überspringt.
- **Symbol- und Makronamen sind schreibungsabhängig** (`mactest` findet
  `MacTest` nicht), Befehlsnamen nicht.
- Hinter einem Befehl **ohne Operanden** ist das dritte Feld schon der
  Kommentar: `rte   * Kommentar` ist im Korpus üblich, und ein `*` dort ist
  kein Operand.

### Sprungweiten: `-b`

Ohne `-b` kodiert r68 **immer** die Wortform und warnt höchstens. Mit `-b`
wählt es selbst — und **übergeht den angegebenen Buchstaben ganz**: `bra.w`
auf ein nahes Ziel wird kurz, `beq.s` auf ein fernes wird zur Wortform. Ein
Ziel außerhalb des Moduls bleibt Wortform. Die SDK-Makefiles bauen alle
Treiber mit `-qb`, deshalb kann `qr68` das auch.

Bei **Abstand 0** — das Ziel ist die nächste Anweisung — lässt r68 den
Befehl ganz weg. Bei `bra`/`Bcc` ist das gleichbedeutend, bei `bsr` nicht
(die Rücksprungadresse fehlt dann). `qr68` bricht dort ab, statt eine
Bedeutungsänderung nachzubauen.

### Ein Defekt in r68 V2.9.1

Zwei Stellen, an denen `qr68` bewusst nicht folgt.

Die **lange Sprungform** (`bra.l`, `bsr.l`, `bcc.l`, 68020) ist kaputt: `r68`
gibt `6000 00000000` aus — ohne das nötige `$FF` im unteren Byte des
Befehlsworts und ohne den Abstand einzusetzen. Der erzeugte Sprung geht ins
Leere. Im ganzen handgeschriebenen Korpus kommt die Form zweimal vor
(`MWOS/OS9/SRC/IO/SCF/DRVR/sc68562.a:252`), beide Male trifft sie diesen
Defekt. `qr68` **bricht dafür ab** statt entweder den Defekt nachzubauen oder
still davon abzuweichen.

Und **`rept` spult falsch zurück**: für jede Wiederholung liest r68 die
Quellzeilen erneut, landet dabei aber mitten in einer vorangehenden Zeile.
Schon `delay35 / rept (35-5-9)/2 / nop / endr` — so steht es in
`MWOS/OS9/SRC/IO/SCF/DRVR/sc8x30.a` — ergibt neun `bad label`-Fehler. Die
erzeugten Bytes stimmen dabei zwar, als Orakel taugt es aber nicht; `qr68`
wiederholt genau den Rumpf zwischen `rept` und `endr`.

### Mehrere Durchläufe statt zwei

Weil die Befehlslänge an Symbolwerten hängt (ADDQ), messen zwei feste
Durchläufe nicht sicher: ein Vorwärtsbezug kann alle Adressen dahinter
verschieben. `qr68` läuft deshalb so lange, bis sich **zwei Durchläufe
hintereinander** nichts mehr bewegt, und gibt erst dann aus (`-v` zeigt die
Durchläufe). `r68` selbst hat genau zwei und meldet in diesem Fall
„phasing error" — `qr68` ist hier also verträglicher, ohne bei den Quellen,
die `r68` annimmt, ein anderes Ergebnis zu liefern.

## Bewusste Entscheidungen

- **Zeitstempel ist fest** (`-fdate=J,M,T,S,Mi,Se` setzt ihn). Reproduzierbare
  Ausgabe ist wertvoller als eine Uhr im Objekt; für den Vergleich mit `r68`
  wird dessen Stempel damit nachgebildet.
- **Geschrieben im QCC-Subset** wie `qcpp` (keine Unions, kein `->`, kein
  `float`, Arraygrößen als Literale, feste Tabellen statt `malloc`), damit
  `qr68` sich später selbst übersetzen lässt.
- **Zeilenenden LF, CR+LF und CR** werden alle verstanden. OS-9-Textdateien
  enden mit CR — bei `qcpp` hat genau das im Emulator dafür gesorgt, dass die
  ganze Datei eine Zeile war.
- **An Modellgrenzen wird abgebrochen**, nicht geraten.

## Wo es beim Treiberkorpus noch klemmt

`./test/mwos.sh` fährt die 29 SCF-Treiber des SDK aus einem Port-Verzeichnis,
so wie es die SDK-Makefiles tun (`-qb -u=. -u=<DEFS> -u=<MACROS>`). Von den
15, die `r68` selbst übersetzt (die übrigen 14 brauchen Definitionen anderer
Boards), sind **11 byteidentisch**. Die restlichen vier:

- **`sc68562`** benutzt `bsr.l`/`bcs.l` — die in r68 kaputte lange Sprungform
  (s. u.). `qr68` bricht dort ab.
- **`sc68990`** springt auf die unmittelbar folgende Anweisung; r68 lässt den
  Befehl mit `-b` weg, was sich nicht stabil nachbilden lässt (s. u.).
- **`sc8251a`** prüft `ifeq CPUType-FM16s` mit einem Namen, den es nicht
  gibt. r68 meldet dort „illegal external reference" und übersetzt **beide**
  Zweige — ein Ergebnis, das niemand haben will. `qr68` bricht ab.
- **`sccom`** findet `backplane.d` nicht; daran scheitert schon `r68`.

## Nächste Schritte

1. Der Rest des Korpus: die RBF- und SBF-Treiber, die Dateimanager und die
   Descriptor-Quellen — die SCF-Treiber sind nur ein Ausschnitt.
2. Kette `qcc_backend → qr68 → l68` einmal bis zum laufenden Modul auf dem
   68030 fahren (bisher ist nur gezeigt, dass `l68` von `qr68` byteweise
   dasselbe bekommt wie von `r68`).
3. Voller 68000/010/020/030/040-Integerbestand, getrieben vom Korpus
   (`move16`/Cache 61×, Bitfelder 23×, `divul` 8×, `pmove` 4×).
4. `qr68` als OS-9-Modul und auf dem 68030 laufen lassen.

**Zwei bekannte Grenzen dabei:**

- Die Quelle wird ganz in eine Arena gelesen (4 MB). Die größten vom Backend
  erzeugten Dateien sind 18 MB — dafür braucht es einen strömenden Leser,
  zumal die Arena später die Modulgröße mitbestimmt.
- 8 Sekunden für 3,5 MB Quelle. Die Symbolsuche ist linear (`symFind` über
  12.000 Symbole, `internN` über den ganzen Namensspeicher); eine
  Hashtabelle ist der erste Kandidat, wenn das auf dem 68030 stört.
