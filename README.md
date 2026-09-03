# Q9-qr68 — der 68k-Assembler der Q9-Werkzeugkette

Ausführbares Programm: **`qr68`**

```
qr68 [Optionen] <eingabe.a> <ausgabe.r>
```

Ziel: Microwares `r68` ersetzen. Präprozessor (`qcpp`) und Compiler-Frontend
(QCC) laufen bereits auf echtem 68030 — Assembler und Binder sind die letzten
Fremdteile der Kette.

## Stand (2026-09-03)

Erste Etappe, bewusst am schwierigsten Ende angefangen: der **ROF-Schreiber**
steht und ist gegen `r68` verifiziert.

| Prüfung | Ergebnis |
|---|---|
| 9 Proben (Kopf, Globale, Externe, vsect, Padding) gegen `r68` | **byteidentisch**, Zeitstempel ausgenommen |

Abgedeckt: `psect`/`vsect`/`ends`, `equ`/`set`, `dc.b/.w/.l` (auch
Zeichenketten), `ds.b/.w/.l`, `align`, `end`, die beschreibenden Direktiven
(`nam`/`ttl`/`page`/`opt`/`spc`), Ausdrücke mit `$`/`%`/`@`/Zeichen, `+ - * /
& ! ^ << >> ~`, Klammern und `*` als aktueller Ort, Symbole mit
Vorwärtsreferenzen, globale Labels (`name:`), externe Referenzen — und als
Befehle bislang nur `rts`, `nop`, `rte`, `jsr` (absolut lang).

**Alles andere bricht mit Meldung ab.** Das ist Absicht: eine still falsche
Kodierung wäre schlimmer als eine fehlende.

## Der Prüfstein

`r68` ist byteweise reproduzierbar — bis auf **sechs Zeitstempelbytes** im
ROF-Kopf. Also:

> qr68 ist richtig, wenn seine Ausgabe zu der von `r68` byteidentisch ist,
> diese sechs Bytes ausgenommen.

```
./test/difftest.sh                 # die eingebauten Proben
./test/difftest.sh datei.a         # eine echte Quelle
```

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

Gemessene Typwörter — Globale: Code `$0004`, initialisierte Daten `$0001`,
reservierte Daten `$0000`. Externe Referenz, 32 Bit absolut im Code: `$0038`.
Weitere Fälle werden beim Ausbau **einzeln gemessen**, nicht abgeleitet.

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

1. Befehlstabelle auf den Bestand des QCC-Backends bringen (~40 Formen, 9
   Adressierungsarten) — dann läuft die Kette `qcc_backend → qr68 → l68`.
2. Sprungweitenwahl an `r68` messen (kurz/lang) — das ist der wahrscheinlichste
   Stolperstein für Byteidentität.
3. `use` (Include), `macro`/`endm`, bedingte Assemblierung, `os9`.
4. Voller 68000/010/020/030/040-Integerbestand, getrieben vom Korpus.
5. `qr68` als OS-9-Modul und auf dem 68030 laufen lassen.
