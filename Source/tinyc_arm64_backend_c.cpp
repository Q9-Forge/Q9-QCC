//============================================================================
// tinyc_arm64_backend_c.cpp -- reines-C-Gegenstueck zu tinyc_arm64_backend.cpp
//
// Verhaltensgleicher Nachbau ohne STL/Exceptions/std::string: feste globale
// Tabellen + lineare Suche, im selben Stil wie ebnf.cpp/codegen.cpp. Das
// Original (tinyc_arm64_backend.cpp) bleibt unveraendert als Referenz liegen;
// siehe docs/SELFHOSTING_LUECKENLISTE.md Abschnitt 5. Um auf die C++-Version
// zurueckzuschalten, in runtests.sh wieder tinyc_arm64_backend.cpp bauen.
//============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_LEN          24
#define ARG_LEN         64
#define NAME_LEN        64
#define LINE_LEN        512
#define MAX_ARGS        6
#define MAX_IR_LINES    8192
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
	   OHNE Rumpf in dieser Datei (definiert in einer anderen Tiny-C-Datei).
	   isStatic steuert die .globl-Emission (siehe emit()) -- anders als beim
	   68k/l68-Ziel (kein Sichtbarkeitskonzept, siehe tinyc_backend_c.cpp)
	   unterstuetzt Mach-O/ld ECHTE lokale Symbole: ein Label OHNE .globl ist
	   fuer andere Objektdateien schlicht unsichtbar (empirisch verifiziert --
	   zwei separat kompilierte .o mit je einem lokalen "_tc_priv" linken ohne
	   Konflikt, "duplicate symbol" tritt NICHT auf). Deshalb reicht hier reines
	   Weglassen von .globl, KEINE Namensverfremdung noetig (Unterschied zu
	   tinyc_backend_c.cpp!). */
	int declOnly, isStatic;
} Function;

typedef struct {
	char name[NAME_LEN];
	int initialValue;
	int isChar;
	int isPointer;
	int isArray;
	int length;
	int init[MAX_ARRAY_LEN];
	int declOnly, isStatic; /* siehe Function */
} Global;

static Instr ir[MAX_IR_LINES];
static int irCount = 0;

static Function funcs[MAX_FUNCS];
static int funcCount = 0;

static Global globals[MAX_GLOBALS];
static int globalCount = 0;

/* -part (2026-07-25, Mehrdatei-Uebersetzung): diese Datei ist EIN TEIL eines
   Mehrdatei-Programms -- die main/funcCount-Pflicht wird gelockert, siehe
   collectFunctions()/emit(). Anders als beim 68k-Backend gibt es hier KEIN
   "-runtime"-Aequivalent: putint/putchar/exit werden extern in
   runtime/arm64_darwin/start.s bereitgestellt (nie pro Datei emittiert), es
   gibt also keinen gemeinsamen Anker, den nur EINE Datei tragen duerfte. */
static int partMode = 0;

static void fatal(const char* msg) {
	fprintf(stderr, "tinyc_arm64_backend: %s\n", msg);
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
	if (text[0] == '\0') { sprintf(msg, "IR Zeile %d: Zahl erwartet", line); fatal(msg); }
	v = strtol(text, &end, 10);
	if (*end != '\0') { sprintf(msg, "IR Zeile %d: Zahl erwartet", line); fatal(msg); }
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
	/* GLOBAL/GARRAY/GINIT duerfen auch INNERHALB einer Funktion stehen -- eine "static"
	   lokale Variable (siehe Data/tinyc.lextab, tc_staticlocal) wird als ganz normaler
	   GLOBAL registriert, an der Textstelle ihrer Deklaration, also moeglicherweise
	   mitten in einer FUNC...ENDFUNC-Spanne. collectFunctions() prueft weiterhin, dass
	   so eine Zeile innerhalb einer offenen Funktion oder vor der ersten Funktion liegt,
	   nicht "zwischen" zwei Funktionen. */
	int i, gi, idx, len;
	char msg[300];
	globalCount = 0;
	for (i = 0; i < irCount; i++) {
		Instr* x = &ir[i];
		if (strcmp(x->op, "GINIT") == 0) {
			int found = 0;
			if (x->argc != 3) fatal("ungueltiges GINIT");
			for (gi = 0; gi < globalCount; gi++) {
				if (strcmp(globals[gi].name, x->args[0]) == 0 && globals[gi].isArray) {
					idx = number(x->args[1], x->line);
					if (idx < 0 || idx >= globals[gi].length) fatal("GINIT-Index ausserhalb Array");
					globals[gi].init[idx] = number(x->args[2], x->line);
					if (globals[gi].isChar) globals[gi].init[idx] &= 255;
					found = 1;
					break;
				}
			}
			if (!found) fatal("GINIT fuer unbekanntes Array");
			continue;
		}
		if (strcmp(x->op, "GLOBAL") != 0 && strcmp(x->op, "GARRAY") != 0) continue;
		// Original prueft Duplikat VOR der GARRAY/GLOBAL-Unterscheidung und meldet das
		// einheitlich als "ungueltiges GLOBAL" -- bewusst NICHT dieselbe Meldung wie im
		// 68k-Backend (dort getrennt je Zweig formuliert).
		if (findGlobal(x->args[0]) >= 0) {
			sprintf(msg, "IR Zeile %d: ungueltiges GLOBAL", x->line);
			fatal(msg);
		}
		if (strcmp(x->op, "GARRAY") == 0) {
			/* 4. Argument (2026-07-25, Mehrdatei-Uebersetzung): optionales isstatic-Flag,
			   hier noch nicht ausgewertet. */
			if ((x->argc != 3 && x->argc != 4) || !isNumWord(x->args[1])) fatal("ungueltiges GARRAY");
			len = number(x->args[2], x->line);
			if (len <= 0) fatal("GARRAY-Laenge muss positiv sein");
			if (len > MAX_ARRAY_LEN) fatal("GARRAY-Laenge ueberschreitet MAX_ARRAY_LEN");
			if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
			gi = globalCount++;
			memset(&globals[gi], 0, sizeof(Global));
			strncpy(globals[gi].name, x->args[0], NAME_LEN - 1);
			globals[gi].isChar = isByteWord(x->args[1]);
			globals[gi].isPointer = strcmp(x->args[1], "p") == 0;
			globals[gi].isArray = 1;
			globals[gi].length = len;
			globals[gi].isStatic = x->argc >= 4 && number(x->args[3], x->line) != 0;
			continue;
		}
		if (x->argc != 1 && x->argc != 2 && x->argc != 3 && x->argc != 4) fatal("ungueltiges GLOBAL");
		/* argc>=3 statt ==3 (2026-07-25): 4. Argument ist das optionale isstatic-Flag
		   (Mehrdatei-Uebersetzung), der Typtag bleibt immer an Position 2. */
		if (x->argc >= 3 && !isNumWord(x->args[2])) fatal("unbekannter Globaltyp");
		if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
		gi = globalCount++;
		memset(&globals[gi], 0, sizeof(Global));
		strncpy(globals[gi].name, x->args[0], NAME_LEN - 1);
		globals[gi].initialValue = x->argc >= 2 ? number(x->args[1], x->line) : 0;
		globals[gi].isChar = x->argc >= 3 && isByteWord(x->args[2]);
		globals[gi].isPointer = x->argc >= 3 && strcmp(x->args[2], "p") == 0;
		globals[gi].isArray = 0;
		globals[gi].length = 1;
		globals[gi].isStatic = x->argc >= 4 && number(x->args[3], x->line) != 0;
	}
	for (i = 0; i < irCount; i++) {
		Instr* x = &ir[i];
		char msg[300];
		if (strcmp(x->op, "GLOBALDECL") != 0) continue;
		if (x->argc != 2) fatal("ungueltiges GLOBALDECL");
		if (findGlobal(x->args[0]) >= 0) {
			sprintf(msg, "IR Zeile %d: ungueltiges GLOBAL", x->line);
			fatal(msg);
		}
		if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
		gi = globalCount++;
		memset(&globals[gi], 0, sizeof(Global));
		strncpy(globals[gi].name, x->args[0], NAME_LEN - 1);
		globals[gi].isChar = isByteWord(x->args[1]);
		globals[gi].isPointer = strcmp(x->args[1], "p") == 0;
		globals[gi].declOnly = 1;
	}
}

static void collectFunctions(void) {
	int i, open = 0, seen = 0;
	Function current;
	funcCount = 0;
	memset(&current, 0, sizeof(current));
	for (i = 0; i < irCount; i++) {
		Instr* x = &ir[i];
		if (strcmp(x->op, "GLOBAL") == 0 || strcmp(x->op, "GARRAY") == 0 || strcmp(x->op, "GINIT") == 0) {
			/* vor der ersten Funktion ODER innerhalb einer offenen Funktion (static
			   lokale Variable) erlaubt -- NICHT zwischen zwei Funktionen. */
			if (!open && seen) fatal("ungueltiges GLOBAL");
		} else if (strcmp(x->op, "FUNCDECL") == 0 || strcmp(x->op, "GLOBALDECL") == 0) {
			/* Mehrdatei-Uebersetzung (2026-07-25): "existiert, ist aber nicht hier
			   definiert" -- ausserhalb jeder FUNC-Spanne erlaubt (wie GLOBAL/GARRAY);
			   FUNCDECL wird unten in einem separaten Durchlauf registriert (analog
			   zu GLOBALDECL in collectGlobals), da es KEINE FUNC/ENDFUNC-Spanne
			   oeffnet/schliesst. */
		} else if (strcmp(x->op, "FUNC") == 0) {
			/* 3. Argument (2026-07-25): optionales isstatic-Flag (steuert .globl in
			   emit()). */
			if (open || (x->argc != 2 && x->argc != 3)) fatal("ungueltiges FUNC");
			memset(&current, 0, sizeof(current));
			strncpy(current.name, x->args[0], NAME_LEN - 1);
			current.nargs = number(x->args[1], x->line);
			current.first = i + 1;
			current.last = -1;
			current.isStatic = x->argc >= 3 && number(x->args[2], x->line) != 0;
			open = 1; seen = 1;
		} else if (strcmp(x->op, "ENDFUNC") == 0) {
			if (!open) fatal("ENDFUNC ohne FUNC");
			current.last = i;
			if (funcCount >= MAX_FUNCS) fatal("zu viele Funktionen");
			funcs[funcCount++] = current;
			open = 0;
		} else if (!open) {
			fatal("Opcode ausserhalb einer Funktion");
		}
	}
	if (open) fatal("unvollstaendige IR");
	/* -part (2026-07-25): eine Datei OHNE main/Funktionen ist zulaessig, solange
	   sie wenigstens globale Deklarationen enthaelt -- komplett leere Datei
	   bleibt ein Fehler. Ohne -part unveraendert immer ein Fehler. */
	if (funcCount == 0 && (!partMode || globalCount == 0)) fatal("unvollstaendige IR");
	for (i = 0; i < irCount; i++) {
		Instr* x = &ir[i];
		if (strcmp(x->op, "FUNCDECL") != 0) continue;
		if (x->argc != 2) fatal("ungueltiges FUNCDECL");
		if (findFunction(x->args[0]) >= 0) fatal("doppelte Funktion");
		if (funcCount >= MAX_FUNCS) fatal("zu viele Funktionen");
		memset(&current, 0, sizeof(current));
		strncpy(current.name, x->args[0], NAME_LEN - 1);
		current.nargs = number(x->args[1], x->line);
		current.first = -1;
		current.last = -1;
		current.declOnly = 1;
		funcs[funcCount++] = current;
	}

	for (i = 0; i < funcCount; i++) {
		Function* f = &funcs[i];
		int top = f->nargs - 1;
		int k, bytes;
		for (k = f->first; k < f->last; k++) {
			Instr* x = &ir[k];
			if ((strcmp(x->op, "LOADL") == 0 || strcmp(x->op, "STOREL") == 0 || strcmp(x->op, "LOADC") == 0 ||
				strcmp(x->op, "STOREC") == 0 || strcmp(x->op, "LOADP") == 0 || strcmp(x->op, "STOREP") == 0 ||
				strcmp(x->op, "ADDRL") == 0 || strcmp(x->op, "LARRAY") == 0) && x->argc > 0) {
				int v = number(x->args[0], x->line);
				if (v > top) top = v;
			}
		}
		f->locals = top >= f->nargs ? top - f->nargs + 1 : 0;
		bytes = f->locals * 16;
		for (k = f->first; k < f->last; k++) {
			if (strcmp(ir[k].op, "LARRAY") == 0) {
				Instr* x = &ir[k];
				int len, align;
				if (x->argc != 3 || !(strcmp(x->args[1], "i") == 0 || isByteWord(x->args[1]) || strcmp(x->args[1], "p") == 0)) fatal("ungueltiges LARRAY");
				len = number(x->args[2], x->line);
				if (len <= 0) fatal("LARRAY-Laenge muss positiv sein");
				align = strcmp(x->args[1], "p") == 0 ? 8 : isByteWord(x->args[1]) ? 1 : 4;
				bytes = (bytes + align - 1) & ~(align - 1);
				bytes += len * align;
			}
		}
		f->frameBytes = (bytes + 15) & ~15;
	}
}

static int arrayOffset(const Function* f, int wanted, int* isChar, int line) {
	int offset = f->locals * 16;
	int k;
	char msg[200];
	for (k = f->first; k < f->last; k++) {
		if (strcmp(ir[k].op, "LARRAY") == 0) {
			Instr* x = &ir[k];
			int slotN, len, align;
			if (x->argc != 3) fatal("ungueltiges LARRAY");
			slotN = number(x->args[0], x->line);
			len = number(x->args[2], x->line);
			align = strcmp(x->args[1], "p") == 0 ? 8 : isByteWord(x->args[1]) ? 1 : 4;
			offset = (offset + align - 1) & ~(align - 1);
			offset += len * align;
			if (slotN == wanted) { *isChar = isByteWord(x->args[1]); return offset; }
		}
	}
	sprintf(msg, "IR Zeile %d: unbekanntes lokales Array", line);
	fatal(msg);
	return 0;
}

static void slotStr(char* out_, int n, const Function* f, int line) {
	char msg[200];
	if (n < 0 || n >= f->nargs + f->locals) {
		sprintf(msg, "IR Zeile %d: Slot ausserhalb Frame", line);
		fatal(msg);
	}
	if (n < f->nargs) sprintf(out_, "#%d", 16 + 16 * (f->nargs - 1 - n));
	else sprintf(out_, "#-%d", 16 * (n - f->nargs + 1));
}

static void push(FILE* o, const char* reg) { fprintf(o, "\tstr\t%s,[sp,#-16]!\n", reg); }
static void pop(FILE* o, const char* reg) { fprintf(o, "\tldr\t%s,[sp]\n\tadd\tsp,sp,#16\n", reg); }

// Skalierungssuffix fuer add/sub bei Pointerarithmetik: char/bool=1 (kein Shift),
// Pointer=#3 (*8), sonst=#2 (*4). Immer gefolgt von "\n".
static const char* scaleSuffix(const char* typeWord) {
	if (isByteWord(typeWord)) return "\n";
	if (strcmp(typeWord, "p") == 0) return " #3\n";
	return " #2\n";
}

static void emit(FILE* o) {
	int fi, k;
	char msg[300];
	char slotBuf[32];

	/* -part (2026-07-25): main darf in einer ANDEREN Datei stehen -- das meldet
	   der echte Linker (ld) von selbst, falls keine der gelinkten Dateien es
	   liefert. */
	if (!partMode && findFunction("main") < 0) fatal("IR: Funktion main fehlt");
	fputs("; Tiny-C ARM64/Darwin -- PIC Programmmodul\n\t.text\n\t.p2align\t2\n", o);

	for (fi = 0; fi < funcCount; fi++) {
		Function* f = &funcs[fi];
		if (f->declOnly) continue; /* definiert in einer ANDEREN Datei, kein Rumpf hier */
		if (!f->isStatic) fprintf(o, "\t.globl\t_tc_%s\n", f->name);
		fprintf(o, "_tc_%s:\n\tstp\tx29,x30,[sp,#-16]!\n\tmov\tx29,sp\n", f->name);
		if (f->frameBytes) fprintf(o, "\tsub\tsp,sp,#%d\n", f->frameBytes);
		for (k = f->first; k < f->last; k++) {
			Instr* x = &ir[k];
			const char* op = x->op;

			if (strcmp(op, "PUSH") == 0 && x->argc == 1) {
				fprintf(o, "\tmov\tw0,#%s\n", x->args[0]); push(o, "w0");
			} else if (strcmp(op, "LOADL") == 0 && x->argc == 1) {
				slotStr(slotBuf, number(x->args[0], x->line), f, x->line);
				fprintf(o, "\tldr\tw0,[x29,%s]\n", slotBuf); push(o, "w0");
			} else if (strcmp(op, "STOREL") == 0 && x->argc == 1) {
				pop(o, "w0");
				slotStr(slotBuf, number(x->args[0], x->line), f, x->line);
				fprintf(o, "\tstr\tw0,[x29,%s]\n", slotBuf);
			} else if (strcmp(op, "LOADC") == 0 && x->argc == 1) {
				slotStr(slotBuf, number(x->args[0], x->line), f, x->line);
				fprintf(o, "\tldrb\tw0,[x29,%s]\n", slotBuf); push(o, "w0");
			} else if (strcmp(op, "STOREC") == 0 && x->argc == 1) {
				pop(o, "w0");
				slotStr(slotBuf, number(x->args[0], x->line), f, x->line);
				fprintf(o, "\tstrb\tw0,[x29,%s]\n", slotBuf);
			} else if (strcmp(op, "LOADP") == 0 && x->argc == 1) {
				slotStr(slotBuf, number(x->args[0], x->line), f, x->line);
				fprintf(o, "\tldr\tx0,[x29,%s]\n", slotBuf); push(o, "x0");
			} else if (strcmp(op, "STOREP") == 0 && x->argc == 1) {
				pop(o, "x0");
				slotStr(slotBuf, number(x->args[0], x->line), f, x->line);
				fprintf(o, "\tstr\tx0,[x29,%s]\n", slotBuf);
			} else if (strcmp(op, "ADDRL") == 0 && x->argc == 1) {
				int n = number(x->args[0], x->line);
				if (n < f->nargs) fprintf(o, "\tadd\tx0,x29,#%d\n", 16 + 16 * (f->nargs - 1 - n));
				else fprintf(o, "\tsub\tx0,x29,#%d\n", 16 * (n - f->nargs + 1));
				push(o, "x0");
			} else if (strcmp(op, "ADDRG") == 0 && x->argc == 1) {
				if (findGlobal(x->args[0]) < 0) fatal("unbekannte globale Variable");
				fprintf(o, "\tadrp\tx0,_tc_g_%s@PAGE\n\tadd\tx0,x0,_tc_g_%s@PAGEOFF\n", x->args[0], x->args[0]);
				push(o, "x0");
			} else if (strcmp(op, "LARRAY") == 0 && x->argc == 3) {
				/* nur Frame-Layout, kein Code */
			} else if (strcmp(op, "PUSHADDR") == 0 && x->argc == 2) {
				int ignored;
				if (strcmp(x->args[0], "L") == 0) {
					int off = arrayOffset(f, number(x->args[1], x->line), &ignored, x->line);
					fprintf(o, "\tsub\tx0,x29,#%d\n", off);
				} else if (strcmp(x->args[0], "P") == 0) {
					slotStr(slotBuf, number(x->args[1], x->line), f, x->line);
					fprintf(o, "\tldr\tx0,[x29,%s]\n", slotBuf);
				} else if (strcmp(x->args[0], "G") == 0 && findGlobal(x->args[1]) >= 0) {
					fprintf(o, "\tadrp\tx0,_tc_g_%s@PAGE\n\tadd\tx0,x0,_tc_g_%s@PAGEOFF\n", x->args[1], x->args[1]);
				} else {
					fatal("unbekanntes Array");
				}
				fputs("\tstr\tx0,[sp,#-16]!\n", o);
			} else if ((strcmp(op, "LOADIDX") == 0 || strcmp(op, "STOREIDX") == 0) && x->argc == 3) {
				int isChar = isByteWord(x->args[2]), isPointer = strcmp(x->args[2], "p") == 0;
				if (strcmp(x->args[2], "i") != 0 && !isChar && !isPointer) fatal("unbekannter Arraytyp");
				if (strcmp(op, "STOREIDX") == 0) { if (isPointer) pop(o, "x0"); else pop(o, "w0"); }
				pop(o, "w1");
				if (strcmp(x->args[0], "L") == 0) {
					int off = arrayOffset(f, number(x->args[1], x->line), &isChar, x->line);
					fprintf(o, "\tsub\tx9,x29,#%d\n", off);
				} else if (strcmp(x->args[0], "P") == 0) {
					slotStr(slotBuf, number(x->args[1], x->line), f, x->line);
					fprintf(o, "\tldr\tx9,[x29,%s]\n", slotBuf);
				} else if (strcmp(x->args[0], "G") == 0 && findGlobal(x->args[1]) >= 0) {
					fprintf(o, "\tadrp\tx9,_tc_g_%s@PAGE\n\tadd\tx9,x9,_tc_g_%s@PAGEOFF\n", x->args[1], x->args[1]);
				} else {
					fatal("unbekanntes Array");
				}
				fprintf(o, "\tadd\tx9,x9,w1,sxtw%s", isChar ? "\n" : isPointer ? " #3\n" : " #2\n");
				if (strcmp(op, "LOADIDX") == 0) {
					fprintf(o, "\tldr%s\t%s,[x9]\n", isChar ? "b" : "", isPointer ? "x0" : "w0");
					if (isPointer) push(o, "x0"); else push(o, "w0");
				} else {
					fprintf(o, "\tstr%s\t%s,[x9]\n", isChar ? "b" : "", isPointer ? "x0" : "w0");
				}
			} else if ((strcmp(op, "LOADG") == 0 || strcmp(op, "STOREG") == 0) && x->argc == 1) {
				if (findGlobal(x->args[0]) < 0) fatal("unbekannte globale Variable");
				fprintf(o, "\tadrp\tx9,_tc_g_%s@PAGE\n", x->args[0]);
				if (strcmp(op, "LOADG") == 0) { fprintf(o, "\tldr\tw0,[x9,_tc_g_%s@PAGEOFF]\n", x->args[0]); push(o, "w0"); }
				else { pop(o, "w0"); fprintf(o, "\tstr\tw0,[x9,_tc_g_%s@PAGEOFF]\n", x->args[0]); }
			} else if ((strcmp(op, "LOADGC") == 0 || strcmp(op, "STOREGC") == 0) && x->argc == 1) {
				if (findGlobal(x->args[0]) < 0) fatal("unbekannte globale Variable");
				fprintf(o, "\tadrp\tx9,_tc_g_%s@PAGE\n", x->args[0]);
				if (strcmp(op, "LOADGC") == 0) { fprintf(o, "\tldrb\tw0,[x9,_tc_g_%s@PAGEOFF]\n", x->args[0]); push(o, "w0"); }
				else { pop(o, "w0"); fprintf(o, "\tstrb\tw0,[x9,_tc_g_%s@PAGEOFF]\n", x->args[0]); }
			} else if ((strcmp(op, "LOADGP") == 0 || strcmp(op, "STOREGP") == 0) && x->argc == 1) {
				if (findGlobal(x->args[0]) < 0) fatal("unbekannte globale Variable");
				fprintf(o, "\tadrp\tx9,_tc_g_%s@PAGE\n", x->args[0]);
				if (strcmp(op, "LOADGP") == 0) { fprintf(o, "\tldr\tx0,[x9,_tc_g_%s@PAGEOFF]\n", x->args[0]); push(o, "x0"); }
				else { pop(o, "x0"); fprintf(o, "\tstr\tx0,[x9,_tc_g_%s@PAGEOFF]\n", x->args[0]); }
			} else if (strcmp(op, "PTRINDEX") == 0 && x->argc == 1) {
				pop(o, "x0"); pop(o, "w1");
				fprintf(o, "\tadd\tx0,x0,w1,sxtw%s", scaleSuffix(x->args[0]));
				push(o, "x0");
			} else if ((strcmp(op, "LOADIND") == 0 || strcmp(op, "STOREIND") == 0) && x->argc == 1) {
				int byte = isByteWord(x->args[0]), ptr = strcmp(x->args[0], "p") == 0;
				if (strcmp(op, "LOADIND") == 0) {
					pop(o, "x9");
					fprintf(o, "\tldr%s\t%s,[x9]\n", byte ? "b" : "", ptr ? "x0" : "w0");
					if (ptr) push(o, "x0"); else push(o, "w0");
				} else {
					if (ptr) pop(o, "x0"); else pop(o, "w0");
					pop(o, "x9");
					fprintf(o, "\tstr%s\t%s,[x9]\n", byte ? "b" : "", ptr ? "x0" : "w0");
				}
			} else if ((strcmp(op, "PADD") == 0 || strcmp(op, "PSUB") == 0) && x->argc == 1) {
				pop(o, "w1"); pop(o, "x0");
				fprintf(o, "\t%s\tx0,x0,w1,sxtw%s", strcmp(op, "PADD") == 0 ? "add" : "sub", scaleSuffix(x->args[0]));
				push(o, "x0");
			} else if (strcmp(op, "IPADD") == 0 && x->argc == 1) {
				pop(o, "x0"); pop(o, "w1");
				fprintf(o, "\tadd\tx0,x0,w1,sxtw%s", scaleSuffix(x->args[0]));
				push(o, "x0");
			} else if (strcmp(op, "IPADDN") == 0 && x->argc == 1) {
				/* wie IPADD, aber Skalierung um eine LAUFZEIT-Byte-Groesse (z.B. structByteSize)
				   statt einer festen Typtag-Groesse -- echte Multiplikation (w2 = Literal, mul,
				   dann sign-extend + add), da scaleSuffix nur feste 1/4/8-Shifts kennt. */
				pop(o, "x0"); pop(o, "w1");
				fprintf(o, "\tmov\tw2,#%s\n\tmul\tw1,w1,w2\n\tadd\tx0,x0,w1,sxtw\n", x->args[0]);
				push(o, "x0");
			} else if (strcmp(op, "PDIFF") == 0 && x->argc == 1) {
				pop(o, "x1"); pop(o, "x0");
				fputs("\tsub\tx0,x0,x1\n", o);
				if (!isByteWord(x->args[0])) fprintf(o, "\tasr\tx0,x0,#%d\n", strcmp(x->args[0], "p") == 0 ? 3 : 2);
				push(o, "w0");
			} else if (strcmp(op, "ADD") == 0 || strcmp(op, "SUB") == 0 || strcmp(op, "MUL") == 0 || strcmp(op, "DIV") == 0 || strcmp(op, "UDIV") == 0) {
				pop(o, "w1"); pop(o, "w0");
				fprintf(o, "\t%s\tw0,w0,w1\n", strcmp(op, "ADD") == 0 ? "add" : strcmp(op, "SUB") == 0 ? "sub" : strcmp(op, "MUL") == 0 ? "mul" : strcmp(op, "DIV") == 0 ? "sdiv" : "udiv");
				push(o, "w0");
			} else if (strcmp(op, "MOD") == 0 || strcmp(op, "UMOD") == 0) {
				pop(o, "w1"); pop(o, "w0");
				fprintf(o, "\t%s\tw2,w0,w1\n\tmsub\tw0,w2,w1,w0\n", strcmp(op, "MOD") == 0 ? "sdiv" : "udiv");
				push(o, "w0");
			} else if (strcmp(op, "NEG") == 0) {
				pop(o, "w0"); fputs("\tneg\tw0,w0\n", o); push(o, "w0");
			} else if (strcmp(op, "NOT") == 0) {
				pop(o, "w0"); fputs("\tcmp\tw0,#0\n\tcset\tw0,eq\n", o); push(o, "w0");
			} else if (strcmp(op, "NOTBIT") == 0) {
				pop(o, "w0"); fputs("\tmvn\tw0,w0\n", o); push(o, "w0");
			} else if (strcmp(op, "BAND") == 0 || strcmp(op, "BXOR") == 0 || strcmp(op, "BOR") == 0) {
				pop(o, "w0"); fputs("\tmov\tw1,w0\n", o); pop(o, "w0");
				fprintf(o, "\t%s\tw0,w0,w1\n", strcmp(op, "BAND") == 0 ? "and" : strcmp(op, "BXOR") == 0 ? "eor" : "orr");
				push(o, "w0");
			} else if (strcmp(op, "SHL") == 0 || strcmp(op, "SHR") == 0 || strcmp(op, "USHR") == 0) {
				pop(o, "w0"); fputs("\tmov\tw1,w0\n", o); pop(o, "w0");
				fprintf(o, "\t%s\tw0,w0,w1\n", strcmp(op, "SHL") == 0 ? "lslv" : strcmp(op, "SHR") == 0 ? "asrv" : "lsrv");
				push(o, "w0");
			} else if (strcmp(op, "NARROWC") == 0) {
				pop(o, "w0"); fputs("\tand\tw0,w0,#255\n", o); push(o, "w0");
			} else if (strcmp(op, "DUP") == 0) {
				fputs("\tldr\tw0,[sp]\n", o); push(o, "w0");
			} else if (strcmp(op, "DUPP") == 0) {
				fputs("\tldr\tx0,[sp]\n", o); push(o, "x0");
			} else if (strncmp(op, "CMP", 3) == 0) {
				const char* cc = strcmp(op, "CMPLT") == 0 ? "lt" : strcmp(op, "CMPGT") == 0 ? "gt" : strcmp(op, "CMPLE") == 0 ? "le" :
					strcmp(op, "CMPGE") == 0 ? "ge" : strcmp(op, "CMPULT") == 0 ? "lo" : strcmp(op, "CMPUGT") == 0 ? "hi" :
					strcmp(op, "CMPULE") == 0 ? "ls" : strcmp(op, "CMPUGE") == 0 ? "hs" : strcmp(op, "CMPEQ") == 0 ? "eq" :
					strcmp(op, "CMPNE") == 0 ? "ne" : NULL;
				if (!cc) fatal("unbekannter Vergleich");
				pop(o, "w1"); pop(o, "w0");
				fprintf(o, "\tcmp\tw0,w1\n\tcset\tw0,%s\n", cc); push(o, "w0");
			} else if (strncmp(op, "PCMP", 4) == 0) {
				const char* cc = strcmp(op, "PCMPLT") == 0 ? "lo" : strcmp(op, "PCMPGT") == 0 ? "hi" : strcmp(op, "PCMPLE") == 0 ? "ls" :
					strcmp(op, "PCMPGE") == 0 ? "hs" : strcmp(op, "PCMPEQ") == 0 ? "eq" : strcmp(op, "PCMPNE") == 0 ? "ne" : NULL;
				if (!cc) fatal("unbekannter Pointervergleich");
				pop(o, "x1"); pop(o, "x0");
				fprintf(o, "\tcmp\tx0,x1\n\tcset\tw0,%s\n", cc); push(o, "w0");
			} else if (strcmp(op, "LABEL") == 0 && x->argc == 1) {
				fprintf(o, "_tc_%s:\n", x->args[0]);
			} else if (strcmp(op, "JMP") == 0 && x->argc == 1) {
				fprintf(o, "\tb\t_tc_%s\n", x->args[0]);
			} else if ((strcmp(op, "JZ") == 0 || strcmp(op, "JNZ") == 0) && x->argc == 1) {
				pop(o, "w0");
				fprintf(o, "\tcb%s\tw0,_tc_%s\n", strcmp(op, "JZ") == 0 ? "z" : "nz", x->args[0]);
			} else if ((strcmp(op, "CALL") == 0 || strcmp(op, "CALLP") == 0) && x->argc == 2) {
				int n = number(x->args[1], x->line);
				if (findFunction(x->args[0]) < 0) fatal("unbekannte Funktion");
				fprintf(o, "\tbl\t_tc_%s\n", x->args[0]);
				if (n) fprintf(o, "\tadd\tsp,sp,#%d\n", n * 16);
				if (strcmp(op, "CALLP") == 0) push(o, "x0"); else push(o, "w0");
			} else if (strcmp(op, "RET") == 0 || strcmp(op, "RETP") == 0) {
				if (strcmp(op, "RETP") == 0) pop(o, "x0"); else pop(o, "w0");
				fputs("\tmov\tsp,x29\n\tldp\tx29,x30,[sp],#16\n\tret\n", o);
			} else if (strcmp(op, "DROP") == 0) {
				fputs("\tadd\tsp,sp,#16\n", o);
			} else if (strcmp(op, "PRINT") == 0) {
				pop(o, "w0"); fputs("\tbl\t_tc_putint\n", o);
			} else if (strcmp(op, "PRINTU") == 0) {
				pop(o, "w0"); fputs("\tbl\t_tc_putuint\n", o);
			} else if (strcmp(op, "PRINTC") == 0) {
				pop(o, "w0"); fputs("\tbl\t_tc_putchar\n", o);
			} else if (strcmp(op, "GLOBAL") == 0 || strcmp(op, "GARRAY") == 0 || strcmp(op, "GINIT") == 0) {
				/* static lokale Variable: bereits von collectGlobals() ausgewertet, hier
				   an dieser Stelle im Funktionskoerper ein reines No-op. */
			} else {
				sprintf(msg, "IR Zeile %d: unbekannter Opcode %s", x->line, op);
				fatal(msg);
			}
		}
		fputs("\n", o);
	}

	{
		int gi, hasData = 0;
		for (gi = 0; gi < globalCount; gi++) {
			Global* g = &globals[gi];
			if (g->declOnly) continue; /* definiert in einer ANDEREN Datei, keine Speicherallokation hier */
			// Nur Skalare erreichen zerofill: Arrays landen (wie im Original, siehe
			// unten) immer im DATA-Zweig, weil ihr init[] beim Anlegen schon mit
			// `length` Nullen belegt wird.
			if (!g->isArray && g->initialValue == 0) {
				if (!g->isStatic) fprintf(o, "\t.globl\t_tc_g_%s\n", g->name);
				fprintf(o, "\t.zerofill\t__DATA,__bss,_tc_g_%s,%d,%d\n", g->name,
					(g->isChar ? 1 : g->isPointer ? 8 : 4) * g->length,
					g->isChar ? 0 : g->isPointer ? 3 : 2);
			}
			hasData |= g->isArray || g->initialValue != 0;
		}
		if (hasData) {
			fputs("\t.section\t__DATA,__data\n\t.p2align\t2\n", o);
			for (gi = 0; gi < globalCount; gi++) {
				Global* g = &globals[gi];
				if (g->declOnly) continue;
				if (g->isArray || g->initialValue != 0) {
					int e;
					if (!g->isChar) fputs("\t.p2align\t2\n", o);
					if (g->isPointer) fputs("\t.p2align\t3\n", o);
					if (!g->isStatic) fprintf(o, "\t.globl\t_tc_g_%s\n", g->name);
					fprintf(o, "_tc_g_%s:\n", g->name);
					if (!g->isArray) {
						fprintf(o, "\t.%s\t%d\n", g->isChar ? "byte" : g->isPointer ? "quad" : "long", g->initialValue);
					} else {
						for (e = 0; e < g->length; e++)
							fprintf(o, "\t.%s\t%d\n", g->isChar ? "byte" : g->isPointer ? "quad" : "long", g->init[e]);
					}
				}
			}
		}
	}
}

int main(int argc, char* argv[]) {
	FILE* out;
	int i;
	if (argc < 3) {
		fprintf(stderr, "usage: %s <input.ir> <output.s> [-part]\n", argv[0]);
		fprintf(stderr, "  -part: diese Datei ist EIN TEIL eines Mehrdatei-Programms (kein\n");
		fprintf(stderr, "         eigenstaendiges main noetig) -- fuer echten Mehrdatei-Link\n");
		fprintf(stderr, "         mit clang/ld gegen andere -part-Module.\n");
		return 2;
	}
	for (i = 3; i < argc; i++) {
		if (strcmp(argv[i], "-part") == 0) partMode = 1;
		else { fprintf(stderr, "unbekannte Option: %s\n", argv[i]); return 2; }
	}
	readIR(argv[1]);
	collectGlobals();
	collectFunctions();
	out = fopen(argv[2], "w");
	if (!out) fatal("kann Ausgabe nicht schreiben");
	emit(out);
	fclose(out);
	return 0;
}
