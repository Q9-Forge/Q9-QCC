//================================================================================
// tinyc_backend_c.cpp -- reines-C-Gegenstueck zu tinyc_backend.cpp
//
// Verhaltensgleicher Nachbau ohne STL/Exceptions/std::string: feste globale
// Tabellen + lineare Suche, im selben Stil wie ebnf.cpp/codegen.cpp. Das
// Original (tinyc_backend.cpp) bleibt unveraendert als Referenz liegen; siehe
// docs/SELFHOSTING_LUECKENLISTE.md Abschnitt 5 fuer den Hintergrund. Um auf die
// C++-Version zurueckzuschalten, in runtests.sh wieder tinyc_backend.cpp bauen.
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
#define MAX_GLOBALS     256
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
	   OHNE Rumpf in dieser Datei (definiert in einer anderen Tiny-C-Datei) --
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
   keine Regression an den TinyVM-/Simulator-Tests). */
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
   ersten genParserC-artigen Skalierungstest (SourceTinyC/codegen.tc mit vielen
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
static void emitLeaGlobal(FILE* out, int gidx, const char* reg) {
	if (largeDataMode) {
		fprintf(out, "\tmove.l\t%d(a3),%s\n", gidx * 4, reg);
	} else {
		char gAsmName[NAME_LEN + 40];
		mangledName(gAsmName, "tc_g_", globals[gidx].name, globals[gidx].isStatic);
		fprintf(out, "\tlea\t%s(pc),%s\n", gAsmName, reg);
	}
}

static void fatal(const char* msg) {
	fprintf(stderr, "tinyc_backend: %s\n", msg);
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

/* Emittiert einen Aufruf zu einem SCHON MANGLED Assembler-Namen (fuer Tiny-C-
   Funktionen, tableOffset = funcIndex*4) ODER einem rohen Laufzeit-Helfer-
   Namen (tableOffset = helperTableOffset(...)) -- small: unveraendert "bsr
   asmName"; large: Tabellen-Indirektion ueber a4/a2, siehe Kommentar oben. */
static void emitCall(FILE* out, const char* asmName, int tableOffset) {
	if (largeDataMode) {
		fprintf(out, "\tmove.l\t%d(a4),a2\n\tjsr\t(a2)\n", tableOffset);
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
	   stehen: eine "static" lokale Variable (Data/tinyc.lextab, tc_staticlocal) wird als
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
		if (strcmp(insP->op, "FUNCDECL") != 0) continue;
		if (insP->argc != 2) fatal("ungueltiges FUNCDECL");
		if (findFunction(insP->args[0]) >= 0) { fprintf(stderr, "tinyc_backend: doppelte Funktion %s\n", insP->args[0]); fatal("doppelte Funktion"); }
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
	int id = (*serial)++;
	fprintf(out, "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tcmp.l\td1,d0\n\tmoveq\t#0,d0\n");
	fprintf(out, "\t%s\ttc_cmp_yes_%d\n\tbra\ttc_cmp_done_%d\n", branch, id, id);
	fprintf(out, "tc_cmp_yes_%d:\tmoveq\t#1,d0\ntc_cmp_done_%d:\tmove.l\td0,-(a7)\n", id, id);
}

// 68000 hat MULS/DIVS nur fuer 16-Bit-Operanden. Diese festen, PIC-faehigen
// Schablonen bilden deshalb die definierte Tiny-C-int32-Arithmetik nach. Sie
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
	   tools/tiny68sim.py beim Debuggen der neuen -os9-putint-Ziffernzerlegung):
	   vor "bsr tc_mul_i32" muss d0 den DIVISOR (d3) tragen, NICHT nochmal den
	   Dividenden (d2) -- sonst wird Quotient*Dividend statt Quotient*Divisor
	   gerechnet. Dieser Bug war seit Einfuehrung von tc_mod_i32/tc_umod_u32
	   unentdeckt, weil kein einziger 68k-Backend-Test (nur die TinyVM-Tests)
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

	fprintf(out, "%s Tiny-C 68k backend -- PIC Einzelmodul, erzeugt aus Stack-IR\n", fullCommentPrefix());
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
		if (largeDataMode) fputs("\tlea\ttc_functab(pc),a4\n", out);
			if (largeDataMode && globalCount > 0) fputs("\tlea\ttc_gadata(pc),a3\n", out);
		if (mainIdx >= 0) {
			char mainAsmName[NAME_LEN + 40];
			mangledName(mainAsmName, "tc_", "main", funcs[mainIdx].isStatic);
			emitCall(out, mainAsmName, mainIdx * 4);
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
		   Tiny-C-Funktionen) bzw. helperTableOffset() (fuer Laufzeit-Helfer) passen. */
		fprintf(out, "%s Funktions-Indirektionstabelle (-largedata): absolute Adressen, PC-relativ erreichbar\n", fullCommentPrefix());
		emitAlign(out);
		fputs("tc_functab:\n", out);
		for (fi = 0; fi < funcCount; fi++) {
			char asmName[NAME_LEN + 40];
			mangledName(asmName, "tc_", funcs[fi].name, funcs[fi].isStatic);
			fprintf(out, "\tdc.l\t%s\n", asmName);
		}
		fputs("\tdc.l\ttc_mul_i32\n\tdc.l\ttc_div_i32\n\tdc.l\ttc_udiv_u32\n", out);
		fputs("\tdc.l\ttc_mod_i32\n\tdc.l\ttc_umod_u32\n", out);
		fputs("\tdc.l\ttc_putint\n\tdc.l\ttc_putuint\n\tdc.l\ttc_putchar\n", out);
		/* Daten-Indirektionstabelle (siehe emitLeaGlobal()-Kommentar): MUSS wie
		   tc_functab direkt nach tc_start/main stehen (VOR den potenziell
		   riesigen Funktionsrumpf-Texten), damit das einmalige
		   "lea tc_gadata(pc),a3" immer erreichbar bleibt, egal wie gross der
		   Rest des Programms wird. Reihenfolge MUSS exakt zu gidx*4 (siehe
		   findGlobal()) passen -- AUCH declOnly-Externe bekommen einen
		   Eintrag ("dc.l tc_g_X" ist fuer diese eine ganz normale externe
		   Symbolreferenz, von l68 wie jede andere aufgeloest). */
		if (globalCount > 0) {
			fprintf(out, "%s Daten-Indirektionstabelle (-largedata): absolute Adressen, PC-relativ erreichbar\n", fullCommentPrefix());
			emitAlign(out);
			fputs("tc_gadata:\n", out);
			for (gi = 0; gi < globalCount; gi++) {
				char gAsmName[NAME_LEN + 40];
				mangledName(gAsmName, "tc_g_", globals[gi].name, globals[gi].isStatic);
				fprintf(out, "\tdc.l\t%s\n", gAsmName);
			}
		}
	}

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
			if (largeDataMode) fputs("\tlea\ttc_functab(pc),a4\n", out);
			if (largeDataMode && globalCount > 0) fputs("\tlea\ttc_gadata(pc),a3\n", out);
		}
		mangledName(asmName, "tc_", fn->name, fn->isStatic);
		fprintf(out, "%s:\tlink\t%s,#%d\n", asmName, framePtr(), -fn->frameBytes);
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
				emitCall(out, "tc_mul_i32", helperTableOffset("tc_mul_i32"));
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
			} else if (strcmp(op, "DUP") == 0 || strcmp(op, "DUPP") == 0) {
				fputs("\tmove.l\t(a7),-(a7)\n", out);
			} else if (strcmp(op, "MUL") == 0 || strcmp(op, "DIV") == 0 || strcmp(op, "UDIV") == 0 || strcmp(op, "MOD") == 0 || strcmp(op, "UMOD") == 0) {
				const char* fn2 = strcmp(op, "MUL") == 0 ? "mul_i32" : strcmp(op, "DIV") == 0 ? "div_i32" : strcmp(op, "UDIV") == 0 ? "udiv_u32" : strcmp(op, "MOD") == 0 ? "mod_i32" : "umod_u32";
				char helperAsmName[24];
				sprintf(helperAsmName, "tc_%s", fn2);
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n", out);
				emitCall(out, helperAsmName, helperTableOffset(helperAsmName));
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
				fprintf(out, "tc_%s:\n", insP->args[0]);
			} else if (strcmp(op, "JMP") == 0 && insP->argc == 1) {
				fprintf(out, "\tbra\ttc_%s\n", insP->args[0]);
			} else if (strcmp(op, "JZ") == 0 && insP->argc == 1) {
				fprintf(out, "\tmove.l\t(a7)+,d0\n\ttst.l\td0\n\tbeq\ttc_%s\n", insP->args[0]);
			} else if (strcmp(op, "JNZ") == 0 && insP->argc == 1) {
				fprintf(out, "\tmove.l\t(a7)+,d0\n\ttst.l\td0\n\tbne\ttc_%s\n", insP->args[0]);
			} else if ((strcmp(op, "CALL") == 0 || strcmp(op, "CALLP") == 0) && insP->argc == 2) {
				int nargsC = number(insP->args[1], insP->line);
				int callee = findFunction(insP->args[0]);
				char asmName[NAME_LEN + 40];
				if (callee < 0) { sprintf(msg, "IR Zeile %d: unbekannte Funktion %s", insP->line, insP->args[0]); fatal(msg); }
				mangledName(asmName, "tc_", insP->args[0], funcs[callee].isStatic);
				emitCall(out, asmName, callee * 4);
				if (nargsC) fprintf(out, "\tlea\t%d(a7),a7\n", nargsC * 4);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if ((strcmp(op, "CALLEXT") == 0 || strcmp(op, "CALLEXTP") == 0) && insP->argc == 3) {
				/* Aufruf einer NICHT in dieser IR definierten (externen) Funktion, z.B.
				   einer echten OS-9/Microware-clib-Funktion (strcmp, printf, malloc, ...).
				   Nutzt die dokumentierte Microware-68K-C/C++-ABI (Ultra C/C++ Processor
				   Guide, Kapitel "Passing Arguments to Functions") statt der sonst hier
				   verwendeten reinen Stack-ABI fuer TINY-C-EIGENE Funktionen: die ERSTEN
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
				int hasD0 = fixedCount >= 1 && nargsC >= 1;
				int hasD1 = fixedCount >= 2 && nargsC >= 2;
				int stackArgs = nargsC - (hasD0 ? 1 : 0) - (hasD1 ? 1 : 0);
				int ai;
				if (stackArgs > 8) { sprintf(msg, "IR Zeile %d: zu viele Stack-Argumente fuer externen Aufruf (max 8)", insP->line); fatal(msg); }
				/* PC-relative Adresse EINMAL in a0 (a0 ist in diesem Backend generell ein
				   freies Scratch-Adressregister, wird von keinem IR-Opcode ueber dessen
				   eigene Emission hinaus als gueltig vorausgesetzt) -- passend zum PIC-Stil
				   des restlichen Backends (vgl. tc_g_<name>(pc)-Zugriffe). */
				if (stackArgs > 0) fputs("\tlea\ttc_extcall_tmp(pc),a0\n", out);
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
				   tools/tiny68sim.py als Mock-Aufruf-Marker erkannt, keine echte
				   Positionsunabhaengigkeit noetig, keine Regression riskieren). */
				fprintf(out, "\t%s\t%s\n", os9Mode ? "bsr" : "jsr", insP->args[0]);
				if (stackArgs) fprintf(out, "\tlea\t%d(a7),a7\n", stackArgs * 4);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "RET") == 0 || strcmp(op, "RETP") == 0) {
				fprintf(out, "\tmove.l\t(a7)+,d0\n\tunlk\t%s\n\trts\n", framePtr());
			} else if (strcmp(op, "DROP") == 0) {
				fputs("\taddq.l\t#4,a7\n", out);
			} else if (strcmp(op, "PRINT") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n", out);
				emitCall(out, "tc_putint", helperTableOffset("tc_putint"));
			} else if (strcmp(op, "PRINTU") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n", out);
				emitCall(out, "tc_putuint", helperTableOffset("tc_putuint"));
			} else if (strcmp(op, "PRINTC") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n", out);
				emitCall(out, "tc_putchar", helperTableOffset("tc_putchar"));
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
	/* Mehrdatei-Uebersetzung (2026-07-25): der gemeinsame 68k-Core/I/O-Anker
	   (siehe partMode/runtimeMode-Kommentar oben) wird unter -part NUR in
	   GENAU EINER Datei emittiert (-runtime) -- sonst meldet l68 fuer JEDES
	   dieser Symbole "duplicate symbol", da jede Datei sonst ihre eigene Kopie
	   mitbraechte. Ohne -part unveraendert immer emittiert (Vollprogramm). */
	if (!partMode || runtimeMode) {
	emitM68kCore(out);
	if (os9Mode) {
		/* Echte Ausgabe ueber die reale Microware-clib.l-Funktion _os_write
		   (Signatur laut OS9/SRC/DEFS/modes.h: error_code _os_write(path_id,
		   const void*, u_int32 *count) -- count ist ein IN/OUT-Zeiger, path 1
		   = stdout, analog zu Unix-Filedeskriptoren). BEWUSST nicht ueber
		   printf/clib-Formatierung: Tiny-C hat noch keine String-Literale, und
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

		/* a1=Puffer, d1=Laenge -- ruft _os_write(1,a1,&tc_io_cnt) auf. */
		fputs("tc_io_write:\n\tlea\ttc_io_cnt(pc),a2\n\tmove.l\td1,(a2)\n\tmove.l\ta1,d1\n", out);
		fputs("\tmove.l\ta2,-(a7)\n\tmoveq\t#1,d0\n\tbsr\t_os_write\n\tlea\t4(a7),a7\n\trts\n\n", out);
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
			fprintf(stderr, "tinyc_backend: Warnung: globale Daten sind mit %ld Byte recht gross fuer das\n", totalGlobalBytes);
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
