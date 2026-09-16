# ISO-C-Lückenliste und Zielplanung

Stand der PLANUNG unten: **2026-07-22**.
Der **gemessene** Stand steht im Abschnitt direkt darunter (2026-09-16) und
geht im Zweifel vor — die Planungstabellen weiter unten sind älter als der
Code.

---

## Gemessener Stand 2026-09-16 (68k-Pfad)

Jeder Fall wurde dreifach geprüft: Meldung des Frontends, VM-Orakel **und ob
`qir68k` übersetzen kann**. Das Orakel allein genügt nicht — mehrere Fehler
dieses Tages waren dort grün und auf dem Ziel falsch.

### Scheitert STUMM (Exit 1, „FAIL", leerer stderr)

Das ist die unangenehmste Kategorie: der Compiler sagt nicht, was er nicht
kann.

| Konstrukt | |
|---|---|
| `int f(int m[2][2])` | mehrdimensionales Array als Parameter |
| `int (*p)[3]` | Zeiger auf Array |
| `int (**q)(int)` | Zeiger auf Funktionszeiger |
| `int f(int a, ...)` | eigene variadische Funktion |
| `signed char`, `long double`, `register` | Schlüsselwörter |
| `sizeof x` ohne Klammern | |
| `+5` | unäres Plus — Ursache bekannt, s. `FLOAT_PLAN_de.md` |
| `struct N;` | getrennte Vorwärtsdeklaration |

### Fehlt, wird aber gemeldet

- `struct S g = {1,2};` **global** (lokal geht seit 2026-09-16)
- Bitfelder, `long long`, `float`
- `p[i]++` über einen **Zeiger** (trifft `int` wie `double`)

### Erledigt seit dieser Messung

- **`struct`-Feld vom Typ `struct`** (eingebetteter Wert) — 2026-09-17.
  Gemessener Anlass: **526 Stellen in 202 von 1917 MWOS-Quellen (10,5 %)**,
  überwiegend Geräte-Header (`quicc.h`, `enet360.h`). Damit war es die mit
  Abstand breiteste verbliebene Lücke — zum Vergleich: 2D-Array als Parameter
  57 Dateien, Bitfelder 10, **eigene variadische Definitionen nur 4**.

  `o.in.a` ist **reine Offset-Addition** — anders als `p->n->v`, wo
  zwischendurch ein Zeiger geladen wird. Ein `LOADIND` zuviel würde die ersten
  vier Byte des Feldes als Adresse deuten. Beide Sorten und ihre *Mischung*
  (`c.b.ap->v`, `p->a.v`) sind getestet.

  Dabei fiel ein Fehler im ersten Anlauf auf, der ohne eingebettete structs
  nie sichtbar war: `viaPtr` beschreibt den Operator, über den man **zum** Feld
  kommt, nicht wie es weitergeht — bei `p->in.a` gehört das `->` zu `p`.
  Jetzt wird mitgeführt, ob das aktuelle Element ein Zeiger ist, und der
  Operator dagegen geprüft. Das diagnostiziert zusätzlich `.` auf einem Zeiger
  und `->` auf einem Wert.

  Abgelehnt bleiben (mit Meldung): ein struct, das sich **selbst per Wert**
  enthält, und ein Feld mit **unvollständigem** struct-Typ (nach bloßer
  Vorwärtsdeklaration) — dessen Größe ist noch unbekannt.

  Verifiziert: `test_struct_68k.sh` **60/60** auf echtem 68030 (neun neue
  Fälle, u. a. Feld hinter dem eingebetteten struct, `char` vor `int` im
  inneren struct, zwei gleiche structs nebeneinander), `runtests.sh` 240 ok /
  537 Programme.

- **K&R-Funktionsdefinitionen** (`int f(a) int a; {…}`) — 2026-09-16.
  Anlass war eine Zählung statt eines Gefühls: **355 K&R-Definitionen in 165
  der 982 C-Quellen** unter `MWOS` (17 % der Dateien). Umgesetzt als dritte
  Alternative in `funcParams` (`voidParams | krParams | normalParams`), also
  genau dort, wo das Zurücksetzen zwischen Alternativen im Generator schon
  vorher trug.

  Zwei Entwurfsentscheidungen, die den Unterschied machen:

  1. *Die Deklarationen gehören mit in `krParams`.* Damit fällt die
     Entscheidung „K&R oder ANSI" vollständig innerhalb **einer** Alternative,
     und der Generator muss nie über eine bereits gewählte Alternative hinaus
     zurücksetzen — das kann er nämlich nicht.
  2. *Mindestens eine Deklaration ist Pflicht.* Genau das entschärft die
     Mehrdeutigkeit, an der das unäre Plus scheitert: bei `f(myint a)` sieht
     die Klammer wie eine K&R-Namensliste aus, aber es folgt `{` statt einer
     Deklaration, also scheitert `krParams` als Ganzes und `normalParams`
     übernimmt. Der Preis ist K&R **mit** implizitem `int` und *ganz ohne*
     Deklaration (`f(x) { }`) — in den 355 gemessenen Fällen kommt das nicht
     vor.

  Die Reihenfolge der Parameter richtet sich nach der **Klammer**, nicht nach
  den Deklarationen (`f(a,b) int b; int a;` ist gültig und häufig). Eine
  Deklaration, die keinen Klammernamen trifft, wird **gemeldet**, nicht
  verschluckt. Ein Klammername ohne Deklaration ist implizit `int`.

  Verifiziert: 20 Fälle auf echtem 68030 (`tools/test_knr_68k.sh`, alle grün,
  inkl. `struct` per Wert, `double`, vertauschter Deklarationsreihenfolge) und
  20 Fälle im Host-Orakel (`runtests.sh`). Bei schmalen Typen mit negativem
  Wert prüfen die Tests bewusst die **Differenz zum ANSI-Zwilling** (Sollwert
  0) statt eines absoluten Werts: QCC erweitert dort nicht vorzeichenrichtig,
  das ist aber ein vorbestehender, allgemeiner Mangel — ANSI liefert exakt
  dasselbe. Ein absoluter Sollwert hätte einen fremden Fehler als K&R-
  Erwartung festgeschrieben.

  Nebenbefund dabei: `AST_MAX_RULES` stand im Codegen noch auf **256**,
  während `MAX_RULES` im Parser längst 512 war. Die Grammatik lief mit 254
  Regeln also zwei Regeln vor eine Wand, und beim Überschreiten kam nur
  „kein AST vorhanden (leer oder Ueberlauf)" ohne Angabe, welche Grenze riss.
  Beide Zahlen stehen jetzt auf 512, und jeder der drei Überläufe meldet sich
  einzeln mit Namen. In allen vier Kopien (`src/`, `Source/`, `src-qcc/`,
  `SourceQCC/`).

- **Verkettete Member-Zugriffe** (`a.n->v`, `p->a->v`) — 2026-09-16, lesend
  und schreibend, 51 Fälle auf echtem 68030.

### OFFEN und neu entdeckt: QCCs struct-Layout ist NICHT ABI-gleich zu xcc

Beim Layout für eingebettete structs gegen `xcc` gemessen (17.09.2026,
`xcc -e=be` und die Bytereservierungen im erzeugten Assembler gelesen):

| Struktur | xcc | QCC |
|---|---|---|
| `struct{char c; int i;}` | **6** (`i` bei 2) | **8** (`i` bei 4) |
| `struct{short s; int i;}` | 6 | 8 |
| `struct{char a,b,c;}` | 3 | 4 |
| `struct{char c; int i; char d;}` | 8 | 12 |
| `struct{char a; char b;}` | 2 | 4 |

**Die Microware-68k-Regel ist: Ausrichtung höchstens 2** — für *jeden* Typ,
auch `int` und `long`; die Gesamtgröße wird auf die Ausrichtung der Struktur
gerundet (1 bei reinen `char`-Strukturen), nicht auf 4.

Genau diese Erkenntnis steckt in QCC schon einmal drin — aber **nur für
`double`** (der Kommentar am Layout nennt `struct{char c; double d;}` = 10
Byte). Sie wurde nie verallgemeinert. Dadurch weicht **jede gemischte
Struktur** ab, nicht erst die verschachtelte.

**Tragweite:** überall dort, wo QCC-Code Strukturen mit xcc-übersetztem Code
oder mit OS-9-Kernel-Datenstrukturen teilt. Rein QCC-interner Code ist in
sich konsistent und daher unauffällig — das erklärt, warum es bisher nicht
aufgefallen ist.

**Bewusst NICHT in einem Aufwasch mitgeändert:** das ist eine ABI-Änderung an
*allen* Strukturen, sie verschiebt Offsets in jedem erzeugten Modul und
verlangt eine eigene Verifikation (Selbsthost, `qclib`, alle 68k-Tests). Beim
eingebetteten struct wird deshalb die **bestehende** QCC-Regel fortgeschrieben
— zwei verschiedene Regeln innerhalb einer Struktur wären schlimmer als eine
durchgängig eigene. Eigener Arbeitsschritt.

### Im 68k-Backend

`extern int g;` (externe **Variable**) wird vom Frontend angenommen, aber
nicht übersetzt. Externe **Funktionen** gehen.

### Steht (auf beiden Backends geprüft)

Alle Ganzzahltypen bis `long` samt `unsigned`, `double`, `enum`, `union`,
`typedef`, `const`/`volatile`, `static` in beiden Bedeutungen, `auto`.
2D/3D-Arrays samt verschachtelter Initialisierer, Zeiger auf Zeiger, Arrays
von Zeigern, Funktionszeiger (68k), **selbstreferenzielle structs und damit
verkettete Listen**, struct-Zuweisung/-Parameter/-Rückgabe, struct-Array-
Felder, `->`. Alle Anweisungen inklusive `goto`, echtem `switch`-Fallthrough,
`continue`, `do-while`. Rekursion und gegenseitige Rekursion. Alle Operatoren
außer unärem Plus. **Prototypen ohne Parameternamen** (`int f(int);`).

### Präprozessor

`qcpp` ist vollständig: objekt- und funktionsartige Makros, `#include`,
`#if`/`#ifdef`/`#else`/`#endif`, `#undef`, `##` und `#`.

### Standardbibliothek — die größte Einzelbaustelle

`qclib` hat eine **eigene Namenswelt** statt der C89-Namen: `qf_*` (Dateien),
`qm_*` (Speicher), `qp_*`/`printf_a` (Ausgabe), `qs_*` (Strings). Die
einzigen C89-Namen sind `isalpha`/`isalnum`/`isspace`/`isprint`.

Für ISO C89 fehlen damit praktisch alle Standardheader — `<stdio.h>`,
`<stdlib.h>`, `<string.h>`, `<math.h>`, `<time.h>`, `<setjmp.h>`,
`<signal.h>`, `<assert.h>`, `<limits.h>`, `<float.h>`, `<stddef.h>`,
`<stdarg.h>`, `<locale.h>`, `<errno.h>`. Die vorhandenen rund 25 Funktionen
decken die Grundbedürfnisse ab, aber nicht die Norm.

---

## Zieldefinition

„Kompletter ISO-Compiler“ muss zuerst auf eine Sprachversion und ein Zielsystem
festgelegt werden. Für die Planung verwenden wir zunächst **ISO C17** als
vollständige Sprachbasis. C23 wird danach als eigener Erweiterungsblock behandelt.

Ein Compiler besteht dabei aus mindestens vier getrennten Bereichen:

1. Übersetzung der C-Sprache (Frontend und Codegenerator)
2. Präprozessor und Übersetzungseinheiten
3. ABI, Linker-Anbindung und Ziel-Runtime
4. C-Standardbibliothek

QCC deckt bisher nur einen kleinen, ausführbaren Kern von Bereich 1 ab.

## Legende

- **erledigt**: im aktuellen QCC-Pfad implementiert und getestet
- **teilweise**: Grundfunktion vorhanden, ISO-Semantik noch unvollständig
- **offen**: noch nicht implementiert
- **separat**: gehört primär in Präprozessor, ABI oder Bibliothek

## 1. Lexik und Präprozessor

| Thema | Status | Priorität |
|---|---|---|
| Kommentare und Whitespace | erledigt | — |
| Integer-, Zeichen- und Bool-Literale | teilweise | hoch |
| String-Literale und Escape-Sequenzen | offen | hoch |
| Floating-Literale und Fließkommatypen | Literale offen, werden aber seit 2026-09-16 gemeldet statt still verschluckt (s. `FLOAT_PLAN_de.md`) | hoch |
| vollständige Tokenregeln/Zeichensätze | teilweise | hoch |
| `#include`, Makros, bedingte Übersetzung | offen | sehr hoch |
| `#define` mit Parametern und `##`/`#` | offen | hoch |
| `#pragma`, vordefinierte Makros, Include-Suche | offen | mittel |

## 2. Typen und Deklaratoren

| Thema | Status | Priorität |
|---|---|---|
| `char`, signed/unsigned Integer | teilweise | hoch |
| `short`, `long`, `long long` | offen | hoch |
| `_Bool` und Qualifizierer | teilweise/offen | hoch |
| `float`, `double`, `long double` | `double` seit 2026-09-16 als Typ deklarierbar (`sizeof` = 8, Struct-Layout gegen xcc gemessen); gerechnet wird damit noch nicht. `float`/`long double` offen | hoch |
| Pointer und Pointerarithmetik | erledigt (Ausnahme aus Nachtrag 2026-09-08 seit 2026-09-09 behoben, s. dort) | — |
| Arrays und Array-Decay | teilweise | sehr hoch |
| Funktionspointer | offen | hoch |
| `void` und `void *` | offen | hoch |
| `struct`, `union`, `enum` | teilweise (struct mit gemischten skalaren Feldtypen erledigt 2026-07-24, `enum` erledigt; `union`, Array-/Pointer-Felder und verschachtelte structs offen) | sehr hoch |

**Nachtrag 2026-09-15 -- Zeigertabellen mit String-Literalen
(`char *tab[] = {"a","b"}`).** Bis dahin ein STILLER Parse-Abbruch: `FAIL`,
0 Meldungen, keine Zeilenangabe. Das Idiom traegt Namens-, Opcode- und
Meldungstabellen und musste in den eigenen Werkzeugen umgangen werden.

Der Kern war nicht die Grammatik, sondern die DATENDARSTELLUNG: der Wert
einer solchen Tabelle ist eine ADRESSE, und die steht erst zur Ladezeit fest.
Laut OS-9-Handbuch ist ein `dc.l <label>` im psect KEINE relokierte Adresse;
der einzige eingebaute Mechanismus ist `M$IRefs`/`F$Fork`, und der gilt nur
fuer initialisierte Zeiger in einem (nicht-remoten) **vsect**.

Umgesetzt als neuer IR-Opcode **`GINITADDR <name> <idx> <symbol>`**. Das
Backend legt Globals mit so einem Initialisierer in einen initialisierten
vsect und gibt `dc.l <symbol>` aus; der ZUGRIFF ist derselbe wie beim vsect
remote (a6-relativ), weshalb `globalInDataArea()` darueber entscheidet und
nicht `globalRemote()` -- zwei Gruende, derselbe Weg, aber verschiedene
Ausgabe. Auf ARM64 genuegt ein `.quad _sym` in `__DATA`, dort relokiert der
Linker selbst.

**Vorher durch BEIDE Binder geprueft** (die Lehre der 400-KB-Probe, bei der
ql68 allein getaeuscht hat): qr68 erzeugt fuer `dc.l <symbol>` im vsect
byteidentisches ROF wie r68, und ql68 baut daraus dasselbe Modul wie l68 --
`M$IData` mit den Zeigern, `M$IRefs` mit beiden Offsets angemeldet.

Mitgekommen ist die **Groesse aus der Liste** (`int a[] = {1,2,3}` und
`char *t[] = {...}`): vorher scheiterte die Deklaration an
`initCount > arrayLen` (arrayLen war noch 0) und die Variable wurde gar nicht
registriert -- sichtbar wurde das erst als "unknown variable" an der
BENUTZUNGSSTELLE.

Geprueft: Suite 208 ok / 0 FAIL inkl. nativem ARM64-Lauf,
`Q9-BACKEND-68K/q9-qclib/tests/ptrtab68k.sh` auf echtem 68030 (der Test
liest zusaetzlich M$IRefs aus dem Modul, damit ein Modul ohne Relokation
nicht als gruen durchgeht), Selbsthost-Fixpunkt und die volle eigene Kette
unveraendert.

**Nachtrag 2026-09-15 -- die Zeigergroesse ist nicht mehr festverdrahtet.**
Bis dahin belegte ein Zeiger im Struct-Layout IMMER acht Byte (`TC_PTR_SLOT`),
damit EIN frontend-berechnetes Offset fuer 68k (4 Byte) und ARM64 (8 Byte)
zugleich gilt -- auf dem 68k war damit die Haelfte jedes Zeigerfelds
verschenkt, und `sizeof` auf einen Zeigertyp musste abgelehnt werden, weil das
Frontend nur EINE Zahl schreiben konnte.

Das Frontend rechnet jedes Layout jetzt ZWEIMAL, einmal je Zeigergroesse, und
gibt Offsets und Groessen als **`k+nP`** aus: `k` ist der zeigerfreie Anteil in
Byte, `n` die Zahl der Zeigergroessen darin. Jeder Konsument setzt sein eigenes
`P` ein -- `qir68k` 4, `qirarm64` 8, `qccvm.py` 8. Die IR bleibt damit fuer
beide Ziele dieselbe; das Frontend ist NICHT zielabhaengig geworden.

Aufgeloest wird beim **Einlesen** der IR-Zeile (`argIntern` in den Backends,
`parse_ir` in `qccvm.py`), nicht an den Verwendungsstellen: sonst muesste jedes
`number(args[i])` davon wissen, und eine vergessene Stelle waere ein stiller
Rechenfehler statt eines Abbruchs.

**Die Linearitaet wird nicht angenommen, sondern nachgerechnet.** Die
Ausrichtung rundet auf, und eine Rundung ist keine lineare Funktion von `P`;
fuer die erlaubten Feldtypen geht es auf, aber `tcPtrLinear()` prueft es je
Feld und meldet eine Verletzung, statt still ein falsches Offset zu liefern.

**Folge fuer die Blockkopie:** sie war byteweise entrollt (acht IR-Zeilen je
Byte) und braucht dafuer eine Zahl. Wo die Groesse einen Zeigeranteil hat,
erzeugt das Frontend jetzt eine Laufzeitschleife aus vorhandenen Opcodes; wo
sie zeigerfrei ist, bleibt es beim Entrollen -- die Schleife kostet zwei Labels
je Kopie, und QCCs eigener Parser kopiert 1340 zeigerfreie structs, womit
`qr68` ueber `SYM_MAX` lief (Zielbuild: 4096 Symbole).

**Gemessen:** `ActionLogEntry {int id; const char* start; const char* end;}`
faellt auf dem 68k von 24 auf 12 Byte, das Aktionslog des Parsers damit von
6.291.456 auf 3.145.728 Byte -- genau der Preis, der am 2026-09-07 gemessen
und bewusst hingenommen wurde. Verifiziert: Suite 207 ok / 0 FAIL,
`tools/test_struct_68k.sh` 38/38 auf echtem 68030, Selbsthost-Fixpunkt mit
byteidentischer IR, und die vollstaendig eigene Kette (qcpp, qcc, qr68, ql68,
qclib) gruen.

**Nachtrag 2026-09-08 — mehrere Zeiger-Deklaratoren in EINER Anweisung
sind ein STILLER Abbruch.** `char *a, *b;` (mit oder ohne `const`, dritter
oder mehr Deklaratoren, immer dasselbe Bild) gibt `FAIL` ohne jede
Meldung — `const char *a;` (EIN Zeiger) geht, `int a, b;` (mehrere
NICHT-Zeiger-Deklaratoren) geht, nur die Kombination bricht. Gefunden
beim Bau des Peephole-Optimierers (`Source/qcc_backend_peephole.c`),
Umgehung dort: je ein eigener Deklarator pro Zeile statt einer
gemeinsamen Anweisung.

**Nachtrag 2026-09-09 — BEHOBEN, und zwar an DREI Stellen, nicht nur der
oben gefundenen.** `pointerDecl` stand in `varDecl`/`structField`/
`plainGlobalDecl` bis eben je EINMAL vor der ganzen Deklaratorliste statt
vor jedem einzelnen Deklarator — echtes C haengt den Stern an den
Deklarator, nicht an den gemeinsamen Typ. Bei Lokalen und Struct-Feldern
war das der oben beschriebene stille Parse-Fehler; bei GLOBALEN Variablen
(eigener Rohtext-Mechanismus in `tc_globalend`/`tcGlobalOne`, da
`Data/qcc_p.c` mehrere Deklaratoren pro Zeile dort schon fuer
Nicht-Zeiger-Faelle wie `static TCType a[512], b[512][64];` unterstuetzte)
war der Bug NIE ein Parse-Fehler, sondern STILL FALSCHER Code: der fuer
weitere Deklaratoren wiederverwendete Typ-Praefix schleppte den Stern des
ERSTEN Deklarators mit, `char *a, *b;` wurde zu `char **b` (Doppelzeiger)
verfaelscht, `char* a, b;` machte das eigentlich nicht-zeigende `b`
faelschlich ZUM Zeiger. Fix: `pointerDecl` jetzt Teil von
`varDeclarator`/`structDeclarator`/`globalDeclarator`; `tc_pointerdecl`
SETZT den Zeigergrad pro Aufruf neu (Basis `tcBasePointers` aus dem Typ,
z. B. einem Zeiger-`typedef`) statt ihn aufzuaddieren; `tc_globalend`
filtert Sterne beim Kopieren des wiederverwendeten Praefixes jetzt heraus
(mit erzwungenem Trenner-Leerzeichen, falls Typwort und Stern im
Originaltext ohne Leerraum aneinanderstiessen, z. B. `char** p, q;`).
Fuenf neue `tc_check`-Faelle (rein und gemischt, lokal/Struct-Feld/global)
in `runtests.sh`, volle Suite inkl. Abgleichtest weiterhin gruen.

**Nachtrag 2026-09-07 — Zeigerarrays als Strukturfeld gehen jetzt.**
`char* args[6]` in einer Struct war bis dahin abgelehnt („pointer arrays as
struct field not supported in this version"). Gebraucht hat es
`qcc_backend_c.cpp`: seine `Instr`-Struktur hält die Argumente einer
IR-Anweisung so, mit 106 Zugriffsstellen. Solange das fehlte, war das
Backend nicht selbst übersetzbar und konnte deshalb **nie auf dem 68030
laufen** — das einzige Glied der Kette mit dieser Lücke.

Der Kern war die **Schrittweite**: `IPADD` skaliert mit der Größe des
Typtags, und ein Zeiger ist auf dem 68k vier Byte — im Struct belegt er
aber acht (`TC_PTR_SLOT`, damit dasselbe Offset auch für ARM64 stimmt).
Für Zeigerarrays wird der Schritt deshalb in **Byte** angegeben
(`IPADDN 8`), dasselbe Mittel, das die 2D-Zeilen schon nutzten. Die Regel
steht in *einer* Funktion (`tcEmitFieldIndexStep`) statt an den sechs
Emissionsstellen — drei lesend, drei schreibend.

**Nachtrag 2026-09-07, zweite Runde — Indizierung *durch* ein skalares
Zeigerfeld geht jetzt auch** (`s.ptr[j]`, `sp->ptr[j]`, lesend und
schreibend, lokal und global). Vorher an sechs Emissionsstellen abgelehnt.
Der Kern: die Aufrufer haben nur die **Adresse des Feldes** auf dem
Stapel, gebraucht wird aber der Zeiger *im* Feld — also erst `LOADIND p`,
dann der Indexschritt. Die Regel steht in `tcEmitPtrFieldIndex`, einer
Funktion für alle sechs Stellen.

**Eine siebte Stelle bleibt abgelehnt:** nach einem Funktionsaufruf
(`f().feld[i]`) gilt eine andere Stapelordnung — dort steht `PADD` mit
vertauschten Operanden statt `IPADD` —, und dafür gibt es keinen Aufrufer.
Eine zweite Ordnung nebenher wäre die nächste Fehlerquelle.

**`arr[i].feld[j]`/`ptr[i].feld[j]` BEHOBEN (08.09.2026).** Structschritt
und Feldschritt in einem Ausdruck brauchten genau den zweiten
Index-Scratch, der hier vorher als offenes Vorhaben stand —
`tcStashChainedIndex`/`tcEmitStashedFieldIndex`, acht Emissionsstellen
(lokal/global x Array-von-structs/Pointer-auf-struct x lesend/
schreibend). Einzelheiten im Nachtrag weiter unten bei `s.t[i][j]`.
Weiterhin kein Aufrufer, also weiterhin nicht umgesetzt: zweidimensionale
Zeigerarrays als Feld.

**Nachtrag 2026-09-07 — ein STILLER Falschcode-Fehler bei `&arr[i]`,
behoben.** `tc_addressref` emittierte für die Adresse eines
Struct-Array-Elements `PTRINDEX` mit dem Typtag, und `tcTypeTag` gibt für
eine Struct `'i'` — also **vier Byte Schrittweite** statt der
Strukturgröße. Bei einer 80 Byte großen Struct zeigte `&arr[1]` vier Byte
hinter `arr[0]`. Aufgefallen ist es erst am Backend auf dem 68030: es holt
seine Anweisungen mit `insP = &ir[irCount]`, und alle landeten
übereinander — sichtbar als ein `op`-Feld `FUNCLOADPUSHCMPLJZ` aus je vier
Zeichen. Im Testbestand kam **kein einziges `&arr[i]` auf ein
Struct-Array** vor; jetzt prüfen es die Fälle 27 und 28 in
`tools/test_struct_68k.sh`, und ihre Sollwerte diskriminieren (mit falscher
Schrittweite bleiben sie auf 0).

**Nachtrag 2026-09-07 — ein zweiter stiller Falschcode-Fehler, behoben:**
eine ganze Struct über einen **Zeiger** zu kopieren
(`struct S *p; v = p[i];`) erzeugte `LOADP / PTRINDEX i / LOADIND i` —
vier Byte Schrittweite *und* ein `LOADIND`, wo die Adresse gebraucht wird;
damit landete ein Datenwert als Quelladresse in A0. Dieselbe Bauform war
für den ARRAY-Fall (`v = arr[i]`) am 2026-09-01 repariert worden, der
Zeigerfall blieb dabei stehen — gefunden erst, als beim Nachmessen der
`&arr[i]`-Sache auch die Nachbarstellen geprüft wurden. Jetzt
`LOADP / IPADDN <Größe>` ohne `LOADIND`: bei einer Struct **ist** die
Adresse der Wert. Fall 35 in `tools/test_struct_68k.sh`.

**Lehre daraus, für die nächste Änderung dieser Art:** eine Regel über
Schrittweiten gilt nie nur an *einer* Stelle. Beim `&arr[i]`-Fund waren es
zwei Emissionsstellen, bei den Zeigerarrays sechs, bei der Indizierung
durch ein Zeigerfeld wieder sechs — und der Zeigerfall von `v = p[i]` ist
1:1 der Array-Fall von vor sechs Tagen. Wer eine solche Stelle anfasst,
sucht die Geschwister mit.

**Nachtrag 2026-09-07 — `const` am Strukturfeld wurde geparst und
verworfen; BEHOBEN am selben Tag.** Vier Formen liefen **still** durch,
während dasselbe bei einer *Variablen* korrekt gemeldet wird:

| | vorher | jetzt |
|---|---|---|
| `s.cp[0] = …` bei `const char *cp` | still durch | `cannot assign through pointer to const` |
| `p->cp[0] = …` | still durch | dito |
| `s.n = 1` bei `const int n` | still durch | `cannot assign to const struct field` |
| `p->n = 1` | still durch | dito |

Die Grammatik nannte den Grund selbst: `fieldConstKw` war eine
**aktionslose** Kopie von `constKw`, denn ein Verweis auf `constKw` löst
`tc_const` aus, dessen `tcPendingConst` erst beim nächsten Parameter oder
Lokalen konsumiert wird — dort erzwänge es fälschlich Konstantheit. Der
Ausweg war richtig, die Folge nicht.

Jetzt hat `fieldConstKw` eine **eigene** Aktion mit einer **eigenen**
Flagge, die nur Felder betrifft: `tc_fieldconst` setzt sie, die
Deklaratoren der Zeile verbrauchen sie (`const int a, b;` trifft **beide**,
nachgemessen), und `tc_fieldconstend` löscht sie nach der ganzen
`structField`-Zeile. `tcPendingConst` bleibt unberührt.

Unterschieden wird wie bei Variablen (`tc_local`): bei einem **Zeiger**
macht `const` den *Pointee* konstant (`pointeeConst` im Feldtyp, geprüft in
`tcEmitPtrFieldIndex` und nur beim Schreiben), bei allem anderen das
**Feld** selbst (`tcStructFieldConst`, geprüft an den **sieben**
Schreibstellen in `tc_target` — nicht drei; die drei mit Index-Zweig sind
nur eine Teilmenge).

**Was ausdrücklich erlaubt bleibt** (alles nachgemessen): `s.cp = b` — nur
der Pointee ist konstant, der Zeiger selbst nicht; Lesen eines
const-Felds; Lesen *durch* ein const-Zeigerfeld; das Nicht-const-Nachbar­
feld; und die nächste Zeile bzw. die nächste `struct` werden nicht
angesteckt. QCCs eigener Parser hängt an genau einer dieser Formen
(`actionLog[i].start = start` bei `const char* start`) — deshalb vor dem
Selbsthost einzeln geprüft.

**Nachtrag 2026-09-07 — ein 2D-Array als Strukturfeld scheiterte STILL.
Der Zugriff `feld[i][j]` selbst ist seit 08.09.2026 UMGESETZT** (s.
Nachtrag am Ende dieses Absatzes; die Deklaration/Meldung unten war der
erste Schritt davon).
Gewöhnliche 2D- und 3D-Arrays gehen; ein 2D-Array *im struct*
(`struct S { char t[4][8]; }`) verhielt sich so:

| Form | vorher | jetzt |
|---|---|---|
| Deklaration, `sizeof` | geht | geht |
| `z = sp->t[i]` (Zeiger auf Zeile i) | geht | geht |
| `s.t[i]` über den Punkt | gemeldet („only supported via `->`") | unverändert |
| **`s.t[i][j]` / `sp->t[i][j]`** | **stilles `FAIL`, keine Meldung** | **gemeldet** |

Die Ursache war die Grammatik: `member [ index ]` — also genau **ein**
optionaler Index nach dem Feld. Zwei Indizes konnte sie nicht lesen, und
ein Parse-Abbruch hat keine Meldung. Jetzt steht dort
`member [ index { index } ]`, **nicht** um die Form zu können, sondern um
sie ablehnen zu können: `chained indexing of a struct field (field[i][j])
not supported in this version`.

**Umgesetzt ist sie weiterhin nicht** — dafür bräuchte es zwei Indizes in
*einem* Ausdruck, also einen zweiten Index-Scratch nach dem Muster von
`tcEmitPointerIndexChain`. Das ist **dieselbe Maschinerie**, die auch
`arr[i].feld[j]` braucht: ein Umbau würde beide Lücken schließen.

**Nachtrag 2026-09-08 — UMGESETZT.** Der Umbau von oben schließt jetzt
beide Lücken: `feld[i][j]` (zweidimensionales Array-Feld, sechs
Emissionsstellen — Punkt lokal/global, Pfeil) und `arr[i].feld[j]`/
`ptr[i].feld[j]` (eindimensionales Array-Feld hinter einer
Array-von-structs- bzw. Pointer-Indizierung, acht Emissionsstellen).
Kern: der ZWEITE (zuletzt gepushte) Indexwert wird mit
`tcStashChainedIndex()` in einen Scratch-Global zwischengelagert,
während dazwischenliegender Code (Feld- bzw. Array-Elementadresse) den
ERSTEN konsumiert — dieselbe Technik wie `tcEmitPointerIndexChain`
(Scratch-Global statt Stack-Rotation, die die IR nicht kennt), hier auf
genau einen gemerkten Wert vereinfacht, weil an beiden Stellen nie mehr
als zwei Indexebenen vorkommen können (ein Feld ist höchstens 2D,
structs schachteln nicht). Zwei neue Hilfsfunktionen tragen die Regel:
`tcEmitFieldRowColIndex` (2D-Feld, Zeilen- dann Spaltenschritt) und
`tcEmitStashedFieldIndex` (1D-Feld hinter arr[i]/ptr[i]). Verifiziert
über QCCVM mit echten Werten (lesend und schreibend, lokal/global, alle
acht bzw. sechs Stellen), volle Regressionssuite grün, BEIDE
Selbsthost-Fixpunkte (r68/l68 und qr68/ql68/qclib) unverändert
byteidentisch nach der Änderung an `tc_varref`/`tc_target`.

**Nachtrag 2026-09-07 — die Verschachtelungsgrenze war um vier zu knapp.**
`TC_MAX_CTRL` (vorher das Literal 64 an sechs Stellen) ist jetzt 128.
Gemessen: `qcc_backend_c.cpp` braucht **68** — seine Opcode-Verteilung ist
eine lange `else if`-Kette, und jedes Glied ist im Modell eine Ebene tiefer.
Mit 67 kippt es, mit 68 läuft es durch. Zu tief zu schachteln bleibt eine
echte Grenze *mit Meldung*.
| `typedef` | erledigt (Skalar-/Pointer-Aliase, `typedef struct Name Alias;`, `typedef struct { ... } Name;` anonym inline seit 2026-07-24) | — |
| Bitfelder und `_Alignas`/`_Alignof` | offen | mittel |
| variable length arrays | offen | mittel |

## 3. Ausdrücke und Operatoren

| Thema | Status | Priorität |
|---|---|---|
| arithmetische Operatoren | erledigt | — |
| Vergleiche und Gleichheit | erledigt | — |
| `&&`, `||` mit Kurzschluss | erledigt | — |
| bitweise Operatoren und Shifts | erledigt | — |
| Zuweisungen und kombinierte Zuweisungen | erledigt | — |
| Prä-/Postinkrement und -dekrement | erledigt (nur einfache int/unsigned/char-Skalare) | — |
| Casts und implizite Konversionen | teilweise (nur int/unsigned/char/bool, kein Pointer/typedef als Cast-Ziel) | sehr hoch |
| `sizeof` und `_Alignof` | teilweise (`sizeof` auf int/char/bool/unsigned/struct, keine Pointer, kein `_Alignof`) | hoch |

**Nachtrag 2026-09-07, gemessen.** Zwei Dinge, die auseinanderzuhalten sind:

**1. Das 8-Byte-Layout für Zeigerfelder ist ABSICHT, kein Mangel.**
`Data/qcc.lextab` begründet es an der Stelle selbst (2026-07-25): ein
Zeigerfeld belegt *immer* 8 Byte, damit **ein einzelnes
frontend-berechnetes Offset für beide Backends gültig bleibt** — 68k-Zeiger
sind 4 Byte, ARM64-Zeiger 8, und dieselbe IR wird von beiden verarbeitet
(`Source/qcc_arm64_backend.cpp` ist verzeichnet). Folge: ein
`struct { int id; const char *start; const char *end; }` ist **24** Byte
groß, nicht 12, und `sizeof` liefert konsequenterweise 24. Das *nur* in
`sizeof` auf 12 zu ändern wäre schlimmer als der Status quo — ein
`n * sizeof(eintrag)` würde dann zu wenig anfordern.

Der Preis ist messbar: QCCs erzeugter Parser braucht für sein Aktions-Log
**6 291 456 statt 3 145 728 Byte** (262 144 Einträge). Aufgefallen beim
Ziellauf gegen `qclib`, wo diese Anforderung in `E$NoRAM` lief. Wer den
Speicher halbieren will, muss das **Frontend zielabhängig** machen (etwa
ein `-m32`), und dann ist die IR nicht mehr für beide Backends dieselbe.
Das ist eine Architekturentscheidung, keine Fehlerbehebung.

**2. `sizeof` auf einen Zeigertyp log SCHWEIGEND — behoben 2026-09-07.**
`sizeof(char *)` ergab `PUSH 1`, also die Größe von `char`, mit
Schlusswort `OK` und **ohne jede Meldung**. Die Unterstützung fehlt
bewusst (siehe Zeile oben), aber `tc_sizeof` *wollte* das melden — der
Zweig war nur nie erreichbar: `tc_sizeof` ruft `tc_type` für dieselbe
Spanne noch einmal auf, und `TC_SET_CURRENT` setzt `pointers` dabei auf 0
zurück, obwohl die Aktion an `pointerDecl` vorher schon gezählt hatte.
`tc_sizeof` zählt den Zeigergrad jetzt selbst. Ein
`malloc(n * sizeof(char*))` bekam vorher ein Viertel des Nötigen.

## 4. Anweisungen und Funktionen

| Thema | Status | Priorität |
|---|---|---|
| Ausdrucksanweisungen, Block, `if/else`, `while` | erledigt | — |
| `for` und `do/while` | erledigt | — |
| `switch`, `case`, `default` | teilweise (gestapelte Case-Label, aber KEIN Fallthrough mit Code zwischen Bodies) | hoch |
| `break` und `continue` | erledigt | — |
| `goto` und Labels | offen | mittel |
| `return` | teilweise | hoch |
| Funktionsdefinitionen und Parameter | erledigt | — |
| Prototypen und separate Deklarationen | teilweise | sehr hoch |
| `inline`, `_Noreturn`, variadische Funktionen | offen | mittel |

**Nachtrag 2026-09-07 — `(void)` als Parameterliste geht in der DEFINITION,
aber nicht in einer `extern`-Deklaration. BEHOBEN 2026-09-08.** Gemessen:

| | vorher | jetzt |
|---|---|---|
| `extern int f(void); f();` | `wrong argument count (expected 1, got 0)` | geht |
| `extern int f(); f();` | geht | geht |
| `int f(void){ … }` (Definition) | geht | geht |
| `typedef int (*fp)(void); fp p; p();` | dieselbe Luecke | geht |

Ursache: `type` (in `externParam`) schliesst `"void"` als eigenen Typ ein,
also las `(void)` als EIN Parameter vom Typ `void`. `funcParams` kannte das
Problem schon und loeste es mit zwei echten NTS-Alternativen
(`voidParams | normalParams`, Klammern JEWEILS im Zweig, s. Kommentar dort
zur Backtracking-Falle bei `"void* p"`). `externDecl` und `fnPtrTypedef`
teilten sich bis dahin nur `externParamList` OHNE diese Absicherung — beide
haengen an derselben Regel und hatten deshalb dieselbe Luecke (Geschwister-
Suche, s. `feedback_schrittweiten`). Fix: neue `externParams`/`fnPtrParams`
mit `voidParams | externRealParams`, `voidParams` dabei aus `funcParams`
WIEDERVERWENDET (deren Aktion ist bereits leer). Sechs neue Faelle in
`runtests.sh` (Q9-Parsec), inkl. der `void*`-Backtracking-Probe.

## 5. Übersetzungseinheiten und Semantik

| Thema | Status | Priorität |
|---|---|---|
| lokale/globale Objekte | teilweise | sehr hoch |
| Speicherklassen `static`, `extern`, `register`, `_Thread_local` | offen | hoch |
| Sichtbarkeitsbereiche und Namensräume | teilweise | sehr hoch |
| Linkage über mehrere Dateien | offen | sehr hoch |
| Initialisierer für Aggregate | teilweise | hoch |
| konstante und nichtkonstante globale Initialisierer | offen | hoch |
| Diagnose von Constraint-Verletzungen | teilweise | sehr hoch |
| Übersetzung in mehreren Phasen | offen | hoch |
| definiertes Verhalten vs. Implementation-defined/undefined | offen | sehr hoch |

## 6. Codegenerator, ABI und Runtime

| Thema | Status | Priorität |
|---|---|---|
| Stack-IR und QCCVM | erledigt | — |
| 68000-Backend und Simulator | funktionsfähig | — |
| ARM64/Darwin-Backend | funktionsfähig | — |
| vollständiges Ziel-ABI für ein Betriebssystem | teilweise | sehr hoch |
| Register-/Stack-Calling-Convention für alle C-Typen | teilweise | sehr hoch |
| Struct-/Union-Rückgabe und -Parameter | offen | sehr hoch |
| Floating-Point-Codegen | offen | hoch |
| Linker-/Objektformat-Anbindung | offen | sehr hoch |
| Debug-Informationen | offen | niedrig |
| Optimierung und Codequalität | offen | mittel |

## 7. ISO-C-Standardbibliothek

Die Bibliothek ist ein eigenes Projektpaket und darf nicht mit dem Parserumfang
verwechselt werden. Für einen praktisch nutzbaren Compiler werden mindestens
benötigt:

- `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`, `<limits.h>`
- `<stdio.h>` und `<stdlib.h>`
- `<string.h>` und `<ctype.h>`
- `<assert.h>`, `<errno.h>`, `<locale.h>`
- `<math.h>` inklusive Floating-Point-Runtime
- `<time.h>`
- später `<signal.h>`, `<setjmp.h>`, `<threads.h>` und weitere optionale Teile

Der genaue Umfang hängt vom Zielsystem ab. Für Q9 wäre zunächst eine kleine
zielsystemspezifische Runtime sinnvoll, nicht sofort die komplette Bibliothek.

## Empfohlene Ausbaustufen

### Stufe A: brauchbares C-Subset

`for`, `do/while`, `break`, `continue`, `typedef` inkl. `typedef struct { ... }
Name;` anonym inline (erledigt, 2026-07-24),
`struct` mit gemischten skalaren Feldtypen (erledigt, 2026-07-24; Array-Felder,
Pointer-Felder und verschachtelte structs noch offen, siehe
SELFHOSTING_GAP_LIST_de.md), Prä-/Postinkrement (erledigt,
2026-07-23), `sizeof` auf Basistypen/struct (erledigt, 2026-07-23; auf Pointer
weiterhin offen), noch offen: Casts, Funktionsprototypen, `enum`-Typsicherheit
(Konstanten sind erledigt, aber ohne eigenen Typ) und ein robuster Präprozessor.

### Stufe B: C17-Sprachkern

Alle Standardtypen, Konversionen, Qualifizierer, Speicherklassen, mehrere
Übersetzungseinheiten, vollständige Initialisierer, Funktionspointer und ein
festgelegtes ABI einschließlich Structs und Floating Point.

### Stufe C: Zielsystem und Bibliothek

Linker-/Objektformat, Startcode, Speicherverwaltung, I/O und schrittweise die
C-Standardbibliothek. Erst hier wird aus dem Sprachcompiler ein benutzbares
Entwicklungssystem.

### Stufe D: C23

Erst nach C17: neue C23-Schlüsselwörter und Sprachregeln, Attribute, geänderte
Deklarationsmöglichkeiten, neue Bibliotheksbestandteile und die jeweilige
Implementierungsdokumentation.

## Empfehlung für das Projekt

Wir sollten nicht „ISO komplett“ als nächsten Einzelauftrag behandeln. Der
sinnvolle nächste Plan ist ein klarer **C17-Kern ohne Bibliotheksvollständigkeit**:

1. `for`/`do`, `break`/`continue`, Inkrement
2. `typedef`, Prototypen, `struct`/`enum`
3. Casts, Konversionen, `sizeof`, Qualifizierer
4. Präprozessor und mehrere Übersetzungseinheiten
5. ABI-/Runtime-Entscheidung für ein erstes echtes Zielsystem

Nach jeder Stufe wird der Umfang neu bewertet. So bleibt sichtbar, welche Arbeit
Sprachsemantik, welche Arbeit Backend und welche Arbeit Bibliothek ist.

