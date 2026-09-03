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
| `test/insn.a` — jede kodierbare Befehls- und Adressform, 217 Zeilen | **byteidentisch**, Zeile für Zeile |
| 10 Module aus QCCs Backend, bis 146.848 Zeilen / 1,07 MB ROF | **byteidentisch** |

Der Zeitstempel ist dabei nicht ausgenommen, sondern nachgebildet (`-fdate=`).

Abgedeckt: `psect`/`vsect`/`ends`, `equ`/`set`, `dc.b/.w/.l` (auch
Zeichenketten), `ds.b/.w/.l`, `align`, `end`, die beschreibenden Direktiven
(`nam`/`ttl`/`page`/`opt`/`spc`), Ausdrücke mit `$`/`%`/`@`/Zeichen, `+ - * /
& ! ^ << >> ~`, Klammern und `*` als aktueller Ort, Symbole mit
Vorwärtsreferenzen, globale Labels (`name:`), externe und lokale Referenzen.

**Befehle:** `move`/`movea`/`moveq`, `lea`/`pea`,
`add`/`sub`/`and`/`or`/`eor`/`cmp` mit allen Formen, die `r68` daraus macht
(Grundform, `…a`, `…i`, `…q`), `adda`/`suba`/`cmpa`, `addi`…`cmpi`,
`addq`/`subq`, `muls`/`mulu`/`divs`/`divu` (Wortform), `clr`/`neg`/`negx`/
`not`/`tst`/`tas`/`swap`/`ext`/`extb`, alle acht Schiebe- und Rotierbefehle
(Sofortwert, Register, Speicherform), `Scc` (14 Bedingungen), `bra`/`bsr`/
`Bcc` (kurz und Wort), `dbra`/`dbcc`, `jsr`/`jmp`, `link`/`unlk`,
`rts`/`rte`/`rtr`/`nop`/`trap`/`trapv`/`reset`/`stop`/`illegal`.

**Adressierungsarten:** alle zwölf des 68000 —  `Dn`, `An`, `(An)`, `(An)+`,
`-(An)`, `d16(An)`, `d8(An,Xn)`, `abs.w`, `abs.l`, `d16(PC)`, `d8(PC,Xn)`,
`#imm`.

**Alles andere bricht mit Meldung ab.** Das ist Absicht: eine still falsche
Kodierung wäre schlimmer als eine fehlende. Was am handgeschriebenen Korpus
zuerst fehlt, ist `use` (Include), `macro`/`endm`, die bedingte
Assemblierung, `os9` und `movem` — s. „Nächste Schritte".

## Der Prüfstein

`r68` ist byteweise reproduzierbar — bis auf **sechs Zeitstempelbytes** im
ROF-Kopf. Also:

> qr68 ist richtig, wenn seine Ausgabe zu der von `r68` byteidentisch ist,
> diese sechs Bytes ausgenommen.

```
make test                          # Proben + Befehlstabelle
make backend                       # dazu die Quellen des QCC-Backends
make check                         # beides

./test/difftest.sh                 # die eingebauten Proben
./test/difftest.sh datei.a         # eine echte Quelle, ganze ROF-Datei
./test/insndiff.sh datei.a         # Quellzeile für Quellzeile
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

### Ein Defekt in r68 V2.9.1

Die **lange Sprungform** (`bra.l`, `bsr.l`, `bcc.l`, 68020) ist kaputt: `r68`
gibt `6000 00000000` aus — ohne das nötige `$FF` im unteren Byte des
Befehlsworts und ohne den Abstand einzusetzen. Der erzeugte Sprung geht ins
Leere. Im ganzen handgeschriebenen Korpus kommt die Form zweimal vor
(`MWOS/OS9/SRC/IO/SCF/DRVR/sc68562.a:252`), beide Male trifft sie diesen
Defekt. `qr68` **bricht dafür ab** statt entweder den Defekt nachzubauen oder
still davon abzuweichen.

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

## Nächste Schritte

1. `use` (Include, 965×), `macro`/`endm` (157×), bedingte Assemblierung,
   `os9` (742×) — das ist es, was die handgeschriebenen Quellen als Erstes
   verlangen. `movem` fehlt dafür ebenfalls.
2. Kette `qcc_backend → qr68 → l68` einmal bis zum laufenden Modul auf dem
   68030 fahren (bisher ist nur gezeigt, dass `l68` von `qr68` byteweise
   dasselbe bekommt wie von `r68`).
3. Voller 68000/010/020/030/040-Integerbestand, getrieben vom Korpus
   (`movec` 220×, `move16`/Cache 61×, Bitfelder 23×, `divul` 8×, `pmove` 4×).
4. `qr68` als OS-9-Modul und auf dem 68030 laufen lassen.

**Zwei bekannte Grenzen dabei:**

- Die Quelle wird ganz in eine Arena gelesen (4 MB). Die größten vom Backend
  erzeugten Dateien sind 18 MB — dafür braucht es einen strömenden Leser,
  zumal die Arena später die Modulgröße mitbestimmt.
- 8 Sekunden für 3,5 MB Quelle. Die Symbolsuche ist linear (`symFind` über
  12.000 Symbole, `internN` über den ganzen Namensspeicher); eine
  Hashtabelle ist der erste Kandidat, wenn das auf dem 68030 stört.
