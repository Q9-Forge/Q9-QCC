/*
 * qox86 -- Q9 x86-32 assembler with OS-9000 .r output
 *
 * Usage: qox86 [options] <input.a> <output.r>
 *
 * Purpose: Translate x86-32 assembly (from q9-qirx86 backend) to OS-9000
 * relocatable module format (.r).
 *
 * Design: Table-driven, extensible instruction encoder
 * - Phase 1: Basic parser, symbol table, directives (TODAY)
 * - Phase 2: Instruction table, operand parser, code emission (NEXT)
 * - Phase 3+: Full x86-32 ISA extension, macros, forward references (LATER)
 *
 * Edition history:
 *   2026-09-16  Phase 1: Foundation, parser, symbol table
 *
 * ---------------------------------------------------------------------------
 * OS-9000 .r FORMAT REFERENCE
 *
 * Based on GNU BFD i386os9k.c and Microware documentation.
 * The .r (relocatable module) is a big-endian binary with:
 *   - 56-byte header containing type, size, entry point, etc.
 *   - Symbol table (globals, externals)
 *   - Code section
 *   - Initialized data section (vsect)
 *   - Relocation records
 *
 * This implementation will generate modules compatible with OS-9000/x86
 * by following the BFD layout exactly.
 *
 * ---------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ========================================================== Constants ==== */

#define MAX_LINE        1024
#define MAX_SYMBOLS     4096
#define MAX_CODE        65536
#define MAX_DATA        65536
#define MAX_EXTERNALS   512
#define MAX_RELOCS      2048

/* x86-32 registers */
#define REG_EAX  0
#define REG_ECX  1
#define REG_EDX  2
#define REG_EBX  3
#define REG_ESP  4
#define REG_EBP  5
#define REG_ESI  6
#define REG_EDI  7

/* x86-32 register names (8 regs) */
static const char* regNames[] = {
	"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"
};

/* Operand types for instruction table */
#define OP_NONE    0
#define OP_REG     1    /* register */
#define OP_IMM     2    /* immediate value */
#define OP_MEM     3    /* memory operand [addr] */
#define OP_LABEL   4    /* label/symbol reference */

/* Section identifiers */
#define SECT_NONE   0
#define SECT_CODE   1
#define SECT_VSECT  2

/* ========================================================== Symbol Table === */

typedef struct Symbol {
	char name[256];
	int address;       /* Address in section */
	int section;       /* SECT_CODE, SECT_VSECT, or SECT_NONE */
	int type;          /* Symbol type (global, external, local) */
	int isExternal;    /* Non-zero if external reference */
} Symbol;

static Symbol symbols[MAX_SYMBOLS];
static int numSymbols = 0;

/* Symbol hash table for O(1) lookup */
static int symbolHash[512];

static int hashSymbolName(const char* name) {
	int h = 0;
	while (*name) {
		h = (h * 31 + *name) % 512;
		name++;
	}
	return h;
}

static int findSymbol(const char* name) {
	int h = hashSymbolName(name);
	int idx = symbolHash[h];
	while (idx >= 0) {
		if (strcmp(symbols[idx].name, name) == 0)
			return idx;
		/* Simple chaining: store next in unused field or use separate chain array */
		/* For now, linear search from hash bucket */
		idx = -1;  /* TODO: implement proper chaining */
		for (int i = 0; i < numSymbols; i++) {
			if (strcmp(symbols[i].name, name) == 0)
				return i;
		}
		break;
	}
	return -1;
}

static void addSymbol(const char* name, int address, int section, int type, int isExternal) {
	if (numSymbols >= MAX_SYMBOLS) {
		fprintf(stderr, "Error: Too many symbols\n");
		exit(1);
	}
	int idx = findSymbol(name);
	if (idx >= 0) {
		symbols[idx].address = address;
		symbols[idx].section = section;
		return;  /* Update existing */
	}
	strncpy(symbols[numSymbols].name, name, 255);
	symbols[numSymbols].address = address;
	symbols[numSymbols].section = section;
	symbols[numSymbols].type = type;
	symbols[numSymbols].isExternal = isExternal;
	numSymbols++;
}

/* ======================================================== Code Sections === */

/* Code section */
static unsigned char code[MAX_CODE];
static int codeSize = 0;

/* Data section (vsect) */
static unsigned char vsect[MAX_DATA];
static int vsectSize = 0;

/* Current section being assembled */
static int currentSection = SECT_NONE;
static int currentAddress = 0;

/* PSECT parameters (for module header) */
static char psectName[256] = "";
static int psTyLan = 0;        /* Type/Language from psect */
static int psAttRev = 0;       /* Attributes/Revision */
static int psEdition = 0;      /* Edition */
static int psStack = 0;        /* Stack size */
static int psEntry = 0;        /* Entry point */
static int psTrap = -1;        /* Trap entry (default: -1 = none) */

/* Output buffer for .r module */
static unsigned char outBuf[MAX_CODE * 4];
static int outLen = 0;

/* ========================================== .r Module File Output ===== */

/* Output functions for building .r module (big-endian) */
static void outByteMod(unsigned char b) {
	if (outLen >= (int)sizeof(outBuf)) {
		fprintf(stderr, "Error: Output buffer overflow\n");
		exit(1);
	}
	outBuf[outLen++] = b;
}

static void outWordMod(unsigned short w) {
	outByteMod((w >> 8) & 0xFF);
	outByteMod(w & 0xFF);
}

static void outDwordMod(unsigned int d) {
	outByteMod((d >> 24) & 0xFF);
	outByteMod((d >> 16) & 0xFF);
	outByteMod((d >> 8) & 0xFF);
	outByteMod(d & 0xFF);
}

static void outStrZ(const char* s) {
	if (!s) s = "";
	while (*s) outByteMod(*s++);
	outByteMod(0);  /* null terminator */
}

/* Generate OS-9000 .r module */
static void generateModule(const char* outFile) {
	FILE* out = fopen(outFile, "wb");
	if (!out) {
		fprintf(stderr, "Error: Cannot open output file: %s\n", outFile);
		return;
	}
	
	/* Build the .r module in outBuf */
	outLen = 0;
	
	/* Header (56 bytes, big-endian) */
	outDwordMod(0xDEADFACE);      /* sync */
	outWordMod(psTyLan);          /* type/language */
	outWordMod(psAttRev);         /* attributes/revision */
	outWordMod(0);                /* valid */
	outWordMod(999);              /* series (Q9 assembler ID) */
	/* timestamp (6 bytes): year-1900, month, day, hour, min, sec */
	outByteMod(126);              /* 2026 - 1900 */
	outByteMod(9);                /* September */
	outByteMod(16);               /* day */
	outByteMod(17);               /* hour */
	outByteMod(57);               /* minute */
	outByteMod(31);               /* second */
	outWordMod(psEdition);        /* edition */
	outDwordMod(0);               /* static storage (vsect uninitialized) */
	outDwordMod(vsectSize);       /* initialized data size */
	outDwordMod(codeSize);        /* code size */
	outDwordMod(psStack);         /* stack size */
	outDwordMod(psEntry);         /* entry point */
	outDwordMod(psTrap);          /* trap entry (-1 = none) */
	outDwordMod(0);               /* remote static storage */
	outDwordMod(0);               /* remote initialized data */
	outDwordMod(0);               /* debug size */
	
	/* PSECT name (NUL-terminated) */
	outStrZ(psectName);
	
	/* Global symbols count and list */
	int nGlob = 0;
	for (int i = 0; i < numSymbols; i++) {
		if (!symbols[i].isExternal && symbols[i].section != SECT_NONE)
			nGlob++;
	}
	outDwordMod(nGlob);
	for (int i = 0; i < numSymbols; i++) {
		if (!symbols[i].isExternal && symbols[i].section != SECT_NONE) {
			outStrZ(symbols[i].name);
			/* Type: 0x0004 = code, 0x0001 = initialized data, 0x0000 = reserved */
			unsigned short type = (symbols[i].section == SECT_CODE) ? 0x0004 : 0x0001;
			outWordMod(type);
			outDwordMod(symbols[i].address);  /* address */
		}
	}
	
	/* Write header + metadata to file */
	fwrite(outBuf, 1, outLen, out);
	
	/* Write code section */
	fwrite(code, 1, codeSize, out);
	
	/* Initialized data section (vsect) */
	fwrite(vsect, 1, vsectSize, out);
	
	/* External symbols count (TODO: implement external tracking) */
	unsigned char extBuf[16];
	int extLen = 0;
	/* Write as big-endian */
	extBuf[extLen++] = 0; extBuf[extLen++] = 0;
	extBuf[extLen++] = 0; extBuf[extLen++] = 0;  /* externals count = 0 */
	/* Local symbols count */
	extBuf[extLen++] = 0; extBuf[extLen++] = 0;
	extBuf[extLen++] = 0; extBuf[extLen++] = 0;  /* locals count = 0 */
	/* Common blocks count */
	extBuf[extLen++] = 0; extBuf[extLen++] = 0;
	extBuf[extLen++] = 0; extBuf[extLen++] = 0;  /* commons count = 0 */
	fwrite(extBuf, 1, extLen, out);
	
	fclose(out);
	
	int totalSize = 56 + (int)strlen(psectName) + 1 + 4  /* psect header + globals count */
		+ codeSize + vsectSize + 12;  /* code, vsect, final counts */
	printf("; Generated .r module: %s (%d bytes)\n", outFile, totalSize);
}

/* ============================================= Instruction Encoder ===== */

/* x86-32 instruction encoding helpers */

typedef struct {
	unsigned char reg_rm;
	unsigned char sib;
	int immediateSize;
	int immediateValue;
} EncodedOperand;

/* Parse a single operand: register, immediate, or memory address */
static int parseOperand(char* op, EncodedOperand* result) {
	result->immediateSize = 0;
	result->immediateValue = 0;
	
	/* Register? */
	for (int i = 0; i < 8; i++) {
		if (strcmp(op, regNames[i]) == 0) {
			result->reg_rm = i;
			return OP_REG;
		}
	}
	
	/* Immediate? (decimal or hex with $) */
	if (op[0] == '$') {
		sscanf(op + 1, "%x", &result->immediateValue);
		result->immediateSize = 4;
		return OP_IMM;
	}
	if (isdigit(op[0]) || op[0] == '-') {
		sscanf(op, "%d", &result->immediateValue);
		result->immediateSize = 4;
		return OP_IMM;
	}
	
	/* Memory operand? [addr], [reg], [reg + offset], etc */
	if (op[0] == '[') {
		result->reg_rm = REG_EAX;
		return OP_MEM;
	}
	
	/* Label/symbol reference */
	if (isalpha(op[0]) || op[0] == '_') {
		result->reg_rm = 0;
		result->immediateSize = 4;
		return OP_LABEL;
	}
	
	return OP_NONE;
}

/* Emit a byte to the current section */
static void emitByte(unsigned char b) {
	if (currentSection == SECT_CODE) {
		if (codeSize >= MAX_CODE) {
			fprintf(stderr, "Error: Code section overflow\n");
			exit(1);
		}
		code[codeSize++] = b;
		currentAddress++;
	} else if (currentSection == SECT_VSECT) {
		if (vsectSize >= MAX_DATA) {
			fprintf(stderr, "Error: Data section overflow\n");
			exit(1);
		}
		vsect[vsectSize++] = b;
		currentAddress++;
	}
}

/* Emit word (2 bytes, little-endian for x86) */
static void emitWord(unsigned short w) {
	emitByte(w & 0xFF);
	emitByte((w >> 8) & 0xFF);
}

/* Emit dword (4 bytes, little-endian) */
static void emitDword(unsigned int d) {
	emitByte(d & 0xFF);
	emitByte((d >> 8) & 0xFF);
	emitByte((d >> 16) & 0xFF);
	emitByte((d >> 24) & 0xFF);
}

/* x86-32 MOD/RM encoding */
#define MOD_INDIRECT    0x00
#define MOD_BYTE_OFFSET 0x40
#define MOD_WORD_OFFSET 0x80
#define MOD_REGISTER    0xC0

static unsigned char makeModRM(int mod, int reg, int rm) {
	return (mod & 0xC0) | ((reg & 0x7) << 3) | (rm & 0x7);
}

/* Forward declarations for instruction encoders */
static void encodeMovInstruction(char* dst, char* src);
static void encodePushInstruction(char* op);
static void encodePopInstruction(char* op);
static void encodeRetInstruction(void);
static void encodeAddInstruction(char* dst, char* src);
static void encodeSubInstruction(char* dst, char* src);
static void encodeNegInstruction(char* op);
static void encodeCmpInstruction(char* dst, char* src);
static void encodeTestInstruction(char* dst, char* src);
static void encodeBitwiseInstruction(char* opcode, char* dst, char* src);
static void encodeNotInstruction(char* op);
static void encodeShiftInstruction(char* opcode, char* dst, char* src);
static void encodeJumpInstruction(char* op, char* target);
static void encodeCallInstruction(char* target);
static void encodeCdqInstruction(void);
static void encodeMulInstruction(char* dst, char* src);
static void encodeDivInstruction(char* op);
static void encodeLeaInstruction(char* dst, char* src);
static void encodeMovzxInstruction(char* dst, char* src);

/* =========================================================== Line Input === */

static char lineBuf[MAX_LINE];
static int lineNum = 0;

static char* readLine(FILE* f) {
	if (fgets(lineBuf, MAX_LINE, f) == NULL)
		return NULL;
	lineNum++;
	/* Remove trailing newline */
	char* p = lineBuf + strlen(lineBuf) - 1;
	if (*p == '\n') *p = 0;
	return lineBuf;
}

/* ========================================================== Tokenizer ==== */

static char token[MAX_LINE];
static char* tokenPtr;

static void skipWhitespace(char** pp) {
	while (**pp && isspace(**pp))
		(*pp)++;
}

static int getToken(char** pp) {
	skipWhitespace(pp);
	if (!**pp)
		return 0;
	
	char* start = *pp;
	if (isalpha(**pp) || **pp == '_' || **pp == '.') {
		while (isalnum(**pp) || **pp == '_' || **pp == '.')
			(*pp)++;
	} else if (isdigit(**pp) || **pp == '-' || **pp == '+') {
		while (isdigit(**pp) || **pp == 'x' || **pp == 'X' || isalpha(**pp))
			(*pp)++;
	} else if (**pp == '$' || **pp == '@' || **pp == '%') {
		(*pp)++;
		while (isalnum(**pp))
			(*pp)++;
	} else {
		(*pp)++;  /* Single-char token: [, ], comma, etc */
	}
	
	int len = *pp - start;
	if (len >= MAX_LINE) len = MAX_LINE - 1;
	strncpy(token, start, len);
	token[len] = 0;
	return len;
}

/* ========================================================== Directives === */

static void handlePsect(char* line) {
	/* psect name, type, attr, rev, stack, entry
	   Format: psect name,type,attr,rev,stack,entry */
	char* p = line;
	getToken(&p);  /* consume 'psect' */
	
	int paramIdx = 0;
	while (getToken(&p)) {
		if (token[0] == ',') continue;
		
		if (paramIdx == 0) {
			/* psect name */
			strncpy(psectName, token, 255);
		} else if (paramIdx == 1) {
			/* type/language */
			sscanf(token, "%d", &psTyLan);
		} else if (paramIdx == 2) {
			/* attributes/revision */
			sscanf(token, "%d", &psAttRev);
		} else if (paramIdx == 3) {
			/* edition */
			sscanf(token, "%d", &psEdition);
		} else if (paramIdx == 4) {
			/* stack size */
			sscanf(token, "%d", &psStack);
		} else if (paramIdx == 5) {
			/* entry point */
			sscanf(token, "%d", &psEntry);
		}
		paramIdx++;
	}
	
	printf("; psect: %s (type=%d, attr=%d, stack=%d, entry=%d)\n", 
		psectName, psTyLan, psAttRev, psStack, psEntry);
	
	currentSection = SECT_CODE;
	currentAddress = 0;
	codeSize = 0;
}

static void handleVsect(char* line) {
	currentSection = SECT_VSECT;
	currentAddress = 0;
	vsectSize = 0;
}

static void handleEnds(char* line) {
	currentSection = SECT_NONE;
}

static void handleNam(char* line) {
	/* nam: set module name */
	char* p = line;
	getToken(&p);  /* consume 'nam' */
	if (getToken(&p)) {
		printf("; module name: %s\n", token);
	}
}

static void handleDcl(char* line) {
	/* dc.l: initialize word (4 bytes) */
	if (currentSection != SECT_VSECT) {
		fprintf(stderr, "Error at line %d: dc.l outside vsect\n", lineNum);
		return;
	}
	
	char* p = line;
	getToken(&p);  /* consume 'dc.l' */
	
	/* Parse comma-separated values */
	while (getToken(&p)) {
		if (token[0] == 0) break;
		if (token[0] == ',') continue;
		
		/* Parse value (decimal, hex, or symbol) */
		int value = 0;
		if (token[0] == '$') {
			sscanf(token + 1, "%x", &value);
		} else if (isdigit(token[0]) || token[0] == '-') {
			sscanf(token, "%d", &value);
		} else {
			/* Symbol reference - mark for relocation */
			value = 0;
		}
		
		/* Store 4 bytes big-endian */
		if (vsectSize + 4 <= MAX_DATA) {
			vsect[vsectSize++] = (value >> 24) & 0xFF;
			vsect[vsectSize++] = (value >> 16) & 0xFF;
			vsect[vsectSize++] = (value >> 8) & 0xFF;
			vsect[vsectSize++] = value & 0xFF;
		}
	}
}

static void handleDsl(char* line) {
	/* ds.l: reserve space (4 bytes) */
	if (currentSection != SECT_VSECT) {
		fprintf(stderr, "Error at line %d: ds.l outside vsect\n", lineNum);
		return;
	}
	
	char* p = line;
	getToken(&p);  /* consume 'ds.l' */
	
	if (getToken(&p)) {
		int count = 1;
		if (isdigit(token[0])) {
			sscanf(token, "%d", &count);
		}
		vsectSize += count * 4;
	}
}

static void handleEqu(char* line) {
	/* equ: define constant */
	char* p = line;
	getToken(&p);  /* name or label? */
	char name[256];
	strcpy(name, token);
	
	/* Skip to value */
	getToken(&p);  /* consume 'equ' */
	getToken(&p);  /* get value */
	
	int value = 0;
	if (token[0] == '$') {
		sscanf(token + 1, "%x", &value);
	} else {
		sscanf(token, "%d", &value);
	}
	
	addSymbol(name, value, SECT_NONE, 0, 0);
}

/* ======================================================== Line Handler === */

/* Dispatch instruction to appropriate encoder */
static void encodeInstruction(char* line) {
	/* Parse mnemonic and operands */
	char* p = line;
	skipWhitespace(&p);
	
	/* Get mnemonic */
	char mnemonic[32];
	char* m = mnemonic;
	while (*p && !isspace(*p) && m < mnemonic + 31) {
		*m++ = *p++;
	}
	*m = 0;
	
	skipWhitespace(&p);
	
	/* Get operands (comma-separated) */
	char operands[2][256] = {"", ""};
	int opCount = 0;
	
	if (*p) {
		char* op = operands[0];
		while (*p && opCount < 2) {
			if (*p == ',') {
				*op = 0;
				opCount++;
				if (opCount < 2) op = operands[1];
				p++;
				skipWhitespace(&p);
			} else {
				*op++ = *p++;
			}
		}
		*op = 0;
		if (opCount == 0 && operands[0][0]) opCount = 1;
		else opCount++;
	}
	
	/* Remove trailing whitespace from operands */
	for (int i = 0; i < 2; i++) {
		/* Trim leading whitespace */
		char* start = operands[i];
		while (*start && isspace(*start)) start++;
		/* Trim trailing whitespace */
		char* end = operands[i] + strlen(operands[i]) - 1;
		while (end >= start && isspace(*end)) *end-- = 0;
		/* Copy trimmed version back */
		if (start > operands[i]) {
			char* dst = operands[i];
			while (*start) *dst++ = *start++;
			*dst = 0;
		}
	}
	
	/* Dispatch to encoder */
	if (strcmp(mnemonic, "push") == 0 && opCount >= 1) {
		encodePushInstruction(operands[0]);
	} else if (strcmp(mnemonic, "pop") == 0 && opCount >= 1) {
		encodePopInstruction(operands[0]);
	} else if (strcmp(mnemonic, "mov") == 0 && opCount >= 2) {
		encodeMovInstruction(operands[0], operands[1]);
	} else if (strcmp(mnemonic, "ret") == 0) {
		encodeRetInstruction();
	} else if (strcmp(mnemonic, "add") == 0 && opCount >= 2) {
		encodeAddInstruction(operands[0], operands[1]);
	} else if (strcmp(mnemonic, "sub") == 0 && opCount >= 2) {
		encodeSubInstruction(operands[0], operands[1]);
	} else if (strcmp(mnemonic, "neg") == 0 && opCount >= 1) {
		encodeNegInstruction(operands[0]);
	} else if (strcmp(mnemonic, "cmp") == 0 && opCount >= 2) {
		encodeCmpInstruction(operands[0], operands[1]);
	} else if (strcmp(mnemonic, "test") == 0 && opCount >= 2) {
		encodeTestInstruction(operands[0], operands[1]);
	} else if (strcmp(mnemonic, "and") == 0 && opCount >= 2) {
		encodeBitwiseInstruction("AND", operands[0], operands[1]);
	} else if (strcmp(mnemonic, "or") == 0 && opCount >= 2) {
		encodeBitwiseInstruction("OR", operands[0], operands[1]);
	} else if (strcmp(mnemonic, "xor") == 0 && opCount >= 2) {
		encodeBitwiseInstruction("XOR", operands[0], operands[1]);
	} else if (strcmp(mnemonic, "not") == 0 && opCount >= 1) {
		encodeNotInstruction(operands[0]);
	} else if (strcmp(mnemonic, "shl") == 0 && opCount >= 2) {
		encodeShiftInstruction("SHL", operands[0], operands[1]);
	} else if (strcmp(mnemonic, "shr") == 0 && opCount >= 2) {
		encodeShiftInstruction("SHR", operands[0], operands[1]);
	} else if (strcmp(mnemonic, "sar") == 0 && opCount >= 2) {
		encodeShiftInstruction("SAR", operands[0], operands[1]);
	} else if (strcmp(mnemonic, "jmp") == 0 && opCount >= 1) {
		encodeJumpInstruction(mnemonic, operands[0]);
	} else if (strcmp(mnemonic, "jz") == 0 && opCount >= 1) {
		encodeJumpInstruction(mnemonic, operands[0]);
	} else if (strcmp(mnemonic, "jne") == 0 && opCount >= 1) {
		encodeJumpInstruction(mnemonic, operands[0]);
	} else if (strcmp(mnemonic, "jnz") == 0 && opCount >= 1) {
		encodeJumpInstruction(mnemonic, operands[0]);
	} else if (strcmp(mnemonic, "call") == 0 && opCount >= 1) {
		encodeCallInstruction(operands[0]);
	} else if (strcmp(mnemonic, "cdq") == 0) {
		encodeCdqInstruction();
	} else if (strcmp(mnemonic, "mul") == 0 && opCount >= 2) {
		encodeMulInstruction(operands[0], operands[1]);
	} else if (strcmp(mnemonic, "imul") == 0 && opCount >= 2) {
		encodeMulInstruction(operands[0], operands[1]);
	} else if (strcmp(mnemonic, "div") == 0 && opCount >= 1) {
		encodeDivInstruction(operands[0]);
	} else if (strcmp(mnemonic, "idiv") == 0 && opCount >= 1) {
		encodeDivInstruction(operands[0]);
	} else if (strcmp(mnemonic, "lea") == 0 && opCount >= 2) {
		encodeLeaInstruction(operands[0], operands[1]);
	} else if (strcmp(mnemonic, "movzx") == 0 && opCount >= 2) {
		encodeMovzxInstruction(operands[0], operands[1]);
	} else {
		/* Other instructions: stub for now */
		printf("; instr: %s %s %s (TODO)\n", mnemonic, operands[0], opCount > 1 ? operands[1] : "");
	}
}

static void processLine(char* line) {
	if (!line || line[0] == 0)
		return;
	
	/* Skip comments */
	char* comment = strchr(line, ';');
	if (comment)
		*comment = 0;
	
	/* Skip whitespace */
	char* p = line;
	skipWhitespace(&p);
	if (!*p)
		return;
	
	/* Check for directives */
	if (strncmp(p, "psect", 5) == 0) {
		handlePsect(p);
	} else if (strncmp(p, "vsect", 5) == 0) {
		handleVsect(p);
	} else if (strncmp(p, "ends", 4) == 0 || strncmp(p, "endp", 4) == 0) {
		handleEnds(p);
	} else if (strncmp(p, "nam", 3) == 0) {
		handleNam(p);
	} else if (strncmp(p, "dc.l", 4) == 0) {
		handleDcl(p);
	} else if (strncmp(p, "ds.l", 4) == 0) {
		handleDsl(p);
	} else if (strncmp(p, "equ", 3) == 0) {
		handleEqu(p);
	} else if (strncmp(p, "use ", 4) == 0) {
		/* use: include file - skip for now */
	} else if (strncmp(p, "endc", 4) == 0) {
		/* endc: end conditional - skip */
	} else {
		/* Instruction or label */
		if (p[strlen(p)-1] == ':') {
			/* Label */
			p[strlen(p)-1] = 0;
			addSymbol(p, currentAddress, currentSection, 0, 0);
		} else if (currentSection == SECT_CODE) {
			/* Encode instruction if in code section */
			encodeInstruction(p);
		} else {
			printf("; skip (not in code section): %s\n", p);
		}
	}
}

/* ========================================================== Main Driver === */

/* Instruction Encoder Implementations */

/* Encode MOV instruction: MOV dst, src */
static void encodeMovInstruction(char* dst, char* src) {
	EncodedOperand destOp, srcOp;
	int destType = parseOperand(dst, &destOp);
	int srcType = parseOperand(src, &srcOp);
	
	/* MOV r32, r32: 89 C0 + reg encoding */
	if (destType == OP_REG && srcType == OP_REG) {
		emitByte(0x89);
		emitByte(makeModRM(MOD_REGISTER, srcOp.reg_rm, destOp.reg_rm));
		return;
	}
	
	/* MOV r32, imm32: B8 + reg + imm32 */
	if (destType == OP_REG && srcType == OP_IMM) {
		emitByte(0xB8 | destOp.reg_rm);
		emitDword(srcOp.immediateValue);
		return;
	}
	
	printf("; MOV %s, %s (STUB)\n", dst, src);
}

/* Encode PUSH instruction */
static void encodePushInstruction(char* op) {
	EncodedOperand operand;
	int opType = parseOperand(op, &operand);
	
	if (opType == OP_REG) {
		/* PUSH r32: 50 + reg */
		emitByte(0x50 | operand.reg_rm);
		return;
	}
	
	if (opType == OP_IMM) {
		if (operand.immediateValue >= -128 && operand.immediateValue <= 127) {
			/* PUSH imm8: 6A imm8 */
			emitByte(0x6A);
			emitByte(operand.immediateValue & 0xFF);
		} else {
			/* PUSH imm32: 68 imm32 */
			emitByte(0x68);
			emitDword(operand.immediateValue);
		}
		return;
	}
	
	printf("; PUSH %s (STUB)\n", op);
}

/* Encode POP instruction */
static void encodePopInstruction(char* op) {
	EncodedOperand operand;
	int opType = parseOperand(op, &operand);
	
	if (opType == OP_REG) {
		/* POP r32: 58 + reg */
		emitByte(0x58 | operand.reg_rm);
		return;
	}
	
	printf("; POP %s (STUB)\n", op);
}

/* Encode RET instruction */
static void encodeRetInstruction(void) {
	emitByte(0xC3);
}

/* ============================================ Arithmetic Instructions ==== */

/* Encode ADD r/m32, r32 / ADD r32, r/m32 / ADD r32, imm32 */
static void encodeAddInstruction(char* dst, char* src) {
	EncodedOperand destOp, srcOp;
	int destType = parseOperand(dst, &destOp);
	int srcType = parseOperand(src, &srcOp);
	
	if (destType == OP_REG && srcType == OP_REG) {
		/* ADD r32, r32: 01 /r */
		emitByte(0x01);
		emitByte(makeModRM(MOD_REGISTER, srcOp.reg_rm, destOp.reg_rm));
		return;
	}
	if (destType == OP_REG && srcType == OP_IMM) {
		/* ADD r32, imm32: 81 /0 */
		emitByte(0x81);
		emitByte(makeModRM(MOD_REGISTER, 0, destOp.reg_rm));
		emitDword(srcOp.immediateValue);
		return;
	}
	printf("; ADD %s, %s (STUB)\n", dst, src);
}

/* Encode SUB r32, r32 / SUB r32, imm32 */
static void encodeSubInstruction(char* dst, char* src) {
	EncodedOperand destOp, srcOp;
	int destType = parseOperand(dst, &destOp);
	int srcType = parseOperand(src, &srcOp);
	
	if (destType == OP_REG && srcType == OP_REG) {
		/* SUB r32, r32: 29 /r */
		emitByte(0x29);
		emitByte(makeModRM(MOD_REGISTER, srcOp.reg_rm, destOp.reg_rm));
		return;
	}
	if (destType == OP_REG && srcType == OP_IMM) {
		/* SUB r32, imm32: 81 /5 */
		emitByte(0x81);
		emitByte(makeModRM(MOD_REGISTER, 5, destOp.reg_rm));
		emitDword(srcOp.immediateValue);
		return;
	}
	printf("; SUB %s, %s (STUB)\n", dst, src);
}

/* Encode NEG r32 */
static void encodeNegInstruction(char* op) {
	EncodedOperand operand;
	int opType = parseOperand(op, &operand);
	if (opType == OP_REG) {
		/* NEG r32: F7 /3 */
		emitByte(0xF7);
		emitByte(makeModRM(MOD_REGISTER, 3, operand.reg_rm));
		return;
	}
	printf("; NEG %s (STUB)\n", op);
}

/* Encode CMP r32, r32 / CMP r32, imm32 */
static void encodeCmpInstruction(char* dst, char* src) {
	EncodedOperand destOp, srcOp;
	int destType = parseOperand(dst, &destOp);
	int srcType = parseOperand(src, &srcOp);
	
	if (destType == OP_REG && srcType == OP_REG) {
		/* CMP r32, r32: 39 /r */
		emitByte(0x39);
		emitByte(makeModRM(MOD_REGISTER, srcOp.reg_rm, destOp.reg_rm));
		return;
	}
	if (destType == OP_REG && srcType == OP_IMM) {
		/* CMP r32, imm32: 81 /7 */
		emitByte(0x81);
		emitByte(makeModRM(MOD_REGISTER, 7, destOp.reg_rm));
		emitDword(srcOp.immediateValue);
		return;
	}
	printf("; CMP %s, %s (STUB)\n", dst, src);
}

/* Encode TEST r32, r32 */
static void encodeTestInstruction(char* dst, char* src) {
	EncodedOperand destOp, srcOp;
	int destType = parseOperand(dst, &destOp);
	int srcType = parseOperand(src, &srcOp);
	
	if (destType == OP_REG && srcType == OP_REG) {
		/* TEST r32, r32: 85 /r */
		emitByte(0x85);
		emitByte(makeModRM(MOD_REGISTER, srcOp.reg_rm, destOp.reg_rm));
		return;
	}
	printf("; TEST %s, %s (STUB)\n", dst, src);
}

/* ============================================ Bitwise Instructions ===== */

/* Encode AND/OR/XOR r32, r32 / r32, imm32 */
static void encodeBitwiseInstruction(char* opcode, char* dst, char* src) {
	EncodedOperand destOp, srcOp;
	int destType = parseOperand(dst, &destOp);
	int srcType = parseOperand(src, &srcOp);
	
	unsigned char op1_reg = 0x21;  /* AND */
	unsigned char op2_reg = 7;     /* op extension for imm */
	
	if (*opcode == 'O') {
		op1_reg = 0x09;  /* OR */
		op2_reg = 1;
	} else if (*opcode == 'X') {
		op1_reg = 0x31;  /* XOR */
		op2_reg = 6;
	}
	
	if (destType == OP_REG && srcType == OP_REG) {
		emitByte(op1_reg);
		emitByte(makeModRM(MOD_REGISTER, srcOp.reg_rm, destOp.reg_rm));
		return;
	}
	if (destType == OP_REG && srcType == OP_IMM) {
		emitByte(0x81);
		emitByte(makeModRM(MOD_REGISTER, op2_reg, destOp.reg_rm));
		emitDword(srcOp.immediateValue);
		return;
	}
	printf("; %s %s, %s (STUB)\n", opcode, dst, src);
}

/* Encode NOT r32 */
static void encodeNotInstruction(char* op) {
	EncodedOperand operand;
	int opType = parseOperand(op, &operand);
	if (opType == OP_REG) {
		/* NOT r32: F7 /2 */
		emitByte(0xF7);
		emitByte(makeModRM(MOD_REGISTER, 2, operand.reg_rm));
		return;
	}
	printf("; NOT %s (STUB)\n", op);
}

/* Encode SHL/SHR/SAR r32, cl / r32, imm8 */
static void encodeShiftInstruction(char* opcode, char* dst, char* src) {
	EncodedOperand destOp, srcOp;
	int destType = parseOperand(dst, &destOp);
	int srcType = parseOperand(src, &srcOp);
	
	unsigned char op_ext = 4;  /* SHL */
	if (opcode[2] == 'R') op_ext = 5;  /* SHR */
	if (opcode[1] == 'A') op_ext = 7;  /* SAR */
	
	if (destType == OP_REG && srcType == OP_REG) {
		if (srcOp.reg_rm == REG_ECX) {
			/* SHL r32, cl: D3 /4 */
			emitByte(0xD3);
			emitByte(makeModRM(MOD_REGISTER, op_ext, destOp.reg_rm));
			return;
		}
	}
	if (destType == OP_REG && srcType == OP_IMM) {
		/* SHL r32, imm8: C1 /4 */
		emitByte(0xC1);
		emitByte(makeModRM(MOD_REGISTER, op_ext, destOp.reg_rm));
		emitByte(srcOp.immediateValue & 0xFF);
		return;
	}
	printf("; %s %s, %s (STUB)\n", opcode, dst, src);
}

/* ============================================ Control Instructions ===== */

/* Encode JMP label / JZ label / JNZ label */
static void encodeJumpInstruction(char* op, char* target) {
	/* For now, generate relative short jumps (placeholder) */
	/* Proper linking will fill in correct offsets */
	if (*op == 'J' && op[1] == 'Z') {
		/* JZ: 74 rel8 */
		emitByte(0x74);
		emitByte(0);  /* Will be filled by linker */
	} else if (*op == 'J' && op[1] == 'N') {
		/* JNZ: 75 rel8 */
		emitByte(0x75);
		emitByte(0);
	} else {
		/* JMP: E9 rel32 or EB rel8 */
		emitByte(0xE9);
		emitDword(0);  /* Will be filled by linker */
	}
}

/* Encode CALL label */
static void encodeCallInstruction(char* target) {
	/* CALL rel32: E8 */
	emitByte(0xE8);
	emitDword(0);  /* Will be filled by linker */
}

/* Encode CDQ (sign-extend EAX to EDX:EAX) */
static void encodeCdqInstruction(void) {
	emitByte(0x99);  /* CDQ */
}

/* Encode MUL/DIV/IMUL/IDIV */
static void encodeMulInstruction(char* dst, char* src) {
	EncodedOperand destOp, srcOp;
	int destType = parseOperand(dst, &destOp);
	int srcType = parseOperand(src, &srcOp);
	
	if (destType == OP_REG && srcType == OP_REG) {
		/* IMUL r32, r32: 0F AF /r */
		emitByte(0x0F);
		emitByte(0xAF);
		emitByte(makeModRM(MOD_REGISTER, destOp.reg_rm, srcOp.reg_rm));
		return;
	}
	printf("; MUL %s, %s (STUB)\n", dst, src);
}

static void encodeDivInstruction(char* op) {
	EncodedOperand operand;
	int opType = parseOperand(op, &operand);
	
	if (opType == OP_REG) {
		if (operand.reg_rm == REG_EAX) {
			/* Special case: DIV EAX */
			emitByte(0xF7);
			emitByte(0xF0);  /* DIV EAX */
		} else {
			/* DIV r32: F7 /6 */
			emitByte(0xF7);
			emitByte(makeModRM(MOD_REGISTER, 6, operand.reg_rm));
		}
		return;
	}
	printf("; DIV %s (STUB)\n", op);
}

/* Encode LEA r32, [addr] */
static void encodeLeaInstruction(char* dst, char* src) {
	EncodedOperand destOp;
	int destType = parseOperand(dst, &destOp);
	
	if (destType == OP_REG) {
		/* LEA r32, [addr]: 8D /r (with ModRM byte) */
		emitByte(0x8D);
		emitByte(makeModRM(MOD_REGISTER, destOp.reg_rm, destOp.reg_rm));
		return;
	}
	printf("; LEA %s, %s (STUB)\n", dst, src);
}

/* Encode MOVZX r32, r/m8 / MOVZX r32, r/m16 */
static void encodeMovzxInstruction(char* dst, char* src) {
	EncodedOperand destOp, srcOp;
	int destType = parseOperand(dst, &destOp);
	int srcType = parseOperand(src, &srcOp);
	
	if (destType == OP_REG && srcType == OP_REG) {
		/* MOVZX r32, r8: 0F B6 /r */
		emitByte(0x0F);
		emitByte(0xB6);
		emitByte(makeModRM(MOD_REGISTER, destOp.reg_rm, srcOp.reg_rm));
		return;
	}
	printf("; MOVZX %s, %s (STUB)\n", dst, src);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <input.a> <output.r>\n", argv[0]);
		return 1;
	}
	
	FILE* inFile = fopen(argv[1], "r");
	if (!inFile) {
		fprintf(stderr, "Error: Cannot open input file: %s\n", argv[1]);
		return 1;
	}
	
	/* Initialize symbol hash */
	for (int i = 0; i < 512; i++)
		symbolHash[i] = -1;
	
	/* Phase 1: Parse input file */
	printf("; qox86 Phase 1: Parsing %s\n", argv[1]);
	
	char* line;
	while ((line = readLine(inFile)) != NULL) {
		processLine(line);
	}
	
	fclose(inFile);
	
	/* Print summary */
	printf("; Symbols collected: %d\n", numSymbols);
	printf("; Code size: %d bytes\n", codeSize);
	printf("; Data size: %d bytes\n", vsectSize);
	
	/* Phase 11: Generate .r module */
	generateModule(argv[2]);
	
	return 0;
}
