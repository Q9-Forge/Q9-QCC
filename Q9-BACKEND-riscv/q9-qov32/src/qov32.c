/*
 * qov32 -- Q9 RISC-V-32 assembler with OS-9000 .r output
 *
 * Usage: qov32 [options] <input.srv32> <output.r>
 *
 * Translates RISC-V-32 assembly to OS-9000 relocatable module format.
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

/* RISC-V registers (32 total) */
#define REG_ZERO  0
#define REG_RA    1
#define REG_SP    2
#define REG_GP    3
#define REG_TP    4
#define REG_T0    5
#define REG_T1    6
#define REG_T2    7
#define REG_S0    8
#define REG_S1    9
#define REG_A0    10
#define REG_A1    11
#define REG_A2    12
#define REG_A3    13
#define REG_A4    14
#define REG_A5    15
#define REG_A6    16
#define REG_A7    17
#define REG_S2    18
#define REG_S3    19
#define REG_S4    20
#define REG_S5    21
#define REG_S6    22
#define REG_S7    23
#define REG_S8    24
#define REG_S9    25
#define REG_S10   26
#define REG_S11   27
#define REG_T3    28
#define REG_T4    29
#define REG_T5    30
#define REG_T6    31

/* Section identifiers */
#define SECT_NONE   0
#define SECT_CODE   1
#define SECT_VSECT  2

/* RISC-V opcodes */
#define OP_LOAD     0x03
#define OP_STORE    0x23
#define OP_ARITH    0x33
#define OP_ARITH_I  0x13
#define OP_JAL      0x6F
#define OP_JALR     0x67
#define OP_BRANCH   0x63
#define OP_LUI      0x37
#define OP_AUIPC    0x17

typedef struct Symbol {
	char name[256];
	int address;
	int section;
	int type;
	int isExternal;
} Symbol;

static Symbol symbols[MAX_SYMBOLS];
static int numSymbols = 0;

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
	if (!name || !*name) return -1;
	int h = hashSymbolName(name);
	int i = symbolHash[h];
	while (i >= 0) {
		if (strcmp(symbols[i].name, name) == 0) return i;
		i = symbols[i].address;  /* Reuse address field for chain in hash table (hack) */
	}
	return -1;
}

static int addSymbol(const char* name, int addr, int sect, int type, int isExt) {
	if (numSymbols >= MAX_SYMBOLS) {
		fprintf(stderr, "Error: too many symbols\n");
		return -1;
	}
	int idx = numSymbols++;
	Symbol* s = &symbols[idx];
	strncpy(s->name, name, 255);
	s->address = addr;
	s->section = sect;
	s->type = type;
	s->isExternal = isExt;
	return idx;
}

/* Register name to number mapping */
static int regNumber(const char* name) {
	if (strcmp(name, "zero") == 0 || strcmp(name, "x0") == 0) return REG_ZERO;
	if (strcmp(name, "ra") == 0 || strcmp(name, "x1") == 0) return REG_RA;
	if (strcmp(name, "sp") == 0 || strcmp(name, "x2") == 0) return REG_SP;
	if (strcmp(name, "gp") == 0 || strcmp(name, "x3") == 0) return REG_GP;
	if (strcmp(name, "tp") == 0 || strcmp(name, "x4") == 0) return REG_TP;
	if (strcmp(name, "t0") == 0 || strcmp(name, "x5") == 0) return REG_T0;
	if (strcmp(name, "t1") == 0 || strcmp(name, "x6") == 0) return REG_T1;
	if (strcmp(name, "t2") == 0 || strcmp(name, "x7") == 0) return REG_T2;
	if (strcmp(name, "s0") == 0 || strcmp(name, "fp") == 0 || strcmp(name, "x8") == 0) return REG_S0;
	if (strcmp(name, "s1") == 0 || strcmp(name, "x9") == 0) return REG_S1;
	if (strcmp(name, "a0") == 0 || strcmp(name, "x10") == 0) return REG_A0;
	if (strcmp(name, "a1") == 0 || strcmp(name, "x11") == 0) return REG_A1;
	if (strcmp(name, "a2") == 0 || strcmp(name, "x12") == 0) return REG_A2;
	if (strcmp(name, "a3") == 0 || strcmp(name, "x13") == 0) return REG_A3;
	if (strcmp(name, "a4") == 0 || strcmp(name, "x14") == 0) return REG_A4;
	if (strcmp(name, "a5") == 0 || strcmp(name, "x15") == 0) return REG_A5;
	if (strcmp(name, "a6") == 0 || strcmp(name, "x16") == 0) return REG_A6;
	if (strcmp(name, "a7") == 0 || strcmp(name, "x17") == 0) return REG_A7;
	if (strcmp(name, "s2") == 0 || strcmp(name, "x18") == 0) return REG_S2;
	if (strcmp(name, "s3") == 0 || strcmp(name, "x19") == 0) return REG_S3;
	if (strcmp(name, "s4") == 0 || strcmp(name, "x20") == 0) return REG_S4;
	if (strcmp(name, "s5") == 0 || strcmp(name, "x21") == 0) return REG_S5;
	if (strcmp(name, "s6") == 0 || strcmp(name, "x22") == 0) return REG_S6;
	if (strcmp(name, "s7") == 0 || strcmp(name, "x23") == 0) return REG_S7;
	if (strcmp(name, "s8") == 0 || strcmp(name, "x24") == 0) return REG_S8;
	if (strcmp(name, "s9") == 0 || strcmp(name, "x25") == 0) return REG_S9;
	if (strcmp(name, "s10") == 0 || strcmp(name, "x26") == 0) return REG_S10;
	if (strcmp(name, "s11") == 0 || strcmp(name, "x27") == 0) return REG_S11;
	if (strcmp(name, "t3") == 0 || strcmp(name, "x28") == 0) return REG_T3;
	if (strcmp(name, "t4") == 0 || strcmp(name, "x29") == 0) return REG_T4;
	if (strcmp(name, "t5") == 0 || strcmp(name, "x30") == 0) return REG_T5;
	if (strcmp(name, "t6") == 0 || strcmp(name, "x31") == 0) return REG_T6;
	return -1;
}

/* Global buffers */
static unsigned char code[MAX_CODE];
static int codeSize = 0;
static unsigned char vsect[MAX_DATA];
static int vsectSize = 0;
static unsigned char outBuf[MAX_CODE];
static int outLen = 0;
static int currentSection = SECT_NONE;
static int currentAddress = 0;

/* PSECT parameters */
static char psectName[256] = "tc_prog";
static int psTyLan = 0;
static int psAttRev = 0;
static int psEdition = 1;
static int psStack = 4096;
static int psEntry = 0;
static int psTrap = -1;

/* Emit byte to current section */
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

/* Emit word (2 bytes, little-endian) */
static void emitWord(unsigned short w) {
	emitByte(w & 0xFF);
	emitByte((w >> 8) & 0xFF);
}

/* Emit dword (4 bytes, little-endian for RISC-V) */
static void emitDword(unsigned int d) {
	emitByte(d & 0xFF);
	emitByte((d >> 8) & 0xFF);
	emitByte((d >> 16) & 0xFF);
	emitByte((d >> 24) & 0xFF);
}

/* RISC-V instruction encoders */

/* R-type: funct7(7) | rs2(5) | rs1(5) | funct3(3) | rd(5) | opcode(7) */
static unsigned int encodeRType(int opcode, int rd, int funct3, int rs1, int rs2, int funct7) {
	return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
}

/* I-type: imm(12) | rs1(5) | funct3(3) | rd(5) | opcode(7) */
static unsigned int encodeIType(int opcode, int rd, int funct3, int rs1, int imm) {
	imm = imm & 0xFFF;
	return (imm << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
}

/* S-type: imm[11:5](7) | rs2(5) | rs1(5) | funct3(3) | imm[4:0](5) | opcode(7) */
static unsigned int encodeSType(int opcode, int funct3, int rs1, int rs2, int imm) {
	int imm_hi = (imm >> 5) & 0x7F;
	int imm_lo = imm & 0x1F;
	return (imm_hi << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (imm_lo << 7) | opcode;
}

/* B-type: imm[12]|imm[10:5] (7) | rs2(5) | rs1(5) | funct3(3) | imm[4:1]|imm[11] (5) | opcode(7) */
static unsigned int encodeBType(int opcode, int funct3, int rs1, int rs2, int imm) {
	int imm_12 = (imm >> 12) & 1;
	int imm_11 = (imm >> 11) & 1;
	int imm_10_5 = (imm >> 5) & 0x3F;
	int imm_4_1 = (imm >> 1) & 0xF;
	int imm_hi = (imm_12 << 6) | imm_10_5;
	int imm_lo = (imm_11 << 4) | imm_4_1;
	return (imm_hi << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (imm_lo << 7) | opcode;
}

/* U-type: imm[31:12](20) | rd(5) | opcode(7) */
static unsigned int encodeUType(int opcode, int rd, int imm) {
	imm = (imm >> 12) & 0xFFFFF;
	return (imm << 12) | (rd << 7) | opcode;
}

/* J-type: imm[20]|imm[10:1]|imm[11]|imm[19:12] (20) | rd(5) | opcode(7) */
static unsigned int encodeJType(int opcode, int rd, int imm) {
	int imm_20 = (imm >> 20) & 1;
	int imm_19_12 = (imm >> 12) & 0xFF;
	int imm_11 = (imm >> 11) & 1;
	int imm_10_1 = (imm >> 1) & 0x3FF;
	int imm_encoded = (imm_20 << 19) | (imm_19_12 << 11) | (imm_11 << 10) | imm_10_1;
	return (imm_encoded << 12) | (rd << 7) | opcode;
}

/* Parse immediate value (decimal, hex with $, or label) */
static int parseImmediate(const char* str, int currentAddr) {
	if (str[0] == '$') {
		int val;
		sscanf(str + 1, "%x", &val);
		return val;
	}
	if (isdigit(str[0]) || (str[0] == '-' && isdigit(str[1]))) {
		return atoi(str);
	}
	/* Label reference - look it up */
	int sym = findSymbol(str);
	if (sym >= 0) {
		return symbols[sym].address - currentAddr;
	}
	return 0;
}

/* Parse register reference with optional offset: "offset(reg)" */
static int parseOffset(const char* str) {
	char* paren = strchr(str, '(');
	if (paren) {
		return atoi(str);
	}
	return 0;
}

/* Parse register name from "offset(reg)" or plain register */
static const char* parseRegister(const char* str) {
	const char* paren = strchr(str, '(');
	if (paren) {
		paren++;
		const char* end = strchr(paren, ')');
		static char regbuf[32];
		int len = end - paren;
		if (len >= 32) len = 31;
		strncpy(regbuf, paren, len);
		regbuf[len] = 0;
		return regbuf;
	}
	return str;
}

/* ========================================================== Line Input === */

static char lineBuf[MAX_LINE];
static int lineNum = 0;

static char* readLine(FILE* f) {
	if (fgets(lineBuf, MAX_LINE, f) == NULL)
		return NULL;
	lineNum++;
	char* p = lineBuf + strlen(lineBuf) - 1;
	if (*p == '\n') *p = 0;
	return lineBuf;
}

/* ========================================================== Tokenizer === */

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
		(*pp)++;
	}
	
	int len = *pp - start;
	if (len >= MAX_LINE) len = MAX_LINE - 1;
	strncpy(token, start, len);
	token[len] = 0;
	return len;
}

/* ========================================================== Directives === */

static void handlePsect(char* line) {
	char* p = line;
	getToken(&p);  /* consume 'psect' */
	
	int paramIdx = 0;
	while (getToken(&p)) {
		if (token[0] == ',') continue;
		
		if (paramIdx == 0) {
			strncpy(psectName, token, 255);
		} else if (paramIdx == 1) {
			sscanf(token, "%d", &psTyLan);
		} else if (paramIdx == 2) {
			sscanf(token, "%d", &psAttRev);
		} else if (paramIdx == 3) {
			sscanf(token, "%d", &psEdition);
		} else if (paramIdx == 4) {
			sscanf(token, "%d", &psStack);
		} else if (paramIdx == 5) {
			sscanf(token, "%d", &psEntry);
		}
		paramIdx++;
	}
	
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
	char* p = line;
	getToken(&p);  /* consume 'nam' */
	if (getToken(&p)) {
		/* Set module name if needed */
	}
}

static void handleDcl(char* line) {
	if (currentSection != SECT_VSECT) {
		fprintf(stderr, "Error at line %d: dc.l outside vsect\n", lineNum);
		return;
	}
	
	char* p = line;
	getToken(&p);  /* consume 'dc.l' */
	
	while (getToken(&p)) {
		if (token[0] == 0) break;
		if (token[0] == ',') continue;
		
		int value = 0;
		if (token[0] == '$') {
			sscanf(token + 1, "%x", &value);
		} else if (isdigit(token[0]) || token[0] == '-') {
			sscanf(token, "%d", &value);
		}
		
		if (vsectSize + 4 <= MAX_DATA) {
			vsect[vsectSize++] = (value >> 24) & 0xFF;
			vsect[vsectSize++] = (value >> 16) & 0xFF;
			vsect[vsectSize++] = (value >> 8) & 0xFF;
			vsect[vsectSize++] = value & 0xFF;
		}
	}
}

static void handleDsl(char* line) {
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

/* ========================================================== Instruction Encoding === */

/* Simple instruction encoder that dispatches by mnemonic */
static void encodeInstruction(char* line) {
	char* p = line;
	skipWhitespace(&p);
	
	if (!*p || *p == ';') return;
	
	char mnem[32];
	char* m = mnem;
	while (*p && !isspace(*p) && m < mnem + 31) {
		*m++ = *p++;
	}
	*m = 0;
	
	/* Parse operands */
	char op1[256] = "", op2[256] = "", op3[256] = "", op4[256] = "";
	skipWhitespace(&p);
	if (*p != ';' && *p) {
		char* start = p;
		while (*p && *p != ',' && !isspace(*p)) p++;
		int len = p - start;
		if (len > 0 && len < 255) {
			strncpy(op1, start, len);
			op1[len] = 0;
		}
	}
	
	if (*p == ',') {
		p++;
		skipWhitespace(&p);
		char* start = p;
		while (*p && *p != ',' && !isspace(*p)) p++;
		int len = p - start;
		if (len > 0 && len < 255) {
			strncpy(op2, start, len);
			op2[len] = 0;
		}
	}
	
	if (*p == ',') {
		p++;
		skipWhitespace(&p);
		char* start = p;
		while (*p && *p != ',' && !isspace(*p) && *p != '(') p++;
		int len = p - start;
		if (len > 0 && len < 255) {
			strncpy(op3, start, len);
			op3[len] = 0;
		}
		if (*p == '(') {
			p++;
			start = p;
			while (*p && *p != ')') p++;
			len = p - start;
			if (len > 0 && len < 255) {
				strncpy(op4, start, len);
				op4[len] = 0;
			}
		}
	}
	
	/* Emit instruction based on mnemonic */
	unsigned int instr = 0;
	
	if (strcmp(mnem, "addi") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int imm = parseImmediate(op3, currentAddress);
		instr = encodeIType(OP_ARITH_I, rd, 0, rs1, imm);
		emitDword(instr);
	}
	else if (strcmp(mnem, "add") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 0, rs1, rs2, 0);
		emitDword(instr);
	}
	else if (strcmp(mnem, "sub") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 0, rs1, rs2, 0x20);
		emitDword(instr);
	}
	else if (strcmp(mnem, "mul") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 0, rs1, rs2, 1);
		emitDword(instr);
	}
	else if (strcmp(mnem, "div") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 4, rs1, rs2, 1);
		emitDword(instr);
	}
	else if (strcmp(mnem, "divu") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 5, rs1, rs2, 1);
		emitDword(instr);
	}
	else if (strcmp(mnem, "rem") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 6, rs1, rs2, 1);
		emitDword(instr);
	}
	else if (strcmp(mnem, "remu") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 7, rs1, rs2, 1);
		emitDword(instr);
	}
	else if (strcmp(mnem, "and") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 7, rs1, rs2, 0);
		emitDword(instr);
	}
	else if (strcmp(mnem, "or") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 6, rs1, rs2, 0);
		emitDword(instr);
	}
	else if (strcmp(mnem, "xor") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 4, rs1, rs2, 0);
		emitDword(instr);
	}
	else if (strcmp(mnem, "sll") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 1, rs1, rs2, 0);
		emitDword(instr);
	}
	else if (strcmp(mnem, "srl") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 5, rs1, rs2, 0);
		emitDword(instr);
	}
	else if (strcmp(mnem, "sra") == 0 && *op1 && *op2 && *op3) {
		int rd = regNumber(op1);
		int rs1 = regNumber(op2);
		int rs2 = regNumber(op3);
		instr = encodeRType(OP_ARITH, rd, 5, rs1, rs2, 0x20);
		emitDword(instr);
	}
	else if (strcmp(mnem, "lw") == 0 && *op1 && *op3 && *op4) {
		int rd = regNumber(op1);
		int offset = parseImmediate(op3, currentAddress);
		int rs1 = regNumber(op4);
		instr = encodeIType(OP_LOAD, rd, 2, rs1, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "lhu") == 0 && *op1 && *op3 && *op4) {
		int rd = regNumber(op1);
		int offset = parseImmediate(op3, currentAddress);
		int rs1 = regNumber(op4);
		instr = encodeIType(OP_LOAD, rd, 5, rs1, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "lbu") == 0 && *op1 && *op3 && *op4) {
		int rd = regNumber(op1);
		int offset = parseImmediate(op3, currentAddress);
		int rs1 = regNumber(op4);
		instr = encodeIType(OP_LOAD, rd, 4, rs1, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "sw") == 0 && *op1 && *op3 && *op4) {
		int rs2 = regNumber(op1);
		int offset = parseImmediate(op3, currentAddress);
		int rs1 = regNumber(op4);
		instr = encodeSType(OP_STORE, 2, rs1, rs2, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "sh") == 0 && *op1 && *op3 && *op4) {
		int rs2 = regNumber(op1);
		int offset = parseImmediate(op3, currentAddress);
		int rs1 = regNumber(op4);
		instr = encodeSType(OP_STORE, 1, rs1, rs2, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "sb") == 0 && *op1 && *op3 && *op4) {
		int rs2 = regNumber(op1);
		int offset = parseImmediate(op3, currentAddress);
		int rs1 = regNumber(op4);
		instr = encodeSType(OP_STORE, 0, rs1, rs2, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "beq") == 0 && *op1 && *op2 && *op3) {
		int rs1 = regNumber(op1);
		int rs2 = regNumber(op2);
		int offset = parseImmediate(op3, currentAddress);
		instr = encodeBType(OP_BRANCH, 0, rs1, rs2, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "bne") == 0 && *op1 && *op2 && *op3) {
		int rs1 = regNumber(op1);
		int rs2 = regNumber(op2);
		int offset = parseImmediate(op3, currentAddress);
		instr = encodeBType(OP_BRANCH, 1, rs1, rs2, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "blt") == 0 && *op1 && *op2 && *op3) {
		int rs1 = regNumber(op1);
		int rs2 = regNumber(op2);
		int offset = parseImmediate(op3, currentAddress);
		instr = encodeBType(OP_BRANCH, 4, rs1, rs2, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "bge") == 0 && *op1 && *op2 && *op3) {
		int rs1 = regNumber(op1);
		int rs2 = regNumber(op2);
		int offset = parseImmediate(op3, currentAddress);
		instr = encodeBType(OP_BRANCH, 5, rs1, rs2, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "bltu") == 0 && *op1 && *op2 && *op3) {
		int rs1 = regNumber(op1);
		int rs2 = regNumber(op2);
		int offset = parseImmediate(op3, currentAddress);
		instr = encodeBType(OP_BRANCH, 6, rs1, rs2, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "bgeu") == 0 && *op1 && *op2 && *op3) {
		int rs1 = regNumber(op1);
		int rs2 = regNumber(op2);
		int offset = parseImmediate(op3, currentAddress);
		instr = encodeBType(OP_BRANCH, 7, rs1, rs2, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "jal") == 0 && *op1 && *op2) {
		int rd = regNumber(op1);
		int offset = parseImmediate(op2, currentAddress);
		instr = encodeJType(OP_JAL, rd, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "jalr") == 0 && *op1 && *op3) {
		int rd = regNumber(op1);
		int offset = parseImmediate(op3, currentAddress);
		int rs1 = regNumber(op2);
		instr = encodeIType(OP_JALR, rd, 0, rs1, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "li") == 0 && *op1 && *op2) {
		int rd = regNumber(op1);
		int imm = parseImmediate(op2, currentAddress);
		if (imm >= -2048 && imm <= 2047) {
			instr = encodeIType(OP_ARITH_I, rd, 0, 0, imm);
		} else {
			instr = encodeUType(OP_LUI, rd, imm);
		}
		emitDword(instr);
	}
	else if (strcmp(mnem, "la") == 0 && *op1 && *op2) {
		/* la (load address) - implemented as auipc + addi */
		int rd = regNumber(op1);
		instr = encodeUType(OP_AUIPC, rd, 0);
		emitDword(instr);
		instr = encodeIType(OP_ARITH_I, rd, 0, rd, 0);
		emitDword(instr);
	}
	else if (strcmp(mnem, "not") == 0 && *op1 && *op2) {
		/* not rd, rs = xori rd, rs, -1 */
		int rd = regNumber(op1);
		int rs = regNumber(op2);
		instr = encodeIType(OP_ARITH_I, rd, 4, rs, -1);
		emitDword(instr);
	}
	else if (strcmp(mnem, "seqz") == 0 && *op1 && *op2) {
		/* seqz rd, rs = sltiu rd, rs, 1 */
		int rd = regNumber(op1);
		int rs = regNumber(op2);
		instr = encodeIType(OP_ARITH_I, rd, 3, rs, 1);
		emitDword(instr);
	}
	else if (strcmp(mnem, "call") == 0 && *op1) {
		/* call func - jal ra, func */
		int offset = parseImmediate(op1, currentAddress);
		instr = encodeJType(OP_JAL, REG_RA, offset);
		emitDword(instr);
	}
	else if (strcmp(mnem, "ret") == 0) {
		/* ret - jalr x0, 0(ra) */
		instr = encodeIType(OP_JALR, 0, 0, REG_RA, 0);
		emitDword(instr);
	}
	else if (mnem[0] != '.' && mnem[0] != ';' && mnem[strlen(mnem)-1] != ':') {
		/* Unknown instruction - emit as comment */
		fprintf(stderr, "Warning at line %d: unknown instruction %s\n", lineNum, mnem);
	}
}

/* ========================================================== Main === */

static void writeROFModule(const char* outFile) {
	FILE* out = fopen(outFile, "wb");
	if (!out) {
		fprintf(stderr, "Error: cannot open output file\n");
		return;
	}
	
	outLen = 0;
	
	/* Helper to write to outBuf */
	#define outByteMod(b) do { if (outLen < MAX_CODE) outBuf[outLen++] = (b); } while(0)
	#define outWordMod(w) do { \
		int _w = (w); \
		outByteMod((_w >> 8) & 0xFF); \
		outByteMod(_w & 0xFF); \
	} while(0)
	#define outDwordMod(d) do { \
		int _d = (d); \
		outWordMod((_d >> 16) & 0xFFFF); \
		outWordMod(_d & 0xFFFF); \
	} while(0)
	#define outStrZ(s) do { \
		const char* _s = (s); \
		while (*_s && outLen < MAX_CODE) outBuf[outLen++] = *_s++; \
		if (outLen < MAX_CODE) outBuf[outLen++] = 0; \
	} while(0)
	
	/* Header (56 bytes, big-endian) */
	outDwordMod(0xDEADFACE);      /* sync */
	outWordMod(psTyLan);          /* type/language */
	outWordMod(psAttRev);         /* attributes/revision */
	outWordMod(0);                /* valid */
	outWordMod(999);              /* series */
	outByteMod(126);              /* year - 1900 */
	outByteMod(9);                /* month */
	outByteMod(16);               /* day */
	outByteMod(17);               /* hour */
	outByteMod(57);               /* minute */
	outByteMod(31);               /* second */
	outWordMod(psEdition);        /* edition */
	outDwordMod(0);               /* static storage */
	outDwordMod(vsectSize);       /* initialized data */
	outDwordMod(codeSize);        /* code size */
	outDwordMod(psStack);         /* stack */
	outDwordMod(psEntry);         /* entry point */
	outDwordMod(psTrap);          /* trap entry */
	outDwordMod(0);               /* remote static */
	outDwordMod(0);               /* remote idata */
	outDwordMod(0);               /* debug */
	
	/* PSECT name */
	outStrZ(psectName);
	
	/* Global symbols */
	int nGlob = 0;
	for (int i = 0; i < numSymbols; i++) {
		if (!symbols[i].isExternal && symbols[i].section != SECT_NONE)
			nGlob++;
	}
	outDwordMod(nGlob);
	for (int i = 0; i < numSymbols; i++) {
		if (!symbols[i].isExternal && symbols[i].section != SECT_NONE) {
			outStrZ(symbols[i].name);
			unsigned short type = (symbols[i].section == SECT_CODE) ? 0x0004 : 0x0001;
			outWordMod(type);
			outDwordMod(symbols[i].address);
		}
	}
	
	/* Write to file */
	fwrite(outBuf, 1, outLen, out);
	fwrite(code, 1, codeSize, out);
	fwrite(vsect, 1, vsectSize, out);
	
	/* External symbols (0) */
	unsigned char extBuf[12];
	extBuf[0] = 0; extBuf[1] = 0; extBuf[2] = 0; extBuf[3] = 0;
	extBuf[4] = 0; extBuf[5] = 0; extBuf[6] = 0; extBuf[7] = 0;
	extBuf[8] = 0; extBuf[9] = 0; extBuf[10] = 0; extBuf[11] = 0;
	fwrite(extBuf, 1, 12, out);
	
	fclose(out);
	
	int totalSize = 56 + strlen(psectName) + 1 + 4 + codeSize + vsectSize + 12;
	printf("; Generated .r module: %s (%d bytes)\n", outFile, totalSize);
}

/* Main program */
int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s <input.srv32> <output.r>\n", argv[0]);
		return 1;
	}
	
	const char* infile = argv[1];
	const char* outfile = argv[2];
	
	FILE* in = fopen(infile, "r");
	if (!in) {
		fprintf(stderr, "Error: cannot open input file %s\n", infile);
		return 1;
	}
	
	char* line;
	while ((line = readLine(in))) {
		int len = strlen(line);
		while (len > 0 && isspace(line[len-1])) line[--len] = '\0';
		
		if (!len || line[0] == ';' || line[0] == '#') continue;
		
		char* colon = strchr(line, ':');
		if (colon && (colon == line || isspace(line[colon - line - 1]))) {
			/* Label definition */
			char label[256];
			int llen = colon - line;
			strncpy(label, line, llen);
			label[llen] = 0;
			
			/* Trim whitespace from label */
			char* lp = label;
			while (*lp && isspace(*lp)) lp++;
			char* lend = lp + strlen(lp) - 1;
			while (lend >= lp && isspace(*lend)) *lend-- = 0;
			
			addSymbol(lp, currentAddress, currentSection, 0, 0);
			continue;
		}
		
		if (strncmp(line, "\tpsect", 6) == 0 || strncmp(line, "psect", 5) == 0) {
			handlePsect(line);
		} else if (strncmp(line, "\tvsect", 6) == 0 || strncmp(line, "vsect", 5) == 0) {
			handleVsect(line);
		} else if (strncmp(line, "\tends", 5) == 0 || strncmp(line, "tends", 5) == 0) {
			handleEnds(line);
		} else if (strncmp(line, "\tnam", 4) == 0 || strncmp(line, "nam", 3) == 0) {
			handleNam(line);
		} else if (strncmp(line, "\tdc.l", 5) == 0 || strncmp(line, "dc.l", 4) == 0) {
			handleDcl(line);
		} else if (strncmp(line, "\tds.l", 5) == 0 || strncmp(line, "ds.l", 4) == 0) {
			handleDsl(line);
		} else if (currentSection == SECT_CODE && (line[0] == '\t' || line[0] != '.')) {
			encodeInstruction(line);
		}
	}
	
	fclose(in);
	
	writeROFModule(outfile);
	
	fprintf(stderr, "OK: %d bytes code, %d bytes data, %d symbols\n", codeSize, vsectSize, numSymbols);
	return 0;
}
