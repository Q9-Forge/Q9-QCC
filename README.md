# Q9-ql68 — der Binder der Q9-Werkzeugkette

Ausführbares Programm: **`ql68`**

```
ql68 [Optionen] <eingabe.r> -O=<modul>
```

Ziel: Microwares `l68` ersetzen. Nach `qcpp` (Präprozessor), QCC (Compiler)
und `qr68` (Assembler) ist der Binder das letzte große Fremdteil der Kette
— danach bleibt nur noch Microwares `clib`.

```
QCC / qcpp  →  .a  →  qr68  →  .r (ROF)  →  ql68  →  OS-9-Modul
                                            ^^^^
```

## Methode

Dieselbe wie bei [`Q9-qr68`](../Q9-qr68/): **`l68` ist das Orakel.** Jede
Formatfrage wird an einer kleinen Probe gemessen und byteweise verglichen,
nicht aus Dokumentation abgeleitet. Was Microware selbst beschreibt, steht
in `../Q9-qr68/docs/ROF_UND_LINKER_QUELLEN.md` — es gibt die Struktur vor,
die Messung die Bytes.

**`l68` ist byteweise reproduzierbar** — und zwar ohne jede Ausnahme.
Anders als `r68`, das sechs Zeitstempelbytes einträgt, enthält ein
OS-9-Modul gar kein Datum. Zweimal binden ergibt zweimal dieselbe Datei.
Der Vergleich braucht deshalb kein Gegenstück zu `qr68 -fdate=`.

> **Falle beim ersten Messen:** der **Modulname kommt aus dem Ausgabenamen**
> (`-O=`), nicht aus dem psect. Zwei Läufe mit verschiedenen Ausgabenamen
> unterscheiden sich deshalb in genau vier Bytes — dem Namen und dem CRC
> darüber. Das sah nach fehlender Reproduzierbarkeit aus und war keine.

## Was gemessen ist

### Der Modulkopf

Aufbau nach `MWOS/OS9/SRC/DEFS/module.h` (`struct modhcom` + `mod_exec`),
Byte für Byte an einem gebundenen Modul nachgeprüft:

| Offset | Feld | im Beispiel |
|---|---|---|
| `$00` | `M$ID` Sync | `4afc` |
| `$02` | `M$SysRev` | `0001` |
| `$04` | `M$Size` | Gesamtgröße **einschließlich** Kopf und CRC |
| `$08` | `M$Owner` | `00010000` ohne `-gu=` |
| `$0c` | `M$Name` | Offset auf den Namensstring |
| `$10` | `M$Accs` | `0555` ohne `-p=` |
| `$12` | `M$TyLan` | aus dem psect |
| `$14` | `M$AttRev` | aus dem psect |
| `$16` | `M$Edit` | aus dem psect |
| `$18` | `M$Usage` | 0 |
| `$1c` | `M$Symbol` | 0 |
| `$20` | `M$Ident` | 0 |
| `$22` | 6 Reservebytes | 0 |
| `$28` | `M$HdExt` | 0 |
| `$2c` | `M$HdExtSz` | 0 |
| `$2e` | `M$Parity` | s. u. |
| `$30` | `M$Exec` | Einsprung, Modulabstand |
| `$34` | `M$Excpt` | Ausnahmeeinsprung |
| `$38` | `M$Data` | Gesamtgröße des Datenbereichs |
| `$3c` | `M$Stack` | aus dem psect |
| `$40` | `M$IData` | Abstand auf die initialisierten Daten |
| `$44` | `M$IRefs` | Abstand auf die Zeigerlisten |

Danach: Name (nullterminiert, auf gerade aufgefüllt), Code, IData, IRefs,
CRC.

### Kopfparität

**Das Einerkomplement des XOR aller Kopfworte von `$00` bis `$2d`.**
Gemessen und nachgerechnet — stimmt beim ersten Versuch.

### CRC

24 Bit, **Polynom `$800063`**, Startwert `$FFFFFF`, über das Modul **ohne**
die letzten drei Bytes, das Ergebnis **komplementiert**.

Die Konstante `CRCCON = $800FE3` aus `module.h` ist **nicht** das Polynom,
sondern der **Sollrest**: rechnet man den CRC über das *ganze* Modul
einschließlich seiner drei CRC-Bytes, kommt genau `$800FE3` heraus. Genau
das prüft der Kernel. An `l68`s Ausgabe nachgerechnet, beides stimmt.

### Der Datenbereich

**Erst die uninitialisierten, dann die initialisierten Daten.** Bei
`wert dc.l / zeiger dc.l / puffer ds.b 8` liegt `puffer` auf 0..7 und
`wert`/`zeiger` auf 8..15 — deshalb nennt der IData-Abschnitt den Offset 8.

### IData-Abschnitt

```
<Offset in den Datenbereich: 4 Byte>
<Anzahl Bytes: 4 Byte>
<die Bytes>
```

### IRefs-Abschnitt

**Zwei Listen**: zuerst die Zeiger, auf die der **Code**basis aufaddiert
wird, dann die auf die **Daten**basis. Jede Liste ist eine Folge von
Gruppen

```
<msw: 2 Byte>  <Anzahl: 2 Byte>  <lsw: 2 Byte × Anzahl>
```

und endet mit einer Gruppe der Anzahl 0. Die vollständige Adresse eines
Eintrags ist `msw<<16 | lsw` — der Abstand im Datenbereich, an dem der zu
relozierende Zeiger liegt.

Gemessen an vier Zeigern (`p1 dc.l start`, `w1 dc.l $aabbccdd`,
`p2 dc.l start`, `p3 dc.l p1`): Liste 1 nennt `$0000` und `$0008` (die
beiden Codezeiger), Liste 2 nennt `$000c` (den Datenzeiger), `w1` kommt in
keiner Liste vor. Beim leeren Modul sind beide Listen leer — daher die acht
Nullbytes plus ein Füllbyte auf gerade Länge.

### Der Datenbias von `$8000` — aber nur bei Programmen

Ein Programm greift über `a6` auf seine Daten zu, und `a6` zeigt **nicht**
auf den Anfang des Datenbereichs, sondern `$8000` dahinter. So reicht ein
16-Bit-Displacement ±32K weit statt nur 0…32K.

Gemessen: aus `move.l zeiger(a6),d1` mit `zeiger` auf Datenoffset `$000c`
macht `l68` das Displacement **`$800c`**. Der Bias gilt nur für dieses
Displacement im Code — ein 32-Bit-Zeiger *in* den Daten bleibt
unvorgespannt (`p3 dc.l p1` mit `p1` auf Offset 0 ergibt 0).

**Ein Treiber kennt den Vorspann nicht.** Er bekommt seinen statischen
Speicher direkt (in `a2`): aus `move.w d2,$001c(a2)` im Treiber `sc172`
macht `l68` genau `$001c`. Der Bias hängt also am **Modultyp**, nicht am
Register — eine zu breite Regel hatte hier zunächst ein Byte verdorben.

### Die Zeigerlisten sind nicht sortiert

`l68` stellt jeden neuen Eintrag **vorne an** (LIFO). Bei einem einzelnen
psect sieht das Ergebnis aufsteigend aus, weil der ROF seine lokalen
Referenzen absteigend liefert — ein Trugschluss, den erst der zweite psect
aufdeckt: zwei Codezeiger auf Datenoffset `$10` (Wurzel) und `$18`
(zweiter psect) ergeben die Liste **`$18, $10`**.

### Der Modulaufbau hängt an der SPRACHE, nicht am Typ

| | Erweiterung | Name | IData/IRefs |
|---|---|---|---|
| **Sprache 0** | keine | **hinter** dem Code (`$30`) | nein |
| Sprache ≠ 0, Typ ≠ 1 | `_mexec`/`_mexcpt`/`_mdata` (12 Byte) | **hinter** dem Code (`$3c`) | nein |
| Typ 1 (Prgm) | `mod_exec`, 24 Byte mit Stack, IData, IRefs | **vor** dem Code (`$48`) | ja |

Sprache 0 heißt „nicht ausführbar" — so ein Modul braucht keine
Einsprungfelder. Das trifft die Descriptoren (`0f00`) **und** die
Init-Module (`0c00`) mit derselben Regel.

> **Eine Fehlverallgemeinerung, die der Prüfstand aufgedeckt hat.** Nach
> den ersten Messungen stand hier eine Typliste: „Typ 2, 12 und 14 haben
> 12 Byte, Typ 15 keine." Der Lauf über den SDK-Korpus zeigte, dass
> **Typ 12 in beiden Formen vorkommt** — `snoop162` ist `0c01` und liegt
> auf `$3c`, `init` ist `0c00` und liegt auf `$30`. Erst die Sprache
> erklärt beide.

Wo die Abschnitte fehlen, fehlen auch die Felder — ein Treiber mit
initialisierten Daten braucht bei `l68` eigens `-i`.

Gemessen an `sc8x30.a` (Treiber `sc172`): `_mexec = $3c` zeigt auf die
Routinentabelle, die die **ersten 14 Codebytes** sind; `_mdata = $114`
sind die 276 Byte `ds`; der Name liegt auf `$664 = $3c + 1576`.

### Mehrere psects

Code in Reihenfolge der Kommandozeile, ohne Auffüllen dazwischen. Der
**Datenbereich** dagegen gruppiert: **erst alle reservierten Daten aller
psects, dann alle initialisierten** (Handbuch Abb. 9-2). Nachgemessen an
zwei psects: `mvar` landet auf `$0c`, `svar` auf `$14`.

Nur der **erste** ROF darf einen Wurzel-psect haben (Typ/Sprache ≠ 0); aus
ihm allein entsteht der Modulkopf.

### Abziehende und relative Referenzen

Bit 6 heißt „den Wert **abgezogen** eintragen" — so entsteht die Differenz
zweier Bezüge in einem Ausdruck (`PD_PAR-PD_OPT+M$DTyp(a1)` legt drei
Referenzen auf denselben Offset ab, eine davon mit diesem Bit). Bit 7 heißt
**relativ zur Referenzstelle**: `jsr sub1(pc)` mit `sub1` auf `$5a` und dem
Erweiterungswort auf `$52` ergibt `$0008`.

### Zwei Kleinigkeiten, die man sonst sucht

- **`M$Excpt` wird 0, nicht `$ffffffff`.** Fehlt der siebte
  psect-Parameter, trägt `r68` `utrap = -1` in den ROF ein; `l68` macht
  daraus im Modul die 0.
- **Die Zeigerlisten sind aufsteigend sortiert.** Der ROF liefert seine
  lokalen Referenzen absteigend nach Offset — `l68` dreht sie um.

### Bibliotheken sind kein Sonderformat

`-l=sys.l` liest **eine Folge von ROF-Dateien**, hintereinander in einer
Datei — kein libgen-Format. Nachgemessen an
`MWOS/OS9/68000/LIB/sys.l`: sieben ROFs, 1747 Globale, **alle vom Typ
`$0006` (equ)**, und die Längenrechnung landet exakt auf dem Dateiende.
Genau das meint die Dokumentation mit „the `sys.l` library module …
contains only `equ` symbol definitions".

Die Länge eines ROF ist dabei: 56 + Name + Globale + Code + init. Daten +
externe Referenzen + lokale Referenzen + **vier abschließende
Langwörter**.

### Externe Referenzen

Der Wert des Symbols wird an der Referenzstelle **aufaddiert** — der
Assembler hat dort schon den konstanten Anteil des Ausdrucks abgelegt.
Umfang und Ort stehen im selben Typwort wie bei den lokalen Referenzen.

Dazu kommen die Symbole, die **erst der Binder kennt** (Tabelle 9-8/9-9
des Handbuchs): `bname`/`_bname` (Offset auf den Modulnamen),
`btext`/`_btext`, `etext`/`_etext`, `end`/`_enddata`. Sie werden **nach**
den Bibliotheken eingetragen und verdecken damit eine gleichnamige
Definition von dort.

### Gerätedescriptoren haben einen anderen Aufbau

Ein Modul vom **Typ 15 (Devic)** benutzt nicht `mod_exec`, sondern
`mod_dev`: sein ROF-**Code ist die Kopferweiterung** — Portadresse,
Vektor, IRQ-Ebene, Modus, die Namensoffsets für Dateimanager und Treiber —
und liegt unmittelbar hinter dem gemeinsamen Kopf auf `$30`. Danach folgt
nur der Name und der CRC; **es gibt weder IData- noch IRefs-Abschnitt**.

Gemessen an `SRC/IO/SCF/DESC/term.a`: die Namensoffsets im Code springen
von `$0034`/`$0038`/`$003e` auf `$0064`/`$0068`/`$006e` — genau um die
Codebasis `$30`.

## Stand

**Programme, Descriptoren und Treiber werden byteidentisch zu `l68`
gebunden — auch aus mehreren ROFs.**

| Probe | Inhalt | Ergebnis |
|---|---|---|
| `test/tiny.a` | nur Code | **byteidentisch** (102 Byte) |
| `test/dat.a` | Code, `dc`- und `ds`-Daten, ein `a6`-Displacement | **byteidentisch** (118 Byte) |
| `test/ref.a` | vier Zeiger: zweimal Code, einmal Daten, einer ohne Bezug | **byteidentisch** (130 Byte) |

| `test/multi*.a` | zwei psects, Aufruf über psect-Grenze, Daten beider | **byteidentisch** (142 Byte) |
| **7 SCF-Descriptoren des SDK** (`term`, `t1`–`t3`, `p1`–`p3`) | mit `sys.l`, `-gu=0.0`, `-p=577`, je ~24 externe Referenzen | **byteidentisch** |
| **Der SCF-Treiber `sc172`** (aus `sc8x30.a`) | 1576 Byte Code, 276 Byte Daten, abziehende Referenzen | **byteidentisch** (1646 Byte) |

`./test/difftest.sh` fährt die eigenen Proben, `./test/descs.sh` die
Descriptoren des SDK mit den Aufrufen aus dessen Makefile. Beide
vergleichen byteweise;
`tools/modcmp.py` benennt bei einer Abweichung das betroffene Kopffeld,
statt nur einen Offset zu zeigen.

## Der SDK-Korpus

`./test/sdkdiff.sh` holt die `l68`-Kommandozeilen aus den SDK-Makefiles
selbst — derselbe Hebel wie bei `Q9-qr68`:

```
MWMAKEOPTS=-u  os9make -nn -u
```

Anders als beim Assembler braucht der Binder aber seine **Eingaben**: die
`.r`-Dateien entstehen erst durch die `r68`-Zeilen desselben Trockenlaufs.
Das Skript fährt deshalb beide Sorten Zeilen der Reihe nach und biegt alle
Ausgaben in ein Temporärverzeichnis um — in den SDK-Baum wird nichts
geschrieben.

```
467 l68-Aufrufe: 227 gleich, 0 abweichend, 230 uebersprungen, 10 doppelt
```

227 Aufrufe über **88 verschiedene Module**, und alle sechs im Korpus
vorkommenden Typ/Sprach-Kombinationen sind dabei:

| | Module |
|---|---|
| Typ 15 Sprache 0 (Descriptoren) | 155 |
| Typ 12 Sprache 0 (Init) | 27 |
| Typ 12 Sprache 1 (Systemmodule) | 17 |
| Typ 14 Sprache 1 (Treiber) | 12 |
| Typ 2 Sprache 1 (Unterprogramme) | 6 |
| Typ 1 Sprache 1 (Programme) | 5 |

Von den 230 Übersprungenen sind 40 `-r=` (rohe Binärausgabe — ein eigener
Modus, kein Modul); bei den übrigen kommt `l68` selbst nicht durch.

### Der Prüfstand musste selbst erst geprüft werden

Von den vier Abweichungen, die die Läufe meldeten, waren **drei Fehler im
Skript** und nur eine ein echter Formatbefund:

- Ein angehängter Zähler am Ausgabenamen — und der **Modulname kommt
  daraus**. `l68` bekam `8-p3`, `ql68` `p3`; acht Module schienen um zwei
  Byte zu differieren.
- Alle Ports bildeten ihr `RELS\` auf **dasselbe** Ziel ab und
  überschrieben sich gegenseitig.
- Eine **Shell-Umleitung** (`> …/null.map`) wurde als Eingabedatei
  durchgereicht.

Nur der vierte Befund — Sprache statt Typ — lag am Binder.

## Die rohe Binärausgabe (`-r=<basis>`)

Kein Modulkopf, kein Name, kein CRC — der Code liegt ab Dateianfang,
dahinter IData und IRefs wie sonst. Für den ROM-Code des SDK (40 Aufrufe).

**Die Basis wirkt nur auf Codebezüge im Code.** Ein Label auf Codeoffset 6
wird im Code zu `$1006`; ein Zeiger darauf *in den Daten* bleibt `$0006` —
den setzt erst der Startcode über die Zeigerliste. Das deckt sich mit dem,
was das Handbuch zu `M$IRefs` sagt: „Adjust code pointers by adding the
absolute starting address of the object code area."

**Der `$8000`-Datenbias entfällt.** In dieser Betriebsart legt ihn der
Startcode selbst an (Handbuch Kap. 9: „Some processors may require biasing
and the initialization of a code area data pointer").

> **Eine Eigenheit, die sich nicht herleiten lässt.** Ist **genau eine**
> der beiden Zeigerlisten leer, hängt `l68` vier Nullbytes an; sind beide
> leer oder beide gefüllt, kommt nichts. An sieben Fällen durchgemessen —
> keine Zeiger, nur Code-, nur Daten-, beide Arten, ein bis drei Stück.
> Im **Modul**aufbau gibt es das nicht, dort steht das Auffüllen auf gerade
> Länge. `ql68` bildet es nach, weil es sonst keine Byteidentität gibt.

`./test/rawtest.sh` fährt elf Quellen mit je zwei Basisadressen (`0` und
`$1000`), damit sichtbar bleibt, dass die Basis nur im Code wirkt:
**22 gleich, 0 abweichend**.

### Nachtrag zum Datenbias

Das technische Handbuch schreibt: *„(a6) is actually biased by `$8000`, but
this can usually be ignored because the linker biases all data references
by `-$8000`."* Also ein **Minus** — numerisch dasselbe wie das gemessene
`+$8000` modulo 16 Bit (`$0c` → `$800c` ist `−$7ff4`), aber die richtige
Lesart.

## Die Schalter, die das Modul verändern

`l68` hat rund zwanzig Schalter. Die meisten schreiben nur eine Karte oder
eine Nebendatei; sieben verändern das Modul selbst. Jeder einzelne ist
gegen den Lauf **ohne** ihn gemessen — `./test/optstest.sh`.

| Schalter | Wirkung, gemessen |
|---|---|
| `-M=<n>[K]` | Zuschlag auf `M$Stack`. **Die Zahl zählt immer in K**: `-M=1` und `-M=1K` ergeben beide 1024 dazu, `-M=100` volle 102400. Das `K` ist schmückend. |
| `-b=<n>` | Richtet Codeanfang **und** Datenblöcke auf `n` aus (2, 4, 8, 16). |
| `-x=<n>` | Richtet **nur** den Codeanfang aus. |
| `-S` | Modul bleibt im Speicher: im Attributwort kommt `$4000` dazu (`$8000` → `$c000`). |
| `-R=<n>` | Revisionsnummer, das untere Byte desselben Wortes. |
| `-e=<n>` | Editionsnummer (war schon da). |
| `-p=<hex>`, `-gu=` | Zugriffsrechte und Eigentümer im Kopf (waren schon da). |

### `-b=` und `-x=` richten den Codeanfang aus, nicht den Einsprung

Die Probe `test/opt/od.a` hat ihren Einsprung bewusst auf Codeabstand 4.
Ohne Schalter beginnt der Code auf `$4e`, `M$Exec` steht auf `$52`. Mit
`-b=4` rückt der **Code** auf `$50` und `M$Exec` auf `$54` — ausgerichtet
wird also der Codeanfang, der Einsprung wandert mit. `-b=16` verschiebt
zusätzlich die Datenblöcke: `M$Data` wächst von `$10` auf `$20`, weil
jeder der beiden Blöcke einzeln auf 16 aufgerundet wird.

> **`-b=2` und `-x=2` bleiben wirkungslos, und das ist kein Testloch.**
> `r68` liefert die vsect-Größen bereits auf ein Vielfaches von 4
> gerundet (`ds.b 5` → 8, `ds.b 1` → 4, 6 Byte `dc` → 8), und der
> Codeanfang steht ohnehin auf gerader Adresse. Erst ab `-b=4` gibt es
> etwas auszurichten. `optstest.sh` meldet solche Fälle eigens als „ohne
> Wirkung" und zählt sie getrennt — ein Fall, bei dem beide Binder den
> Schalter ignorieren, ist kein Nachweis, und er soll nicht in einer
> grünen Zahl verschwinden.

### Die harmlosen Schalter — und dass `l68` sie bündelt

`-m`, `-s`, `-w`, `-j`, `-g`, `-v`, `-c`, `-i`, `-q`, `-f=`, `-mt<x>`
ändern das Modul nicht; `ql68` nimmt sie an und übergeht sie, damit die
Aufrufe der SDK-Makefiles unverändert durchlaufen. **`l68` nimmt die
Einzelbuchstaben auch als Bündel**: das `-swam` der ROM-Makefiles ist
`-s -w -a -m`.

`-t=` nimmt `ql68` nur als `-t=os9_68k` an. Die übrigen Ziele von `l68`
sind ganz andere Modulformate — da ist ein Abbruch besser, als still das
falsche Format zu schreiben.

### Der Wächter, der acht grüne Module zerlegt hat

Ein Zwischenstand hatte einen Wächter, der abbricht, wenn ein Bezug nicht
in sein Feld passt — mit der Begründung, `l68` brauche dafür `-a`. Er hat
im SDK-Korpus **acht zuvor byteidentische Module zerlegt** (`sc68990`,
`sc147`, `sc162`, `sc167`, `sc172`, `sc177`, `sc68360`, `ram`).

Der Grund steht wörtlich in den SCF-Treibern:

```
    move.b  PD_PAR-PD_OPT+M$DTyp(a1),d0
```

Das erzeugt **drei Referenzen auf dasselbe Byte-Displacement**. `ql68`
verrechnet sie nacheinander und kappt dabei jedes Mal auf die Feldbreite.
Das ist kein Verlust, sondern Rechnen modulo 256: `(a−b+c) mod 256` kommt
richtig heraus, gleich an welcher Stelle gekappt wird. `PD_PAR` allein ist
`$1005c` — erst der Abzug von `PD_OPT` macht daraus ein kleines
Displacement. `l68` rechnet genauso und meldet nichts.

**Der Endwert eines Feldes steht erst fest, wenn alle Referenzen darauf
abgearbeitet sind.** Eine Einzelreferenz taugt nicht als Prüfstelle. Der
Wächter ist wieder draußen.

Für einen wirklich zu weiten Bezug legt `l68` mit `-a` eine Sprungtabelle
an — gemessen: aus `bsr sub1` wird `jsr d16(a6)`, und in den
initialisierten Daten steht ein 6 Byte langer Eintrag `jmp $xxxxxxxx`,
dessen Adresse in der Code-Zeigerliste mitgeführt wird. Das ist bewusst
nicht nachgebaut: im ganzen SDK-Korpus kommt der Fall nirgends vor, alle
227 Aufrufe sind ohne Sprungtabelle byteidentisch.

### Welche Schalter der Korpus überhaupt benutzt

`tools/optcensus.sh` fährt denselben Trockenlauf wie `sdkdiff.sh`, bindet
aber nichts, sondern **zählt die Schalter**. Über alle 467 `l68`-Aufrufe:

| Schalter | Aufrufe | | Schalter | Aufrufe |
|---|---|---|---|---|
| `-l=` | 559 | | `-r=` | 40 |
| `-O=` | 467 | | `-w` `-s` `-a` `-m` (aus `-swam`) | je 20 |
| `-gu=` | 429 | | `-M=` | 20 |
| `-n=` | 178 | | `-b=` | 20 |
| `-g` | 88 | | `-s` | 1 |
| `-p=` | 84 | | | |

Mehr ist es nicht. `-x=`, `-S`, `-R=`, `-e=`, `-t=`, `-c`, `-i`, `-f=`,
`-mt<x>`, `-z` kommen im ganzen SDK **nicht vor** — sie sind trotzdem
gebaut und gemessen, denn „kommt im Korpus nicht vor" heißt nicht „gibt es
nicht".

### Die harmlosen Schalter sind nachgemessen, nicht behauptet

Dass `-m -s -w -j -g -v -c -i -q -f= -mt<x>` das Modul nicht verändern,
stand lange nur als Kommentar im Code. Teil 2 von `optstest.sh` misst es
mit **umgekehrter Erwartung**: `l68` *muss* mit dem Schalter dieselben
Bytes liefern wie ohne ihn. Täte es das nicht, würde `ql68` ihn zu Unrecht
übergehen. **16 Formen geprüft — darunter die Bündel `-swam` und `-gwj` —
keine wirkt.**

### `-r` ohne Basis und die Antwortdatei `-z=`

Zwei Formen aus `l68`s Liste, die im Korpus nicht vorkommen und trotzdem
zum Sprachumfang gehören:

`-r` ohne `=` ist die rohe Ausgabe mit Basis 0 — an `l68` gemessen liefern
`-r` und `-r=0` dieselben 54 Byte.

`-z=<datei>` liest Dateinamen und Optionen aus einer Datei. **Jede Zeile
ist genau ein Eintrag**, und das ist der Befund, den man nicht raten kann:
steht `od.r -M=8K` in *einer* Zeile, sucht `l68` eine Datei dieses Namens
(`can't open file, od.r -M=8K`). Dateinamen und Optionen dürfen sich
mischen, die Reihenfolge ist egal — zwei Läufe mit vertauschten Zeilen
ergaben dieselben Bytes. `ql68` löst `-z=` beim Aufbau der Argumentliste
auf, an Ort und Stelle; ein `-z=` innerhalb der Datei wird wieder
aufgelöst.

> **`-z` ohne `=` liest bei `l68` die Standardeingabe — das baut `ql68`
> nicht nach.** Es kennt nur `fopen`; eine `stdin`-Deklaration wäre
> plattformabhängig und stünde der Übersetzbarkeit durch QCC im Weg.
> `ql68` bricht mit genau dieser Begründung ab, statt still etwas anderes
> zu tun.

Der Wirkungsnachweis steckt mit im Prüfstand: `-M=8K` aus der
Antwortdatei muss im Modulkopf ankommen (`M$Stack` `$64` → `$2064`).
Sonst wären die `-z=`-Fälle nur deshalb grün, weil beide Binder die Datei
gleichermaßen ignorieren.

## Was als Nächstes ansteht

1. **Die Schalter sind abgearbeitet** — gegen `l68 -?` durchgezählt,
   gegen den Korpus gemessen. Bewusst offen bleiben genau zwei Dinge:
   die Sprungtabelle von `-a` (kommt im Korpus nirgends vor) und `-z`
   ohne `=` von der Standardeingabe (setzt `stdin` voraus, das `ql68`
   nicht hat). Beide sind oben begründet.
