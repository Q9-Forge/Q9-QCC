//================================================================================
// qcc_backend_c.cpp -- reines-C-Gegenstueck zu qcc_backend.cpp
//
// Verhaltensgleicher Nachbau ohne STL/Exceptions/std::string: feste globale
// Tabellen + lineare Suche, im selben Stil wie parsec.cpp/codegen.cpp. Das
// Original (qcc_backend.cpp) bleibt unveraendert als Referenz liegen; siehe
// docs/SELFHOSTING_LUECKENLISTE.md Abschnitt 5 fuer den Hintergrund. Um auf die
// C++-Version zurueckzuschalten, in runtests.sh wieder qcc_backend.cpp bauen.
//================================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_LEN          24
#define ARG_LEN         64
#define NAME_LEN        64
#define LINE_LEN        512
#define MAX_ARGS        6
/* 2026-07-25: von 8192 erhoeht -- beim Skalierungstest fuer den -largedata-
   Funktionsaufruf-Schalter (a4/a2-Indirektionstabelle statt bsr) blockierte
   dieser Cap den Nachweis bei realistischer Groessenordnung (150 generierte
   Funktionen ergaben bereits >36000 IR-Zeilen). Bereits vorher als fatal()
   sauber/laut abgesichert (kein stiller Bug), nur zu knapp bemessen. */
#define MAX_IR_LINES    65536
#define MAX_FUNCS       256
/* 2026-07-25: von 256 erhoeht -- beim Skalierungstest fuer SourceQCC/
   codegen.tc selbst (genParser68kTo-Chunk) blockierte dieser Cap den
   Nachweis: JEDES String-Literal im QCC-Quelltext wird zu einem
   anonymen __strN-Global, und das kumulative Kompilat hat inzwischen weit
   ueber 256 solcher Literale (dazu die "echten" Globalen wie nodes[8192]).
   Bereits vorher als fatal() sauber/laut abgesichert (kein stiller Bug),
   nur zu knapp bemessen -- analog zum MAX_IR_LINES-Fund oben. */
#define MAX_GLOBALS     1024
#define MAX_ARRAY_LEN   4096

typedef struct {
	char op[OP_LEN];
	char args[MAX_ARGS][ARG_LEN];
	int argc;
	int line;
} Instr;

typedef struct {
	char name[NAME_LEN];
	int nargs, first, last, locals, frameBytes;
	/* Mehrdatei-Uebersetzung (2026-07-25): declOnly = per FUNCDECL registriert,
	   OHNE Rumpf in dieser Datei (definiert in einer anderen QCC-Datei) --
	   first/last/locals/frameBytes bleiben dann unbenutzt (0/-1). isStatic
	   steuert die Namensverfremdung (siehe mangledName()) -- r68/l68 kennen
	   KEIN Sichtbarkeitskonzept (siehe docs/STATUS.md), Mangling ist die einzige
	   Moeglichkeit, dass zwei Dateien denselben privaten Helfernamen frei
	   verwenden koennen, ohne dass l68 "duplicate symbol" meldet. */
	int declOnly, isStatic;
} Function;

typedef struct {
	char name[NAME_LEN];
	int initialValue;
	int isChar;
	int isArray;
	int length;
	int init[MAX_ARRAY_LEN];
	int hasGinit; /* 2026-07-25: mind. ein GINIT fuer dieses Array gesehen (siehe unten) */
	int declOnly, isStatic; /* siehe Function */
} Global;

static Instr ir[MAX_IR_LINES];
static int irCount = 0;

static Function funcs[MAX_FUNCS];
static int funcCount = 0;

static Global globals[MAX_GLOBALS];
static int globalCount = 0;

static void fatal(const char* msg); /* Definition weiter unten, hier nur fuer registerExtern()/externTableOffset() vorwaertsdeklariert */

/* 2026-07-26, live auf Q9 gefunden (siehe emitLeaGlobal()/emitCall()-Kommentar
   in qcc_backend_c.cpp): jeder CALLEXT/CALLEXTP-Aufruf ging bisher per rohem
   "bsr <rawname>" direkt an die externe clib.l-Funktion -- das zerstoert a3/a4
   (reine ABI-Temporaer-Register, siehe Ultra-C/C++ Processor Guide Table
   1-12), UND ein Versuch, a3/a4 direkt an der Aufrufstelle wieder aufzufrischen
   ("lea (pc)"), scheitert bei r68 mit "value out of range", sobald die
   Aufrufstelle mehr als 32 KB von tc_functab/tc_gadata entfernt liegt (die
   ganze a3/a4-Indirektion existiert ja GENAU wegen dieser Grenze). LOESUNG:
   jede ECHTE externe Funktion, die per CALLEXT/CALLEXTP gerufen wird, bekommt
   einen EIGENEN kleinen Wrapper-Stub ("tc_extwrap_<name>", physisch DIREKT
   neben tc_gadata platziert, siehe emitIR()) -- der Wrapper macht den echten
   "bsr <rawname>" (immer PC-relativ sicher, da er nah an allem anderen
   Fruehen liegt) und frischt DANACH a3/a4 auf (ebenfalls sicher, da der
   Wrapper selbst nah an den Tabellen liegt). Aufrufstellen rufen NICHT mehr
   direkt "bsr <rawname>", sondern den Wrapper -- ueber genau denselben
   a4-Tabellen-Indirektionsmechanismus wie interne QCC-Funktionen
   (emitCall()), der beliebige Entfernungen bereits beherrscht (Register-
   indirekter jsr, keine PC-relative Distanzgrenze). */
#define MAX_EXTERNS 128
static char externNames[MAX_EXTERNS][NAME_LEN];
static int externCount = 0;

static int findExtern(const char* name) {
	int i;
	for (i = 0; i < externCount; i++) if (strcmp(externNames[i], name) == 0) return i;
	return -1;
}

static int registerExtern(const char* name) {
	int idx = findExtern(name);
	if (idx >= 0) return idx;
	if (externCount >= MAX_EXTERNS) fatal("zu viele verschiedene externe Funktionen (CALLEXT/CALLEXTP)");
	strncpy(externNames[externCount], name, NAME_LEN - 1);
	return externCount++;
}

/* Tabellen-Offset EINES externen Wrappers, direkt NACH QCC-Funktionen und
   den 8 eingebauten Laufzeit-Helfern (siehe helperTableOffset()). */
static int externTableOffset(const char* name) {
	int idx = findExtern(name);
	if (idx < 0) fatal("interner Fehler: externe Funktion nicht registriert");
	return funcCount * 4 + 8 * 4 + idx * 4;
}

/* -os9: Microware-r68-Ausgabeformat statt vasm-kompatiblem "nacktem" Motorola-
   Format (siehe genParser68kTo in Source/codegen.cpp fuer denselben Trick beim
   Parser-Codegen -- dort empirisch verifiziert: r68 akzeptiert Label-Doppel-
   punkte und ";"-Endkommentare unveraendert, es braucht nur "*" statt ";" fuer
   VOLLE Kommentarzeilen sowie einen nam/psect/ends-Rahmen). Der eigentliche
   Instruktions-Codegen (emitIR-Dispatch weiter unten) ist DAHER GROESSTENTEILS
   fuer beide Formate identisch -- MIT EINER wichtigen Ausnahme: dem Frame-
   Pointer-Register (siehe framePtr() direkt unten). */
static int os9Mode = 0;
/* -part (2026-07-25, Mehrdatei-Uebersetzung): diese Datei ist EIN TEIL eines
   Mehrdatei-Programms, kein vollstaendiges Programm fuer sich -- die main/
   funcCount-Pflicht wird gelockert, siehe collectFunctions()/emitIR(). */
static int partMode = 0;
/* -runtime (2026-07-25, Mehrdatei-Uebersetzung): der 68k-Core (mul/div,
   emitM68kCore) sowie putint/putuint/putchar/tc_io_write + deren Scratch-
   Speicher (tc_extcall_tmp/tc_io_buf/tc_io_cnt) werden OHNE -part IMMER
   emittiert (Vollprogramm-Annahme, unveraendert). Unter -part wuerde JEDE
   Datei ihre EIGENE Kopie dieser Symbole mitbringen -- l68 lehnt das beim
   Linken zuverlaessig als "duplicate symbol" ab (siehe docs/STATUS.md,
   empirisch verifiziert). Deshalb: unter -part NUR emittieren, wenn
   zusaetzlich -runtime gesetzt ist -- GENAU EINE Datei im Mehrdatei-Programm
   traegt so den gemeinsamen Anker, alle anderen referenzieren ihn per
   undefiniertem Symbolverweis (vom Linker aufgeloest, wie jeder andere
   Cross-Datei-Aufruf auch). */
static int runtimeMode = 0;
/* -largedata (2026-07-25, "Speichermodell"-Schalter): siehe grosser Kommentar bei
   emitLeaGlobal() weiter unten -- Standardmodell adressiert jedes Globale
   AUSSCHLIESSLICH PC-relativ (echte 68000-Grenze: 16-Bit-Displacement, +-32 KB),
   dieser Schalter wechselt auf eine zusaetzliche Indirektionstabelle mit
   absoluten Adressen (vom Linker aufgeloest), die beliebig weit entfernte
   Globale erreichbar macht -- auf Kosten eines zusaetzlichen Speicherzugriffs
   pro Zugriff. */
static int largeDataMode = 0;
static char psectName[NAME_LEN] = "tc_prog";
static const char* fullCommentPrefix(void) { return os9Mode ? "*" : ";"; }
/* Register Use Table im Ultra-C/C++-Prozessorhandbuch (ultrac_pg.pdf, Kapitel
   "68K" -> "Register Usage"): a5 = Frame/local pointer, a6 = STATIC STORAGE
   POINTER (nicht Frame-Pointer!). Das echte cstart.r/clib.l nutzt a6 als
   Zeiger auf den eigenen statischen Datenbereich UEBER DIE GESAMTE LAUFZEIT
   des (mit unserem Code zu EINEM Modul zusammengelinkten) Programms -- wird
   dieser Wert von unserem eigenen Code ueberschrieben (was das Default-/vasm-
   Format mit "link a6,#N" tut), stuerzt jeder nachfolgende echte clib-Aufruf
   mit einem PMMU-Fehler ab (live am echten Q9 verifiziert, 2026-07-24: ein
   Testmodul, das a6 als eigenen Frame-Pointer benutzte UND anschliessend
   _os_write aufrief, brachte den Emulator zum Absturz -- a6 zeigte auf den
   eigenen Frame statt auf den echten statischen Datenbereich). Deshalb NUR im
   -os9-Modus a5 statt a6 als Frame-Pointer verwenden (a6 bleibt dann komplett
   unangetastet); das Default-/vasm-Format bleibt bei a6 (keine Notwendigkeit,
   keine Regression an den QCCVM-/Simulator-Tests). */
static const char* framePtr(void) { return os9Mode ? "a5" : "a6"; }
/* Namensverfremdung fuer static-Symbole (Mehrdatei-Uebersetzung, 2026-07-25):
   r68/l68 kennen KEIN Sichtbarkeitskonzept (kein xdef/xref, jedes Label ist
   beim Linken automatisch fuer JEDE andere gelinkte Datei sichtbar -- empirisch
   verifiziert, siehe docs/STATUS.md). Ohne Verfremdung wuerde "static" seinen
   Hauptzweck verfehlen: zwei unabhaengig kompilierte Dateien koennen jeweils
   einen privaten Helfer GLEICHEN Namens haben wollen (z.B. beide ein eigenes
   "static int init()"), was l68 sonst als "duplicate symbol" ablehnt (ebenso
   empirisch bestaetigt). Nur eine KONVENTION, KEINE echte Durchsetzung -- der
   psect-Name (aus dem Ausgabedateinamen abgeleitet, siehe main()) ist bereits
   der natuerliche Ort fuer Eindeutigkeit pro Datei. */
static char* mangledName(char* buf, const char* prefix, const char* name, int isStatic) {
	if (os9Mode && isStatic) sprintf(buf, "%s%s__%s", prefix, name, psectName);
	else sprintf(buf, "%s%s", prefix, name);
	return buf;
}
/* vasm kennt "even" (Ausrichtung auf gerade Adresse); der echte Microware-r68-
   Assembler kennt "even" NICHT (empirisch verifiziert: "bad mnemonic"), wohl
   aber "align 4" (Longword-Ausrichtung -- strenger als "even", aber fuer
   dc.l-Daten das eigentlich Gemeinte und ebenfalls empirisch verifiziert). */
static void emitAlign(FILE* out) {
	fputs(os9Mode ? "\talign\t4\n" : "\teven\n", out);
}

/* "Speichermodell"-Schalter (2026-07-25, siehe -largedata in main()/usage()):
   Standardmodell ("small") adressiert JEDES Globale ausschliesslich PC-relativ
   ("lea tc_g_X(pc),a0") -- das ist eine ECHTE 68000-Hardware-Grenze (16-Bit-
   Displacement, +-32 KB Reichweite von der jeweiligen Instruktion aus), keine
   willkuerliche Software-Grenze. Empirisch am echten r68-Assembler bestaetigt
   (siehe docs/FORTSCHRITT.md): ein 8192-Elemente-Array (458 KB) wird mit
   "value out of range" abgelehnt.
   "-largedata" wechselt auf eine zusaetzliche Indirektionstabelle (tc_gadata,
   ein 4-Byte-Eintrag "dc.l tc_g_X" pro Globaler IN globals[]-REIHENFOLGE,
   siehe emitIR() weiter unten) -- l68 loest "dc.l tc_g_X" als ABSOLUTE Adresse
   auf (normale Relokation, keine Distanzbeschraenkung).
   URSPRUENGLICHES Design (bis 2026-07-25 abends) lud JEDEN Tabelleneintrag per
   EIGENEM PC-relativem Label "movea.l tc_ga_X(pc),reg" -- das brach beim
   ersten genParserC-artigen Skalierungstest (SourceQCC/codegen.tc mit vielen
   Funktionen VOR der Tabelle, die selbst NACH dem gesamten Funktionscode
   liegt): "main" (per Funktionstabellen-Fix immer zuerst emittiert) konnte die
   Tabelle nicht mehr per PC-relativem Label erreichen, sobald der GESAMTE
   Funktionscode zwischen main und Tabelle mehr als 32 KB umfasste -- derselbe
   Grenzwert, nur diesmal fuer den TABELLENZUGRIFF SELBST statt fuer die Daten
   dahinter. GEFIXT nach EXAKT demselben Muster wie tc_functab/a4 (siehe
   emitCall()-Kommentar): EIN Register (a3) wird EINMAL beim Programmstart auf
   die absolute Adresse von tc_gadata gesetzt ("lea tc_gadata(pc),a3" -- die
   Tabelle liegt bewusst DIREKT nach tc_functab, also VOR allen Funktionsrumpf-
   Texten, bleibt also immer erreichbar). Jeder Globalzugriff wird dann zu
   "move.l <gidx*4>(a3),reg" -- a3-relative Adressierung hat zwar auch nur
   16-Bit-Displacement, aber die Tabelle waechst nur mit der ANZAHL der
   Globalen (4 Byte/Eintrag), nicht mit der Code-GROESSE. a3 war zuvor an
   keiner Stelle im Backend belegt (a0=Skalar-Scratch, a1=Puffer in tc_putint/
   tc_putuint/tc_putchar, a2=Aufruf-Scratch fuer emitCall, a4=Funktionstabelle,
   a5/a6=Frame-Pointer je nach os9Mode). */
/* WICHTIG (2026-07-26, live auf Q9 gefunden, siehe emitCall()-Kommentar):
   tc_gadata enthaelt KEINE absoluten Adressen mehr, sondern Link-Zeit-Offsets
   (Ziel minus Tabellenbasis) -- move.l laedt den Offset, "adda.l a3,reg" macht
   daraus die echte Laufzeitadresse (a3 ist per "lea (pc)" bereits korrekt
   geladen). reg ist an JEDER Aufrufstelle ein Adressregister (a0), adda.l
   akzeptiert ein Adressregister als Quelle problemlos.
   ZWEITER FUND (2026-07-26, live auf Q9, Ultra-C/C++ Processor Guide Table
   1-12 "Register Use"): a3 (wie a4, a0-a2) ist laut offizieller Microware-ABI
   ein reines TEMPORAER-Register ("The compiler uses all other registers for
   temporaries") -- NUR d0/d1 (Parameter/Rueckgabe), a5 (Frame), a6 (Static
   Storage) und a7 (Stack) sind reserviert. Jede ECHTE clib.l-Funktion
   (fopen/strlen/fprintf/_os_write/...) darf a3 also ungefragt ueberschreiben.
   Das urspruengliche Design ("a3 EINMAL beim Programmstart setzen, bleibt
   fuer immer gueltig") bricht deshalb beim ERSTEN echten externen Aufruf nach
   dem allerersten Globalzugriff -- live reproduziert (tc_putint ->
   tc_io_write -> bsr _os_write zerstoerte a3, der naechste Globalzugriff las
   von einer falschen Basisadresse).
   ERSTER FIX-VERSUCH (verworfen): a3 vor JEDEM Zugriff per "lea (pc)" neu
   laden -- brach den echten r68-Assembler ("value out of range"), weil "lea
   X(pc)" selbst wieder der 16-Bit-PC-relativ-Distanzgrenze unterliegt, die
   die ganze a3/a4-Indirektion ja gerade umgehen sollte. RICHTIGER FIX: a3/a4
   werden NUR direkt NACH jedem CALLEXT/CALLEXTP neu geladen (siehe dortigen
   Kommentar) -- das ist der EINZIGE Ort, an dem sie kaputtgehen koennen, und
   die Auffrischung steht IMMER im selben Funktionskoerper wie der Aufruf
   selbst (kurze Distanz, nie ueber 32 KB). emitLeaGlobal()/emitCall() selbst
   bleiben unveraendert (verlassen sich weiterhin auf den zuletzt
   aufgefrischten Wert). */
static void emitLeaGlobal(FILE* out, int gidx, const char* reg) {
	if (largeDataMode) {
		fprintf(out, "\tmove.l\t%d(a3),%s\n\tadda.l\ta3,%s\n", gidx * 4, reg, reg);
	} else {
		char gAsmName[NAME_LEN + 40];
		mangledName(gAsmName, "tc_g_", globals[gidx].name, globals[gidx].isStatic);
		fprintf(out, "\tlea\t%s(pc),%s\n", gAsmName, reg);
	}
}

static void fatal(const char* msg) {
	fprintf(stderr, "qcc_backend: %s\n", msg);
	exit(1);
}

static int findFunction(const char* name) {
	int i;
	for (i = 0; i < funcCount; i++) if (strcmp(funcs[i].name, name) == 0) return i;
	return -1;
}

static int findGlobal(const char* name) {
	int i;
	for (i = 0; i < globalCount; i++) if (strcmp(globals[i].name, name) == 0) return i;
	return -1;
}

/* -largedata (Funktionsaufruf-Teil, 2026-07-25, Nutzerwunsch "automatisch eine
   jmp table bauen wenn die Spruenge zu gross werden"): bsr ist wie lea(pc)
   PC-relativ-16-Bit -- betrifft NICHT die internen bra/beq/bne-Sprungziele
   INNERHALB einer Funktion (LABEL/JMP/JZ/JNZ, immer durch die Groesse EINER
   Funktion begrenzt), sondern FUNKTIONSUEBERGREIFENDE Aufrufe (CALL/CALLP,
   interne Laufzeit-Helfer wie tc_mul_i32), deren Aufrufstellen ueber ein
   beliebig grosses Programm verstreut sein koennen.
   Loesung: EIN Register (a4) wird EINMAL beim Programmstart auf die absolute
   Adresse einer kleinen Tabelle (tc_functab) gesetzt ("lea tc_functab(pc),a4"
   -- die Tabelle liegt bewusst DIREKT nach tc_start/main, bleibt also immer
   erreichbar, WIE GROSS der Rest des Programms auch wird). Jeder Aufruf wird
   dann zu "move.l N(a4),a2\njsr (a2)" statt "bsr X" -- a4-relative
   Adressierung hat zwar auch nur 16-Bit-Displacement, aber die Tabelle selbst
   waechst nur mit der ANZAHL der Funktionen (4 Byte/Eintrag), nicht mit der
   Code-GROESSE -- bleibt fuer jede realistische Anzahl Funktionen klein genug.
   a2 als Scratch-Register gewaehlt (NICHT a0/a1): a0 ist z.B. in IPADDN ueber
   den bsr hinweg belegt (Pointer-Wert), a1 in tc_putint/tc_putuint/tc_putchar
   (Puffer-Zeiger, siehe deren Definition) -- a2 ist an JEDER betroffenen
   Aufrufstelle nachweislich frei. */
static int helperTableOffset(const char* rawName) {
	static const char* helperNames[8] = {
		"tc_mul_i32", "tc_div_i32", "tc_udiv_u32", "tc_mod_i32", "tc_umod_u32",
		"tc_putint", "tc_putuint", "tc_putchar"
	};
	int i;
	for (i = 0; i < 8; i++) if (strcmp(helperNames[i], rawName) == 0) return funcCount * 4 + i * 4;
	fatal("interner Fehler: unbekannter Laufzeit-Helfer fuer -largedata-Funktionstabelle");
	return -1;
}

/* Emittiert einen Aufruf zu einem SCHON MANGLED Assembler-Namen (fuer QCC-
   Funktionen, tableOffset = funcIndex*4) ODER einem rohen Laufzeit-Helfer-
   Namen (tableOffset = helperTableOffset(...)) -- small: unveraendert "bsr
   asmName"; large: Tabellen-Indirektion ueber a4/a2, siehe Kommentar oben.
   WICHTIG (2026-07-26, live auf Q9 gefunden): die Tabelle enthaelt KEINE
   absoluten Adressen mehr (siehe tc_functab-Emissionskommentar) -- a2 traegt
   nach dem move.l erst den Link-Zeit-Offset (Ziel minus Tabellenbasis), "adda.l
   a4,a2" macht daraus die echte Laufzeitadresse (a4 ist per "lea (pc)" bereits
   korrekt geladen).
   ZWEITER FUND (2026-07-26, siehe emitLeaGlobal()-Kommentar): a4 ist laut
   Ultra-C/C++-ABI (Table 1-12) genauso ein reines Temporaer-Register wie a3 --
   jede echte clib.l-Funktion darf es zerstoeren. Fix (nach verworfenem
   Versuch, a4 hier bei JEDEM Aufruf neu zu laden -- brach r68 mit "value out
   of range", siehe emitLeaGlobal()-Kommentar): a4 wird stattdessen NUR direkt
   NACH jedem CALLEXT/CALLEXTP neu geladen (dort, wo es kaputtgehen kann). */
/* BUG 5 (2026-07-26, live auf Q9 gefunden, FUENFTER -largedata-Bug dieser
   Sitzung): jede Funktion frischt a3/a4 seit dem Bug-4-Fix GLEICH NACH dem
   eigenen "link" auf IHRE EIGENE Tabelle auf -- das heisst aber auch: NACH
   der Rueckkehr aus JEDEM internen Aufruf (CALL/CALLP, auch Laufzeit-Helfer
   wie tc_putint) zeigen a3/a4 auf die Tabelle der AUFGERUFENEN Funktion,
   NICHT mehr auf die eigene! Bei einem gleichdatei-Aufruf faellt das nicht
   auf (dieselbe Tabelle), bei einem Cross-File-Aufruf (FUNCDECL-Ziel in
   einer ANDEREN Datei, z.B. tcCopyBounded in codegen.tc von ebnf.tc aus
   aufgerufen) zeigt a4 danach auf die TABELLE DER FREMDEN DATEI -- der
   naechste Tabellen-Zugriff im Aufrufer (z.B. ein simples putchar(...)
   direkt nach dem Aufruf) laedt dadurch einen voellig falschen
   Funktionszeiger und stuerzt ab ("PMMU: Unhandled Table C/D mode 0",
   sofortiger Komplettabsturz des Emulators). Live bewiesen per gezielter
   Bisektion: Eintritt in tcCopyBounded UND ihr kompletter Funktionskoerper
   (samt Schleife) laufen nachweislich fehlerfrei -- der Fehler tritt exakt
   zwischen ihrer Rueckkehr und dem naechsten Tabellenzugriff im Aufrufer
   auf. FIX: nach JEDEM internen Aufruf (jsr ueber die Tabelle) frischt der
   AUFRUFER a3/a4 sofort wieder auf SEINE EIGENE Tabelle auf -- per
   selbstreferenzierendem lea+adda auf ein NEUES, direkt an dieser Stelle
   emittiertes lokales Label (nicht auf den Funktionsnamen selbst, der bei
   einem Aufruf mitten in einer langen Funktion zu weit entfernt sein
   koennte -- exakt dasselbe Distanzproblem, das der Bug-4-Fix schon einmal
   loesen musste). */
static void emitCall(FILE* out, const char* asmName, int tableOffset, int* serial, const char* psectName) {
	if (largeDataMode) {
		int id = (*serial)++;
		fprintf(out, "\tmove.l\t%d(a4),a2\n\tadda.l\ta4,a2\n\tjsr\t(a2)\n", tableOffset);
		fprintf(out, "tc_callret_%d__%s:\n", id, psectName);
		fprintf(out, "\tlea\ttc_callret_%d__%s(pc),a4\n\tadda.l\t#(tc_functab__%s-tc_callret_%d__%s),a4\n", id, psectName, psectName, id, psectName);
		fprintf(out, "\tlea\ttc_callret_%d__%s(pc),a3\n\tadda.l\t#(tc_gadata__%s-tc_callret_%d__%s),a3\n", id, psectName, psectName, id, psectName);
	} else {
		fprintf(out, "\tbsr\t%s\n", asmName);
	}
}

static int isNumWord(const char* w) {
	return strcmp(w, "i") == 0 || strcmp(w, "u") == 0 || strcmp(w, "c") == 0 || strcmp(w, "b") == 0 || strcmp(w, "p") == 0;
}

static int isByteWord(const char* w) {
	return strcmp(w, "c") == 0 || strcmp(w, "b") == 0;
}

static int number(const char* text, int line) {
	char* end;
	long v;
	char msg[160];
	if (text[0] == '\0') { sprintf(msg, "IR Zeile %d: Zahl erwartet: %s", line, text); fatal(msg); }
	v = strtol(text, &end, 10);
	if (*end != '\0') { sprintf(msg, "IR Zeile %d: Zahl erwartet: %s", line, text); fatal(msg); }
	return (int)v;
}

static void readIR(const char* path) {
	FILE* fp;
	char raw[LINE_LEN];
	int line = 0;
	char* tok;
	Instr* insP;
	char msg[300];

	fp = fopen(path, "r");
	if (!fp) { sprintf(msg, "kann IR nicht lesen: %s", path); fatal(msg); }
	while (fgets(raw, sizeof(raw), fp)) {
		line++;
		tok = strtok(raw, " \t\r\n");
		if (!tok || tok[0] == ';' || tok[0] == '#') continue;
		if (strcmp(tok, "OK") == 0 || strcmp(tok, "FAIL") == 0) continue;
		if (irCount >= MAX_IR_LINES) fatal("IR: zu viele Zeilen");
		insP = &ir[irCount++];
		strncpy(insP->op, tok, OP_LEN - 1); insP->op[OP_LEN - 1] = '\0';
		insP->line = line;
		insP->argc = 0;
		while ((tok = strtok(NULL, " \t\r\n")) != NULL) {
			if (insP->argc < MAX_ARGS) {
				strncpy(insP->args[insP->argc], tok, ARG_LEN - 1);
				insP->args[insP->argc][ARG_LEN - 1] = '\0';
			}
			insP->argc++;
		}
	}
	fclose(fp);
}

static void collectGlobals(void) {
	/* GLOBAL/GARRAY/GINIT duerfen -- anders als frueher -- auch INNERHALB einer Funktion
	   stehen: eine "static" lokale Variable (Data/qcc.lextab, tc_staticlocal) wird als
	   ganz normaler GLOBAL registriert, an genau der Textstelle, an der ihre Deklaration
	   im Quelltext steht, also moeglicherweise mitten in einer FUNC...ENDFUNC-Spanne.
	   collectFunctions() prueft weiterhin, dass jede Zeile entweder zu GLOBAL/GARRAY/GINIT
	   gehoert oder innerhalb einer offenen Funktion liegt -- eine Zeile "zwischen" zwei
	   Funktionen ausserhalb jeder FUNC-Spanne bleibt also weiterhin ein Fehler. */
	int i, gi, idx, len;
	char msg[300];
	globalCount = 0;
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		if (strcmp(insP->op, "GINIT") == 0) {
			int found = 0;
			if (insP->argc != 3) fatal("ungueltiges GINIT");
			for (gi = 0; gi < globalCount; gi++) {
				if (strcmp(globals[gi].name, insP->args[0]) == 0 && globals[gi].isArray) {
					idx = number(insP->args[1], insP->line);
					if (idx < 0 || idx >= globals[gi].length) fatal("GINIT-Index ausserhalb Array");
					/* 2026-07-25: die MAX_ARRAY_LEN-Grenze gilt jetzt NUR fuer tatsaechlich per
					   GINIT gesetzte Indizes (init[] ist ein fester Puffer), NICHT mehr fuer die
					   deklarierte GARRAY-Laenge selbst -- ein grosses, aber unbenutztes/komplett
					   nullinitialisiertes Array (z.B. ein 8192-Elemente-AST-Knotenpuffer) braucht
					   dafuer keinen Speicher, siehe emitIR()-Nullfuellung weiter unten. */
					if (idx >= MAX_ARRAY_LEN) fatal("GINIT-Index ueberschreitet MAX_ARRAY_LEN");
					globals[gi].init[idx] = number(insP->args[2], insP->line);
					if (globals[gi].isChar) globals[gi].init[idx] &= 255;
					globals[gi].hasGinit = 1;
					found = 1;
					break;
				}
			}
			if (!found) fatal("GINIT fuer unbekanntes Array");
			continue;
		}
		if (strcmp(insP->op, "GLOBAL") != 0 && strcmp(insP->op, "GARRAY") != 0) continue;
		if (strcmp(insP->op, "GARRAY") == 0) {
			/* 4. Argument (2026-07-25, Mehrdatei-Uebersetzung): optionales isstatic-Flag,
			   hier noch nicht ausgewertet (siehe collectFunctions/GLOBALDECL/FUNCDECL). */
			if ((insP->argc != 3 && insP->argc != 4) || !isNumWord(insP->args[1])) fatal("ungueltiges GARRAY");
			if (findGlobal(insP->args[0]) >= 0) fatal("doppelte globale Variable");
			len = number(insP->args[2], insP->line);
			if (len <= 0) fatal("GARRAY-Laenge muss positiv sein");
			/* KEINE MAX_ARRAY_LEN-Grenze mehr hier -- siehe Kommentar bei GINIT weiter oben.
			   Ein grosses, nie per GINIT gesetztes Array (komplett nullinitialisiert) braucht
			   keinen init[]-Speicher und wird unten kompakt gefuellt. */
			if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
			gi = globalCount++;
			memset(&globals[gi], 0, sizeof(Global));
			strncpy(globals[gi].name, insP->args[0], NAME_LEN - 1);
			globals[gi].isChar = isByteWord(insP->args[1]);
			globals[gi].isArray = 1;
			globals[gi].length = len;
			globals[gi].isStatic = insP->argc >= 4 && number(insP->args[3], insP->line) != 0;
			continue;
		}
		if (insP->argc != 1 && insP->argc != 2 && insP->argc != 3 && insP->argc != 4) {
			sprintf(msg, "IR Zeile %d: ungueltiges GLOBAL", insP->line);
			fatal(msg);
		}
		if (findGlobal(insP->args[0]) >= 0) {
			sprintf(msg, "IR Zeile %d: doppelte globale Variable %s", insP->line, insP->args[0]);
			fatal(msg);
		}
		/* argc>=3 statt ==3 (2026-07-25): 4. Argument ist das optionale isstatic-Flag
		   (Mehrdatei-Uebersetzung), der Typtag bleibt immer an Position 2. */
		if (insP->argc >= 3 && !isNumWord(insP->args[2])) {
			sprintf(msg, "IR Zeile %d: unbekannter Globaltyp", insP->line);
			fatal(msg);
		}
		if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
		gi = globalCount++;
		memset(&globals[gi], 0, sizeof(Global));
		strncpy(globals[gi].name, insP->args[0], NAME_LEN - 1);
		globals[gi].initialValue = insP->argc >= 2 ? number(insP->args[1], insP->line) : 0;
		globals[gi].isChar = insP->argc >= 3 && isByteWord(insP->args[2]);
		globals[gi].isArray = 0;
		globals[gi].length = 1;
		globals[gi].isStatic = insP->argc >= 4 && number(insP->args[3], insP->line) != 0;
	}
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		if (strcmp(insP->op, "GLOBALDECL") != 0) continue;
		if (insP->argc != 2) fatal("ungueltiges GLOBALDECL");
		if (findGlobal(insP->args[0]) >= 0) {
			sprintf(msg, "IR Zeile %d: doppelte globale Variable %s", insP->line, insP->args[0]);
			fatal(msg);
		}
		if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
		gi = globalCount++;
		memset(&globals[gi], 0, sizeof(Global));
		strncpy(globals[gi].name, insP->args[0], NAME_LEN - 1);
		globals[gi].isChar = isByteWord(insP->args[1]);
		globals[gi].declOnly = 1;
	}
}

static void collectFunctions(void) {
	int i, open = 0, seenFunction = 0;
	Function current;
	char msg[300];
	funcCount = 0;
	memset(&current, 0, sizeof(current));
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		if (strcmp(insP->op, "GLOBAL") == 0 || strcmp(insP->op, "GARRAY") == 0 || strcmp(insP->op, "GINIT") == 0) {
			/* vor der ersten Funktion (echte globale Variablen) ODER innerhalb einer
			   offenen Funktion (static lokale Variable, siehe collectGlobals) erlaubt --
			   NICHT zwischen zwei Funktionen (ausserhalb jeder FUNC-Spanne). */
			if ((!open && seenFunction) || (insP->argc != 1 && insP->argc != 2 && insP->argc != 3 && insP->argc != 4)) {
				sprintf(msg, "IR Zeile %d: ungueltiges GLOBAL", insP->line);
				fatal(msg);
			}
		} else if (strcmp(insP->op, "FUNCDECL") == 0 || strcmp(insP->op, "GLOBALDECL") == 0) {
			/* Mehrdatei-Uebersetzung (2026-07-25): "existiert, ist aber nicht hier
			   definiert" -- ausserhalb jeder FUNC-Spanne erlaubt (wie GLOBAL/GARRAY);
			   FUNCDECL wird unten in einem separaten Durchlauf registriert (analog
			   zu GLOBALDECL in collectGlobals), da es KEINE FUNC/ENDFUNC-Spanne
			   oeffnet/schliesst. */
		} else if (strcmp(insP->op, "FUNC") == 0) {
			/* 3. Argument (2026-07-25): optionales isstatic-Flag (Namensverfremdung
			   in emitIR, siehe mangledName() -- r68/l68 kennen kein Sichtbarkeits-
			   konzept, siehe docs/STATUS.md). */
			if (open || (insP->argc != 2 && insP->argc != 3)) { sprintf(msg, "IR Zeile %d: ungueltiges FUNC", insP->line); fatal(msg); }
			memset(&current, 0, sizeof(current));
			strncpy(current.name, insP->args[0], NAME_LEN - 1);
			current.nargs = number(insP->args[1], insP->line);
			current.first = i + 1;
			current.last = -1;
			current.isStatic = insP->argc >= 3 && number(insP->args[2], insP->line) != 0;
			open = 1;
			seenFunction = 1;
		} else if (strcmp(insP->op, "ENDFUNC") == 0) {
			if (!open) { sprintf(msg, "IR Zeile %d: ENDFUNC ohne FUNC", insP->line); fatal(msg); }
			current.last = i;
			if (funcCount >= MAX_FUNCS) fatal("zu viele Funktionen");
			funcs[funcCount++] = current;
			open = 0;
		} else if (!open) {
			sprintf(msg, "IR Zeile %d: Opcode ausserhalb einer Funktion", insP->line);
			fatal(msg);
		}
	}
	if (open) fatal("IR: fehlendes ENDFUNC");
	/* -part (2026-07-25, Mehrdatei-Uebersetzung): eine Datei OHNE main/Funktionen
	   ist zulaessig, solange sie wenigstens globale Deklarationen enthaelt --
	   eine komplett leere Datei bleibt weiterhin ein Fehler. Ohne -part
	   unveraendert immer ein Fehler (Vollprogramm-Annahme). */
	if (funcCount == 0 && (!partMode || globalCount == 0)) fatal("IR: keine Funktion");
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		int existing;
		if (strcmp(insP->op, "FUNCDECL") != 0) continue;
		if (insP->argc != 2) fatal("ungueltiges FUNCDECL");
		existing = findFunction(insP->args[0]);
		if (existing >= 0) {
			/* Vorwaertsdeklaration innerhalb DERSELBEN Datei, deren echter Rumpf
			   bereits (an anderer Stelle im selben IR) gefunden wurde -- das ist
			   der normale Fall bei gegenseitig rekursiven Funktionen (A ruft B vor
			   dessen Definition auf), KEIN Duplikat. Nur wenn die vorhandene
			   Registrierung selbst noch declOnly ist (zwei FUNCDECL fuer denselben
			   Namen ohne jemals einen echten Rumpf), bleibt es ein echter Fehler. */
			if (!funcs[existing].declOnly) continue;
			fprintf(stderr, "qcc_backend: doppelte Funktion %s\n", insP->args[0]); fatal("doppelte Funktion");
		}
		if (funcCount >= MAX_FUNCS) fatal("zu viele Funktionen");
		memset(&current, 0, sizeof(current));
		strncpy(current.name, insP->args[0], NAME_LEN - 1);
		current.nargs = number(insP->args[1], insP->line);
		current.first = -1;
		current.last = -1;
		current.declOnly = 1;
		funcs[funcCount++] = current;
	}

	for (i = 0; i < funcCount; i++) {
		Function* fn = &funcs[i];
		int highest = fn->nargs - 1;
		int k;
		for (k = fn->first; k < fn->last; k++) {
			Instr* x = &ir[k];
			if ((strcmp(x->op, "LOADL") == 0 || strcmp(x->op, "STOREL") == 0 || strcmp(x->op, "LOADC") == 0 ||
				strcmp(x->op, "STOREC") == 0 || strcmp(x->op, "LOADP") == 0 || strcmp(x->op, "STOREP") == 0 ||
				strcmp(x->op, "ADDRL") == 0 || strcmp(x->op, "LARRAY") == 0) && x->argc > 0) {
				int slotN = number(x->args[0], x->line);
				if (slotN < 0) { sprintf(msg, "IR Zeile %d: negativer lokaler Slot", x->line); fatal(msg); }
				if (slotN > highest) highest = slotN;
			}
		}
		fn->locals = highest >= fn->nargs ? highest - fn->nargs + 1 : 0;
		fn->frameBytes = fn->locals * 4;
		for (k = fn->first; k < fn->last; k++) {
			if (strcmp(ir[k].op, "LARRAY") == 0) {
				Instr* x = &ir[k];
				int len, align;
				if (x->argc != 3) fatal("ungueltiges LARRAY");
				len = number(x->args[2], x->line);
				if (len <= 0) fatal("LARRAY-Laenge muss positiv sein");
				align = isByteWord(x->args[1]) ? 1 : 2;
				fn->frameBytes = (fn->frameBytes + align - 1) & ~(align - 1);
				fn->frameBytes += len * (isByteWord(x->args[1]) ? 1 : 4);
			}
		}
		fn->frameBytes = (fn->frameBytes + 3) & ~3;
	}
}

/* 2026-07-26 (siehe registerExtern()-Kommentar oben): sammelt EINMAL vorab
   alle in dieser Datei per CALLEXT/CALLEXTP gerufenen externen Rohnamen
   (strlen/fopen/printf/...) -- muss VOR jeder Codeemission laufen, damit
   Tabellenindex UND Wrapper-Emission konsistent dieselbe Reihenfolge sehen. */
static void collectExterns(void) {
	int i;
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		if ((strcmp(insP->op, "CALLEXT") == 0 || strcmp(insP->op, "CALLEXTP") == 0) && insP->argc == 3) {
			registerExtern(insP->args[0]);
		}
	}
}

static int arrayOffset(const Function* fn, int wanted, int* isChar, int line) {
	int offset = fn->locals * 4;
	int k;
	char msg[200];
	for (k = fn->first; k < fn->last; k++) {
		if (strcmp(ir[k].op, "LARRAY") == 0) {
			Instr* x = &ir[k];
			int slotN, len, align;
			if (x->argc != 3) fatal("ungueltiges LARRAY");
			slotN = number(x->args[0], x->line);
			len = number(x->args[2], x->line);
			align = isByteWord(x->args[1]) ? 1 : 2;
			offset = (offset + align - 1) & ~(align - 1);
			offset += len * (isByteWord(x->args[1]) ? 1 : 4);
			if (slotN == wanted) {
				*isChar = isByteWord(x->args[1]);
				return offset;
			}
		}
	}
	sprintf(msg, "IR Zeile %d: unbekanntes lokales Array", line);
	fatal(msg);
	return 0;
}

static void slotAddress(char* out, int slotN, const Function* fn, int line) {
	char msg[200];
	if (slotN < fn->nargs) {
		sprintf(out, "%d(%s)", 8 + 4 * (fn->nargs - 1 - slotN), framePtr());
		return;
	}
	if (slotN >= fn->nargs + fn->locals) {
		sprintf(msg, "IR Zeile %d: Slot ausserhalb des Frames", line);
		fatal(msg);
	}
	sprintf(out, "%d(%s)", -4 * (slotN - fn->nargs + 1), framePtr());
}

static void emitCompare(FILE* out, const char* branch, int* serial) {
	/* tc_cmp_yes_<id>/tc_cmp_done_<id> sind reine interne Sprungmarken, KEINE
	   QCC-Symbole -- ohne psectName-Suffix kollidieren sie beim Mehrdatei-
	   Link, sobald ZWEI separat kompilierte Dateien beide mindestens einen
	   Vergleichsoperator benutzen (r68/l68 kennen kein Sichtbarkeitskonzept,
	   siehe mangledName()-Kommentar -- id allein ist nur PRO DATEI eindeutig,
	   der serial-Zaehler startet in jeder Datei wieder bei 0). Live gefunden
	   beim ersten echten Zwei-Datei-Link von SourceQCC/ebnf.tc gegen
	   codegen.tc (2026-07-26, writeWorkfile-Chunk), siehe docs/FORTSCHRITT.md.
	   FUNDAMENTALER FUND (2026-07-26, live auf Q9 gefunden -- ALLE Vergleiche
	   waren betroffen, live reproduziert bis in ein winziges Standalone-
	   Programm): "moveq #0,d0" ZWISCHEN "cmp.l" und dem bedingten Branch
	   (frueherer Code hier) ZERSTOERT die von cmp.l gesetzten Flags, BEVOR der
	   Branch sie liest -- MOVEQ setzt selbst N/Z (loescht V/C) basierend auf
	   dem bewegten Wert, und "moveq #0,d0" bewegt IMMER eine 0, setzt also
	   IMMER Z=1. Ergebnis: "beq" (End-Test auf Z=1) sprang IMMER (jeder
	   "=="-Vergleich war IMMER wahr), "bne" sprang NIE (jeder "!="-Vergleich
	   war IMMER falsch) -- UNABHAENGIG von den tatsaechlichen Werten. Nie
	   vorher aufgefallen, weil QCCVM UND tools/qcc68sim.py (unser Test-
	   Simulator) Vergleiche als reinen Werttransport modellieren, NICHT ueber
	   echte CPU-Flags -- der Bug war fuer BEIDE unsichtbar, erst die echte
	   68030-Hardware auf Q9 zeigte ihn. FIX: "moveq #0,d0" NACH den Branch
	   verschoben (in den sonst-Zweig, der NUR erreicht wird, wenn der Branch
	   NICHT genommen wurde) -- die Flags von cmp.l bleiben bis zum Branch
	   selbst unangetastet. */
	int id = (*serial)++;
	fprintf(out, "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tcmp.l\td1,d0\n");
	fprintf(out, "\t%s\ttc_cmp_yes_%d__%s\n\tmoveq\t#0,d0\n\tbra\ttc_cmp_done_%d__%s\n", branch, id, psectName, id, psectName);
	fprintf(out, "tc_cmp_yes_%d__%s:\tmoveq\t#1,d0\ntc_cmp_done_%d__%s:\tmove.l\td0,-(a7)\n", id, psectName, id, psectName);
}

// 68000 hat MULS/DIVS nur fuer 16-Bit-Operanden. Diese festen, PIC-faehigen
// Schablonen bilden deshalb die definierte QCC-int32-Arithmetik nach. Sie
// erhalten d2-d5 (ABI-freundlich) und geben ausschliesslich d0 zurueck.
static void emitM68kCore(FILE* out) {
	fprintf(out, "%s 68k-Core: int32 MUL/DIV, keine OS- oder Q9-Abhaengigkeit\n", fullCommentPrefix());
	fputs("tc_mul_i32:\n", out);
	fputs("\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td4,-(a7)\n", out);
	fputs("\tmoveq\t#0,d2\n\tmoveq\t#0,d4\n\ttst.l\td0\n\tbpl\ttc_mul_a_pos\n", out);
	fputs("\tneg.l\td0\n\taddq.l\t#1,d4\n", out);
	fputs("tc_mul_a_pos:\ttst.l\td1\n\tbpl\ttc_mul_b_pos\n\tneg.l\td1\n\teori.l\t#1,d4\n", out);
	fputs("tc_mul_b_pos:\tmoveq\t#31,d3\n", out);
	fputs("tc_mul_loop:\tlsr.l\t#1,d1\n\tbcc\ttc_mul_skip\n\tadd.l\td0,d2\n", out);
	fputs("tc_mul_skip:\tadd.l\td0,d0\n\tdbra\td3,tc_mul_loop\n", out);
	fputs("\ttst.l\td4\n\tbeq\ttc_mul_done\n\tneg.l\td2\n", out);
	fputs("tc_mul_done:\tmove.l\td2,d0\n\tmove.l\t(a7)+,d4\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n", out);

	fputs("tc_div_i32:\n", out);
	fputs("\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td4,-(a7)\n\tmove.l\td5,-(a7)\n", out);
	fputs("\tmoveq\t#0,d5\n\ttst.l\td1\n\tbne\ttc_div_nonzero\n\tmoveq\t#0,d0\n\tbra\ttc_div_done\n", out);
	fputs("tc_div_nonzero:\ttst.l\td0\n\tbpl\ttc_div_a_pos\n\tneg.l\td0\n\taddq.l\t#1,d5\n", out);
	fputs("tc_div_a_pos:\ttst.l\td1\n\tbpl\ttc_div_b_pos\n\tneg.l\td1\n\teori.l\t#1,d5\n", out);
	fputs("tc_div_b_pos:\tmoveq\t#0,d2\n\tmoveq\t#0,d3\n\tmoveq\t#31,d4\n", out);
	fputs("tc_div_loop:\tlsl.l\t#1,d0\n\troxl.l\t#1,d3\n\tlsl.l\t#1,d2\n", out);
	fputs("\tcmp.l\td1,d3\n\tbcs\ttc_div_skip\n\tsub.l\td1,d3\n\taddq.l\t#1,d2\n", out);
	fputs("tc_div_skip:\tdbra\td4,tc_div_loop\n\ttst.l\td5\n\tbeq\ttc_div_result\n\tneg.l\td2\n", out);
	fputs("tc_div_result:\tmove.l\td2,d0\n", out);
	fputs("tc_div_done:\tmove.l\t(a7)+,d5\n\tmove.l\t(a7)+,d4\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n", out);

	fputs("tc_udiv_u32:\n", out);
	fputs("\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td4,-(a7)\n", out);
	fputs("\ttst.l\td1\n\tbne\ttc_udiv_nonzero\n\tmoveq\t#0,d0\n\tbra\ttc_udiv_done\n", out);
	fputs("tc_udiv_nonzero:\tmoveq\t#0,d2\n\tmoveq\t#0,d3\n\tmoveq\t#31,d4\n", out);
	fputs("tc_udiv_loop:\tlsl.l\t#1,d0\n\troxl.l\t#1,d3\n\tlsl.l\t#1,d2\n", out);
	fputs("\tcmp.l\td1,d3\n\tbcs\ttc_udiv_skip\n\tsub.l\td1,d3\n\taddq.l\t#1,d2\n", out);
	fputs("tc_udiv_skip:\tdbra\td4,tc_udiv_loop\n\tmove.l\td2,d0\n", out);
	fputs("tc_udiv_done:\tmove.l\t(a7)+,d4\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n", out);

	/* Rest = Dividend - Quotient*Divisor. WICHTIG (2026-07-24, gefunden ueber
	   tools/qcc68sim.py beim Debuggen der neuen -os9-putint-Ziffernzerlegung):
	   vor "bsr tc_mul_i32" muss d0 den DIVISOR (d3) tragen, NICHT nochmal den
	   Dividenden (d2) -- sonst wird Quotient*Dividend statt Quotient*Divisor
	   gerechnet. Dieser Bug war seit Einfuehrung von tc_mod_i32/tc_umod_u32
	   unentdeckt, weil kein einziger 68k-Backend-Test (nur die QCCVM-Tests)
	   den "%"-Operator ueber den echten 68k-Pfad ausgefuehrt hat. */
	fputs("tc_mod_i32:\n", out);
	fputs("\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td0,d2\n\tmove.l\td1,d3\n\tbsr\ttc_div_i32\n\tmove.l\td0,d1\n\tmove.l\td3,d0\n\tbsr\ttc_mul_i32\n\tsub.l\td0,d2\n\tmove.l\td2,d0\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n", out);
	fputs("tc_umod_u32:\n", out);
	fputs("\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td0,d2\n\tmove.l\td1,d3\n\tbsr\ttc_udiv_u32\n\tmove.l\td0,d1\n\tmove.l\td3,d0\n\tbsr\ttc_mul_i32\n\tsub.l\td0,d2\n\tmove.l\td2,d0\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n", out);
}

static void emitIR(FILE* out) {
	int serial = 0;
	int fi, k;
	char msg[300];
	char addrBuf[64];
	int order[MAX_FUNCS];
	int oi;
	int gi;

	/* -part (2026-07-25): main darf in einer ANDEREN Datei des Mehrdatei-
	   Programms stehen -- das meldet der echte Linker (l68) von selbst, falls
	   keine der gelinkten Dateien es liefert. */
	if (!partMode && findFunction("main") < 0) fatal("IR: Funktion main fehlt");

	fprintf(out, "%s QCC 68k backend -- PIC Einzelmodul, erzeugt aus Stack-IR\n", fullCommentPrefix());
	fprintf(out, "%s a7: Operand-Stack, %s: aktueller Frame, d0/d1: Scratch/Rueckgabe\n\n", fullCommentPrefix(), framePtr());
	if (os9Mode) {
		fprintf(out, "\tnam\t%s\n", psectName);
		fprintf(out, "\tpsect\t%s,0,0,1,0,0\n\n", psectName);
		fprintf(out, "%s Kein eigener tc_start-Boot-Code hier: cstart.r (echte Microware-\n", fullCommentPrefix());
		fprintf(out, "%s C-Laufzeit) ruft \"main\" direkt auf und kuemmert sich selbst ums\n", fullCommentPrefix());
		fprintf(out, "%s Beenden -- a6 bleibt dadurch als dessen statischer Datenzeiger\n", fullCommentPrefix());
		fprintf(out, "%s unangetastet (siehe framePtr()-Kommentar oben im Quelltext).\n\n", fullCommentPrefix());
	} else {
		int mainIdx = findFunction("main");
		fputs("tc_start:\n", out);
		if (largeDataMode) fprintf(out, "\tlea\ttc_functab__%s(pc),a4\n", psectName);
			if (largeDataMode) fprintf(out, "\tlea\ttc_gadata__%s(pc),a3\n", psectName);
		if (mainIdx >= 0) {
			char mainAsmName[NAME_LEN + 40];
			mangledName(mainAsmName, "tc_", "main", funcs[mainIdx].isStatic);
			emitCall(out, mainAsmName, mainIdx * 4, &serial, psectName);
		} else {
			fputs("\tbsr\ttc_main\n", out); /* main nicht in dieser Datei -- wie zuvor, siehe -part oben */
		}
		fputs("\tbra\ttc_exit\n\n", out);
	}
	if (largeDataMode) {
		/* Funktions-Indirektionstabelle (siehe emitCall()-Kommentar): MUSS direkt nach
		   tc_start/main stehen (VOR den potenziell riesigen Funktionsrumpf-Texten),
		   damit das einmalige "lea tc_functab(pc),a4" immer erreichbar bleibt, egal wie
		   gross der Rest des Programms wird. Reihenfolge MUSS exakt zu funcIndex*4 (fuer
		   QCC-Funktionen) bzw. helperTableOffset() (fuer Laufzeit-Helfer) passen. */
		/* WICHTIG (2026-07-26, live auf Q9 gefunden -- echter PMMU-Absturz beim
		   allerersten Funktionsaufruf in main()): "dc.l <label>" ist auf OS-9
		   KEINE automatisch relozierte absolute Adresse! Laut OS-9 for 68K
		   Processors Technical Manual muss ein Assemblerprogrammierer absolute
		   Adressmodi selbst vermeiden -- der einzige eingebaute Loader-
		   Relokationsmechanismus (M$IRefs/F$Fork) gilt nur fuer
		   Compiler-generierte initialisierte Zeigervariablen in vsects (eigenes,
		   rohes MS-Word/Count/LS-Word-Tabellenformat), nicht fuer beliebige
		   "dc.l label" in einem psect. l68 loest so ein "dc.l label" nur als
		   psect-INTERNEN Offset auf (gueltig fuer einen angenommenen Ladeort 0),
		   NICHT als echte Laufzeitadresse -- deshalb Tabelleneintraege jetzt als
		   Link-Zeit-KONSTANTE Differenz zur Tabellenbasis selbst ("label-tab",
		   von l68 rein psect-intern berechnet, KEINE Laufzeit-Relokation noetig,
		   da beide Labels im selben Psect fest zueinander stehen). emitCall()
		   addiert die per "lea (pc)" bereits korrekt geladene Tabellenbasis
		   (a4) auf diesen Offset, BEVOR gesprungen wird. */
		fprintf(out, "%s Funktions-Indirektionstabelle (-largedata): Link-Zeit-Offsets relativ zur Tabellenbasis (siehe emitCall())\n", fullCommentPrefix());
		emitAlign(out);
		fprintf(out, "tc_functab__%s:\n", psectName);
		for (fi = 0; fi < funcCount; fi++) {
			char asmName[NAME_LEN + 40];
			mangledName(asmName, "tc_", funcs[fi].name, funcs[fi].isStatic);
			fprintf(out, "\tdc.l\t%s-tc_functab__%s\n", asmName, psectName);
		}
		fprintf(out, "\tdc.l\ttc_mul_i32-tc_functab__%s\n\tdc.l\ttc_div_i32-tc_functab__%s\n\tdc.l\ttc_udiv_u32-tc_functab__%s\n",
			psectName, psectName, psectName);
		fprintf(out, "\tdc.l\ttc_mod_i32-tc_functab__%s\n\tdc.l\ttc_umod_u32-tc_functab__%s\n", psectName, psectName);
		fprintf(out, "\tdc.l\ttc_putint-tc_functab__%s\n\tdc.l\ttc_putuint-tc_functab__%s\n\tdc.l\ttc_putchar-tc_functab__%s\n",
			psectName, psectName, psectName);
		/* 2026-07-26 (siehe registerExtern()-Kommentar): externe CALLEXT/CALLEXTP-
		   Ziele bekommen KEINEN direkten Tabelleneintrag auf den rohen externen
		   Namen (der laege ausserhalb dieses Psects, "label-tab" waere dann keine
		   Link-Zeit-Konstante mehr innerhalb DIESES Psects -- tatsaechlich hatten
		   wir das fuer echte externe Symbole schon erfolgreich getestet, aber der
		   eigentliche Grund fuer diese Tabelle ist ja gerade, a3/a4 NACH dem
		   externen Aufruf aufzufrischen, siehe emitCallExtWrapper()) -- sondern
		   auf den WRAPPER-Stub direkt darunter. */
		for (fi = 0; fi < externCount; fi++) {
			fprintf(out, "\tdc.l\ttc_extwrap_%s__%s-tc_functab__%s\n", externNames[fi], psectName, psectName);
		}
		/* Daten-Indirektionstabelle (siehe emitLeaGlobal()-Kommentar): MUSS wie
		   tc_functab direkt nach tc_start/main stehen (VOR den potenziell
		   riesigen Funktionsrumpf-Texten), damit das einmalige
		   "lea tc_gadata(pc),a3" immer erreichbar bleibt, egal wie gross der
		   Rest des Programms wird. Reihenfolge MUSS exakt zu gidx*4 (siehe
		   findGlobal()) passen -- AUCH declOnly-Externe bekommen einen
		   Eintrag ("dc.l tc_g_X" ist fuer diese eine ganz normale externe
		   Symbolreferenz, von l68 wie jede andere aufgeloest). IMMER emittiert
		   (nicht nur bei globalCount > 0): der ZUSAETZLICHE Eintrag am Ende
		   (Offset globalCount*4, siehe extcallTmpTableOffset()) fuer
		   tc_extcall_tmp wird UNABHAENGIG von echten Globalen gebraucht, sobald
		   irgendein externer Aufruf mit Stack-Argumenten vorkommt -- dasselbe
		   Skalierungsproblem wie bei echten Globalen (PC-relatives
		   "lea tc_extcall_tmp(pc),a0" direkt an der Aufrufstelle wuerde brechen,
		   sobald der Abstand zum spaet liegenden Scratch-Puffer >32 KB wird). */
		/* WICHTIG (2026-07-26, siehe tc_functab-Kommentar oben): auch hier
		   Link-Zeit-Offsets relativ zur Tabellenbasis statt roher "dc.l label"
		   -- exakt dieselbe OS-9-Positionsunabhaengigkeits-Anforderung betrifft
		   Globalzugriffe genauso wie Funktionsaufrufe. emitLeaGlobal() addiert
		   die Tabellenbasis (a3) auf diesen Offset. */
		fprintf(out, "%s Daten-Indirektionstabelle (-largedata): Link-Zeit-Offsets relativ zur Tabellenbasis (siehe emitLeaGlobal())\n", fullCommentPrefix());
		emitAlign(out);
		fprintf(out, "tc_gadata__%s:\n", psectName);
		for (gi = 0; gi < globalCount; gi++) {
			char gAsmName[NAME_LEN + 40];
			mangledName(gAsmName, "tc_g_", globals[gi].name, globals[gi].isStatic);
			fprintf(out, "\tdc.l\t%s-tc_gadata__%s\n", gAsmName, psectName);
		}
		fprintf(out, "\tdc.l\ttc_extcall_tmp-tc_gadata__%s\n", psectName);
		/* 2026-07-26 (siehe registerExtern()-Kommentar oben): ein Wrapper-Stub pro
		   externer Funktion, DIREKT hier (nah an tc_functab/tc_gadata, also immer
		   PC-relativ sicher erreichbar) platziert. "jsr (a2)" (von der Aufrufstelle,
		   ueber den a4-Tabellenmechanismus) hat bereits EINE Ruecksprungadresse auf
		   a7 gepusht -- die wird zuerst nach d7 (freies Scratch-Register)
		   herausgeholt, DAMIT "bsr rawname" exakt dieselbe Stack-Position fuer
		   seine EIGENE Ruecksprungadresse UND fuer eventuelle Stack-Argumente
		   sieht, die die Aufrufstelle VOR dem jsr bereits gepusht hat (ohne diesen
		   Zwischenschritt saehe die externe Funktion ihre eigenen Stack-Argumente
		   um vier Byte verschoben -- durch die zusaetzliche jsr-Ruecksprungadresse
		   des Wrappers). Nach der Rueckkehr von rawname (d0 traegt den
		   Rueckgabewert, bleibt unangetastet) a3/a4 auffrischen (siehe
		   emitCall()/emitLeaGlobal()-Kommentar), dann die urspruengliche
		   Ruecksprungadresse aus d7 zurueckpushen und rts -- geht exakt zur
		   Aufrufstelle zurueck, als waere direkt "bsr rawname" aufgerufen worden,
		   nur mit aufgefrischtem a3/a4. */
		for (fi = 0; fi < externCount; fi++) {
			fprintf(out, "tc_extwrap_%s__%s:\n", externNames[fi], psectName);
			fprintf(out, "\tmove.l\t(a7)+,d7\n");
			fprintf(out, "\t%s\t%s\n", os9Mode ? "bsr" : "jsr", externNames[fi]);
			fprintf(out, "\tlea\ttc_functab__%s(pc),a4\n\tlea\ttc_gadata__%s(pc),a3\n", psectName, psectName);
			fprintf(out, "\tmove.l\td7,-(a7)\n\trts\n");
		}
	}

	/* 2026-07-26, live auf Q9 gefunden: emitM68kCore()/tc_putint/tc_putuint/
	   tc_putchar/tc_io_write MUESSEN (wie tc_functab/tc_gadata/die Extern-
	   Wrapper-Stubs oben) NAH BEIEINANDER UND NAH AN DEN TABELLEN liegen --
	   vorher stand dieser ganze Block NACH der kompletten Funktionsrumpf-
	   Schleife weiter unten, was bei einem grossen Programm (z.B. ebnf.tc
	   allein, >17000 Zeilen generierter Assembler) "value out of range" fuer
	   das "lea tc_functab/tc_gadata(pc)" INNERHALB von tc_io_write ausloeste
	   (echter r68-Assemblierungsfehler, live reproduziert) -- tc_io_write lag
	   dann selbst weit ausserhalb der 32-KB-PC-relativ-Reichweite zu den
	   Tabellen. Deshalb JETZT hier (VOR der Funktionsrumpf-Schleife) statt
	   danach emittiert -- inhaltlich unveraendert, nur die Position im
	   erzeugten Assemblertext verschoben (keine der INTERNEN "bsr"-Distanzen
	   innerhalb dieses Blocks aendert sich dadurch, nur seine absolute
	   Position im Gesamttext). Mehrdatei-Uebersetzung (2026-07-25): der
	   gemeinsame 68k-Core/I/O-Anker (siehe partMode/runtimeMode-Kommentar
	   oben) wird unter -part NUR in GENAU EINER Datei emittiert (-runtime) --
	   sonst meldet l68 fuer JEDES dieser Symbole "duplicate symbol", da jede
	   Datei sonst ihre eigene Kopie mitbraechte. Ohne -part unveraendert
	   immer emittiert (Vollprogramm). */
	if (!partMode || runtimeMode) {
	emitM68kCore(out);
	if (os9Mode) {
		/* Echte Ausgabe ueber die reale Microware-clib.l-Funktion _os_write
		   (Signatur laut OS9/SRC/DEFS/modes.h: error_code _os_write(path_id,
		   const void*, u_int32 *count) -- count ist ein IN/OUT-Zeiger, path 1
		   = stdout, analog zu Unix-Filedeskriptoren). BEWUSST nicht ueber
		   printf/clib-Formatierung: QCC hat noch keine String-Literale, und
		   die direkte Ganzzahl->ASCII-Umwandlung hier (analog zu
		   runtime/arm64_darwin/start.s) haelt das gelinkte Programm klein --
		   kein printf-Formatstring-Parser wird ueberhaupt erst hereingezogen.
		   Ziffernzerlegung nutzt die BEREITS VORHANDENEN tc_udiv_u32/
		   tc_umod_u32-Routinen aus emitM68kCore (kein neuer Opcode). d2/d3/d4
		   ueberleben den bsr in diese Routinen unveraendert, da beide selbst
		   d2-d4 sichern/wiederherstellen (siehe deren Definition oben) --
		   deshalb hier KEIN eigenes Push/Pop noetig. */
		fprintf(out, "tc_putint:\n\tlink\t%s,#0\n", framePtr());
		fputs("\tmove.l\td0,d2\n\tmoveq\t#0,d3\n\ttst.l\td2\n\tbge\ttc_pi_nonneg\n", out);
		fputs("\tneg.l\td2\n\tmoveq\t#1,d3\n", out);
		fputs("tc_pi_nonneg:\tlea\ttc_io_buf+11(pc),a1\n\tmove.b\t#13,(a1)\n", out);
		fputs("tc_pi_loop:\tmove.l\td2,d0\n\tmoveq\t#10,d1\n\tbsr\ttc_udiv_u32\n\tmove.l\td0,d4\n", out);
		fputs("\tmove.l\td2,d0\n\tmoveq\t#10,d1\n\tbsr\ttc_umod_u32\n", out);
		fputs("\taddi.b\t#48,d0\n\tsubq.l\t#1,a1\n\tmove.b\td0,(a1)\n", out);
		fputs("\tmove.l\td4,d2\n\ttst.l\td2\n\tbne\ttc_pi_loop\n", out);
		fputs("\ttst.l\td3\n\tbeq\ttc_pi_go\n\tsubq.l\t#1,a1\n\tmove.b\t#45,(a1)\n", out);
		fputs("tc_pi_go:\tlea\ttc_io_buf+12(pc),a2\n\tmove.l\ta2,d1\n\tsub.l\ta1,d1\n\tbsr\ttc_io_write\n", out);
		fprintf(out, "\tunlk\t%s\n\trts\n\n", framePtr());

		fprintf(out, "tc_putuint:\n\tlink\t%s,#0\n", framePtr());
		fputs("\tmove.l\td0,d2\n\tlea\ttc_io_buf+11(pc),a1\n\tmove.b\t#13,(a1)\n", out);
		fputs("tc_pu_loop:\tmove.l\td2,d0\n\tmoveq\t#10,d1\n\tbsr\ttc_udiv_u32\n\tmove.l\td0,d4\n", out);
		fputs("\tmove.l\td2,d0\n\tmoveq\t#10,d1\n\tbsr\ttc_umod_u32\n", out);
		fputs("\taddi.b\t#48,d0\n\tsubq.l\t#1,a1\n\tmove.b\td0,(a1)\n", out);
		fputs("\tmove.l\td4,d2\n\ttst.l\td2\n\tbne\ttc_pu_loop\n", out);
		fputs("\tlea\ttc_io_buf+12(pc),a2\n\tmove.l\ta2,d1\n\tsub.l\ta1,d1\n\tbsr\ttc_io_write\n", out);
		fprintf(out, "\tunlk\t%s\n\trts\n\n", framePtr());

		fprintf(out, "tc_putchar:\n\tlink\t%s,#0\n", framePtr());
		fputs("\tlea\ttc_io_buf(pc),a1\n\tmove.b\td0,(a1)\n\tmoveq\t#1,d1\n\tbsr\ttc_io_write\n", out);
		fprintf(out, "\tunlk\t%s\n\trts\n\n", framePtr());

		/* a1=Puffer, d1=Laenge -- ruft _os_write(1,a1,&tc_io_cnt) auf.
		   2026-07-26, live auf Q9 gefunden (siehe emitLeaGlobal()/CALLEXT-
		   Kommentar): "bsr _os_write" ist ein ECHTER externer Aufruf wie jeder
		   CALLEXT, zerstoert also genauso a3/a4 (reine ABI-Temporaer-Register).
		   Dieser Aufruf hier ist aber FEST verdrahtet (nicht ueber die generische
		   CALLEXT-IR-Behandlung, siehe registerExtern()-Kommentar), muss daher
		   SEPARAT geschuetzt werden -- sonst korrumpiert JEDER putint/putuint/
		   putchar-Aufruf a3/a4 fuer den Rest des Programms (live reproduziert:
		   eigene Debug-putchar/putint-Aufrufe waehrend der Fehlersuche
		   korrumpierten a3/a4 und verfaelschten genau die Werte, die beobachtet
		   werden sollten). Auffrischung NUR bei largeDataMode noetig (ohne
		   -largedata gibt es keine a3/a4-Tabellenbasis) -- UND jetzt, da dieser
		   ganze Block nah an den Tabellen liegt, ist auch das "lea (pc)" hier
		   selbst immer sicher erreichbar. */
		fputs("tc_io_write:\n\tlea\ttc_io_cnt(pc),a2\n\tmove.l\td1,(a2)\n\tmove.l\ta1,d1\n", out);
		fputs("\tmove.l\ta2,-(a7)\n\tmoveq\t#1,d0\n\tbsr\t_os_write\n\tlea\t4(a7),a7\n", out);
		if (largeDataMode) fprintf(out, "\tlea\ttc_functab__%s(pc),a4\n\tlea\ttc_gadata__%s(pc),a3\n", psectName, psectName);
		fputs("\trts\n\n", out);
	} else {
		// Target-Runtime-Stubs: austauschbar; kein absoluter Zugriff und damit PIC-freundlich.
		fputs("tc_putint:\trts\t; Target Runtime ersetzt dies spaeter durch Ausgabe\n", out);
		fputs("tc_putuint:\trts\t; Target Runtime ersetzt dies spaeter durch Ausgabe\n", out);
		fputs("tc_putchar:\trts\t; Target Runtime ersetzt dies spaeter durch Ausgabe\n", out);
		fputs("tc_exit:\trts\t; Target Runtime beendet den Prozess\n", out);
	}
	/* Scratch-Feld fuer CALLEXT/CALLEXTP (siehe dort) -- max. 8 auf den Stack
	   gereichte Argumente eines externen Aufrufs. Immer deklariert (32 Byte),
	   unabhaengig davon ob das Programm CALLEXT tatsaechlich nutzt. vasm kennt
	   "ds.l" (reservierter, uninitialisierter Speicher); der echte Microware-
	   r68-Assembler kennt "ds.l" NICHT (empirisch verifiziert: "bad mnemonic"),
	   daher im os9-Modus stattdessen 8x "dc.l 0" (funktional gleichwertig: alle
	   Backend-Opcodes lesen den Wert erst NACH einem STORE hierher). */
	emitAlign(out);
	fputs(os9Mode ? "tc_extcall_tmp:\tdc.l\t0,0,0,0,0,0,0,0\n" : "tc_extcall_tmp:\tds.l\t8\n", out);
	if (os9Mode) {
		/* tc_io_buf: Ziffernpuffer fuer tc_putint/tc_putuint (max. "-2147483648\r"
		   = 12 Byte, rueckwaerts befuellt) UND Einzelbyte-Puffer fuer tc_putchar
		   (nutzt nur das erste Byte). tc_io_cnt: IN/OUT-Zaehlzelle fuer den
		   echten _os_write-Aufruf (siehe tc_io_write oben). */
		fputs("tc_io_buf:\tdc.l\t0,0,0\n", out);
		fputs("tc_io_cnt:\tdc.l\t0\n", out);
	}
	} /* !partMode || runtimeMode */

	/* os9Mode + largeDataMode: main ist der einzige Einsprungpunkt (kein
	   eigener tc_start) und muss dort "lea tc_functab(pc),a4" ausfuehren --
	   diese lea ist selbst PC-relativ und daher nur gueltig, wenn main DIREKT
	   nach der Tabelle liegt. main kann aber an beliebiger Stelle in funcs[]
	   registriert sein -- steht main nicht zuerst im Quelltext, koennen
	   riesige Funktionsrumpf-Texte ZWISCHEN Tabelle und main landen (genau
	   das brach beim 150-Funktionen-Skalierungstest fuer diese Erweiterung:
	   main stand am Ende, >32 KB entfernt, "value out of range" bei echtem
	   r68). Deshalb wird main hier -- NUR bei os9+largedata -- unabhaengig
	   von ihrer Position in funcs[] als allererste Funktion emittiert. Die
	   Funktionsindirektionstabelle selbst bleibt in Registrierungsreihenfolge
	   (ihre Eintraege sind absolute, vom Linker aufgeloeste Adressen und
	   haengen nicht von der Emissionsreihenfolge ab). */
	if (os9Mode && largeDataMode) {
		int mainIdx = findFunction("main");
		oi = 0;
		if (mainIdx >= 0) order[oi++] = mainIdx;
		for (fi = 0; fi < funcCount; fi++) if (fi != mainIdx) order[oi++] = fi;
	} else {
		for (fi = 0; fi < funcCount; fi++) order[fi] = fi;
	}
	for (oi = 0; oi < funcCount; oi++) {
		fi = order[oi];
		Function* fn = &funcs[fi];
		char asmName[NAME_LEN + 40];
		if (fn->declOnly) continue; /* definiert in einer ANDEREN Datei, kein Rumpf hier */
		if (os9Mode && strcmp(fn->name, "main") == 0) {
			fputs("main:\n", out);
			if (largeDataMode) fprintf(out, "\tlea\ttc_functab__%s(pc),a4\n", psectName);
			if (largeDataMode) fprintf(out, "\tlea\ttc_gadata__%s(pc),a3\n", psectName);
		}
		mangledName(asmName, "tc_", fn->name, fn->isStatic);
		fprintf(out, "%s:\tlink\t%s,#%d\n", asmName, framePtr(), -fn->frameBytes);
		/* WICHTIG (2026-07-26, live auf Q9 gefunden -- vierter, tiefster
		   -largedata-Bug dieser Sitzung): a3/a4 werden bisher NUR beim
		   Programmstart (main:) einmalig gesetzt UND nach jedem CALLEXT/
		   CALLEXTP aufgefrischt (siehe emitCall()/emitLeaGlobal()-Kommentar)
		   -- das reicht NICHT, sobald eine Funktion PER FUNCDECL AUS EINER
		   ANDEREN DATEI aufgerufen wird (Mehrdatei-Uebersetzung, jede Datei
		   hat ihre EIGENE tc_functab/tc_gadata)! Live reproduziert: ruft
		   Datei A eine in Datei B definierte Funktion auf (a4 zeigt zu
		   diesem Zeitpunkt noch auf DATEI A's Tabelle, vom Aufrufer
		   gesetzt), und DIESE Funktion ruft INTERN eine dritte Funktion
		   (z.B. einen Laufzeit-Helfer wie tc_putint) per emitCall() auf, so
		   verwendet dieser interne Aufruf FAELSCHLICH weiterhin Datei A's
		   Tabelle (a4 wurde nie auf Datei B's EIGENE Tabelle umgestellt) --
		   der Tabellenoffset selbst ist korrekt (verifiziert), aber er zeigt
		   in die FALSCHE Tabelle, ruft also eine VOELLIG ANDERE Funktion an
		   derselben Indexposition auf. Symptom im minimalen Reproduktionsfall:
		   ein Aufruf zu "putint(99)" in einer cross-file Funktion rief
		   stattdessen lautlos "tc_div_i32" auf (kein Absturz, aber keine
		   Ausgabe) -- im echten, groesseren ebnf.tc+codegen.tc-Programm mit
		   VIEL laengeren, unterschiedlich sortierten Tabellen fuehrt derselbe
		   Mechanismus zum beobachteten PMMU-Absturz (falscher Tabelleneintrag
		   zeigt auf Datenmuell, der als Adresse interpretiert wird). FIX:
		   JEDE Funktion (nicht nur main) frischt a3/a4 auf IHRE EIGENE Tabelle
		   auf, GLEICH NACH dem eigenen "link" -- unabhaengig davon, ob sie
		   aus derselben oder einer anderen Datei aufgerufen wurde. Kostet
		   zwei zusaetzliche Instruktionen pro Funktionsaufruf (ueberschaubarer
		   Overhead), garantiert aber Korrektheit unabhaengig vom Aufrufer.
		   main() selbst behaelt sein bereits vorhandenes Refresh VOR dem
		   eigenen Label (siehe oben) -- das hier ist zusaetzlich, harmlos
		   redundant fuer main, aber noetig fuer ALLE anderen Funktionen.
		   ZWEITER FUND (direkt im Anschluss, live auf Q9): ein simples
		   "lea tc_functab(pc),a4" HIER (an JEDER Funktion, potenziell weit
		   von der eigenen Tabelle entfernt in einer grossen Datei) sprengt
		   sofort wieder die 16-Bit-PC-relativ-Grenze ("value out of range"
		   bei echtem r68, live reproduziert an writeWorkfile()) -- GENAU das
		   Problem, das die ganze a3/a4-Indirektion ja eigentlich umgehen
		   sollte. RICHTIGER FIX (verifiziert per direkter Byte-Analyse eines
		   Minimaltests mit 20000 nop dazwischen, sowohl r68-Assemblierung ALS
		   AUCH die erzeugten Bytes bestaetigt korrekt): PC-relative
		   Adressierung selbst hat KEINE Moeglichkeit, weiter als 32 KB zu
		   reichen -- das ist eine echte 68000-Hardwaregrenze, keine
		   Syntaxfrage. Aber eine Funktion kann IMMER sicher (Distanz 0) ihre
		   EIGENE Adresse per "lea <eigenerName>(pc),aX" laden (bezieht sich
		   auf sich selbst!), und DANACH per "adda.l #(ziel-eigenerName),aX"
		   eine LINK-ZEIT-KONSTANTE Differenz addieren -- diese Differenz ist
		   ein reiner arithmetischer 32-Bit-Immediate-Wert OHNE jede
		   Distanzbeschraenkung (nur "adda.l"/"add.l #imm32,Dn" selbst hat
		   keine PC-relativ-Grenze, im Gegensatz zu "d(pc)"-Adressierungs-
		   arten). So kann JEDE Funktion, egal wie weit von ihrer eigenen
		   Tabelle entfernt, diese trotzdem sicher erreichen. */
		if (largeDataMode) {
			fprintf(out, "\tlea\t%s(pc),a4\n\tadda.l\t#(tc_functab__%s-%s),a4\n", asmName, psectName, asmName);
			fprintf(out, "\tlea\t%s(pc),a3\n\tadda.l\t#(tc_gadata__%s-%s),a3\n", asmName, psectName, asmName);
		}
		/* BIG-ENDIAN-KORREKTUR FUER char-PARAMETER (2026-08-10, live am
		   selbstgehosteten EBNF-Generator gefunden). Der Aufrufer legt JEDES
		   Argument als volles 32-Bit-Langwort ab ("move.l #wert,-(a7)", siehe
		   PUSH/emitCall) -- der Bytewert eines char-Parameters steht damit im
		   NIEDERWERTIGSTEN Byte des Slots, auf dem Big-Endian-68k also bei
		   Slot+3. LOADC/STOREC/ADDRL adressieren aber die Slot-BASIS (bei
		   LOKALEN Slots ist das korrekt und in sich konsistent, weil dort
		   STOREC und LOADC dieselbe Adresse benutzen) und lasen deshalb das
		   HOECHSTWERTIGE Byte -- fuer jeden ASCII-Wert konstant 0.
		   SYMPTOM: der von QCC uebersetzte Generator erzeugte fuer JEDEN
		   Zeichenbereich der Grammatik ("0"~"9", "A"~"F", "a"~"z", "A"~"Z")
		   die Grenzen 0x00/0x00 statt 0x30/0x39 usw. -- astPushRNG(char lo,
		   char hi) bekam beide Grenzen als 0. Sonst war die Ausgabe mit der
		   xcc-gebauten Referenz bitgleich.
		   WARUM BISHER UNENTDECKT: ARM64 ist Little-Endian (dort liegt das
		   niederwertige Byte ZUFAELLIG an der Slot-Basis, der Code war also
		   versehentlich richtig) und QCCVM haelt typisierte Slots statt roher
		   Stack-Langworte -- beide Referenzpfade konnten den Fehler prinzipiell
		   nicht zeigen. Nur echtes 68k-Big-Endian ist betroffen.
		   FIX: einmalig im Prolog das niederwertige Byte an die Slot-Basis
		   kopieren. Danach stimmen ALLE bestehenden Byte-Zugriffspfade
		   (LOADC, STOREC sowie ADDRL+LOADIND/STOREIND) unveraendert ueberein --
		   exakt wie bei lokalen char-Slots, kein Eingriff an den Opcodes noetig.
		   Betroffen sind nur Parameter, die im Rumpf TATSAECHLICH byteweise
		   benutzt werden (LOADC/STOREC auf ihrem Slot) -- das ist der
		   eindeutige Beleg, dass es ein char-Parameter ist; int- und
		   Pointer-Parameter bleiben unangetastet.
		   BEWUSST OFFENE RESTLUECKE: ein char-Parameter, dessen Adresse per
		   ADDRL genommen wird, OHNE dass er irgendwo per LOADC/STOREC
		   angefasst wird ("void f(char c){char* p; p=&c; ...}"), wird hier
		   nicht erkannt -- die IR ("FUNC <name> <nargs>", s. docs/IR_OPCODES.md)
		   traegt keine Parametertypen, und ADDRL allein ist kein Beleg fuer
		   char (bei einem int-Parameter waere die Verengung sogar falsch).
		   Im echten Generator kommt dieser Fall nicht vor. */
		{
			int pslot;
			int pk;
			for (pslot = 0; pslot < fn->nargs; pslot++) {
				int usedAsChar = 0;
				int off;
				for (pk = fn->first; pk < fn->last; pk++) {
					Instr* px = &ir[pk];
					if (px->argc == 1 &&
						(strcmp(px->op, "LOADC") == 0 || strcmp(px->op, "STOREC") == 0) &&
						number(px->args[0], px->line) == pslot) {
						usedAsChar = 1;
						break;
					}
				}
				if (usedAsChar) {
					off = 8 + 4 * (fn->nargs - 1 - pslot);
					fprintf(out, "\tmove.b\t%d(%s),%d(%s)\n", off + 3, framePtr(), off, framePtr());
				}
			}
		}
		for (k = fn->first; k < fn->last; k++) {
			Instr* insP = &ir[k];
			const char* op = insP->op;

			if (strcmp(op, "PUSH") == 0 && insP->argc == 1) {
				fprintf(out, "\tmove.l\t#%s,-(a7)\n", insP->args[0]);
			} else if (strcmp(op, "LOADL") == 0 && insP->argc == 1) {
				slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
				fprintf(out, "\tmove.l\t%s,-(a7)\n", addrBuf);
			} else if (strcmp(op, "STOREL") == 0 && insP->argc == 1) {
				slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
				fprintf(out, "\tmove.l\t(a7)+,%s\n", addrBuf);
			} else if (strcmp(op, "LOADC") == 0 && insP->argc == 1) {
				slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
				fprintf(out, "\tmoveq\t#0,d0\n\tmove.b\t%s,d0\n\tmove.l\td0,-(a7)\n", addrBuf);
			} else if (strcmp(op, "STOREC") == 0 && insP->argc == 1) {
				slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
				fprintf(out, "\tmove.l\t(a7)+,d0\n\tmove.b\td0,%s\n", addrBuf);
			} else if (strcmp(op, "LOADP") == 0 && insP->argc == 1) {
				slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
				fprintf(out, "\tmove.l\t%s,-(a7)\n", addrBuf);
			} else if (strcmp(op, "STOREP") == 0 && insP->argc == 1) {
				slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
				fprintf(out, "\tmove.l\t(a7)+,%s\n", addrBuf);
			} else if (strcmp(op, "ADDRL") == 0 && insP->argc == 1) {
				slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
				fprintf(out, "\tlea\t%s,a0\n\tmove.l\ta0,-(a7)\n", addrBuf);
			} else if (strcmp(op, "ADDRG") == 0 && insP->argc == 1) {
				int gidx = findGlobal(insP->args[0]);
				if (gidx < 0) fatal("unbekannte globale Variable");
				emitLeaGlobal(out, gidx, "a0");
				fputs("\tmove.l\ta0,-(a7)\n", out);
			} else if (strcmp(op, "LARRAY") == 0 && insP->argc == 3) {
				/* nur Frame-Layout, kein Code */
			} else if (strcmp(op, "PUSHADDR") == 0 && insP->argc == 2) {
				int ignored;
				if (strcmp(insP->args[0], "L") == 0) {
					int off = arrayOffset(fn, number(insP->args[1], insP->line), &ignored, insP->line);
					fprintf(out, "\tlea\t-%d(%s),a0\n", off, framePtr());
				} else if (strcmp(insP->args[0], "P") == 0) {
					slotAddress(addrBuf, number(insP->args[1], insP->line), fn, insP->line);
					fprintf(out, "\tmove.l\t%s,a0\n", addrBuf);
				} else if (findGlobal(insP->args[1]) >= 0 && strcmp(insP->args[0], "G") == 0) {
					emitLeaGlobal(out, findGlobal(insP->args[1]), "a0");
				} else {
					fatal("unbekanntes Array");
				}
				fputs("\tmove.l\ta0,-(a7)\n", out);
			} else if ((strcmp(op, "LOADIDX") == 0 || strcmp(op, "STOREIDX") == 0) && insP->argc == 3) {
				int isChar = isByteWord(insP->args[2]);
				if (strcmp(insP->args[2], "i") != 0 && strcmp(insP->args[2], "p") != 0 && !isChar) fatal("unbekannter Arraytyp");
				if (strcmp(op, "STOREIDX") == 0) fputs("\tmove.l\t(a7)+,d0\n", out);
				fputs("\tmove.l\t(a7)+,d1\n", out);
				if (!isChar) fputs("\tlsl.l\t#2,d1\n", out);
				if (strcmp(insP->args[0], "L") == 0) {
					int off = arrayOffset(fn, number(insP->args[1], insP->line), &isChar, insP->line);
					fprintf(out, "\tlea\t-%d(%s),a0\n", off, framePtr());
				} else if (strcmp(insP->args[0], "P") == 0) {
					slotAddress(addrBuf, number(insP->args[1], insP->line), fn, insP->line);
					fprintf(out, "\tmove.l\t%s,a0\n", addrBuf);
				} else if (findGlobal(insP->args[1]) >= 0 && strcmp(insP->args[0], "G") == 0) {
					emitLeaGlobal(out, findGlobal(insP->args[1]), "a0");
				} else {
					fatal("unbekanntes Array");
				}
				fputs("\tadd.l\td1,a0\n", out);
				if (strcmp(op, "LOADIDX") == 0) {
					if (isChar) fputs("\tmoveq\t#0,d0\n\tmove.b\t(a0),d0\n", out);
					else fputs("\tmove.l\t(a0),d0\n", out);
					fputs("\tmove.l\td0,-(a7)\n", out);
				} else {
					fprintf(out, "\tmove.%s\td0,(a0)\n", isChar ? "b" : "l");
				}
			} else if (strcmp(op, "LOADG") == 0 && insP->argc == 1) {
				int gidx = findGlobal(insP->args[0]); char gAsmName[NAME_LEN + 40];
				if (gidx < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
				mangledName(gAsmName, "tc_g_", insP->args[0], globals[gidx].isStatic);
				/* small: direkter PC-relativer Wert-Load (Kurzform); large: erst die
				   Adresse aus der Indirektionstabelle holen, dann dereferenzieren --
				   siehe emitLeaGlobal()-Kommentar. */
				if (largeDataMode) { emitLeaGlobal(out, gidx, "a0"); fputs("\tmove.l\t(a0),-(a7)\n", out); }
				else fprintf(out, "\tmove.l\t%s(pc),-(a7)\n", gAsmName);
			} else if (strcmp(op, "STOREG") == 0 && insP->argc == 1) {
				int gidx = findGlobal(insP->args[0]); char gAsmName[NAME_LEN + 40];
				if (gidx < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
				mangledName(gAsmName, "tc_g_", insP->args[0], globals[gidx].isStatic);
				fputs("\tmove.l\t(a7)+,d0\n", out);
				emitLeaGlobal(out, gidx, "a0");
				fputs("\tmove.l\td0,(a0)\n", out);
			} else if (strcmp(op, "LOADGC") == 0 && insP->argc == 1) {
				int gidx = findGlobal(insP->args[0]); char gAsmName[NAME_LEN + 40];
				if (gidx < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
				mangledName(gAsmName, "tc_g_", insP->args[0], globals[gidx].isStatic);
				if (largeDataMode) { emitLeaGlobal(out, gidx, "a0"); fputs("\tmoveq\t#0,d0\n\tmove.b\t(a0),d0\n\tmove.l\td0,-(a7)\n", out); }
				else fprintf(out, "\tmoveq\t#0,d0\n\tmove.b\t%s(pc),d0\n\tmove.l\td0,-(a7)\n", gAsmName);
			} else if (strcmp(op, "STOREGC") == 0 && insP->argc == 1) {
				int gidx = findGlobal(insP->args[0]); char gAsmName[NAME_LEN + 40];
				if (gidx < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
				mangledName(gAsmName, "tc_g_", insP->args[0], globals[gidx].isStatic);
				fputs("\tmove.l\t(a7)+,d0\n", out);
				emitLeaGlobal(out, gidx, "a0");
				fputs("\tmove.b\td0,(a0)\n", out);
			} else if ((strcmp(op, "LOADGP") == 0 || strcmp(op, "STOREGP") == 0) && insP->argc == 1) {
				int gidx = findGlobal(insP->args[0]); char gAsmName[NAME_LEN + 40];
				if (gidx < 0) fatal("unbekannte globale Variable");
				mangledName(gAsmName, "tc_g_", insP->args[0], globals[gidx].isStatic);
				if (strcmp(op, "LOADGP") == 0) {
					if (largeDataMode) { emitLeaGlobal(out, gidx, "a0"); fputs("\tmove.l\t(a0),-(a7)\n", out); }
					else fprintf(out, "\tmove.l\t%s(pc),-(a7)\n", gAsmName);
				} else {
					fputs("\tmove.l\t(a7)+,d0\n", out);
					emitLeaGlobal(out, gidx, "a0");
					fputs("\tmove.l\td0,(a0)\n", out);
				}
			} else if (strcmp(op, "PTRINDEX") == 0 && insP->argc == 1) {
				fputs("\tmove.l\t(a7)+,a0\n\tmove.l\t(a7)+,d0\n", out);
				if (!isByteWord(insP->args[0])) fputs("\tlsl.l\t#2,d0\n", out);
				fputs("\tadda.l\td0,a0\n\tmove.l\ta0,-(a7)\n", out);
			} else if ((strcmp(op, "LOADIND") == 0 || strcmp(op, "STOREIND") == 0) && insP->argc == 1) {
				int byte = isByteWord(insP->args[0]);
				if (strcmp(op, "LOADIND") == 0) {
					fputs("\tmove.l\t(a7)+,a0\n", out);
					if (byte) fputs("\tmoveq\t#0,d0\n\tmove.b\t(a0),d0\n", out); else fputs("\tmove.l\t(a0),d0\n", out);
					fputs("\tmove.l\td0,-(a7)\n", out);
				} else {
					fprintf(out, "\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,a0\n\tmove.%s\td0,(a0)\n", byte ? "b" : "l");
				}
			} else if ((strcmp(op, "PADD") == 0 || strcmp(op, "PSUB") == 0) && insP->argc == 1) {
				fputs("\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,a0\n", out);
				if (!isByteWord(insP->args[0])) fputs("\tlsl.l\t#2,d0\n", out);
				if (strcmp(op, "PSUB") == 0) fputs("\tneg.l\td0\n", out);
				fputs("\tadda.l\td0,a0\n\tmove.l\ta0,-(a7)\n", out);
			} else if (strcmp(op, "IPADD") == 0 && insP->argc == 1) {
				fputs("\tmove.l\t(a7)+,a0\n\tmove.l\t(a7)+,d0\n", out);
				if (!isByteWord(insP->args[0])) fputs("\tlsl.l\t#2,d0\n", out);
				fputs("\tadda.l\td0,a0\n\tmove.l\ta0,-(a7)\n", out);
			} else if (strcmp(op, "IPADDN") == 0 && insP->argc == 1) {
				/* wie IPADD, aber Skalierung um eine LAUFZEIT-Byte-Groesse (z.B. structByteSize)
				   statt einer festen Typtag-Groesse -- kein lsl.l (Groesse ist beliebig, nicht
				   nur 1/4), echte Multiplikation ueber tc_mul_i32 (siehe emitM68kCore). a0 (Pointer)
				   bleibt beim bsr unangetastet -- tc_mul_i32 nutzt nur d0-d4. */
				fprintf(out, "\tmove.l\t(a7)+,a0\n\tmove.l\t(a7)+,d0\n\tmove.l\t#%s,d1\n", insP->args[0]);
				emitCall(out, "tc_mul_i32", helperTableOffset("tc_mul_i32"), &serial, psectName);
				fputs("\tadda.l\td0,a0\n\tmove.l\ta0,-(a7)\n", out);
			} else if (strcmp(op, "PDIFF") == 0 && insP->argc == 1) {
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tsub.l\td1,d0\n", out);
				if (!isByteWord(insP->args[0])) fputs("\tasr.l\t#2,d0\n", out);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "ADD") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tadd.l\t(a7)+,d1\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "SUB") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tsub.l\td1,d0\n\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "NEG") == 0) {
				fputs("\tneg.l\t(a7)\n", out);
			} else if (strcmp(op, "NOT") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n\ttst.l\td0\n\tseq\td0\n\tandi.l\t#1,d0\n\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "NOTBIT") == 0) {
				fputs("\tnot.l\t(a7)\n", out);
			} else if (strcmp(op, "BAND") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tand.l\t(a7)+,d1\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "BXOR") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\teor.l\td1,d0\n\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "BOR") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tor.l\t(a7)+,d1\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "SHL") == 0 || strcmp(op, "SHR") == 0 || strcmp(op, "USHR") == 0) {
				const char* mnem = strcmp(op, "SHL") == 0 ? "lsl" : strcmp(op, "SHR") == 0 ? "asr" : "lsr";
				fprintf(out, "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\t%s.l\td1,d0\n\tmove.l\td0,-(a7)\n", mnem);
			} else if (strcmp(op, "NARROWC") == 0) {
				fputs("\tmove.l\t(a7),d0\n\tandi.l\t#255,d0\n\tmove.l\td0,(a7)\n", out);
			} else if (strcmp(op, "SWAP") == 0) {
				/* Vertauscht die obersten zwei Stackelemente. Gebraucht ueberall dort,
				   wo ein Ergebniswert UNTER einer Adresse liegen bleiben muss --
				   "(*p)++", "a[i]++" und die Kettenzuweisung scheiterten allesamt
				   daran, dass sich der Stack bisher nicht umordnen liess (es gab nur
				   DUP). */
				fputs("\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,d1\n\tmove.l\td0,-(a7)\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "DUP") == 0 || strcmp(op, "DUPP") == 0) {
				fputs("\tmove.l\t(a7),-(a7)\n", out);
			} else if (strcmp(op, "MUL") == 0 || strcmp(op, "DIV") == 0 || strcmp(op, "UDIV") == 0 || strcmp(op, "MOD") == 0 || strcmp(op, "UMOD") == 0) {
				const char* fn2 = strcmp(op, "MUL") == 0 ? "mul_i32" : strcmp(op, "DIV") == 0 ? "div_i32" : strcmp(op, "UDIV") == 0 ? "udiv_u32" : strcmp(op, "MOD") == 0 ? "mod_i32" : "umod_u32";
				char helperAsmName[24];
				sprintf(helperAsmName, "tc_%s", fn2);
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n", out);
				emitCall(out, helperAsmName, helperTableOffset(helperAsmName), &serial, psectName);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "CMPLT") == 0) { emitCompare(out, "blt", &serial);
			} else if (strcmp(op, "CMPGT") == 0) { emitCompare(out, "bgt", &serial);
			} else if (strcmp(op, "CMPLE") == 0) { emitCompare(out, "ble", &serial);
			} else if (strcmp(op, "CMPGE") == 0) { emitCompare(out, "bge", &serial);
			} else if (strcmp(op, "CMPEQ") == 0) { emitCompare(out, "beq", &serial);
			} else if (strcmp(op, "CMPNE") == 0) { emitCompare(out, "bne", &serial);
			} else if (strcmp(op, "CMPULT") == 0) { emitCompare(out, "bcs", &serial);
			} else if (strcmp(op, "CMPUGT") == 0) { emitCompare(out, "bhi", &serial);
			} else if (strcmp(op, "CMPULE") == 0) { emitCompare(out, "bls", &serial);
			} else if (strcmp(op, "CMPUGE") == 0) { emitCompare(out, "bcc", &serial);
			} else if (strcmp(op, "PCMPEQ") == 0) { emitCompare(out, "beq", &serial);
			} else if (strcmp(op, "PCMPNE") == 0) { emitCompare(out, "bne", &serial);
			} else if (strcmp(op, "PCMPLT") == 0) { emitCompare(out, "bcs", &serial);
			} else if (strcmp(op, "PCMPGT") == 0) { emitCompare(out, "bhi", &serial);
			} else if (strcmp(op, "PCMPLE") == 0) { emitCompare(out, "bls", &serial);
			} else if (strcmp(op, "PCMPGE") == 0) { emitCompare(out, "bcc", &serial);
			} else if (strcmp(op, "LABEL") == 0 && insP->argc == 1) {
				/* psectName-Suffix aus demselben Grund wie bei emitCompare oben:
				   LABEL-Namen (tc_L0, tc_L1, ...) kommen aus der QCC-Frontend-
				   eigenen Label-Nummerierung, die in JEDER Datei wieder bei 0
				   startet -- ohne Suffix kollidieren sie beim Mehrdatei-Link,
				   sobald zwei Dateien beide Kontrollfluss (if/while/for/...)
				   enthalten (praktisch immer der Fall). */
				fprintf(out, "tc_%s__%s:\n", insP->args[0], psectName);
			} else if (strcmp(op, "JMP") == 0 && insP->argc == 1) {
				fprintf(out, "\tbra\ttc_%s__%s\n", insP->args[0], psectName);
			} else if (strcmp(op, "JZ") == 0 && insP->argc == 1) {
				fprintf(out, "\tmove.l\t(a7)+,d0\n\ttst.l\td0\n\tbeq\ttc_%s__%s\n", insP->args[0], psectName);
			} else if (strcmp(op, "JNZ") == 0 && insP->argc == 1) {
				fprintf(out, "\tmove.l\t(a7)+,d0\n\ttst.l\td0\n\tbne\ttc_%s__%s\n", insP->args[0], psectName);
			} else if ((strcmp(op, "CALL") == 0 || strcmp(op, "CALLP") == 0) && insP->argc == 2) {
				int nargsC = number(insP->args[1], insP->line);
				int callee = findFunction(insP->args[0]);
				char asmName[NAME_LEN + 40];
				if (callee < 0) { sprintf(msg, "IR Zeile %d: unbekannte Funktion %s", insP->line, insP->args[0]); fatal(msg); }
				mangledName(asmName, "tc_", insP->args[0], funcs[callee].isStatic);
				emitCall(out, asmName, callee * 4, &serial, psectName);
				if (nargsC) fprintf(out, "\tlea\t%d(a7),a7\n", nargsC * 4);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "PUSHFN") == 0 && insP->argc == 1) {
				/* Adresse einer QCC-Funktion als Wert auf den Stack (Funktionszeiger).
				   Im -largedata-Modus liegt sie NICHT als Symbol vor, sondern als
				   Link-Zeit-Offset in der Funktionsindirektionstabelle: die echte
				   Laufzeitadresse ist a4 + *(a4 + index*4) -- exakt dieselbe Rechnung,
				   die emitCall() fuer den direkten Aufruf macht (siehe dort). */
				int fnIdx = findFunction(insP->args[0]);
				char asmName[NAME_LEN + 40];
				if (fnIdx < 0) { sprintf(msg, "IR Zeile %d: unbekannte Funktion %s", insP->line, insP->args[0]); fatal(msg); }
				if (largeDataMode) {
					/* "add.l a4,d0", NICHT "adda.l": ADDA verlangt ein ADRESSregister
					   als Ziel (emitCall() rechnet deshalb in a2). Hier ist das Ziel
					   ein Datenregister, also das normale ADD -- "ADD.L An,Dn" ist
					   zulaessig. Der echte r68 weist "adda.l a4,d0" korrekt ab
					   ("incomplete line: code not generated"). */
					fprintf(out, "\tmove.l\t%d(a4),d0\n\tadd.l\ta4,d0\n\tmove.l\td0,-(a7)\n", fnIdx * 4);
				} else {
					mangledName(asmName, "tc_", insP->args[0], funcs[fnIdx].isStatic);
					fprintf(out, "\tlea\t%s(pc),a0\n\tmove.l\ta0,-(a7)\n", asmName);
				}
			} else if ((strcmp(op, "CALLIND") == 0 || strcmp(op, "CALLINDP") == 0) && insP->argc == 1) {
				/* Indirekter Aufruf ueber einen Funktionszeiger. Stapelbelegung beim
				   Eintritt (von UNTEN nach oben): zuerst der Zeiger, darueber
				   arg1..argN. Diese Reihenfolge ergibt sich zwangslaeufig aus dem
				   Parsen: bei "ausdruck(args)" wird der Callee-Ausdruck VOR den
				   Argumenten ausgewertet und legt seinen Wert deshalb zuerst ab
				   (anders als beim direkten CALL, wo der Name gar keinen Code
				   erzeugt). Der Zeiger wird folglich NICHT gepoppt, sondern in der
				   Tiefe nargs*4 gelesen; am Ende werden Argumente UND Zeiger
				   gemeinsam abgeraeumt. a3/a4 muessen nach dem jsr aufgefrischt
				   werden (gleiche Begruendung wie in emitCall(): der Aufgerufene hat
				   seine EIGENEN Tabellenzeiger gesetzt). */
				int nargsI = number(insP->args[0], insP->line);
				fprintf(out, "\tmove.l\t%d(a7),a2\n\tjsr\t(a2)\n", nargsI * 4);
				if (largeDataMode) {
					int id = serial++;
					fprintf(out, "tc_callret_%d__%s:\n", id, psectName);
					fprintf(out, "\tlea\ttc_callret_%d__%s(pc),a4\n\tadda.l\t#(tc_functab__%s-tc_callret_%d__%s),a4\n", id, psectName, psectName, id, psectName);
					fprintf(out, "\tlea\ttc_callret_%d__%s(pc),a3\n\tadda.l\t#(tc_gadata__%s-tc_callret_%d__%s),a3\n", id, psectName, psectName, id, psectName);
				}
				fprintf(out, "\tlea\t%d(a7),a7\n", (nargsI + 1) * 4);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if ((strcmp(op, "CALLEXT") == 0 || strcmp(op, "CALLEXTP") == 0) && insP->argc == 3) {
				/* Aufruf einer NICHT in dieser IR definierten (externen) Funktion, z.B.
				   einer echten OS-9/Microware-clib-Funktion (strcmp, printf, malloc, ...).
				   Nutzt die dokumentierte Microware-68K-C/C++-ABI (Ultra C/C++ Processor
				   Guide, Kapitel "Passing Arguments to Functions") statt der sonst hier
				   verwendeten reinen Stack-ABI fuer QCC-EIGENE Funktionen: die ERSTEN
				   BEIDEN FEST DEKLARIERTEN Parameter -> d0/d1 (GENAU wie bei einem
				   nicht-variadischen Aufruf), ALLE weiteren Argumente (der variadische
				   "..."-Teil, z.B. printfs Werte nach dem Formatstring) auf den Stack, in
				   UMGEKEHRTER Erscheinungsreihenfolge gepusht (das erste ueberzaehlige
				   Argument landet dadurch am NAECHSTEN zur Ruecksprungadresse).
				   WICHTIG (2026-07-24, live gegen die echte Microware-clib.l auf Q9
				   gefunden UND korrigiert): das dritte IR-Feld ist NICHT die Gesamtzahl
				   der Argumente oder ein reines variadic-Bool, sondern tcFunctionNargs[f]
				   -- die Anzahl der FEST DEKLARIERTEN Parameter laut extern-Deklaration
				   ("..." selbst zaehlt nicht mit). Der fruehere Stand nahm faelschlich an,
				   ein variadischer Aufruf lege AUSNAHMSLOS alles auf den Stack (0 Register)
				   -- das entsprach nur unserem eigenen, nie gegen echten Compiler-Code
				   verifizierten Mock-Test. Der ECHTE, von xcc erzeugte Aufrufcode zu
				   printf(fmt, x) zeigt: der Formatstring selbst (1 fest deklarierter
				   Parameter) landet ganz normal in d0, NUR x (der variadische Teil) auf
				   dem Stack -- das PMMU-Absturzbild auf Q9 (Adresse 1 statt eines echten
				   Pointers) entstand exakt daraus, dass unser altes "0 Register bei
				   variadisch"-Schema den Formatstring faelschlich auf den Stack statt nach
				   d0 legte.
				   Unser eigener Stack-IR liefert alle Argumente bereits in
				   Erscheinungsreihenfolge auf a7 (1. Argument am weitesten unten, da
				   zuerst gepusht) -- die obersten "stackArgs" Werte werden daher zuerst
				   in ein festes Scratch-Feld ausgelagert (tc_extcall_tmp), damit sie
				   NICHT verloren gehen, wenn darunter noch d0/d1 herausgeholt werden
				   muessen; anschliessend werden sie in DERSELBEN (bereits umgekehrten)
				   Reihenfolge zurueckgepusht. Der Aufruf selbst geht per "jsr" auf den
				   ROHEN Funktionsnamen (kein "tc_"-Praefix wie bei internen Aufrufen) --
				   das eigentliche Linken gegen die reale clib.l (externe Symbolaufloesung
				   via l68 statt der aktuellen "vasm -Fbin"-Direktassemblierung) ist noch
				   NICHT Teil dieses Schritts, siehe docs/FORTSCHRITT.md. */
				int nargsC = number(insP->args[1], insP->line);
				int fixedCount = number(insP->args[2], insP->line);
				/* BUG (2026-07-26/27, live auf Q9 gefunden, per capstone-Disassemblierung
				   der ECHTEN clib.l-printf bestaetigt): die Annahme "NUR die FEST
				   deklarierten Parameter gehen nach d0/d1, der GESAMTE variadische Teil
				   auf den Stack" (2026-07-24-Fund) war FALSCH bzw. unvollstaendig. Die
				   echte, kompilierte printf(char* fmt, ...) beginnt mit "move.l d0,-(a7)"
				   gefolgt von "move.l d1,d0" -- sie erwartet also ihr ERSTES variadisches
				   Argument (falls vorhanden) IMMER in d1, unabhaengig davon, ob es laut
				   Deklaration "fest" oder Teil von "..." ist. Die REALE Regel ist: die
				   ERSTEN ZWEI ARGUMENTE INSGESAMT (fest+variadisch zusammengezaehlt) gehen
				   nach d0/d1, NUR ab dem DRITTEN Argument geht es auf den Stack -- exakt
				   wie bei einem nicht-variadischen Aufruf, OHNE Sonderrolle fuer "...".
				   fixedCount wird nicht mehr fuer die Register/Stack-Aufteilung gebraucht
				   (bleibt nur zur IR-Validierung erhalten). */
				int hasD0 = nargsC >= 1;
				int hasD1 = nargsC >= 2;
				int stackArgs = nargsC - (hasD0 ? 1 : 0) - (hasD1 ? 1 : 0);
				int ai;
				if (stackArgs > 8) { sprintf(msg, "IR Zeile %d: zu viele Stack-Argumente fuer externen Aufruf (max 8)", insP->line); fatal(msg); }
				/* Adresse EINMAL in a0 (a0 ist in diesem Backend generell ein freies
				   Scratch-Adressregister, wird von keinem IR-Opcode ueber dessen eigene
				   Emission hinaus als gueltig vorausgesetzt). small: PC-relative "lea"
				   passend zum PIC-Stil des restlichen Backends (vgl. tc_g_<name>(pc)-
				   Zugriffe); large: derselbe a3-Indirektionsmechanismus wie bei echten
				   Globalen (siehe emitLeaGlobal()-Kommentar) -- tc_extcall_tmp bekommt
				   dafuer einen zusaetzlichen Tabelleneintrag NACH allen echten Globalen
				   (Offset globalCount*4). */
				if (stackArgs > 0) {
					/* 2026-07-26: tc_gadata-Eintrag ist ein Link-Zeit-Offset, kein
					   absoluter Zeiger (siehe emitLeaGlobal()-Kommentar) -- adda.l
					   noetig wie ueberall sonst. */
					if (largeDataMode) fprintf(out, "\tmove.l\t%d(a3),a0\n\tadda.l\ta3,a0\n", globalCount * 4);
					else fputs("\tlea\ttc_extcall_tmp(pc),a0\n", out);
				}
				for (ai = 0; ai < stackArgs; ai++) fprintf(out, "\tmove.l\t(a7)+,%d(a0)\n", ai * 4);
				if (hasD1) fputs("\tmove.l\t(a7)+,d1\n", out);
				if (hasD0) fputs("\tmove.l\t(a7)+,d0\n", out);
				for (ai = 0; ai < stackArgs; ai++) fprintf(out, "\tmove.l\t%d(a0),-(a7)\n", ai * 4);
				/* WICHTIG (2026-07-24, live am echten Q9 gefunden): "jsr <name>" auf
				   ein externes Symbol wird von r68/l68 NUR dann PIC-sicher (PC-
				   relativ) aufgeloest, wenn es als "bsr" geschrieben wird -- ein
				   rohes "jsr" assembliert stattdessen zu einer ABSOLUTEN Adresse
				   (r68 kennt bei "jsr" keine automatische PC-relative Kodierung fuer
				   externe/undefinierte Symbole), was bei einem NICHT bei Adresse 0
				   geladenen OS-9-Modul sofort einen PMMU-Fehler ausloest (reproduziert:
				   ein Testaufruf gegen die echte clib.l stuerzte den Q9-Emulator ab,
				   Disassemblierung zeigte "JSR $xxxx.L" statt einer PC-relativen
				   Kodierung). "bsr" ist dagegen inhaerent PC-relativ; bei zu grosser
				   Distanz haengt der Microware-Linker automatisch eine PIC-taugliche
				   Jumptable-Indirektion ein (l68 -a). NUR im -os9-Modus relevant --
				   im Default-/vasm-/Simulator-Modus bleibt "jsr" (von
				   tools/qcc68sim.py als Mock-Aufruf-Marker erkannt, keine echte
				   Positionsunabhaengigkeit noetig, keine Regression riskieren).
				   ZWEITER FUND (2026-07-26, live auf Q9, Ultra-C/C++ Processor Guide
				   Table 1-12 "Register Use"): a3/a4 (unsere -largedata-Tabellenbasen)
				   sind reine ABI-Temporaer-Register -- JEDE echte clib.l-Funktion darf
				   sie zerstoeren. Ein direktes "bsr/jsr <name>" HIER wuerde a3/a4 also
				   unbemerkt korrumpieren. Deshalb (NUR largeDataMode): nicht direkt
				   rufen, sondern ueber denselben a4-Tabellen-Indirektionsmechanismus
				   wie interne QCC-Funktionen (emitCall()) einen kleinen Wrapper-Stub
				   rufen (tc_extwrap_<name>, siehe Tabellen-Emission weiter oben) -- der
				   macht den echten Aufruf UND frischt a3/a4 danach auf, physisch nah an
				   den Tabellen platziert (PC-relativ immer sicher erreichbar, anders als
				   die Aufrufstelle hier, die beliebig weit entfernt sein kann). Der
				   Wrapper ist transparent: er sieht/reicht dieselben d0/d1/Stack-Werte
				   durch wie ein direkter Aufruf, der Aufrufer hier aendert sich NICHT
				   (Push der Stack-Argumente vorher, Cleanup+d0-Auswertung danach exakt
				   wie zuvor). Ohne largeDataMode bleibt der direkte "bsr/jsr <name>"
				   unveraendert (keine Tabellen, kein Korruptionsrisiko in der Praxis,
				   da dieser Modus bisher nur fuer Simulator-Mocks genutzt wird). */
				if (largeDataMode) {
					fprintf(out, "\tmove.l\t%d(a4),a2\n\tadda.l\ta4,a2\n\tjsr\t(a2)\n", externTableOffset(insP->args[0]));
				} else {
					fprintf(out, "\t%s\t%s\n", os9Mode ? "bsr" : "jsr", insP->args[0]);
				}
				if (stackArgs) fprintf(out, "\tlea\t%d(a7),a7\n", stackArgs * 4);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "RET") == 0 || strcmp(op, "RETP") == 0) {
				fprintf(out, "\tmove.l\t(a7)+,d0\n\tunlk\t%s\n\trts\n", framePtr());
			} else if (strcmp(op, "DROP") == 0) {
				fputs("\taddq.l\t#4,a7\n", out);
			} else if (strcmp(op, "PRINT") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n", out);
				emitCall(out, "tc_putint", helperTableOffset("tc_putint"), &serial, psectName);
			} else if (strcmp(op, "PRINTU") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n", out);
				emitCall(out, "tc_putuint", helperTableOffset("tc_putuint"), &serial, psectName);
			} else if (strcmp(op, "PRINTC") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n", out);
				emitCall(out, "tc_putchar", helperTableOffset("tc_putchar"), &serial, psectName);
			} else if (strcmp(op, "GLOBAL") == 0 || strcmp(op, "GARRAY") == 0 || strcmp(op, "GINIT") == 0) {
				/* static lokale Variable: bereits von collectGlobals() ausgewertet (Adresse/
				   Initialwert stehen im DATA/BSS-Abschnitt) -- an dieser Stelle im Funktions-
				   koerper ein reines No-op, keine Laufzeit-Aktion. */
			} else {
				sprintf(msg, "IR Zeile %d: unbekannter oder unvollstaendiger Opcode %s", insP->line, op);
				fatal(msg);
			}
		}
		fputs("\n", out);
	}

	{
		int hasData = 0, hasBss = 0, gi;
		// std::vector<int>(len) im Original ist NIE leer -- jedes Array landet
		// deshalb immer im DATA-Zweig, nie im BSS-Zweig. Das wird hier bewusst
		// direkt als Regel (isArray || initialValue!=0) nachgebildet.
		for (gi = 0; gi < globalCount; gi++) {
			if (globals[gi].declOnly) continue; /* definiert in einer ANDEREN Datei, keine Speicherallokation hier */
			hasData |= globals[gi].isArray || globals[gi].initialValue != 0;
			hasBss |= !globals[gi].isArray && globals[gi].initialValue == 0;
		}
		/* Die -largedata-Datenindirektionstabelle (tc_gadata) wird NICHT mehr
		   hier emittiert (siehe emitLeaGlobal()-Kommentar) -- sie sitzt jetzt
		   VOR allen Funktionsrumpf-Texten, direkt nach tc_functab, damit sie
		   ueber das einmalige "lea tc_gadata(pc),a3" immer erreichbar bleibt,
		   egal wie gross der Rest des Programms (inkl. dieses DATA/BSS-Blocks)
		   wird. */
		if (hasData) {
			fprintf(out, "\n%s DATA-Aequivalent des flachen Einzelmoduls: statisch initialisierte int32-Globals\n", fullCommentPrefix());
			emitAlign(out);
			for (gi = 0; gi < globalCount; gi++) {
				Global* g = &globals[gi];
				if (g->declOnly) continue;
				if (g->isArray || g->initialValue != 0) {
					int e; char gAsmName[NAME_LEN + 40];
					mangledName(gAsmName, "tc_g_", g->name, g->isStatic);
					if (!g->isChar) emitAlign(out);
					if (!g->isArray) {
						fprintf(out, "%s:\tdc.%s\t%d\n", gAsmName, g->isChar ? "b" : "l", g->initialValue);
					} else if (!g->hasGinit) {
						/* 2026-07-25: komplett nullinitialisiertes Array (nie per GINIT gesetzt) --
						   kompakt fuellen statt eine dc.b/dc.l-Zeile PRO ELEMENT zu schreiben (bei
						   grossen Arrays, z.B. ein 8192-Elemente-AST-Knotenpuffer als Byte-Array,
						   waeren das sonst hunderttausende Zeilen UND braeuchte ein entsprechend
						   grosses init[]). r68 kennt kein ds.b/rmb (siehe tc_extcall_tmp-Kommentar
						   weiter oben) -- wie dort mehrere wiederholte "0"-Werte kommagetrennt
						   pro Zeile, analog zu "dc.l 0,0,0,0,0,0,0,0". */
						int perLine = g->isChar ? 40 : 20;
						fprintf(out, "%s:\n", gAsmName);
						for (e = 0; e < g->length; ) {
							int n = g->length - e < perLine ? g->length - e : perLine, k;
							fprintf(out, "\tdc.%s\t0", g->isChar ? "b" : "l");
							for (k = 1; k < n; k++) fprintf(out, ",0");
							fprintf(out, "\n");
							e += n;
						}
					} else {
						fprintf(out, "%s:\n", gAsmName);
						for (e = 0; e < g->length; e++) fprintf(out, "\tdc.%s\t%d\n", g->isChar ? "b" : "l", g->init[e]);
					}
				}
			}
		}
		if (hasBss) {
			fprintf(out, "\n%s BSS-Aequivalent des flachen Einzelmoduls: nullinitialisierte int32-Globals\n", fullCommentPrefix());
			emitAlign(out);
			for (gi = 0; gi < globalCount; gi++) {
				Global* g = &globals[gi];
				if (g->declOnly) continue;
				if (!g->isArray && g->initialValue == 0) {
					char gAsmName[NAME_LEN + 40];
					mangledName(gAsmName, "tc_g_", g->name, g->isStatic);
					if (!g->isChar) emitAlign(out);
					fprintf(out, "%s:\tdc.%s\t0\n", gAsmName, g->isChar ? "b" : "l");
				}
			}
		}
	}
	if (os9Mode) fputs("\tends\n", out);
}

int main(int argc, char* argv[]) {
	FILE* out;
	char msg[300];
	int i;
	if (argc < 3) {
		fprintf(stderr, "usage: %s <input.ir> <output.s68> [-os9] [-part] [-runtime] [-largedata]\n", argv[0]);
		fprintf(stderr, "  -os9:       Microware-r68-Ausgabeformat (nam/psect/ends, \"*\" statt \";\"\n");
		fprintf(stderr, "              fuer volle Kommentarzeilen) statt vasm-kompatiblem Format.\n");
		fprintf(stderr, "  -part:      diese Datei ist EIN TEIL eines Mehrdatei-Programms (kein\n");
		fprintf(stderr, "              eigenstaendiges main noetig) -- fuer echten Mehrdatei-Link\n");
		fprintf(stderr, "              mit l68 gegen andere -os9/-part-Module.\n");
		fprintf(stderr, "  -runtime:   nur mit -part: diese Datei traegt zusaetzlich den gemeinsamen\n");
		fprintf(stderr, "              68k-Core/I/O-Anker (tc_mul_i32 etc.) -- GENAU EINE Datei im\n");
		fprintf(stderr, "              Mehrdatei-Programm muss dies setzen, sonst 'duplicate symbol'.\n");
		fprintf(stderr, "  -largedata: \"grosses Speichermodell\" -- adressiert globale Variablen ueber\n");
		fprintf(stderr, "              eine Indirektionstabelle statt direkt PC-relativ. Noetig, sobald\n");
		fprintf(stderr, "              r68 bei -os9 \"value out of range\" meldet (echte 68000-Grenze:\n");
		fprintf(stderr, "              PC-relative Adressierung reicht nur +-32 KB) -- Standard (ohne\n");
		fprintf(stderr, "              diese Option) ist schneller/kompakter, reicht aber nur fuer\n");
		fprintf(stderr, "              kleinere Programme mit wenig globalem Zustand.\n");
		return 2;
	}
	for (i = 3; i < argc; i++) {
		if (strcmp(argv[i], "-os9") == 0) os9Mode = 1;
		else if (strcmp(argv[i], "-part") == 0) partMode = 1;
		else if (strcmp(argv[i], "-runtime") == 0) runtimeMode = 1;
		else if (strcmp(argv[i], "-largedata") == 0) largeDataMode = 1;
		else { fprintf(stderr, "unbekannte Option: %s\n", argv[i]); return 2; }
	}
	readIR(argv[1]);
	collectGlobals();
	collectFunctions();
	collectExterns();
	if (!largeDataMode) {
		/* Heuristik-Warnung (2026-07-25, siehe -largedata/emitLeaGlobal()): wir koennen
		   NICHT wissen, ob r68 die PC-relative Reichweite tatsaechlich ueberschreiten
		   wird (haengt von der GESAMTEN Code+Daten-Distanz ab, die erst der Assembler
		   kennt) -- aber ein grober Schwellenwert ueber die reine globale Datenmenge
		   gibt fruehzeitig einen Hinweis, BEVOR ein kryptisches "value out of range"
		   vom echten r68 kommt. 16000 Byte ist bewusst konservativ (deutlich unter den
		   theoretischen 32 KB), da Code UND andere Symbole denselben Adressraum teilen. */
		long totalGlobalBytes = 0; int gi;
		for (gi = 0; gi < globalCount; gi++) {
			Global* g = &globals[gi];
			if (g->declOnly) continue;
			totalGlobalBytes += (long)(g->isChar ? 1 : 4) * (g->isArray ? g->length : 1);
		}
		if (totalGlobalBytes > 16000) {
			fprintf(stderr, "qcc_backend: Warnung: globale Daten sind mit %ld Byte recht gross fuer das\n", totalGlobalBytes);
			fprintf(stderr, "  Standard-Speichermodell (PC-relative Adressierung, echte 68000-Grenze ist\n");
			fprintf(stderr, "  +-32 KB von JEDER referenzierenden Instruktion aus). Falls r68 spaeter mit\n");
			fprintf(stderr, "  \"value out of range\" fehlschlaegt: mit -largedata neu uebersetzen.\n");
		}
	}
	if (os9Mode) {
		/* psect-Name aus dem Ausgabedateinamen ableiten (ohne Pfad/Endung), analog
		   zum Default in codegen.cpp (genParser68kTo: <basisname>_p). */
		const char* base = strrchr(argv[2], '/');
		const char* dot;
		int n, i;
		base = base ? base + 1 : argv[2];
		dot = strrchr(base, '.');
		n = dot ? (int)(dot - base) : (int)strlen(base);
		if (n > (int)sizeof(psectName) - 3) n = (int)sizeof(psectName) - 3;
		for (i = 0; i < n; i++) psectName[i] = base[i];
		psectName[n] = '\0';
		strcat(psectName, "_p");
	}
	out = fopen(argv[2], "w");
	if (!out) { sprintf(msg, "kann Ausgabe nicht schreiben: %s", argv[2]); fatal(msg); }
	emitIR(out);
	if (ferror(out)) fatal("Schreibfehler in Assembler-Ausgabe");
	fclose(out);
	return 0;
}
