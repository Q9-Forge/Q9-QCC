//============================================================================
// qcc_arm64_backend_c.cpp -- C-only counterpart to qcc_arm64_backend.cpp
//
// Behaviorally equivalent implementation without STL, exceptions or
// std::string: fixed global tables and linear search, in the style of
// parsec.cpp/codegen.cpp. The original C++ file remains as a reference; see
// section 5 of docs/SELFHOSTING_LUECKENLISTE.md. To switch back, build
// qcc_arm64_backend.cpp again in runtests.sh.
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
	/* Multi-file translation (2026-07-25): declOnly is registered by FUNCDECL
	   without a body in this file (defined in another QCC file). isStatic
	   controls .globl emission. Unlike the 68k/l68 target, Mach-O/ld supports
	   local symbols: a label without .globl is invisible to other object files.
	   Therefore omitting .globl is sufficient; no name mangling is needed. */
	int declOnly, isStatic;
} Function;

typedef struct {
	char name[NAME_LEN];
	int initialValue;
	int isChar;
	int isShort;
	int isPointer;
	int isArray;
	int length;
	int init[MAX_ARRAY_LEN];
	int hasGinit; /* 2026-07-25: mind. ein GINIT fuer dieses Array gesehen (siehe emitIR) */
	int declOnly, isStatic; /* siehe Function */
} Global;

static Instr ir[MAX_IR_LINES];
static int irCount = 0;

static Function funcs[MAX_FUNCS];
static int funcCount = 0;

static Global globals[MAX_GLOBALS];
static int globalCount = 0;

/* -part (2026-07-25, multi-file translation): this file is one part of a
   multi-file program, so the main/funcCount requirement is relaxed; see
   collectFunctions()/emit(). Unlike the 68k backend, there is no -runtime
   equivalent here: putint/putchar/exit are provided externally by
   runtime/arm64_darwin/start.s and are never emitted per file. */
static int partMode = 0;

static void fatal(const char* msg) {
	fprintf(stderr, "qcc_arm64_backend: %s\n", msg);
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
	return strcmp(w, "i") == 0 || strcmp(w, "u") == 0 || strcmp(w, "c") == 0 || strcmp(w, "b") == 0 ||
	       strcmp(w, "h") == 0 || strcmp(w, "p") == 0;
}

static int isByteWord(const char* w) {
	return strcmp(w, "c") == 0 || strcmp(w, "b") == 0;
}

/* short (2026-09-10; see qcc_backend_c.cpp for the 68k counterpart): a real
   16-bit type, always zero-extended like char. ARM64 ldrh/strh already zero-
   extend into w0, just as ldrb does for char. */
static int isShortWord(const char* w) { return strcmp(w, "h") == 0; }

/* Return the size of a type tag in bytes for array and pointer addressing.
   Unlike the 68k backend, a pointer is 8 bytes on ARM64, not 4. */
static int elemBytes(const char* w) {
	if (strcmp(w, "c") == 0 || strcmp(w, "b") == 0) return 1;
	if (strcmp(w, "h") == 0) return 2;
	if (strcmp(w, "p") == 0) return 8;
	return 4;
}
/* Return the ldr/str size suffix: "b" (byte), "h" (halfword), or ""
   (word/doubleword, selected by the w/x register). */
static const char* elemSuffix(const char* w) {
	if (isByteWord(w)) return "b";
	if (isShortWord(w)) return "h";
	return "";
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
	/* GLOBAL/GARRAY/GINIT may also occur inside a function: a "static" local
	   variable is registered as a normal GLOBAL at its declaration site. It may
	   therefore occur inside a FUNC...ENDFUNC span. collectFunctions() still
	   requires such a line to be inside an open function or before the first
	   function, never between two functions. */
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
					/* As in the 68k backend, MAX_ARRAY_LEN limits only indices actually
					   initialized by GINIT, not the declared GARRAY length. */
					if (idx >= MAX_ARRAY_LEN) fatal("GINIT-Index ueberschreitet MAX_ARRAY_LEN");
					globals[gi].init[idx] = number(x->args[2], x->line);
					if (globals[gi].isChar) globals[gi].init[idx] &= 255;
					else if (globals[gi].isShort) globals[gi].init[idx] &= 65535;
					globals[gi].hasGinit = 1;
					found = 1;
					break;
				}
			}
			if (!found) fatal("GINIT fuer unbekanntes Array");
			continue;
		}
		if (strcmp(x->op, "GLOBAL") != 0 && strcmp(x->op, "GARRAY") != 0) continue;
		// The original checks duplicates before distinguishing GARRAY from GLOBAL
		// and reports both uniformly as "invalid GLOBAL". This intentionally differs
		// from the 68k backend, which formats the message separately per branch.
		if (findGlobal(x->args[0]) >= 0) {
			sprintf(msg, "IR Zeile %d: ungueltiges GLOBAL", x->line);
			fatal(msg);
		}
		if (strcmp(x->op, "GARRAY") == 0) {
			/* Fourth argument (2026-07-25, multi-file translation): optional
			   isstatic flag, not evaluated here yet. */
			if ((x->argc != 3 && x->argc != 4) || !isNumWord(x->args[1])) fatal("ungueltiges GARRAY");
			len = number(x->args[2], x->line);
			if (len <= 0) fatal("GARRAY-Laenge muss positiv sein");
			/* No MAX_ARRAY_LEN limit here; see the GINIT comment above. */
			if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
			gi = globalCount++;
			memset(&globals[gi], 0, sizeof(Global));
			strncpy(globals[gi].name, x->args[0], NAME_LEN - 1);
			globals[gi].isChar = isByteWord(x->args[1]);
			globals[gi].isShort = isShortWord(x->args[1]);
			globals[gi].isPointer = strcmp(x->args[1], "p") == 0;
			globals[gi].isArray = 1;
			globals[gi].length = len;
			globals[gi].isStatic = x->argc >= 4 && number(x->args[3], x->line) != 0;
			continue;
		}
		if (x->argc != 1 && x->argc != 2 && x->argc != 3 && x->argc != 4) fatal("ungueltiges GLOBAL");
		/* argc>=3 rather than ==3 (2026-07-25): the fourth argument is the
		   optional isstatic flag; the type tag remains at position 2. */
		if (x->argc >= 3 && !isNumWord(x->args[2])) fatal("unbekannter Globaltyp");
		if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
		gi = globalCount++;
		memset(&globals[gi], 0, sizeof(Global));
		strncpy(globals[gi].name, x->args[0], NAME_LEN - 1);
		globals[gi].initialValue = x->argc >= 2 ? number(x->args[1], x->line) : 0;
		globals[gi].isChar = x->argc >= 3 && isByteWord(x->args[2]);
		globals[gi].isShort = x->argc >= 3 && isShortWord(x->args[2]);
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
		globals[gi].isShort = isShortWord(x->args[1]);
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
			/* Allowed before the first function or inside an open function (static
			   local variable), but not between two functions. */
			if (!open && seen) fatal("ungueltiges GLOBAL");
		} else if (strcmp(x->op, "FUNCDECL") == 0 || strcmp(x->op, "GLOBALDECL") == 0) {
			/* Multi-file translation (2026-07-25): "exists but is not defined here".
			   Allowed outside every FUNC span, like GLOBAL/GARRAY. FUNCDECL is
			   registered in a separate pass below because it does not open or close
			   a FUNC/ENDFUNC span. */
		} else if (strcmp(x->op, "FUNC") == 0) {
			/* Third argument (2026-07-25): optional isstatic flag controlling .globl
			   emission. */
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
	/* -part (2026-07-25): a file without main/functions is valid if it contains
	   at least one global declaration; a completely empty file remains an error.
	   Without -part, the original error behavior is preserved. */
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
			/* LOADLH/STORELH (2026-09-10, short) must be counted here. Otherwise a
			   slot accessed only through them remains below "top" and the frame is
			   too small, as in the 68k backend. */
			if ((strcmp(x->op, "LOADL") == 0 || strcmp(x->op, "STOREL") == 0 || strcmp(x->op, "LOADC") == 0 ||
				strcmp(x->op, "STOREC") == 0 || strcmp(x->op, "LOADLH") == 0 || strcmp(x->op, "STORELH") == 0 ||
				strcmp(x->op, "LOADP") == 0 || strcmp(x->op, "STOREP") == 0 ||
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
				if (x->argc != 3 || !(strcmp(x->args[1], "i") == 0 || isByteWord(x->args[1]) || isShortWord(x->args[1]) || strcmp(x->args[1], "p") == 0)) fatal("ungueltiges LARRAY");
				len = number(x->args[2], x->line);
				if (len <= 0) fatal("LARRAY-Laenge muss positiv sein");
				align = elemBytes(x->args[1]);
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
			align = elemBytes(x->args[1]);
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

/* Can "mov w0,#v" be encoded as one instruction (movz/movn alias)? Only if
   one 16-bit half of v or ~v is zero. This was found during short validation:
   most values do not satisfy either form, and clang rejects "mov w0,#99999". */
static int fitsSingleMov(unsigned int v) {
	return (v & 0xffffu) == 0 || (v >> 16) == 0 || (~v & 0xffffu) == 0 || (~v >> 16) == 0;
}

static void push(FILE* o, const char* reg) { fprintf(o, "\tstr\t%s,[sp,#-16]!\n", reg); }
static void pop(FILE* o, const char* reg) { fprintf(o, "\tldr\t%s,[sp]\n\tadd\tsp,sp,#16\n", reg); }

// Scaling suffix for add/sub pointer arithmetic: char/bool=1 (no shift),
// short=#1 (*2), pointer=#3 (*8), otherwise=#2 (*4). Always includes "\n".
static const char* scaleSuffix(const char* typeWord) {
	if (isByteWord(typeWord)) return "\n";
	if (isShortWord(typeWord)) return " #1\n";
	if (strcmp(typeWord, "p") == 0) return " #3\n";
	return " #2\n";
}

static void emit(FILE* o) {
	int fi, k;
	char msg[300];
	char slotBuf[32];

	/* -part (2026-07-25): main may be in another file. The real linker (ld)
	   reports an error if none of the linked files provides it. */
	if (!partMode && findFunction("main") < 0) fatal("IR: Funktion main fehlt");
	fputs("; QCC ARM64/Darwin -- PIC Programmmodul\n\t.text\n\t.p2align\t2\n", o);

	for (fi = 0; fi < funcCount; fi++) {
		Function* f = &funcs[fi];
		if (f->declOnly) continue; /* Defined in another file; no body here. */
		if (!f->isStatic) fprintf(o, "\t.globl\t_tc_%s\n", f->name);
		fprintf(o, "_tc_%s:\n\tstp\tx29,x30,[sp,#-16]!\n\tmov\tx29,sp\n", f->name);
		if (f->frameBytes) fprintf(o, "\tsub\tsp,sp,#%d\n", f->frameBytes);
		for (k = f->first; k < f->last; k++) {
			Instr* x = &ir[k];
			const char* op = x->op;

			if (strcmp(op, "PUSH") == 0 && x->argc == 1) {
				unsigned int uv = (unsigned int) strtol(x->args[0], NULL, 10);
				if (fitsSingleMov(uv)) fprintf(o, "\tmov\tw0,#%s\n", x->args[0]);
				else fprintf(o, "\tmovz\tw0,#%u\n\tmovk\tw0,#%u,lsl #16\n", uv & 0xffffu, uv >> 16);
				push(o, "w0");
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
			} else if (strcmp(op, "LOADLH") == 0 && x->argc == 1) {
				slotStr(slotBuf, number(x->args[0], x->line), f, x->line);
				fprintf(o, "\tldrh\tw0,[x29,%s]\n", slotBuf); push(o, "w0");
			} else if (strcmp(op, "STORELH") == 0 && x->argc == 1) {
				pop(o, "w0");
				slotStr(slotBuf, number(x->args[0], x->line), f, x->line);
				fprintf(o, "\tstrh\tw0,[x29,%s]\n", slotBuf);
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
				/* Frame layout only; no code is emitted. */
			} else if (strcmp(op, "PUSHADDR") == 0 && x->argc == 2) {
				int ignored;
				if (strcmp(x->args[0], "L") == 0) {
					int slotN = number(x->args[1], x->line);
					if (slotN < f->nargs) {
						slotStr(slotBuf, slotN, f, x->line);
						fprintf(o, "\tadd\tx0,x29,%s\n", slotBuf);
					} else {
						int off = arrayOffset(f, slotN, &ignored, x->line);
						fprintf(o, "\tsub\tx0,x29,#%d\n", off);
					}
				} else if (strcmp(x->args[0], "P") == 0) {
					slotStr(slotBuf, number(x->args[1], x->line), f, x->line);
					fprintf(o, "\tldr\tx0,[x29,%s]\n", slotBuf);
				} else if (strcmp(x->args[0], "G") == 0 && findGlobal(x->args[1]) >= 0) {
					fprintf(o, "\tadrp\tx0,_tc_g_%s@PAGE\n\tadd\tx0,x0,_tc_g_%s@PAGEOFF\n", x->args[1], x->args[1]);
				} else {
					fatal("unbekanntes Array");
				}
				fputs("\tstr\tx0,[sp,#-16]!\n", o);
			} else if ((strcmp(op, "LOADIDX") == 0 || strcmp(op, "STOREIDX") == 0 || strcmp(op, "STOREIDXKEEP") == 0) && x->argc == 3) {
				/* 2026-09-10: use isByteWord/isShortWord on the original tag,
				   with short as a third element size. */
				int isPointer = strcmp(x->args[2], "p") == 0;
				int keepValue = strcmp(op, "STOREIDXKEEP") == 0;
				if (strcmp(x->args[2], "i") != 0 && !isByteWord(x->args[2]) && !isShortWord(x->args[2]) && !isPointer)
					fatal("unbekannter Arraytyp");
				if (strcmp(op, "STOREIDX") == 0 || keepValue) { if (isPointer) pop(o, "x0"); else pop(o, "w0"); }
				pop(o, "w1");
				if (strcmp(x->args[0], "L") == 0) {
					int ignored;
					int off = arrayOffset(f, number(x->args[1], x->line), &ignored, x->line);
					fprintf(o, "\tsub\tx9,x29,#%d\n", off);
				} else if (strcmp(x->args[0], "P") == 0) {
					slotStr(slotBuf, number(x->args[1], x->line), f, x->line);
					fprintf(o, "\tldr\tx9,[x29,%s]\n", slotBuf);
				} else if (strcmp(x->args[0], "G") == 0 && findGlobal(x->args[1]) >= 0) {
					fprintf(o, "\tadrp\tx9,_tc_g_%s@PAGE\n\tadd\tx9,x9,_tc_g_%s@PAGEOFF\n", x->args[1], x->args[1]);
				} else {
					fatal("unbekanntes Array");
				}
				fprintf(o, "\tadd\tx9,x9,w1,sxtw%s", scaleSuffix(x->args[2]));
				if (strcmp(op, "LOADIDX") == 0) {
					fprintf(o, "\tldr%s\t%s,[x9]\n", elemSuffix(x->args[2]), isPointer ? "x0" : "w0");
					if (isPointer) push(o, "x0"); else push(o, "w0");
				} else {
					fprintf(o, "\tstr%s\t%s,[x9]\n", elemSuffix(x->args[2]), isPointer ? "x0" : "w0");
					if (isByteWord(x->args[2]) && keepValue) fputs("\tuxtb\tw0,w0\n", o);
					else if (isShortWord(x->args[2]) && keepValue) fputs("\tuxth\tw0,w0\n", o);
					if (keepValue) { if (isPointer) push(o, "x0"); else push(o, "w0"); }
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
			} else if ((strcmp(op, "LOADGH") == 0 || strcmp(op, "STOREGH") == 0) && x->argc == 1) {
				if (findGlobal(x->args[0]) < 0) fatal("unbekannte globale Variable");
				fprintf(o, "\tadrp\tx9,_tc_g_%s@PAGE\n", x->args[0]);
				if (strcmp(op, "LOADGH") == 0) { fprintf(o, "\tldrh\tw0,[x9,_tc_g_%s@PAGEOFF]\n", x->args[0]); push(o, "w0"); }
				else { pop(o, "w0"); fprintf(o, "\tstrh\tw0,[x9,_tc_g_%s@PAGEOFF]\n", x->args[0]); }
			} else if ((strcmp(op, "LOADGP") == 0 || strcmp(op, "STOREGP") == 0) && x->argc == 1) {
				if (findGlobal(x->args[0]) < 0) fatal("unbekannte globale Variable");
				fprintf(o, "\tadrp\tx9,_tc_g_%s@PAGE\n", x->args[0]);
				if (strcmp(op, "LOADGP") == 0) { fprintf(o, "\tldr\tx0,[x9,_tc_g_%s@PAGEOFF]\n", x->args[0]); push(o, "x0"); }
				else { pop(o, "x0"); fprintf(o, "\tstr\tx0,[x9,_tc_g_%s@PAGEOFF]\n", x->args[0]); }
			} else if (strcmp(op, "PTRINDEX") == 0 && x->argc == 1) {
				pop(o, "x0"); pop(o, "w1");
				fprintf(o, "\tadd\tx0,x0,w1,sxtw%s", scaleSuffix(x->args[0]));
				push(o, "x0");
			} else if ((strcmp(op, "LOADIND") == 0 || strcmp(op, "STOREIND") == 0 || strcmp(op, "STOREINDKEEP") == 0) && x->argc == 1) {
				int ptr = strcmp(x->args[0], "p") == 0;
				int keepValue = strcmp(op, "STOREINDKEEP") == 0;
				if (strcmp(op, "LOADIND") == 0) {
					pop(o, "x9");
					fprintf(o, "\tldr%s\t%s,[x9]\n", elemSuffix(x->args[0]), ptr ? "x0" : "w0");
					if (ptr) push(o, "x0"); else push(o, "w0");
				} else {
					if (ptr) pop(o, "x0"); else pop(o, "w0");
					pop(o, "x9");
					fprintf(o, "\tstr%s\t%s,[x9]\n", elemSuffix(x->args[0]), ptr ? "x0" : "w0");
					if (isByteWord(x->args[0]) && keepValue) fputs("\tuxtb\tw0,w0\n", o);
					else if (isShortWord(x->args[0]) && keepValue) fputs("\tuxth\tw0,w0\n", o);
					if (keepValue) { if (ptr) push(o, "x0"); else push(o, "w0"); }
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
				/* Like IPADD, but scale by a runtime byte size such as structByteSize
				   instead of a fixed type-tag size. Use multiplication because
				   scaleSuffix supports only fixed 1/4/8 shifts. */
				pop(o, "x0"); pop(o, "w1");
				fprintf(o, "\tmov\tw2,#%s\n\tmul\tw1,w1,w2\n\tadd\tx0,x0,w1,sxtw\n", x->args[0]);
				push(o, "x0");
			} else if (strcmp(op, "PDIFF") == 0 && x->argc == 1) {
				pop(o, "x1"); pop(o, "x0");
				fputs("\tsub\tx0,x0,x1\n", o);
				if (!isByteWord(x->args[0])) {
					int shift = strcmp(x->args[0], "p") == 0 ? 3 : isShortWord(x->args[0]) ? 1 : 2;
					fprintf(o, "\tasr\tx0,x0,#%d\n", shift);
				}
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
			} else if (strcmp(op, "NARROWH") == 0) {
				pop(o, "w0"); fputs("\tand\tw0,w0,#65535\n", o); push(o, "w0");
			} else if (strcmp(op, "DUP") == 0) {
				fputs("\tldr\tw0,[sp]\n", o); push(o, "w0");
			} else if (strcmp(op, "DUPP") == 0) {
				fputs("\tldr\tx0,[sp]\n", o); push(o, "x0");
			} else if (strcmp(op, "SWAP") == 0) {
				/* Oberste zwei 64-bit Stackwerte vertauschen. */
				fputs("\tldr\tx0,[sp]\n\tldr\tx1,[sp,#8]\n\tstr\tx1,[sp]\n\tstr\tx0,[sp,#8]\n", o);
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
				/* Static locals were handled by collectGlobals(); this occurrence in
				   the function body is a no-op. */
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
			if (g->declOnly) continue; /* Defined in another file; no allocation here. */
			// Scalars and (since 2026-07-25) arrays without any GINIT use zerofill.
			// Real BSS needs only the total byte size, allowing arbitrarily large
			// zero-initialized arrays without one .byte/.long line per element.
			if (!g->isArray && g->initialValue == 0) {
				if (!g->isStatic) fprintf(o, "\t.globl\t_tc_g_%s\n", g->name);
				fprintf(o, "\t.zerofill\t__DATA,__bss,_tc_g_%s,%d,%d\n", g->name,
					(g->isChar ? 1 : g->isShort ? 2 : g->isPointer ? 8 : 4) * g->length,
					g->isChar ? 0 : g->isShort ? 1 : g->isPointer ? 3 : 2);
			} else if (g->isArray && !g->hasGinit) {
				if (!g->isStatic) fprintf(o, "\t.globl\t_tc_g_%s\n", g->name);
				fprintf(o, "\t.zerofill\t__DATA,__bss,_tc_g_%s,%d,%d\n", g->name,
					(g->isChar ? 1 : g->isShort ? 2 : g->isPointer ? 8 : 4) * g->length,
					g->isChar ? 0 : g->isShort ? 1 : g->isPointer ? 3 : 2);
			}
			hasData |= (g->isArray && g->hasGinit) || (!g->isArray && g->initialValue != 0);
		}
		if (hasData) {
			fputs("\t.section\t__DATA,__data\n\t.p2align\t2\n", o);
			for (gi = 0; gi < globalCount; gi++) {
				Global* g = &globals[gi];
				if (g->declOnly) continue;
				if ((g->isArray && g->hasGinit) || (!g->isArray && g->initialValue != 0)) {
					int e;
					if (g->isShort) fputs("\t.p2align\t1\n", o);
					else if (!g->isChar) fputs("\t.p2align\t2\n", o);
					if (g->isPointer) fputs("\t.p2align\t3\n", o);
					if (!g->isStatic) fprintf(o, "\t.globl\t_tc_g_%s\n", g->name);
					fprintf(o, "_tc_g_%s:\n", g->name);
					if (!g->isArray) {
						fprintf(o, "\t.%s\t%d\n", g->isChar ? "byte" : g->isShort ? "short" : g->isPointer ? "quad" : "long", g->initialValue);
					} else {
						for (e = 0; e < g->length; e++)
							fprintf(o, "\t.%s\t%d\n", g->isChar ? "byte" : g->isShort ? "short" : g->isPointer ? "quad" : "long", g->init[e]);
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
