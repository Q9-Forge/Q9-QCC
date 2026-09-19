/* q9-qirx86: Q9 Stack IR to x86-32 Assembler Codegen
   
   Translates .ir (stack-based IR) to .sx86 (Intel-syntax x86-32 assembler).
   
   Canonical semantics source: tools/qccvm.py
   IR opcode reference: docs/IR_OPCODES.md
   
   Architecture: Stack-based model (emulate IR stack via EAX/Stack).
   PIC via CALL-$+5 + POP + LEA trick for global address loading.
   Relocation handled by linker (qo/ql), not by backend.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* === Capacity limits (from 68k backend, adjusted for x86) === */
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
	int elemSize;  /* 1=char, 2=short, 4=int/pointer */
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

/* Label counter for conditional jumps */
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

/* Store an argument string in the pool and return pointer */
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

/* Parse a number from string (for slot indices, array lengths, etc.) */
static int number(const char* s, int lineNum) {
	char* end;
	long val = strtol(s, &end, 10);
	if (*end != '\0') {
		fprintf(stderr, "ERROR line %d: not a number: %s\n", lineNum, s);
		exit(1);
	}
	return (int)val;
}

/* Type tag to byte size: c/b=1, h=2, default=4 */
static int tagSize(const char* tag) {
	if (!tag || !*tag) return 4;
	if (tag[0] == 'c' || tag[0] == 'b') return 1;
	if (tag[0] == 'h') return 2;
	return 4;
}

/* Check if string is a valid type tag */
static int isNumWord(const char* tag) {
	if (!tag || !*tag) return 0;
	return tag[0] == 'c' || tag[0] == 'b' || tag[0] == 'h' || tag[0] == 'p' || isdigit(tag[0]);
}

/* Find global by name, return index or -1 */
static int findGlobal(const char* name) {
	int i;
	for (i = 0; i < globalCount; i++) {
		if (strcmp(globals[i].name, name) == 0) return i;
	}
	return -1;
}

/* Find function by name, return index or -1 */
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
		
		/* Remove trailing whitespace */
		int len = strlen(line);
		while (len > 0 && isspace(line[len-1])) line[--len] = '\0';
		
		/* Skip empty lines and comments */
		if (!len || line[0] == ';' || line[0] == '#') continue;
		
		if (irCount >= MAX_IR_LINES) {
			fatal("IR line limit exceeded");
		}
		
		Instr* p = &ir[irCount];
		p->line = lineNum;
		
		/* Parse: OPCODE [arg1 [arg2 ...]] */
		char* tok = strtok(line, " \t");
		if (!tok) continue;
		
		strncpy(p->op, tok, OP_LEN - 1);
		p->op[OP_LEN - 1] = '\0';
		
		p->argc = 0;
		while ((tok = strtok(NULL, " \t")) && p->argc < MAX_ARGS) {
			p->args[p->argc++] = (char*)poolArg(tok);
		}
		
		/* Fill unused slots with empty string pointer (don't increment argc) */
		int j;
		for (j = p->argc; j < MAX_ARGS; j++) {
			p->args[j] = argEmpty;
		}
		
		irCount++;
	}
	
	return irCount;
}

/* Extract globals from IR: GLOBAL, GARRAY, GLOBALDECL */
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
		
		/* GLOBAL can be:
		   - GLOBAL <name>                           (1 arg)
		   - GLOBAL <name> <init>                    (2 args)
		   - GLOBAL <name> <init> <type> [<static>] (3-4 args)
		*/
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
		
		/* Parse arguments based on argc */
		globals[gi].initialValue = (insP->argc >= 2) ? number(insP->args[1], insP->line) : 0;
		globals[gi].elemSize = (insP->argc >= 3) ? tagSize(insP->args[2]) : 4;
		globals[gi].isArray = 0;
		globals[gi].length = 1;
		globals[gi].isStatic = (insP->argc >= 4) ? (number(insP->args[3], insP->line) != 0) : 0;
	}
	
	/* GLOBALDECL: forward declarations from other files */
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

/* Extract functions from IR: FUNC/ENDFUNC, FUNCDECL */
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
	
	/* FUNCDECL: forward declarations from other files */
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		int existing;
		
		if (strcmp(insP->op, "FUNCDECL") != 0) continue;
		
		if (insP->argc != 2 && insP->argc != 3) fatal("invalid FUNCDECL");
		
		existing = findFunction(insP->args[0]);
		if (existing >= 0) {
			if (!funcs[existing].declOnly) continue;
			fprintf(stderr, "qirx86: duplicate function %s\n", insP->args[0]);
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
		
		if (fn->declOnly) continue;  /* No body, skip frame calculation */
		
		for (k = fn->first; k < fn->last; k++) {
			Instr* x = &ir[k];
			int slotN;
			
			/* Check for local variable access */
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

/* === Codegen helpers === */

/* Emit module header for OS-9 format */
static void emitHeader(FILE* out, const char* moduleName) {
	fprintf(out, "; Q9-QCC x86-32 Backend output\n");
	fprintf(out, "; Module: %s\n", moduleName);
	fprintf(out, "; Generated from IR\n");
	fprintf(out, "\n");
}

/* Emit module footer */
static void emitFooter(FILE* out) {
	fprintf(out, "\n; End of module\n");
}

/* Emit psect directive for OS-9 
   Type/Language: Type=0x00 (Object Code), Language=0x02 (x86) */
static void emitPsectStart(FILE* out, const char* name) {
	fprintf(out, "\tnam\t%s\n", name);
	fprintf(out, "\tpsect\t%s,0x0002,0,1,0,0\n", name);
	fprintf(out, "\n");
}

/* Emit function prologue (x86 standard) */
static void emitFunctionPrologue(FILE* out, const char* funcName, int frameBytes) {
	fprintf(out, "%s:\n", funcName);
	fprintf(out, "\tpush\tebp\n");
	fprintf(out, "\tmov\tebp, esp\n");
	if (frameBytes > 0) {
		fprintf(out, "\tsub\tesp, %d\n", frameBytes);
	}
}

/* Emit function epilogue (x86 standard) */
static void emitFunctionEpilogue(FILE* out) {
	fprintf(out, "\tmov\tesp, ebp\n");
	fprintf(out, "\tpop\tebp\n");
	fprintf(out, "\tret\n");
}

/* Emit a PUSH instruction (IR: push value onto stack) */
static void emitPush(FILE* out, int value) {
	fprintf(out, "\tpush\tdword %d\n", value);
}

/* Emit a POP instruction (IR: pop from stack into eax) */
static void emitPop(FILE* out) {
	fprintf(out, "\tpop\teax\n");
}

/* Emit LOADL: load local variable at slot onto stack */
static void emitLoadL(FILE* out, int slot, int paramCount) {
	/* slot 0..paramCount-1 = parameters (on stack above return addr)
	   slot paramCount+ = locals (below EBP)
	   
	   Stack layout (x86):
	   [ESP]           <- top of stack (will grow down)
	   ...
	   [EBP+4]         <- return address
	   [EBP+8]         <- param 0
	   [EBP+12]        <- param 1
	   ...
	   [EBP-4]         <- local 0 (slot paramCount)
	   [EBP-8]         <- local 1
	   ...
	*/
	
	if (slot < paramCount) {
		int offset = 8 + (paramCount - 1 - slot) * 4;
		fprintf(out, "\tmov\teax, [ebp + %d]\n", offset);
	} else {
		int localIndex = slot - paramCount;
		int offset = -(localIndex + 1) * 4;
		fprintf(out, "\tmov\teax, [ebp + %d]\n", offset);
	}
	fprintf(out, "\tpush\teax\n");
}

/* Emit STOREL: pop from stack and store to local variable */
static void emitStoreL(FILE* out, int slot, int paramCount) {
	if (slot < paramCount) {
		int offset = 8 + (paramCount - 1 - slot) * 4;
		fprintf(out, "\tpop\teax\n");
		fprintf(out, "\tmov\t[ebp + %d], eax\n", offset);
	} else {
		int localIndex = slot - paramCount;
		int offset = -(localIndex + 1) * 4;
		fprintf(out, "\tpop\teax\n");
		fprintf(out, "\tmov\t[ebp + %d], eax\n", offset);
	}
}

/* Emit PIC address loading for global: CALL-$+5 + POP + LEA trick
   Note: This is a simplified version; real implementation needs offset calculation
*/
static void emitLoadGlobalAddress(FILE* out, const char* globalName, int globalIndex) {
	fprintf(out, "\tcall\t$ + 5\n");          /* push next instruction addr */
	fprintf(out, "\tpop\tedx\n");             /* EDX = base (current IP) */
	fprintf(out, "\tlea\teax, [edx + __offset_%s]\n", globalName);  /* relative offset (linker fills in) */
}

/* Emit LOADG: load global variable onto stack */
static void emitLoadG(FILE* out, const char* globalName, int globalIndex) {
	emitLoadGlobalAddress(out, globalName, globalIndex);
	fprintf(out, "\tmov\teax, [eax]\n");      /* dereference */
	fprintf(out, "\tpush\teax\n");
}

/* Emit ADDRL: load address of local variable */
static void emitAddrL(FILE* out, int slot, int paramCount) {
	int offset = (slot < paramCount) 
		? (8 + (paramCount - 1 - slot) * 4)      /* parameter offset */
		: (-4 - (slot - paramCount) * 4);        /* local offset */
	fprintf(out, "\tlea\teax, [ebp + %d]\n", offset);
	fprintf(out, "\tpush\teax\n");
}

/* Emit ADDRG: load address of global variable */
static void emitAddrG(FILE* out, const char* globalName, int globalIndex) {
	emitLoadGlobalAddress(out, globalName, globalIndex);
	fprintf(out, "\tpush\teax\n");
}

/* Emit LOADIND: dereference pointer (pop ptr, load [ptr], push value) */
static void emitLoadInd(FILE* out, const char* type) {
	fprintf(out, "\tpop\teax\n");  /* pointer */
	int elemSize = tagSize(type);
	switch (elemSize) {
		case 1:
			fprintf(out, "\tmovzx\teax, byte [eax]\n");
			break;
		case 2:
			fprintf(out, "\tmovzx\teax, word [eax]\n");
			break;
		default: /* 4 */
			fprintf(out, "\tmov\teax, [eax]\n");
			break;
	}
	fprintf(out, "\tpush\teax\n");
}

/* Emit STOREIND: store through pointer (pop value, pop ptr, store [ptr] = value) */
static void emitStoreInd(FILE* out, const char* type) {
	fprintf(out, "\tpop\teax\n");  /* value */
	fprintf(out, "\tpop\tecx\n");  /* pointer */
	int elemSize = tagSize(type);
	switch (elemSize) {
		case 1:
			fprintf(out, "\tmov\t[ecx], al\n");
			break;
		case 2:
			fprintf(out, "\tmov\t[ecx], ax\n");
			break;
		default: /* 4 */
			fprintf(out, "\tmov\t[ecx], eax\n");
			break;
	}
}

/* Emit LOADIDX: array element load (pop index, pop base, load [base + index*elemSize]) */
static void emitLoadIdx(FILE* out, const char* type) {
	fprintf(out, "\tpop\tecx\n");  /* index */
	fprintf(out, "\tpop\teax\n");  /* array base */
	int elemSize = tagSize(type);
	if (elemSize == 1) {
		fprintf(out, "\tmovzx\teax, byte [eax + ecx]\n");
	} else if (elemSize == 2) {
		fprintf(out, "\tmovzx\teax, word [eax + ecx*2]\n");
	} else { /* 4 */
		fprintf(out, "\tmov\teax, [eax + ecx*4]\n");
	}
	fprintf(out, "\tpush\teax\n");
}

/* Emit STOREIDX: array element store (pop value, pop index, pop base, store [base + index*elemSize] = value) */
static void emitStoreIdx(FILE* out, const char* type) {
	fprintf(out, "\tpop\teax\n");  /* value */
	fprintf(out, "\tpop\tecx\n");  /* index */
	fprintf(out, "\tpop\tedx\n");  /* array base */
	int elemSize = tagSize(type);
	if (elemSize == 1) {
		fprintf(out, "\tmov\t[edx + ecx], al\n");
	} else if (elemSize == 2) {
		fprintf(out, "\tmov\t[edx + ecx*2], ax\n");
	} else { /* 4 */
		fprintf(out, "\tmov\t[edx + ecx*4], eax\n");
	}
}

/* Emit PADD: pointer arithmetic (pop offset, pop ptr, push ptr + offset) */
static void emitPAdd(FILE* out) {
	fprintf(out, "\tpop\tecx\n");  /* offset */
	fprintf(out, "\tpop\teax\n");  /* pointer */
	fprintf(out, "\tadd\teax, ecx\n");
	fprintf(out, "\tpush\teax\n");
}

/* Emit PSUB: pointer arithmetic (pop offset, pop ptr, push ptr - offset) */
static void emitPSub(FILE* out) {
	fprintf(out, "\tpop\tecx\n");  /* offset */
	fprintf(out, "\tpop\teax\n");  /* pointer */
	fprintf(out, "\tsub\teax, ecx\n");
	fprintf(out, "\tpush\teax\n");
}

/* Emit LOADP: load pointer value (pop ptr, push [ptr]) */
static void emitLoadP(FILE* out) {
	fprintf(out, "\tpop\teax\n");  /* pointer */
	fprintf(out, "\tmov\teax, [eax]\n");  /* dereference */
	fprintf(out, "\tpush\teax\n");
}

/* Emit STOREP: store through pointer (pop value, pop ptr, [ptr] = value) */
static void emitStoreP(FILE* out) {
	fprintf(out, "\tpop\teax\n");  /* value */
	fprintf(out, "\tpop\tecx\n");  /* pointer */
	fprintf(out, "\tmov\t[ecx], eax\n");
}

/* Emit PTRINDEX: pointer index calculation (pop index, pop ptr, push ptr + index*elemSize)
   Note: This is similar to LOADIDX but doesn't dereference - just calculates address */
static void emitPtrIndex(FILE* out, const char* type) {
	fprintf(out, "\tpop\tecx\n");  /* index */
	fprintf(out, "\tpop\teax\n");  /* pointer */
	int elemSize = tagSize(type);
	if (elemSize == 1) {
		fprintf(out, "\tadd\teax, ecx\n");
	} else if (elemSize == 2) {
		fprintf(out, "\tmov\tedx, ecx\n");
		fprintf(out, "\tshl\tedx, 1\n");
		fprintf(out, "\tadd\teax, edx\n");
	} else { /* 4 */
		fprintf(out, "\tmov\tedx, ecx\n");
		fprintf(out, "\tshl\tedx, 2\n");
		fprintf(out, "\tadd\teax, edx\n");
	}
	fprintf(out, "\tpush\teax\n");
}

/* Emit IPADD: integer pointer add (pop offset, pop ptr, push ptr + offset*elemSize)
   Similar to PTRINDEX but name suggests it's for pointer arithmetic with integer scaling */
static void emitIPAdd(FILE* out, const char* type) {
	emitPtrIndex(out, type);  /* Same implementation */
}

/* Emit PDIFF: pointer difference (pop ptr2, pop ptr1, push ptr1 - ptr2) */
static void emitPDiff(FILE* out) {
	fprintf(out, "\tpop\tecx\n");  /* ptr2 */
	fprintf(out, "\tpop\teax\n");  /* ptr1 */
	fprintf(out, "\tsub\teax, ecx\n");
	fprintf(out, "\tpush\teax\n");
}

/* Emit pointer comparisons (PCMPEQ, PCMPNE, PCMPLT, etc.)
   Result is 0 or 1 on stack */
static void emitPCompare(FILE* out, const char* op) {
	const char* mnem = NULL;
	
	if (strcmp(op, "PCMPEQ") == 0) mnem = "je";
	else if (strcmp(op, "PCMPNE") == 0) mnem = "jne";
	else if (strcmp(op, "PCMPLT") == 0) mnem = "jl";
	else if (strcmp(op, "PCMPLE") == 0) mnem = "jle";
	else if (strcmp(op, "PCMPGT") == 0) mnem = "jg";
	else if (strcmp(op, "PCMPGE") == 0) mnem = "jge";
	
	if (!mnem) return;
	
	fprintf(out, "\tpop\tecx\n");
	fprintf(out, "\tpop\teax\n");
	fprintf(out, "\tcmp\teax, ecx\n");
	fprintf(out, "\txor\teax, eax\n");
	fprintf(out, "\t%s $+2\n", mnem);
	fprintf(out, "\tmov\teax, 1\n");
	fprintf(out, "\tpush\teax\n");
}

/* Emit PRINTC: print character */
static void emitPrintC(FILE* out) {
	fprintf(out, "\tpop\teax\n");
	fprintf(out, "\t; TODO: implement debug char output (char in AL)\n");
}

/* Emit comparison opcodes (result is 0 or 1 on stack) */
static void emitCompare(FILE* out, const char* op) {
	const char* mnem = NULL;
	
	if (strcmp(op, "CMPEQ") == 0) mnem = "je";
	else if (strcmp(op, "CMPNE") == 0) mnem = "jne";
	else if (strcmp(op, "CMPLT") == 0) mnem = "jl";
	else if (strcmp(op, "CMPLE") == 0) mnem = "jle";
	else if (strcmp(op, "CMPGT") == 0) mnem = "jg";
	else if (strcmp(op, "CMPGE") == 0) mnem = "jge";
	else if (strcmp(op, "CMPULT") == 0) mnem = "jb";
	else if (strcmp(op, "CMPULE") == 0) mnem = "jbe";
	else if (strcmp(op, "CMPUGT") == 0) mnem = "ja";
	else if (strcmp(op, "CMPUGE") == 0) mnem = "jae";
	
	if (!mnem) return;  /* Unknown comparison */
	
	/* Pop two values, compare, set EAX to 0 or 1 */
	fprintf(out, "\tpop\tecx\n");           /* right operand */
	fprintf(out, "\tpop\teax\n");           /* left operand */
	fprintf(out, "\tcmp\teax, ecx\n");
	fprintf(out, "\txor\teax, eax\n");      /* EAX = 0 */
	fprintf(out, "\t%s $+2\n", mnem);       /* skip next instruction if false */
	fprintf(out, "\tmov\teax, 1\n");        /* EAX = 1 if true */
	fprintf(out, "\tpush\teax\n");
}

/* Emit NOT (logical not) */
static void emitNot(FILE* out) {
	fprintf(out, "\tpop\teax\n");
	fprintf(out, "\ttest\teax, eax\n");
	fprintf(out, "\txor\teax, eax\n");
	fprintf(out, "\tsete\tal\n");           /* AL = 1 if zero, else 0 */
	fprintf(out, "\tmovzx\teax, al\n");     /* Zero-extend to 32-bit */
	fprintf(out, "\tpush\teax\n");
}

/* Emit NOT bitwise */
static void emitNotBit(FILE* out) {
	fprintf(out, "\tpop\teax\n");
	fprintf(out, "\tnot\teax\n");
	fprintf(out, "\tpush\teax\n");
}

/* Emit MUL (stack: a, b → a*b) */
static void emitMul(FILE* out) {
	fprintf(out, "\tpop\tecx\n");
	fprintf(out, "\tpop\teax\n");
	fprintf(out, "\timul\teax, ecx\n");
	fprintf(out, "\tpush\teax\n");
}

/* Emit DIV (stack: a, b → a/b, signed) */
static void emitDiv(FILE* out) {
	fprintf(out, "\tpop\tecx\n");
	fprintf(out, "\tpop\teax\n");
	fprintf(out, "\tcdq\n");                /* sign-extend EAX to EDX:EAX */
	fprintf(out, "\tidiv\tecx\n");
	fprintf(out, "\tpush\teax\n");
}

/* Emit MOD (stack: a, b → a mod b, signed) */
static void emitMod(FILE* out) {
	fprintf(out, "\tpop\tecx\n");
	fprintf(out, "\tpop\teax\n");
	fprintf(out, "\tcdq\n");
	fprintf(out, "\tidiv\tecx\n");
	fprintf(out, "\tmov\teax, edx\n");      /* remainder in EDX */
	fprintf(out, "\tpush\teax\n");
}

/* Emit UDIV (unsigned division) */
static void emitUDiv(FILE* out) {
	fprintf(out, "\tpop\tecx\n");
	fprintf(out, "\tpop\teax\n");
	fprintf(out, "\txor\tedx, edx\n");      /* EDX:EAX setup for unsigned */
	fprintf(out, "\tdiv\tecx\n");
	fprintf(out, "\tpush\teax\n");
}

/* Emit UMOD (unsigned modulo) */
static void emitUMod(FILE* out) {
	fprintf(out, "\tpop\tecx\n");
	fprintf(out, "\tpop\teax\n");
	fprintf(out, "\txor\tedx, edx\n");
	fprintf(out, "\tdiv\tecx\n");
	fprintf(out, "\tmov\teax, edx\n");
	fprintf(out, "\tpush\teax\n");
}

/* === Main Codegen === */

static void emitIR(FILE* out) {
	int i, fi, ki;
	
	/* Emit module header */
	emitPsectStart(out, "tc_prog");
	
	/* Emit each function */
	for (fi = 0; fi < funcCount; fi++) {
		Function* fn = &funcs[fi];
		
		if (fn->declOnly) {
			fprintf(out, "; FUNCDECL %s (external)\n", fn->name);
			continue;
		}
		
		emitFunctionPrologue(out, fn->name, fn->frameBytes);
		fprintf(out, "\n");
		
		/* Emit function body: process IR instructions */
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
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\tpop\tecx\n");
				fprintf(out, "\tadd\teax, ecx\n");
				fprintf(out, "\tpush\teax\n");
			}
			else if (strcmp(op, "SUB") == 0) {
				fprintf(out, "\t; SUB\n");
				fprintf(out, "\tpop\tecx\n");           /* right operand */
				fprintf(out, "\tpop\teax\n");           /* left operand */
				fprintf(out, "\tsub\teax, ecx\n");
				fprintf(out, "\tpush\teax\n");
			}
			else if (strcmp(op, "NEG") == 0) {
				fprintf(out, "\t; NEG\n");
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\tneg\teax\n");
				fprintf(out, "\tpush\teax\n");
			}
			else if (strcmp(op, "NOT") == 0) {
				fprintf(out, "\t; NOT\n");
				emitNot(out);
			}
			else if (strcmp(op, "NOTBIT") == 0) {
				fprintf(out, "\t; NOTBIT\n");
				emitNotBit(out);
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
			else if (strcmp(op, "CMPEQ") == 0 || strcmp(op, "CMPNE") == 0 ||
					 strcmp(op, "CMPLT") == 0 || strcmp(op, "CMPLE") == 0 ||
					 strcmp(op, "CMPGT") == 0 || strcmp(op, "CMPGE") == 0 ||
					 strcmp(op, "CMPULT") == 0 || strcmp(op, "CMPULE") == 0 ||
					 strcmp(op, "CMPUGT") == 0 || strcmp(op, "CMPUGE") == 0) {
				fprintf(out, "\t; %s\n", op);
				emitCompare(out, op);
			}
			else if (strcmp(op, "JMP") == 0) {
				fprintf(out, "\t; JMP %s\n", insP->args[0]);
				fprintf(out, "\tjmp\t%s\n", insP->args[0]);
			}
			else if (strcmp(op, "JZ") == 0) {
				fprintf(out, "\t; JZ %s\n", insP->args[0]);
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\ttest\teax, eax\n");
				fprintf(out, "\tjz\t%s\n", insP->args[0]);
			}
			else if (strcmp(op, "JNZ") == 0) {
				fprintf(out, "\t; JNZ %s\n", insP->args[0]);
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\ttest\teax, eax\n");
				fprintf(out, "\tjnz\t%s\n", insP->args[0]);
			}
			else if (strcmp(op, "BAND") == 0) {
				fprintf(out, "\t; BAND\n");
				fprintf(out, "\tpop\tecx\n");
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\tand\teax, ecx\n");
				fprintf(out, "\tpush\teax\n");
			}
			else if (strcmp(op, "BOR") == 0) {
				fprintf(out, "\t; BOR\n");
				fprintf(out, "\tpop\tecx\n");
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\tor\teax, ecx\n");
				fprintf(out, "\tpush\teax\n");
			}
			else if (strcmp(op, "BXOR") == 0) {
				fprintf(out, "\t; BXOR\n");
				fprintf(out, "\tpop\tecx\n");
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\txor\teax, ecx\n");
				fprintf(out, "\tpush\teax\n");
			}
			else if (strcmp(op, "SHL") == 0) {
				fprintf(out, "\t; SHL\n");
				fprintf(out, "\tpop\tecx\n");           /* shift amount */
				fprintf(out, "\tpop\teax\n");           /* value */
				fprintf(out, "\tshl\teax, cl\n");
				fprintf(out, "\tpush\teax\n");
			}
			else if (strcmp(op, "SHR") == 0) {
				fprintf(out, "\t; SHR (arithmetic)\n");
				fprintf(out, "\tpop\tecx\n");
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\tsar\teax, cl\n");
				fprintf(out, "\tpush\teax\n");
			}
			else if (strcmp(op, "USHR") == 0) {
				fprintf(out, "\t; USHR (logical)\n");
				fprintf(out, "\tpop\tecx\n");
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\tshr\teax, cl\n");
				fprintf(out, "\tpush\teax\n");
			}
			else if (strcmp(op, "NARROWC") == 0) {
				fprintf(out, "\t; NARROWC\n");
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\tand\teax, 0xFF\n");
				fprintf(out, "\tpush\teax\n");
			}
			else if (strcmp(op, "DROP") == 0) {
				fprintf(out, "\t; DROP\n");
				fprintf(out, "\tadd\tesp, 4\n");
			}
			else if (strcmp(op, "PRINT") == 0) {
				fprintf(out, "\t; PRINT (debug)\n");
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\t; TODO: implement debug output\n");
			}
			else if (strcmp(op, "PRINTU") == 0) {
				fprintf(out, "\t; PRINTU (debug unsigned)\n");
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\t; TODO: implement debug output\n");
			}
			else if (strcmp(op, "SWAP") == 0) {
				fprintf(out, "\t; SWAP\n");
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\tpop\tecx\n");
				fprintf(out, "\tpush\teax\n");
				fprintf(out, "\tpush\tecx\n");
			}
			else if (strcmp(op, "DUP") == 0) {
				fprintf(out, "\t; DUP\n");
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\tpush\teax\n");
				fprintf(out, "\tpush\teax\n");
			}
			else if (strcmp(op, "LABEL") == 0) {
				fprintf(out, "%s:\n", insP->args[0]);
			}
			else if (strcmp(op, "CALL") == 0) {
				const char* fname = insP->args[0];
				int nargs = number(insP->args[1], insP->line);
				fprintf(out, "\t; CALL %s (%d args)\n", fname, nargs);
				fprintf(out, "\tcall\ttc_%s\n", fname);
				fprintf(out, "\tpush\teax\n");
				if (nargs > 0) {
					fprintf(out, "\tadd\tesp, %d\n", nargs * 4);
				}
			}
			else if (strcmp(op, "CALLP") == 0) {
				int nargs = number(insP->args[0], insP->line);
				fprintf(out, "\t; CALLP (%d args via function pointer)\n", nargs);
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\tcall\teax\n");
				fprintf(out, "\tpush\teax\n");
				if (nargs > 0) {
					fprintf(out, "\tadd\tesp, %d\n", nargs * 4);
				}
			}
			else if (strcmp(op, "PUSHFN") == 0) {
				const char* fname = insP->args[0];
				fprintf(out, "\t; PUSHFN %s\n", fname);
				fprintf(out, "\tlea\teax, [tc_%s]\n", fname);
				fprintf(out, "\tpush\teax\n");
			}
			else if (strcmp(op, "RET") == 0 || strcmp(op, "RETP") == 0) {
				fprintf(out, "\t; %s\n", op);
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\tmov\tesp, ebp\n");
				fprintf(out, "\tpop\tebp\n");
				fprintf(out, "\tret\n");
			}
			else if (strcmp(op, "CALLIND") == 0) {
				int nargs = number(insP->args[0], insP->line);
				fprintf(out, "\t; CALLIND (%d args)\n", nargs);
				fprintf(out, "\tpop\teax\n");
				fprintf(out, "\tcall\teax\n");
				fprintf(out, "\tpush\teax\n");
				if (nargs > 0) {
					fprintf(out, "\tadd\tesp, %d\n", nargs * 4);
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
				/* PUSHADDR can be either PUSHADDR L <slot> or PUSHADDR G <name> */
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
				emitIPAdd(out, insP->args[0]);
			}
			else if (strcmp(op, "PRINTC") == 0) {
				fprintf(out, "\t; PRINTC\n");
				emitPrintC(out);
			}
			else if (strcmp(op, "LARRAY") == 0) {
				/* LARRAY <slot> <type> <length>
				   Local array - allocate space on stack
				   Just a declarative statement for now (space already allocated in prologue)
				*/
				fprintf(out, "\t; LARRAY %s %s %s (space pre-allocated)\n", 
					insP->args[0], insP->args[1], insP->args[2]);
			}
			else if (strcmp(op, "GARRAY") == 0) {
				/* GARRAY can appear as IR directive at function level
				   Format: GARRAY <name> <type> <length> [<static>]
				   This is similar to top-level GARRAY but inside function
				   Probably means: declare a static local array
				*/
				fprintf(out, "\t; GARRAY %s %s %s (static local array)\n",
					insP->args[0], insP->args[1], insP->args[2]);
			}
			else if (strcmp(op, "GINIT") == 0) {
				/* GINIT <name> <index> <value>
				   Initialize array element or global
				   Can appear at function level or module level
				   For now: just comment it (linker/loader will handle)
				*/
				fprintf(out, "\t; GINIT %s[%s] = %s (handled by linker)\n",
					insP->args[0], insP->args[1], insP->args[2]);
			}
			else if (strcmp(op, "CALLEXT") == 0 || strcmp(op, "CALLEXTP") == 0) {
				/* CALLEXT <name> <nargs>
				   CALLEXTP <nargs>
				   External C library function call
				   For now: emit as regular CALL with external symbol
				*/
				if (strcmp(op, "CALLEXT") == 0) {
					const char* fname = insP->args[0];
					int nargs = number(insP->args[1], insP->line);
					fprintf(out, "\t; CALLEXT %s (%d args)\n", fname, nargs);
					fprintf(out, "\tcall\t%s\n", fname);  /* No name mangling for external */
					fprintf(out, "\tpush\teax\n");
					if (nargs > 0) {
						fprintf(out, "\tadd\tesp, %d\n", nargs * 4);
					}
				} else {
					/* CALLEXTP: indirect external call */
					int nargs = number(insP->args[0], insP->line);
					fprintf(out, "\t; CALLEXTP (%d args)\n", nargs);
					fprintf(out, "\tpop\teax\n");
					fprintf(out, "\tcall\teax\n");
					fprintf(out, "\tpush\teax\n");
					if (nargs > 0) {
						fprintf(out, "\tadd\tesp, %d\n", nargs * 4);
					}
				}
			}
			else if (strcmp(op, "PDIFF") == 0) {
				fprintf(out, "\t; PDIFF\n");
				emitPDiff(out);
			}
			else if (strcmp(op, "PCMPEQ") == 0 || strcmp(op, "PCMPNE") == 0 ||
					 strcmp(op, "PCMPLT") == 0 || strcmp(op, "PCMPLE") == 0 ||
					 strcmp(op, "PCMPGT") == 0 || strcmp(op, "PCMPGE") == 0) {
				fprintf(out, "\t; %s\n", op);
				emitPCompare(out, op);
			}
			else if (strcmp(op, "ENDFUNC") == 0) {
				break;  /* end of function */
			}
			else {
				fprintf(out, "\t; TODO: %s\n", op);
			}
		}
		
		fprintf(out, "\n");
		emitFunctionEpilogue(out);
		fprintf(out, "\n\n");
	}
	
	/* Emit global variables section */
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
				fprintf(out, "\tds.l\t%d  ; array of %d elements, %d bytes each\n", 
					g->length, g->length, g->elemSize);
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
		fprintf(stderr, "usage: %s <input.ir> [output.sx86]\n", argv[0]);
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
	
	/* Extract functions and globals */
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
	
	/* Emit skeleton */
	emitHeader(out, "tc_prog");
	
	/* Emit IR code */
	emitIR(out);
	
	/* Emit footer */
	emitFooter(out);
	
	if (outfile) fclose(out);
	
	fprintf(stderr, "OK: %d IR lines, %d funcs, %d globals\n", irCount, funcCount, globalCount);
	return 0;
}
