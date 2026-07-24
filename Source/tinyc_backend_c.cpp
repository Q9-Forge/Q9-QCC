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
} Function;

typedef struct {
	char name[NAME_LEN];
	int initialValue;
	int isChar;
	int isArray;
	int length;
	int init[MAX_ARRAY_LEN];
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
   Instruktions-Codegen (emitIR-Dispatch weiter unten) ist DAHER fuer beide
   Formate identisch -- os9Mode wirkt nur auf die paar Kommentarzeilen und den
   Rahmen. */
static int os9Mode = 0;
static char psectName[NAME_LEN] = "tc_prog";
static const char* fullCommentPrefix(void) { return os9Mode ? "*" : ";"; }
/* vasm kennt "even" (Ausrichtung auf gerade Adresse); der echte Microware-r68-
   Assembler kennt "even" NICHT (empirisch verifiziert: "bad mnemonic"), wohl
   aber "align 4" (Longword-Ausrichtung -- strenger als "even", aber fuer
   dc.l-Daten das eigentlich Gemeinte und ebenfalls empirisch verifiziert). */
static void emitAlign(FILE* out) {
	fputs(os9Mode ? "\talign\t4\n" : "\teven\n", out);
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
					globals[gi].init[idx] = number(insP->args[2], insP->line);
					if (globals[gi].isChar) globals[gi].init[idx] &= 255;
					found = 1;
					break;
				}
			}
			if (!found) fatal("GINIT fuer unbekanntes Array");
			continue;
		}
		if (strcmp(insP->op, "GLOBAL") != 0 && strcmp(insP->op, "GARRAY") != 0) continue;
		if (strcmp(insP->op, "GARRAY") == 0) {
			if (insP->argc != 3 || !isNumWord(insP->args[1])) fatal("ungueltiges GARRAY");
			if (findGlobal(insP->args[0]) >= 0) fatal("doppelte globale Variable");
			len = number(insP->args[2], insP->line);
			if (len <= 0) fatal("GARRAY-Laenge muss positiv sein");
			if (len > MAX_ARRAY_LEN) fatal("GARRAY-Laenge ueberschreitet MAX_ARRAY_LEN");
			if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
			gi = globalCount++;
			memset(&globals[gi], 0, sizeof(Global));
			strncpy(globals[gi].name, insP->args[0], NAME_LEN - 1);
			globals[gi].isChar = isByteWord(insP->args[1]);
			globals[gi].isArray = 1;
			globals[gi].length = len;
			continue;
		}
		if (insP->argc != 1 && insP->argc != 2 && insP->argc != 3) {
			sprintf(msg, "IR Zeile %d: ungueltiges GLOBAL", insP->line);
			fatal(msg);
		}
		if (findGlobal(insP->args[0]) >= 0) {
			sprintf(msg, "IR Zeile %d: doppelte globale Variable %s", insP->line, insP->args[0]);
			fatal(msg);
		}
		if (insP->argc == 3 && !isNumWord(insP->args[2])) {
			sprintf(msg, "IR Zeile %d: unbekannter Globaltyp", insP->line);
			fatal(msg);
		}
		if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
		gi = globalCount++;
		memset(&globals[gi], 0, sizeof(Global));
		strncpy(globals[gi].name, insP->args[0], NAME_LEN - 1);
		globals[gi].initialValue = insP->argc >= 2 ? number(insP->args[1], insP->line) : 0;
		globals[gi].isChar = insP->argc == 3 && isByteWord(insP->args[2]);
		globals[gi].isArray = 0;
		globals[gi].length = 1;
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
			if ((!open && seenFunction) || (insP->argc != 1 && insP->argc != 2 && insP->argc != 3)) {
				sprintf(msg, "IR Zeile %d: ungueltiges GLOBAL", insP->line);
				fatal(msg);
			}
		} else if (strcmp(insP->op, "FUNC") == 0) {
			if (open || insP->argc != 2) { sprintf(msg, "IR Zeile %d: ungueltiges FUNC", insP->line); fatal(msg); }
			memset(&current, 0, sizeof(current));
			strncpy(current.name, insP->args[0], NAME_LEN - 1);
			current.nargs = number(insP->args[1], insP->line);
			current.first = i + 1;
			current.last = -1;
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
	if (funcCount == 0) fatal("IR: keine Funktion");

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
		sprintf(out, "%d(a6)", 8 + 4 * (fn->nargs - 1 - slotN));
		return;
	}
	if (slotN >= fn->nargs + fn->locals) {
		sprintf(msg, "IR Zeile %d: Slot ausserhalb des Frames", line);
		fatal(msg);
	}
	sprintf(out, "%d(a6)", -4 * (slotN - fn->nargs + 1));
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

	fputs("tc_mod_i32:\n", out);
	fputs("\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td0,d2\n\tmove.l\td1,d3\n\tbsr\ttc_div_i32\n\tmove.l\td0,d1\n\tmove.l\td2,d0\n\tbsr\ttc_mul_i32\n\tsub.l\td0,d2\n\tmove.l\td2,d0\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n", out);
	fputs("tc_umod_u32:\n", out);
	fputs("\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td0,d2\n\tmove.l\td1,d3\n\tbsr\ttc_udiv_u32\n\tmove.l\td0,d1\n\tmove.l\td2,d0\n\tbsr\ttc_mul_i32\n\tsub.l\td0,d2\n\tmove.l\td2,d0\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n", out);
}

static void emitIR(FILE* out) {
	int serial = 0;
	int fi, k;
	char msg[300];
	char addrBuf[64];

	if (findFunction("main") < 0) fatal("IR: Funktion main fehlt");

	fprintf(out, "%s Tiny-C 68k backend -- PIC Einzelmodul, erzeugt aus Stack-IR\n", fullCommentPrefix());
	fprintf(out, "%s a7: Operand-Stack, a6: aktueller Frame, d0/d1: Scratch/Rueckgabe\n\n", fullCommentPrefix());
	if (os9Mode) {
		fprintf(out, "\tnam\t%s\n", psectName);
		fprintf(out, "\tpsect\t%s,0,0,1,0,0\n\n", psectName);
	}
	fputs("tc_start:\tbsr\ttc_main\n\tbra\ttc_exit\n\n", out);

	for (fi = 0; fi < funcCount; fi++) {
		Function* fn = &funcs[fi];
		fprintf(out, "tc_%s:\tlink\ta6,#%d\n", fn->name, -fn->frameBytes);
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
				if (findGlobal(insP->args[0]) < 0) fatal("unbekannte globale Variable");
				fprintf(out, "\tlea\ttc_g_%s(pc),a0\n\tmove.l\ta0,-(a7)\n", insP->args[0]);
			} else if (strcmp(op, "LARRAY") == 0 && insP->argc == 3) {
				/* nur Frame-Layout, kein Code */
			} else if (strcmp(op, "PUSHADDR") == 0 && insP->argc == 2) {
				int ignored;
				if (strcmp(insP->args[0], "L") == 0) {
					int off = arrayOffset(fn, number(insP->args[1], insP->line), &ignored, insP->line);
					fprintf(out, "\tlea\t-%d(a6),a0\n", off);
				} else if (strcmp(insP->args[0], "P") == 0) {
					slotAddress(addrBuf, number(insP->args[1], insP->line), fn, insP->line);
					fprintf(out, "\tmove.l\t%s,a0\n", addrBuf);
				} else if (strcmp(insP->args[0], "G") == 0 && findGlobal(insP->args[1]) >= 0) {
					fprintf(out, "\tlea\ttc_g_%s(pc),a0\n", insP->args[1]);
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
					fprintf(out, "\tlea\t-%d(a6),a0\n", off);
				} else if (strcmp(insP->args[0], "P") == 0) {
					slotAddress(addrBuf, number(insP->args[1], insP->line), fn, insP->line);
					fprintf(out, "\tmove.l\t%s,a0\n", addrBuf);
				} else if (strcmp(insP->args[0], "G") == 0 && findGlobal(insP->args[1]) >= 0) {
					fprintf(out, "\tlea\ttc_g_%s(pc),a0\n", insP->args[1]);
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
				if (findGlobal(insP->args[0]) < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
				fprintf(out, "\tmove.l\ttc_g_%s(pc),-(a7)\n", insP->args[0]);
			} else if (strcmp(op, "STOREG") == 0 && insP->argc == 1) {
				if (findGlobal(insP->args[0]) < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
				fprintf(out, "\tmove.l\t(a7)+,d0\n\tlea\ttc_g_%s(pc),a0\n\tmove.l\td0,(a0)\n", insP->args[0]);
			} else if (strcmp(op, "LOADGC") == 0 && insP->argc == 1) {
				if (findGlobal(insP->args[0]) < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
				fprintf(out, "\tmoveq\t#0,d0\n\tmove.b\ttc_g_%s(pc),d0\n\tmove.l\td0,-(a7)\n", insP->args[0]);
			} else if (strcmp(op, "STOREGC") == 0 && insP->argc == 1) {
				if (findGlobal(insP->args[0]) < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
				fprintf(out, "\tmove.l\t(a7)+,d0\n\tlea\ttc_g_%s(pc),a0\n\tmove.b\td0,(a0)\n", insP->args[0]);
			} else if ((strcmp(op, "LOADGP") == 0 || strcmp(op, "STOREGP") == 0) && insP->argc == 1) {
				if (findGlobal(insP->args[0]) < 0) fatal("unbekannte globale Variable");
				if (strcmp(op, "LOADGP") == 0) fprintf(out, "\tmove.l\ttc_g_%s(pc),-(a7)\n", insP->args[0]);
				else fprintf(out, "\tmove.l\t(a7)+,d0\n\tlea\ttc_g_%s(pc),a0\n\tmove.l\td0,(a0)\n", insP->args[0]);
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
				fprintf(out, "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tbsr\ttc_%s\n\tmove.l\td0,-(a7)\n", fn2);
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
				if (findFunction(insP->args[0]) < 0) { sprintf(msg, "IR Zeile %d: unbekannte Funktion %s", insP->line, insP->args[0]); fatal(msg); }
				fprintf(out, "\tbsr\ttc_%s\n", insP->args[0]);
				if (nargsC) fprintf(out, "\tlea\t%d(a7),a7\n", nargsC * 4);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if ((strcmp(op, "CALLEXT") == 0 || strcmp(op, "CALLEXTP") == 0) && insP->argc == 3) {
				/* Aufruf einer NICHT in dieser IR definierten (externen) Funktion, z.B.
				   einer echten OS-9/Microware-clib-Funktion (strcmp, printf, malloc, ...).
				   Nutzt die dokumentierte Microware-68K-C/C++-ABI (Ultra C/C++ Processor
				   Guide, Kapitel "Passing Arguments to Functions") statt der sonst hier
				   verwendeten reinen Stack-ABI fuer TINY-C-EIGENE Funktionen:
				     - nicht-variadisch: 1. Argument -> d0, 2. Argument -> d1, ALLE
				       weiteren Argumente auf den Stack, in UMGEKEHRTER Erscheinungs-
				       reihenfolge gepusht (3. Argument landet dadurch am NAECHSTEN zur
				       Ruecksprungadresse, exakt wie es die ABI vorschreibt).
				     - variadisch (z.B. printf): ALLE Argumente auf den Stack, ebenfalls
				       in umgekehrter Reihenfolge, KEINE Register.
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
				int variadic = number(insP->args[2], insP->line);
				int hasD0 = !variadic && nargsC >= 1;
				int hasD1 = !variadic && nargsC >= 2;
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
				fprintf(out, "\tjsr\t%s\n", insP->args[0]);
				if (stackArgs) fprintf(out, "\tlea\t%d(a7),a7\n", stackArgs * 4);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "RET") == 0 || strcmp(op, "RETP") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n\tunlk\ta6\n\trts\n", out);
			} else if (strcmp(op, "DROP") == 0) {
				fputs("\taddq.l\t#4,a7\n", out);
			} else if (strcmp(op, "PRINT") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n\tbsr\ttc_putint\n", out);
			} else if (strcmp(op, "PRINTU") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n\tbsr\ttc_putuint\n", out);
			} else if (strcmp(op, "PRINTC") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n\tbsr\ttc_putchar\n", out);
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
	emitM68kCore(out);
	// Target-Runtime-Stubs: austauschbar; kein absoluter Zugriff und damit PIC-freundlich.
	fputs("tc_putint:\trts\t; Target Runtime ersetzt dies spaeter durch Ausgabe\n", out);
	fputs("tc_putuint:\trts\t; Target Runtime ersetzt dies spaeter durch Ausgabe\n", out);
	fputs("tc_putchar:\trts\t; Target Runtime ersetzt dies spaeter durch Ausgabe\n", out);
	fputs("tc_exit:\trts\t; Target Runtime beendet den Prozess\n", out);
	/* Scratch-Feld fuer CALLEXT/CALLEXTP (siehe dort) -- max. 8 auf den Stack
	   gereichte Argumente eines externen Aufrufs. Immer deklariert (32 Byte),
	   unabhaengig davon ob das Programm CALLEXT tatsaechlich nutzt. vasm kennt
	   "ds.l" (reservierter, uninitialisierter Speicher); der echte Microware-
	   r68-Assembler kennt "ds.l" NICHT (empirisch verifiziert: "bad mnemonic"),
	   daher im os9-Modus stattdessen 8x "dc.l 0" (funktional gleichwertig: alle
	   Backend-Opcodes lesen den Wert erst NACH einem STORE hierher). */
	emitAlign(out);
	fputs(os9Mode ? "tc_extcall_tmp:\tdc.l\t0,0,0,0,0,0,0,0\n" : "tc_extcall_tmp:\tds.l\t8\n", out);

	{
		int hasData = 0, hasBss = 0, gi;
		// std::vector<int>(len) im Original ist NIE leer -- jedes Array landet
		// deshalb immer im DATA-Zweig, nie im BSS-Zweig. Das wird hier bewusst
		// direkt als Regel (isArray || initialValue!=0) nachgebildet.
		for (gi = 0; gi < globalCount; gi++) {
			hasData |= globals[gi].isArray || globals[gi].initialValue != 0;
			hasBss |= !globals[gi].isArray && globals[gi].initialValue == 0;
		}
		if (hasData) {
			fprintf(out, "\n%s DATA-Aequivalent des flachen Einzelmoduls: statisch initialisierte int32-Globals\n", fullCommentPrefix());
			emitAlign(out);
			for (gi = 0; gi < globalCount; gi++) {
				Global* g = &globals[gi];
				if (g->isArray || g->initialValue != 0) {
					int e;
					if (!g->isChar) emitAlign(out);
					if (!g->isArray) {
						fprintf(out, "tc_g_%s:\tdc.%s\t%d\n", g->name, g->isChar ? "b" : "l", g->initialValue);
					} else {
						fprintf(out, "tc_g_%s:\n", g->name);
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
				if (!g->isArray && g->initialValue == 0) {
					if (!g->isChar) emitAlign(out);
					fprintf(out, "tc_g_%s:\tdc.%s\t0\n", g->name, g->isChar ? "b" : "l");
				}
			}
		}
	}
	if (os9Mode) fputs("\tends\n", out);
}

int main(int argc, char* argv[]) {
	FILE* out;
	char msg[300];
	if (argc != 3 && !(argc == 4 && strcmp(argv[3], "-os9") == 0)) {
		fprintf(stderr, "usage: %s <input.ir> <output.s68> [-os9]\n", argv[0]);
		fprintf(stderr, "  -os9: Microware-r68-Ausgabeformat (nam/psect/ends, \"*\" statt \";\"\n");
		fprintf(stderr, "        fuer volle Kommentarzeilen) statt vasm-kompatiblem Format.\n");
		return 2;
	}
	if (argc == 4) os9Mode = 1;
	readIR(argv[1]);
	collectGlobals();
	collectFunctions();
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
