/* q9-qirv32: Q9 Stack IR to RISC-V-32 Assembler Codegen
   
   Translates .ir (stack-based IR) to .srv32 (RISC-V-32 assembler).
   
   RISC-V calling convention:
     a0-a7: argument/return values (a0 is return value)
     sp: stack pointer
     ra: return address
     t0-t6: temporary registers (caller-saved)
     s0-s11: saved registers (callee-saved)
   
   Stack layout:
     [sp]        <- top of stack
     ...
     [sp-4]      <- local 0
     [sp-8]      <- local 1
     ...
     [fp+0]      <- old frame pointer (if used)
     [fp+4]      <- return address
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* === Capacity limits === */
#define MAX_IR_LINES    65536
#define MAX_FUNCS       1024
#define MAX_GLOBALS     3072
#define MAX_ARRAY_LEN   4096
#define MAX_ARGS        6
#define NAME_LEN        256
#define OP_LEN          32
#define ARG_LEN         128
#define ARG_POOL_BYTES  1048576

/* === Data Structures === */

typedef struct {
	char op[OP_LEN];
	char* args[MAX_ARGS];
	int argc;
	int line;
} Instr;

typedef struct {
	char name[NAME_LEN];
	int nargs, first, last, locals, frameBytes;
	int declOnly, isStatic;
} Function;

typedef struct {
	char name[NAME_LEN];
	int initialValue;
	int elemSize;
	int isArray;
	int length;
	int* init;
	int initLen;
	int hasGinit;
	int hasInitAddr;
	int declOnly, isStatic;
} Global;

/* === Static IR storage === */

static Instr ir[MAX_IR_LINES];
static int irCount = 0;

static Function funcs[MAX_FUNCS];
static int funcCount = 0;

static Global globals[MAX_GLOBALS];
static int globalCount = 0;

static char argPool[ARG_POOL_BYTES];
static int argPoolUsed = 0;
static char argEmpty[1];

static int labelCount = 0;

/* === Helper functions === */

static void fatal(const char* msg) {
	fprintf(stderr, "ERROR: %s\n", msg);
	exit(1);
}

static void* xmalloc(size_t size) {
	void* p = malloc(size);
	if (!p) fatal("out of memory");
	return p;
}

static const char* poolArg(const char* arg) {
	int len = strlen(arg) + 1;
	if (argPoolUsed + len > ARG_POOL_BYTES) {
		fatal("argument pool exhausted");
	}
	char* p = &argPool[argPoolUsed];
	strcpy(p, arg);
	argPoolUsed += len;
	return p;
}

static int number(const char* s, int lineNum) {
	char* end;
	long val = strtol(s, &end, 10);
	if (*end != '\0') {
		fprintf(stderr, "ERROR line %d: not a number: %s\n", lineNum, s);
		exit(1);
	}
	return (int)val;
}

static int tagSize(const char* tag) {
	if (!tag || !*tag) return 4;
	if (tag[0] == 'c' || tag[0] == 'b') return 1;
	if (tag[0] == 'h') return 2;
	return 4;
}

static int isNumWord(const char* tag) {
	if (!tag || !*tag) return 0;
	return tag[0] == 'c' || tag[0] == 'b' || tag[0] == 'h' || tag[0] == 'p' || isdigit(tag[0]);
}

static int findGlobal(const char* name) {
	int i;
	for (i = 0; i < globalCount; i++) {
		if (strcmp(globals[i].name, name) == 0) return i;
	}
	return -1;
}

static int findFunction(const char* name) {
	int i;
	for (i = 0; i < funcCount; i++) {
		if (strcmp(funcs[i].name, name) == 0) return i;
	}
	return -1;
}

/* === IR Parsing === */

static int readIR(FILE* in) {
	char line[1024];
	int lineNum = 0;

	while (fgets(line, sizeof(line), in)) {
		lineNum++;
		
		int len = strlen(line);
		while (len > 0 && isspace(line[len-1])) line[--len] = '\0';
		
		if (!len || line[0] == ';' || line[0] == '#') continue;
		
		if (irCount >= MAX_IR_LINES) {
			fatal("IR line limit exceeded");
		}
		
		Instr* p = &ir[irCount];
		p->line = lineNum;
		
		char* tok = strtok(line, " \t");
		if (!tok) continue;
		
		strncpy(p->op, tok, OP_LEN - 1);
		p->op[OP_LEN - 1] = '\0';
		
		p->argc = 0;
		while ((tok = strtok(NULL, " \t")) && p->argc < MAX_ARGS) {
			p->args[p->argc++] = (char*)poolArg(tok);
		}
		
		int j;
		for (j = p->argc; j < MAX_ARGS; j++) {
			p->args[j] = argEmpty;
		}
		
		irCount++;
	}
	
	return irCount;
}

/* Extract globals from IR */
static void collectGlobals(void) {
	int i, gi, len;
	char msg[300];
	
	globalCount = 0;
	
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		
		if (strcmp(insP->op, "GARRAY") == 0) {
			if ((insP->argc != 3 && insP->argc != 4)) {
				fatal("invalid GARRAY");
			}
			if (findGlobal(insP->args[0]) >= 0) {
				fatal("duplicate global variable");
			}
			len = number(insP->args[2], insP->line);
			if (len <= 0) fatal("GARRAY length must be positive");
			if (globalCount >= MAX_GLOBALS) fatal("too many globals");
			
			gi = globalCount++;
			memset(&globals[gi], 0, sizeof(Global));
			strncpy(globals[gi].name, insP->args[0], NAME_LEN - 1);
			globals[gi].elemSize = tagSize(insP->args[1]);
			globals[gi].isArray = 1;
			globals[gi].length = len;
			globals[gi].isStatic = insP->argc >= 4 && number(insP->args[3], insP->line) != 0;
			continue;
		}
		
		if (strcmp(insP->op, "GLOBAL") != 0) continue;
		
		if (insP->argc < 1 || insP->argc > 4) {
			sprintf(msg, "line %d: invalid GLOBAL (argc=%d)", insP->line, insP->argc);
			fatal(msg);
		}
		if (findGlobal(insP->args[0]) >= 0) {
			sprintf(msg, "line %d: duplicate global %s", insP->line, insP->args[0]);
			fatal(msg);
		}
		
		if (globalCount >= MAX_GLOBALS) fatal("too many globals");
		gi = globalCount++;
		memset(&globals[gi], 0, sizeof(Global));
		strncpy(globals[gi].name, insP->args[0], NAME_LEN - 1);
		
		globals[gi].initialValue = (insP->argc >= 2) ? number(insP->args[1], insP->line) : 0;
		globals[gi].elemSize = (insP->argc >= 3) ? tagSize(insP->args[2]) : 4;
		globals[gi].isArray = 0;
		globals[gi].length = 1;
		globals[gi].isStatic = (insP->argc >= 4) ? (number(insP->args[3], insP->line) != 0) : 0;
	}
	
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		if (strcmp(insP->op, "GLOBALDECL") != 0) continue;
		
		if (insP->argc != 2 && insP->argc != 3) fatal("invalid GLOBALDECL");
		if (findGlobal(insP->args[0]) >= 0) {
			sprintf(msg, "line %d: duplicate global %s", insP->line, insP->args[0]);
			fatal(msg);
		}
		if (globalCount >= MAX_GLOBALS) fatal("too many globals");
		
		gi = globalCount++;
		memset(&globals[gi], 0, sizeof(Global));
		strncpy(globals[gi].name, insP->args[0], NAME_LEN - 1);
		globals[gi].elemSize = tagSize(insP->args[1]);
		globals[gi].isStatic = insP->argc == 3 && number(insP->args[2], insP->line) != 0;
		globals[gi].declOnly = 1;
	}
}

/* Extract functions from IR */
static void collectFunctions(void) {
	int i, open = 0;
	Function current;
	char msg[300];
	
	funcCount = 0;
	memset(&current, 0, sizeof(current));
	
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		
		if (strcmp(insP->op, "FUNC") == 0) {
			if (open) {
				sprintf(msg, "line %d: nested FUNC", insP->line);
				fatal(msg);
			}
			if (insP->argc != 2 && insP->argc != 3) {
				sprintf(msg, "line %d: invalid FUNC", insP->line);
				fatal(msg);
			}
			
			memset(&current, 0, sizeof(current));
			strncpy(current.name, insP->args[0], NAME_LEN - 1);
			current.nargs = number(insP->args[1], insP->line);
			current.first = i + 1;
			current.last = -1;
			current.isStatic = insP->argc >= 3 && number(insP->args[2], insP->line) != 0;
			open = 1;
		} 
		else if (strcmp(insP->op, "ENDFUNC") == 0) {
			if (!open) {
				sprintf(msg, "line %d: ENDFUNC without FUNC", insP->line);
				fatal(msg);
			}
			current.last = i;
			if (funcCount >= MAX_FUNCS) fatal("too many functions");
			funcs[funcCount++] = current;
			open = 0;
		}
	}
	
	if (open) fatal("IR: missing ENDFUNC");
	if (funcCount == 0) fatal("IR: no function");
	
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		int existing;
		
		if (strcmp(insP->op, "FUNCDECL") != 0) continue;
		
		if (insP->argc != 2 && insP->argc != 3) fatal("invalid FUNCDECL");
		
		existing = findFunction(insP->args[0]);
		if (existing >= 0) {
			if (!funcs[existing].declOnly) continue;
			fprintf(stderr, "qirv32: duplicate function %s\n", insP->args[0]);
			fatal("duplicate function");
		}
		
		if (funcCount >= MAX_FUNCS) fatal("too many functions");
		memset(&current, 0, sizeof(current));
		strncpy(current.name, insP->args[0], NAME_LEN - 1);
		current.nargs = number(insP->args[1], insP->line);
		current.first = -1;
		current.last = -1;
		current.isStatic = insP->argc == 3 && number(insP->args[2], insP->line) != 0;
		current.declOnly = 1;
		funcs[funcCount++] = current;
	}
	
	/* Calculate local frame size for each function */
	for (i = 0; i < funcCount; i++) {
		Function* fn = &funcs[i];
		int highest = fn->nargs - 1;
		int k;
		
		if (fn->declOnly) continue;
		
		for (k = fn->first; k < fn->last; k++) {
			Instr* x = &ir[k];
			int slotN;
			
			if ((strcmp(x->op, "LOADL") == 0 || strcmp(x->op, "STOREL") == 0 || 
			     strcmp(x->op, "LOADC") == 0 || strcmp(x->op, "STOREC") == 0 ||
			     strcmp(x->op, "LOADP") == 0 || strcmp(x->op, "STOREP") == 0 ||
			     strcmp(x->op, "ADDRL") == 0) && x->argc > 0) {
				slotN = number(x->args[0], x->line);
				if (slotN < 0) {
					sprintf(msg, "line %d: negative local slot", x->line);
					fatal(msg);
				}
				if (slotN > highest) highest = slotN;
			}
		}
		
		fn->locals = highest >= fn->nargs ? highest - fn->nargs + 1 : 0;
		fn->frameBytes = fn->locals * 4;
	}
}

/* === RISC-V Code Generation === */

/* Emit module header for OS-9 format */
static void emitHeader(FILE* out, const char* moduleName) {
	fprintf(out, "; Q9-QCC RISC-V-32 Backend output\n");
	fprintf(out, "; Module: %s\n", moduleName);
	fprintf(out, "; Generated from IR\n");
	fprintf(out, "\n");
}

/* Emit module footer */
static void emitFooter(FILE* out) {
	fprintf(out, "\n; End of module\n");
}

/* Emit psect directive for OS-9 
   Type/Language: Type=0x00 (Object Code), Language=0x03 (RISC-V) */
static void emitPsectStart(FILE* out, const char* name) {
	fprintf(out, "\tnam\t%s\n", name);
	fprintf(out, "\tpsect\t%s,0x0003,0,1,0,0\n", name);
	fprintf(out, "\n");
}

/* RISC-V function prologue
   Stack layout:
   [sp-4]  <- old return address
   [sp-8]  <- local 0
   [sp-12] <- local 1
   ...
*/
static void emitFunctionPrologue(FILE* out, const char* funcName, int frameBytes) {
	fprintf(out, "%s:\n", funcName);
	fprintf(out, "\taddi\tsp, sp, -%d\n", frameBytes + 4);
	fprintf(out, "\tsw\tra, %d(sp)\n", frameBytes);
}

/* RISC-V function epilogue */
static void emitFunctionEpilogue(FILE* out) {
	fprintf(out, "\tlw\tra, 0(sp)\n");
	fprintf(out, "\taddi\tsp, sp, 4\n");
	fprintf(out, "\tjalr\tx0, 0(ra)\n");
}

/* Push to stack (we simulate by adjusting sp)
   For RISC-V, this would typically use t0 as temp
   Stack grows downward, so push means decrement sp
*/
static void emitPush(FILE* out, int value) {
	fprintf(out, "\taddi\tsp, sp, -4\n");
	fprintf(out, "\tli\tt0, %d\n", value);
	fprintf(out, "\tsw\tt0, 0(sp)\n");
}

/* Pop from stack into a0 */
static void emitPop(FILE* out) {
	fprintf(out, "\tlw\ta0, 0(sp)\n");
	fprintf(out, "\taddi\tsp, sp, 4\n");
}

/* Load local variable at slot onto stack */
static void emitLoadL(FILE* out, int slot, int paramCount) {
	int offset = 4 + (slot * 4);
	fprintf(out, "\tlw\ta0, -%d(sp)\n", offset);
	fprintf(out, "\taddi\tsp, sp, -4\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Store to local variable */
static void emitStoreL(FILE* out, int slot, int paramCount) {
	int offset = 4 + (slot * 4);
	fprintf(out, "\tlw\ta0, 0(sp)\n");
	fprintf(out, "\tsw\ta0, -%d(sp)\n", offset);
	fprintf(out, "\taddi\tsp, sp, 4\n");
}

/* Load global variable onto stack */
static void emitLoadG(FILE* out, const char* globalName, int globalIndex) {
	fprintf(out, "\tla\ta0, tc_g_%s\n", globalName);
	fprintf(out, "\tlw\ta0, 0(a0)\n");
	fprintf(out, "\taddi\tsp, sp, -4\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Load address of local variable */
static void emitAddrL(FILE* out, int slot, int paramCount) {
	int offset = 4 + (slot * 4);
	fprintf(out, "\taddi\ta0, sp, -%d\n", offset);
	fprintf(out, "\taddi\tsp, sp, -4\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Load address of global variable */
static void emitAddrG(FILE* out, const char* globalName, int globalIndex) {
	fprintf(out, "\tla\ta0, tc_g_%s\n", globalName);
	fprintf(out, "\taddi\tsp, sp, -4\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Load indirect (dereference pointer) */
static void emitLoadInd(FILE* out, const char* type) {
	fprintf(out, "\tlw\ta0, 0(sp)\n");
	int elemSize = tagSize(type);
	if (elemSize == 1) {
		fprintf(out, "\tlbu\ta0, 0(a0)\n");
	} else if (elemSize == 2) {
		fprintf(out, "\tlhu\ta0, 0(a0)\n");
	} else {
		fprintf(out, "\tlw\ta0, 0(a0)\n");
	}
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Store indirect (through pointer) */
static void emitStoreInd(FILE* out, const char* type) {
	fprintf(out, "\tlw\ta0, 0(sp)\n");
	fprintf(out, "\tlw\ta1, 4(sp)\n");
	int elemSize = tagSize(type);
	if (elemSize == 1) {
		fprintf(out, "\tsb\ta0, 0(a1)\n");
	} else if (elemSize == 2) {
		fprintf(out, "\tsh\ta0, 0(a1)\n");
	} else {
		fprintf(out, "\tsw\ta0, 0(a1)\n");
	}
	fprintf(out, "\taddi\tsp, sp, 8\n");
}

/* Load array element */
static void emitLoadIdx(FILE* out, const char* type) {
	fprintf(out, "\tlw\ta1, 0(sp)\n");
	fprintf(out, "\tlw\ta0, 4(sp)\n");
	int elemSize = tagSize(type);
	if (elemSize == 1) {
		fprintf(out, "\tadd\ta0, a0, a1\n");
		fprintf(out, "\tlbu\ta0, 0(a0)\n");
	} else if (elemSize == 2) {
		fprintf(out, "\tslli\ta1, a1, 1\n");
		fprintf(out, "\tadd\ta0, a0, a1\n");
		fprintf(out, "\tlhu\ta0, 0(a0)\n");
	} else {
		fprintf(out, "\tslli\ta1, a1, 2\n");
		fprintf(out, "\tadd\ta0, a0, a1\n");
		fprintf(out, "\tlw\ta0, 0(a0)\n");
	}
	fprintf(out, "\taddi\tsp, sp, 8\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Store array element */
static void emitStoreIdx(FILE* out, const char* type) {
	fprintf(out, "\tlw\ta0, 0(sp)\n");
	fprintf(out, "\tlw\ta1, 4(sp)\n");
	fprintf(out, "\tlw\ta2, 8(sp)\n");
	int elemSize = tagSize(type);
	if (elemSize == 1) {
		fprintf(out, "\tadd\ta2, a2, a1\n");
		fprintf(out, "\tsb\ta0, 0(a2)\n");
	} else if (elemSize == 2) {
		fprintf(out, "\tslli\ta1, a1, 1\n");
		fprintf(out, "\tadd\ta2, a2, a1\n");
		fprintf(out, "\tsh\ta0, 0(a2)\n");
	} else {
		fprintf(out, "\tslli\ta1, a1, 2\n");
		fprintf(out, "\tadd\ta2, a2, a1\n");
		fprintf(out, "\tsw\ta0, 0(a2)\n");
	}
	fprintf(out, "\taddi\tsp, sp, 12\n");
}

/* Pointer add */
static void emitPAdd(FILE* out) {
	fprintf(out, "\tlw\ta1, 0(sp)\n");
	fprintf(out, "\tlw\ta0, 4(sp)\n");
	fprintf(out, "\tadd\ta0, a0, a1\n");
	fprintf(out, "\taddi\tsp, sp, 8\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Pointer subtract */
static void emitPSub(FILE* out) {
	fprintf(out, "\tlw\ta1, 0(sp)\n");
	fprintf(out, "\tlw\ta0, 4(sp)\n");
	fprintf(out, "\tsub\ta0, a0, a1\n");
	fprintf(out, "\taddi\tsp, sp, 8\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Load pointer value */
static void emitLoadP(FILE* out) {
	fprintf(out, "\tlw\ta0, 0(sp)\n");
	fprintf(out, "\tlw\ta0, 0(a0)\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Store through pointer */
static void emitStoreP(FILE* out) {
	fprintf(out, "\tlw\ta0, 0(sp)\n");
	fprintf(out, "\tlw\ta1, 4(sp)\n");
	fprintf(out, "\tsw\ta0, 0(a1)\n");
	fprintf(out, "\taddi\tsp, sp, 8\n");
}

/* Pointer index calculation */
static void emitPtrIndex(FILE* out, const char* type) {
	fprintf(out, "\tlw\ta1, 0(sp)\n");
	fprintf(out, "\tlw\ta0, 4(sp)\n");
	int elemSize = tagSize(type);
	if (elemSize == 1) {
		fprintf(out, "\tadd\ta0, a0, a1\n");
	} else if (elemSize == 2) {
		fprintf(out, "\tslli\ta1, a1, 1\n");
		fprintf(out, "\tadd\ta0, a0, a1\n");
	} else {
		fprintf(out, "\tslli\ta1, a1, 2\n");
		fprintf(out, "\tadd\ta0, a0, a1\n");
	}
	fprintf(out, "\taddi\tsp, sp, 8\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Comparison operations */
static void emitCompare(FILE* out, const char* op) {
	int label_true = labelCount++;
	int label_end = labelCount++;
	
	fprintf(out, "\tlw\ta1, 0(sp)\n");
	fprintf(out, "\tlw\ta0, 4(sp)\n");
	fprintf(out, "\tli\ta2, 0\n");
	
	if (strcmp(op, "CMPEQ") == 0) {
		fprintf(out, "\tbeq\ta0, a1, L%d\n", label_true);
	} else if (strcmp(op, "CMPNE") == 0) {
		fprintf(out, "\tbne\ta0, a1, L%d\n", label_true);
	} else if (strcmp(op, "CMPLT") == 0) {
		fprintf(out, "\tblt\ta0, a1, L%d\n", label_true);
	} else if (strcmp(op, "CMPLE") == 0) {
		fprintf(out, "\tble\ta0, a1, L%d\n", label_true);
	} else if (strcmp(op, "CMPGT") == 0) {
		fprintf(out, "\tbgt\ta0, a1, L%d\n", label_true);
	} else if (strcmp(op, "CMPGE") == 0) {
		fprintf(out, "\tbge\ta0, a1, L%d\n", label_true);
	} else if (strcmp(op, "CMPULT") == 0) {
		fprintf(out, "\tbltu\ta0, a1, L%d\n", label_true);
	} else if (strcmp(op, "CMPULE") == 0) {
		fprintf(out, "\tbleu\ta0, a1, L%d\n", label_true);
	} else if (strcmp(op, "CMPUGT") == 0) {
		fprintf(out, "\tbgtu\ta0, a1, L%d\n", label_true);
	} else if (strcmp(op, "CMPUGE") ==0) {
		fprintf(out, "\tbgeu\ta0, a1, L%d\n", label_true);
	}
	
	fprintf(out, "\tj\tL%d\n", label_end);
	fprintf(out, "L%d:\n", label_true);
	fprintf(out, "\tli\ta2, 1\n");
	fprintf(out, "L%d:\n", label_end);
	fprintf(out, "\taddi\tsp, sp, 8\n");
	fprintf(out, "\tsw\ta2, 0(sp)\n");
}

/* NOT (logical) */
static void emitNot(FILE* out) {
	fprintf(out, "\tlw\ta0, 0(sp)\n");
	fprintf(out, "\tseqz\ta0, a0\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* NOT (bitwise) */
static void emitNotBit(FILE* out) {
	fprintf(out, "\tlw\ta0, 0(sp)\n");
	fprintf(out, "\tnot\ta0, a0\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Multiply */
static void emitMul(FILE* out) {
	fprintf(out, "\tlw\ta1, 0(sp)\n");
	fprintf(out, "\tlw\ta0, 4(sp)\n");
	fprintf(out, "\tmul\ta0, a0, a1\n");
	fprintf(out, "\taddi\tsp, sp, 8\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Divide (signed) */
static void emitDiv(FILE* out) {
	fprintf(out, "\tlw\ta1, 0(sp)\n");
	fprintf(out, "\tlw\ta0, 4(sp)\n");
	fprintf(out, "\tdiv\ta0, a0, a1\n");
	fprintf(out, "\taddi\tsp, sp, 8\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Modulo (signed) */
static void emitMod(FILE* out) {
	fprintf(out, "\tlw\ta1, 0(sp)\n");
	fprintf(out, "\tlw\ta0, 4(sp)\n");
	fprintf(out, "\trem\ta0, a0, a1\n");
	fprintf(out, "\taddi\tsp, sp, 8\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Unsigned divide */
static void emitUDiv(FILE* out) {
	fprintf(out, "\tlw\ta1, 0(sp)\n");
	fprintf(out, "\tlw\ta0, 4(sp)\n");
	fprintf(out, "\tdivu\ta0, a0, a1\n");
	fprintf(out, "\taddi\tsp, sp, 8\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* Unsigned modulo */
static void emitUMod(FILE* out) {
	fprintf(out, "\tlw\ta1, 0(sp)\n");
	fprintf(out, "\tlw\ta0, 4(sp)\n");
	fprintf(out, "\tremu\ta0, a0, a1\n");
	fprintf(out, "\taddi\tsp, sp, 8\n");
	fprintf(out, "\tsw\ta0, 0(sp)\n");
}

/* === Main Codegen === */

static void emitIR(FILE* out) {
	int i, fi, ki;
	
	emitPsectStart(out, "tc_prog");
	
	for (fi = 0; fi < funcCount; fi++) {
		Function* fn = &funcs[fi];
		
		if (fn->declOnly) {
			fprintf(out, "; FUNCDECL %s (external)\n", fn->name);
			continue;
		}
		
		emitFunctionPrologue(out, fn->name, fn->frameBytes);
		fprintf(out, "\n");
		
		for (ki = fn->first; ki < fn->last; ki++) {
			Instr* insP = &ir[ki];
			const char* op = insP->op;
			
			if (strcmp(op, "PUSH") == 0) {
				int val = number(insP->args[0], insP->line);
				fprintf(out, "\t; PUSH %d\n", val);
				emitPush(out, val);
			}
			else if (strcmp(op, "LOADL") == 0) {
				int slot = number(insP->args[0], insP->line);
				fprintf(out, "\t; LOADL %d\n", slot);
				emitLoadL(out, slot, fn->nargs);
			}
			else if (strcmp(op, "STOREL") == 0) {
				int slot = number(insP->args[0], insP->line);
				fprintf(out, "\t; STOREL %d\n", slot);
				emitStoreL(out, slot, fn->nargs);
			}
			else if (strcmp(op, "LOADG") == 0) {
				fprintf(out, "\t; LOADG %s\n", insP->args[0]);
				emitLoadG(out, insP->args[0], findGlobal(insP->args[0]));
			}
			else if (strcmp(op, "ADD") == 0) {
				fprintf(out, "\t; ADD\n");
				fprintf(out, "\tlw\ta0, 0(sp)\n");
				fprintf(out, "\tlw\ta1, 4(sp)\n");
				fprintf(out, "\tadd\ta0, a1, a0\n");
				fprintf(out, "\taddi\tsp, sp, 8\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
			}
			else if (strcmp(op, "SUB") == 0) {
				fprintf(out, "\t; SUB\n");
				fprintf(out, "\tlw\ta1, 0(sp)\n");
				fprintf(out, "\tlw\ta0, 4(sp)\n");
				fprintf(out, "\tsub\ta0, a0, a1\n");
				fprintf(out, "\taddi\tsp, sp, 8\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
			}
			else if (strcmp(op, "MUL") == 0) {
				fprintf(out, "\t; MUL\n");
				emitMul(out);
			}
			else if (strcmp(op, "DIV") == 0) {
				fprintf(out, "\t; DIV\n");
				emitDiv(out);
			}
			else if (strcmp(op, "MOD") == 0) {
				fprintf(out, "\t; MOD\n");
				emitMod(out);
			}
			else if (strcmp(op, "UDIV") == 0) {
				fprintf(out, "\t; UDIV\n");
				emitUDiv(out);
			}
			else if (strcmp(op, "UMOD") == 0) {
				fprintf(out, "\t; UMOD\n");
				emitUMod(out);
			}
			else if (strcmp(op, "AND") == 0) {
				fprintf(out, "\t; AND\n");
				fprintf(out, "\tlw\ta0, 0(sp)\n");
				fprintf(out, "\tlw\ta1, 4(sp)\n");
				fprintf(out, "\tand\ta0, a1, a0\n");
				fprintf(out, "\taddi\tsp, sp, 8\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
			}
			else if (strcmp(op, "OR") == 0) {
				fprintf(out, "\t; OR\n");
				fprintf(out, "\tlw\ta0, 0(sp)\n");
				fprintf(out, "\tlw\ta1, 4(sp)\n");
				fprintf(out, "\tor\ta0, a1, a0\n");
				fprintf(out, "\taddi\tsp, sp, 8\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
			}
			else if (strcmp(op, "XOR") == 0) {
				fprintf(out, "\t; XOR\n");
				fprintf(out, "\tlw\ta0, 0(sp)\n");
				fprintf(out, "\tlw\ta1, 4(sp)\n");
				fprintf(out, "\txor\ta0, a1, a0\n");
				fprintf(out, "\taddi\tsp, sp, 8\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
			}
			else if (strcmp(op, "NOT") == 0) {
				fprintf(out, "\t; NOT\n");
				emitNot(out);
			}
			else if (strcmp(op, "NOTB") == 0) {
				fprintf(out, "\t; NOTB\n");
				emitNotBit(out);
			}
			else if (strcmp(op, "SHL") == 0) {
				fprintf(out, "\t; SHL\n");
				fprintf(out, "\tlw\ta1, 0(sp)\n");
				fprintf(out, "\tlw\ta0, 4(sp)\n");
				fprintf(out, "\tsll\ta0, a0, a1\n");
				fprintf(out, "\taddi\tsp, sp, 8\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
			}
			else if (strcmp(op, "SHR") == 0) {
				fprintf(out, "\t; SHR\n");
				fprintf(out, "\tlw\ta1, 0(sp)\n");
				fprintf(out, "\tlw\ta0, 4(sp)\n");
				fprintf(out, "\tsrl\ta0, a0, a1\n");
				fprintf(out, "\taddi\tsp, sp, 8\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
			}
			else if (strcmp(op, "SHRA") == 0) {
				fprintf(out, "\t; SHRA\n");
				fprintf(out, "\tlw\ta1, 0(sp)\n");
				fprintf(out, "\tlw\ta0, 4(sp)\n");
				fprintf(out, "\tsra\ta0, a0, a1\n");
				fprintf(out, "\taddi\tsp, sp, 8\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
			}
			else if (strcmp(op, "NARROWC") == 0) {
				fprintf(out, "\t; NARROWC\n");
				fprintf(out, "\tlw\ta0, 0(sp)\n");
				fprintf(out, "\tandi\ta0, a0, 0xFF\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
			}
			else if (strcmp(op, "DROP") == 0) {
				fprintf(out, "\t; DROP\n");
				fprintf(out, "\taddi\tsp, sp, 4\n");
			}
			else if (strcmp(op, "PRINT") == 0) {
				fprintf(out, "\t; PRINT (debug)\n");
				fprintf(out, "\tlw\ta0, 0(sp)\n");
				fprintf(out, "\t; TODO: implement debug output\n");
			}
			else if (strcmp(op, "PRINTU") == 0) {
				fprintf(out, "\t; PRINTU (debug unsigned)\n");
				fprintf(out, "\tlw\ta0, 0(sp)\n");
				fprintf(out, "\t; TODO: implement debug output\n");
			}
			else if (strcmp(op, "SWAP") == 0) {
				fprintf(out, "\t; SWAP\n");
				fprintf(out, "\tlw\ta0, 0(sp)\n");
				fprintf(out, "\tlw\ta1, 4(sp)\n");
				fprintf(out, "\tsw\ta1, 0(sp)\n");
				fprintf(out, "\tsw\ta0, 4(sp)\n");
			}
			else if (strcmp(op, "DUP") == 0) {
				fprintf(out, "\t; DUP\n");
				fprintf(out, "\tlw\ta0, 0(sp)\n");
				fprintf(out, "\taddi\tsp, sp, -4\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
			}
			else if (strcmp(op, "LABEL") == 0) {
				fprintf(out, "%s:\n", insP->args[0]);
			}
			else if (strcmp(op, "CALL") == 0) {
				const char* fname = insP->args[0];
				int nargs = number(insP->args[1], insP->line);
				fprintf(out, "\t; CALL %s (%d args)\n", fname, nargs);
				fprintf(out, "\tcall\ttc_%s\n", fname);
				fprintf(out, "\taddi\tsp, sp, -4\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
				if (nargs > 0) {
					fprintf(out, "\taddi\tsp, sp, %d\n", nargs * 4);
				}
			}
			else if (strcmp(op, "CALLP") == 0) {
				int nargs = number(insP->args[0], insP->line);
				fprintf(out, "\t; CALLP (%d args via function pointer)\n", nargs);
				fprintf(out, "\tlw\ta0, 0(sp)\n");
				fprintf(out, "\tjalr\tra, 0(a0)\n");
				fprintf(out, "\taddi\tsp, sp, 4\n");
				fprintf(out, "\taddi\tsp, sp, -4\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
				if (nargs > 0) {
					fprintf(out, "\taddi\tsp, sp, %d\n", nargs * 4);
				}
			}
			else if (strcmp(op, "PUSHFN") == 0) {
				const char* fname = insP->args[0];
				fprintf(out, "\t; PUSHFN %s\n", fname);
				fprintf(out, "\tla\ta0, tc_%s\n", fname);
				fprintf(out, "\taddi\tsp, sp, -4\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
			}
			else if (strcmp(op, "RET") == 0 || strcmp(op, "RETP") == 0) {
				fprintf(out, "\t; %s\n", op);
				fprintf(out, "\tlw\ta0, 0(sp)\n");
				emitFunctionEpilogue(out);
			}
			else if (strcmp(op, "CALLIND") == 0) {
				int nargs = number(insP->args[0], insP->line);
				fprintf(out, "\t; CALLIND (%d args)\n", nargs);
				fprintf(out, "\tlw\ta0, 0(sp)\n");
				fprintf(out, "\tjalr\tra, 0(a0)\n");
				fprintf(out, "\taddi\tsp, sp, 4\n");
				fprintf(out, "\taddi\tsp, sp, -4\n");
				fprintf(out, "\tsw\ta0, 0(sp)\n");
				if (nargs > 0) {
					fprintf(out, "\taddi\tsp, sp, %d\n", nargs * 4);
				}
			}
			else if (strcmp(op, "ADDRL") == 0) {
				int slot = number(insP->args[0], insP->line);
				fprintf(out, "\t; ADDRL %d\n", slot);
				emitAddrL(out, slot, fn->nargs);
			}
			else if (strcmp(op, "ADDRG") == 0) {
				fprintf(out, "\t; ADDRG %s\n", insP->args[0]);
				emitAddrG(out, insP->args[0], findGlobal(insP->args[0]));
			}
			else if (strcmp(op, "LOADIND") == 0) {
				fprintf(out, "\t; LOADIND %s\n", insP->args[0]);
				emitLoadInd(out, insP->args[0]);
			}
			else if (strcmp(op, "STOREIND") == 0) {
				fprintf(out, "\t; STOREIND %s\n", insP->args[0]);
				emitStoreInd(out, insP->args[0]);
			}
			else if (strcmp(op, "LOADIDX") == 0) {
				fprintf(out, "\t; LOADIDX %s\n", insP->args[0]);
				emitLoadIdx(out, insP->args[0]);
			}
			else if (strcmp(op, "STOREIDX") == 0) {
				fprintf(out, "\t; STOREIDX %s\n", insP->args[0]);
				emitStoreIdx(out, insP->args[0]);
			}
			else if (strcmp(op, "PADD") == 0) {
				fprintf(out, "\t; PADD\n");
				emitPAdd(out);
			}
			else if (strcmp(op, "PSUB") == 0) {
				fprintf(out, "\t; PSUB\n");
				emitPSub(out);
			}
			else if (strcmp(op, "LOADP") == 0) {
				fprintf(out, "\t; LOADP\n");
				emitLoadP(out);
			}
			else if (strcmp(op, "STOREP") == 0) {
				fprintf(out, "\t; STOREP\n");
				emitStoreP(out);
			}
			else if (strcmp(op, "PUSHADDR") == 0) {
				if (insP->argc >= 2) {
					if (strcmp(insP->args[0], "L") == 0) {
						int slot = number(insP->args[1], insP->line);
						fprintf(out, "\t; PUSHADDR L %d\n", slot);
						emitAddrL(out, slot, fn->nargs);
					} else if (strcmp(insP->args[0], "G") == 0) {
						fprintf(out, "\t; PUSHADDR G %s\n", insP->args[1]);
						emitAddrG(out, insP->args[1], findGlobal(insP->args[1]));
					}
				}
			}
			else if (strcmp(op, "PTRINDEX") == 0) {
				fprintf(out, "\t; PTRINDEX %s\n", insP->args[0]);
				emitPtrIndex(out, insP->args[0]);
			}
			else if (strcmp(op, "IPADD") == 0) {
				fprintf(out, "\t; IPADD %s\n", insP->args[0]);
				emitPtrIndex(out, insP->args[0]);
			}
			else if (strcmp(op, "PRINTC") == 0) {
				fprintf(out, "\t; PRINTC\n");
				fprintf(out, "\tlw\ta0, 0(sp)\n");
			}
			else if (strcmp(op, "LARRAY") == 0) {
				fprintf(out, "\t; LARRAY %s %s %s (space pre-allocated)\n", 
					insP->args[0], insP->args[1], insP->args[2]);
			}
			else if (strcmp(op, "GARRAY") == 0) {
				fprintf(out, "\t; GARRAY %s %s %s (static local array)\n",
					insP->args[0], insP->args[1], insP->args[2]);
			}
			else if (strcmp(op, "GINIT") == 0) {
				fprintf(out, "\t; GINIT %s[%s] = %s (handled by linker)\n",
					insP->args[0], insP->args[1], insP->args[2]);
			}
			else if (strcmp(op, "CALLEXT") == 0 || strcmp(op, "CALLEXTP") == 0) {
				if (strcmp(op, "CALLEXT") == 0) {
					const char* fname = insP->args[0];
					int nargs = number(insP->args[1], insP->line);
					fprintf(out, "\t; CALLEXT %s (%d args)\n", fname, nargs);
					fprintf(out, "\tcall\t%s\n", fname);
					fprintf(out, "\taddi\tsp, sp, -4\n");
					fprintf(out, "\tsw\ta0, 0(sp)\n");
					if (nargs > 0) {
						fprintf(out, "\taddi\tsp, sp, %d\n", nargs * 4);
					}
				} else {
					int nargs = number(insP->args[0], insP->line);
					fprintf(out, "\t; CALLEXTP (%d args)\n", nargs);
					fprintf(out, "\tlw\ta0, 0(sp)\n");
					fprintf(out, "\tjalr\tra, 0(a0)\n");
					fprintf(out, "\taddi\tsp, sp, 4\n");
					fprintf(out, "\taddi\tsp, sp, -4\n");
					fprintf(out, "\tsw\ta0, 0(sp)\n");
					if (nargs > 0) {
						fprintf(out, "\taddi\tsp, sp, %d\n", nargs * 4);
					}
				}
			}
			else if (strcmp(op, "PDIFF") == 0) {
				fprintf(out, "\t; PDIFF\n");
				emitPSub(out);
			}
			else if (strcmp(op, "PCMPEQ") == 0 || strcmp(op, "PCMPNE") == 0 ||
					 strcmp(op, "PCMPLT") == 0 || strcmp(op, "PCMPLE") == 0 ||
					 strcmp(op, "PCMPGT") == 0 || strcmp(op, "PCMPGE") == 0) {
				fprintf(out, "\t; %s\n", op);
				emitCompare(out, op);
			}
			else if (strcmp(op, "CMPEQ") == 0 || strcmp(op, "CMPNE") == 0 ||
					 strcmp(op, "CMPLT") == 0 || strcmp(op, "CMPLE") == 0 ||
					 strcmp(op, "CMPGT") == 0 || strcmp(op, "CMPGE") == 0 ||
					 strcmp(op, "CMPULT") == 0 || strcmp(op, "CMPULE") == 0 ||
					 strcmp(op, "CMPUGT") == 0 || strcmp(op, "CMPUGE") == 0) {
				fprintf(out, "\t; %s\n", op);
				emitCompare(out, op);
			}
			else if (strcmp(op, "ENDFUNC") == 0) {
				break;
			}
			else {
				fprintf(out, "\t; TODO: %s\n", op);
			}
		}
		
		fprintf(out, "\n");
		if (ki >= fn->last && strcmp(ir[fn->last - 1].op, "RET") != 0 && 
			strcmp(ir[fn->last - 1].op, "RETP") != 0) {
			fprintf(out, "\tli\ta0, 0\n");
			emitFunctionEpilogue(out);
		}
		fprintf(out, "\n\n");
	}
	
	if (globalCount > 0) {
		fprintf(out, "; Global variables (data section)\n");
		fprintf(out, "\tvsect\n");
		
		int gi;
		for (gi = 0; gi < globalCount; gi++) {
			Global* g = &globals[gi];
			
			if (g->declOnly) {
				fprintf(out, "; GLOBALDECL %s (external)\n", g->name);
				continue;
			}
			
			fprintf(out, "tc_g_%s:\n", g->name);
			if (g->isArray) {
				fprintf(out, "\tds.l\t%d\n", g->length);
			} else {
				fprintf(out, "\tdc.l\t%d\n", g->initialValue);
			}
		}
		
		fprintf(out, "\tends\n");
	}
}

/* === Main === */

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s <input.ir> [output.srv32]\n", argv[0]);
		return 1;
	}
	
	const char* infile = argv[1];
	const char* outfile = (argc > 2) ? argv[2] : NULL;
	
	FILE* in = fopen(infile, "r");
	if (!in) {
		perror(infile);
		return 1;
	}
	
	if (readIR(in) == 0) {
		fprintf(stderr, "no IR lines read\n");
		fclose(in);
		return 1;
	}
	fclose(in);
	
	collectGlobals();
	collectFunctions();
	
	FILE* out = stdout;
	if (outfile) {
		out = fopen(outfile, "w");
		if (!out) {
			perror(outfile);
			return 1;
		}
	}
	
	emitHeader(out, "tc_prog");
	emitIR(out);
	emitFooter(out);
	
	if (outfile) fclose(out);
	
	fprintf(stderr, "OK: %d IR lines, %d funcs, %d globals\n", irCount, funcCount, globalCount);
	return 0;
}
