/*
 * File: qcc_backend_peephole.c
 * Description: Optimize safe adjacent operations in generated 68k assembly.
 * Owner: cla / afoe
 * Version: 0.2
 * Repository: Q9-Forge/Q9-QCC
 *
 * Edition History
 * 2026-11-25  0.2  Normalized the source header for the module audit.
 * 2026-09-11  0.1  Introduced the English source-header format.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PH_CHUNK 65536          /* Read granularity while the text buffer grows. */

static char* phText = 0;
/* Keep entries mutable to match the frontend type system. */
static char** phLines = 0;
static int* phRemoved = 0;
static int phLineCount = 0;
/* Derive pointer spacing from compiler layout. */
static char* phStride[2];

static char* phSynth = 0;
static int phSynthCap = 0;
static int phSynthUsed = 0;

static void phLoad(const char* path) {
	FILE* fp;
	int size, cap, got, i, lines, ptrStride;
	char* grown;
	fp = fopen(path, "r");
	if (!fp) fatal("peephole: unable to read assembly input");
	size = 0;
	cap = 0;
	for (;;) {
		if (size + PH_CHUNK + 1 > cap) {
			cap = cap == 0 ? PH_CHUNK * 4 : cap * 2;
			grown = (char*) realloc(phText, cap);
			if (!grown) fatal("peephole: unable to allocate assembly buffer");
			phText = grown;
		}
		got = fread(phText + size, 1, PH_CHUNK, fp);
		if (got <= 0) break;
		size += got;
	}
	fclose(fp);
	phText[size] = 0;
	/* Count lines before replacing separators; buffer sizes depend on this count. */
	lines = 1;
	for (i = 0; i < size; i++)
		if (phText[i] == '\n' && i + 1 < size) lines++;
	ptrStride = (int) ((char*) &phStride[1] - (char*) &phStride[0]);
	phLines = (char**) realloc(0, lines * ptrStride);
	phRemoved = (int*) realloc(0, lines * 4);
	phSynthCap = size + 1;
	phSynth = (char*) realloc(0, phSynthCap);
	if (!phLines || !phRemoved || !phSynth)
		fatal("peephole: unable to allocate line tables");
	phSynthUsed = 0;
	phLineCount = 0;
	phLines[phLineCount++] = phText;
	for (i = 0; i < size; i++) {
		if (phText[i] != '\n') continue;
		phText[i] = 0;
		if (i + 1 < size) phLines[phLineCount++] = &phText[i + 1];
	}
	for (i = 0; i < phLineCount; i++) phRemoved[i] = 0;
}

/* Next line not marked for removal after i, or -1. After a fold, the logical
 * next line is not necessarily at i+1; see the multiple-pass note above. */
static int phNextKept(int i) {
	int j = i + 1;
	while (j < phLineCount && phRemoved[j]) j++;
	return j < phLineCount ? j : -1;
}

/* Recognizes "move.l SRC,-(a7)", optionally preceded by "label:\t" on the
 * same line. This is read-only so a failed match cannot damage original text. */
static int phMatchPush(const char* line, const char** labelStart, int* labelLen,
                        const char** srcStart, int* srcLen) {
	const char* p;
	const char* colon = strchr(line, ':');
	*labelLen = 0;
	if (colon != 0 && colon[1] == '\t') {
		*labelStart = line;
		*labelLen = (int)(colon - line);
		p = colon + 2;
	} else {
		if (line[0] != '\t') return 0;
		p = line + 1;
	}
	if (strncmp(p, "move.l\t", 7) != 0) return 0;
	p += 7;
	{
		const char* comma = strrchr(p, ',');
		if (comma == 0 || strcmp(comma, ",-(a7)") != 0) return 0;
		*srcStart = p;
		*srcLen = (int)(comma - p);
	}
	return 1;
}

/* Recognizes "move.l (a7)+,DST". No label is allowed before it: it could be a
 * branch target, and label tracking is not implemented here. Do not fold an
 * ambiguous line. */
static int phMatchPop(const char* line, const char** dstStart) {
	if (line[0] != '\t') return 0;
	if (strncmp(line + 1, "move.l\t(a7)+,", 13) != 0) return 0;
	*dstStart = line + 1 + 13;
	return 1;
}

/* Compares a length-limited SRC with NUL-terminated DST for text equality,
 * used by the SRC==DST refinement of patterns one and three. */
static int phSameText(const char* a, int aLen, const char* b) {
	return (int)strlen(b) == aLen && strncmp(a, b, aLen) == 0;
}

static char* phEmitFused(const char* labelStart, int labelLen,
                          const char* srcStart, int srcLen, const char* dst) {
	char* p = phSynth + phSynthUsed;
	int n;
	if (labelLen > 0)
		n = sprintf(p, "%.*s:\tmove.l\t%.*s,%s", labelLen, labelStart, srcLen, srcStart, dst);
	else
		n = sprintf(p, "\tmove.l\t%.*s,%s", srcLen, srcStart, dst);
	phSynthUsed += n + 1;
	if (phSynthUsed >= phSynthCap) fatal("peephole: Synthesepuffer zu klein");
	return p;
}

/* Push immediately followed by its matching pop; see the file header.
 * Check the pop FIRST (read-only and harmless on failure); only after it
 * matches is it worth checking the preceding push in detail. */
static int phFoldPushPop(void) {
	int i, folded = 0;
	for (i = 0; i < phLineCount; i++) {
		/* Do NOT use "const char *a, *b, *c;": multiple pointer declarators in
		   one statement silently abort QCC (FAIL, no diagnostic; found 2026-09-08).
		   The restriction is independent of const and remains unresolved. Use one
		   declarator per statement. */
		const char* labelStart;
		const char* srcStart;
		const char* dst;
		int labelLen, srcLen;
		int j;
		if (phRemoved[i]) continue;
		j = phNextKept(i);
		if (j < 0) continue;
		if (!phMatchPop(phLines[j], &dst)) continue;
		if (!phMatchPush(phLines[i], &labelStart, &labelLen, &srcStart, &srcLen)) continue;
		if (labelLen == 0 && phSameText(srcStart, srcLen, dst)) {
			/* SRC==DST: push and pop cancel completely; see the refinement above.
			   No replacement line is needed. */
			phRemoved[i] = 1;
			phRemoved[j] = 1;
			folded++;
			continue;
		}
		phLines[i] = phEmitFused(labelStart, labelLen, srcStart, srcLen, dst);
		phRemoved[j] = 1;
		folded++;
	}
	return folded;
}

/* Recognizes "move.l SRC,Dn" (Dn is d0-d7, NEVER a0-a6; see the pattern notes
 * above), optionally with a label prefix. Returns both SRC (for pattern three)
 * and the destination register (for pattern two) in one shared matcher. */
static int phMatchMoveIntoDataReg(const char* line, const char** labelStart, int* labelLen,
                                   const char** srcStart, int* srcLen,
                                   const char** regStart, int* regLen) {
	const char* p;
	const char* comma;
	const char* colon = strchr(line, ':');
	*labelLen = 0;
	if (colon != 0 && colon[1] == '\t') {
		*labelStart = line;
		*labelLen = (int)(colon - line);
		p = colon + 2;
	} else {
		if (line[0] != '\t') return 0;
		p = line + 1;
	}
	if (strncmp(p, "move.l\t", 7) != 0) return 0;
	p += 7;
	comma = strrchr(p, ',');
	if (comma == 0) return 0;
	if (comma[1] != 'd' || comma[2] < '0' || comma[2] > '7' || comma[3] != '\0') return 0;
	*srcStart = p;
	*srcLen = (int)(comma - p);
	*regStart = comma + 1;
	*regLen = 2;
	return 1;
}

static int phMatchTst(const char* line, const char* regStart, int regLen) {
	if (line[0] != '\t') return 0;
	if (strncmp(line + 1, "tst.l\t", 6) != 0) return 0;
	if ((int)strlen(line + 7) != regLen) return 0;
	return strncmp(line + 7, regStart, regLen) == 0;
}

/* No replacement is needed: the move remains unchanged and only the redundant
 * TST is removed. */
static int phFoldMoveTst(void) {
	int i, folded = 0;
	for (i = 0; i < phLineCount; i++) {
		/* One declarator per statement; see phFoldPushPop above. */
		const char* labelStart;
		const char* srcStart;
		const char* regStart;
		int labelLen, srcLen, regLen, j;
		if (phRemoved[i]) continue;
		if (!phMatchMoveIntoDataReg(phLines[i], &labelStart, &labelLen, &srcStart, &srcLen, &regStart, &regLen)) continue;
		j = phNextKept(i);
		if (j < 0) continue;
		if (!phMatchTst(phLines[j], regStart, regLen)) continue;
		phRemoved[j] = 1;
		folded++;
	}
	return folded;
}

/* THIRD PATTERN, see the file header: "move.l Dn,DST" -- the same
 * data register filled by the preceding phMatchMoveIntoDataReg line. No label
 * is allowed before it because it could be a branch target, as with phMatchPop.
 * DST extends to the end of the line and uses the same layout as phMatchPop,
 * so it can be passed directly to phEmitFused without another replacement
 * builder. */
static int phMatchMoveFromDataReg(const char* line, const char* regStart, int regLen,
                                   const char** dstStart) {
	const char* p;
	if (line[0] != '\t') return 0;
	if (strncmp(line + 1, "move.l\t", 7) != 0) return 0;
	p = line + 8;
	if (strncmp(p, regStart, regLen) != 0) return 0;
	if (p[regLen] != ',') return 0;
	*dstStart = p + regLen + 1;
	return 1;
}

static int phFoldLoadThenMove(void) {
	int i, folded = 0;
	for (i = 0; i < phLineCount; i++) {
		/* One declarator per statement; see phFoldPushPop above. */
		const char* labelStart;
		const char* srcStart;
		const char* regStart;
		const char* dstStart;
		int labelLen, srcLen, regLen, j;
		if (phRemoved[i]) continue;
		if (!phMatchMoveIntoDataReg(phLines[i], &labelStart, &labelLen, &srcStart, &srcLen, &regStart, &regLen)) continue;
		j = phNextKept(i);
		if (j < 0) continue;
		if (!phMatchMoveFromDataReg(phLines[j], regStart, regLen, &dstStart)) continue;
		if (labelLen == 0 && phSameText(srcStart, srcLen, dstStart)) {
			/* SRC==DST; see the refinement in the file header. */
			phRemoved[i] = 1;
			phRemoved[j] = 1;
			folded++;
			continue;
		}
		phLines[i] = phEmitFused(labelStart, labelLen, srcStart, srcLen, dstStart);
		phRemoved[j] = 1;
		folded++;
	}
	return folded;
}

/* FOURTH PATTERN, see the file header: "move.l SRC,-(a7)" WITHOUT
 * parentheses in SRC (no side effect) and WITHOUT a label (which could be a
 * branch target). This is an existence check only: remove the line entirely;
 * no SRC restoration is needed. */
static int phMatchDroppablePush(const char* line) {
	const char* p;
	const char* comma;
	int i, n;
	if (line[0] != '\t') return 0;
	if (strncmp(line + 1, "move.l\t", 7) != 0) return 0;
	p = line + 8;
	comma = strrchr(p, ',');
	if (comma == 0 || strcmp(comma, ",-(a7)") != 0) return 0;
	n = (int)(comma - p);
	for (i = 0; i < n; i++) if (p[i] == '(') return 0;
	return 1;
}

static int phMatchSingleSlotDrop(const char* line) {
	if (line[0] != '\t') return 0;
	if (strcmp(line + 1, "addq.l\t#4,a7") == 0) return 1;
	if (strcmp(line + 1, "lea\t4(a7),a7") == 0) return 1;
	return 0;
}

static int phFoldDropPush(void) {
	int i, folded = 0;
	for (i = 0; i < phLineCount; i++) {
		int j;
		if (phRemoved[i]) continue;
		if (!phMatchDroppablePush(phLines[i])) continue;
		j = phNextKept(i);
		if (j < 0) continue;
		if (!phMatchSingleSlotDrop(phLines[j])) continue;
		phRemoved[i] = 1;
		phRemoved[j] = 1;
		folded++;
	}
	return folded;
}

/* Reads s[0..len) as a decimal number (optional leading "-"); digits only are
 * accepted. Stops as soon as the value is outside the MOVEQ range, avoiding
 * overflow in v. */
static int phParseSmallImm(const char* s, int len, int* value) {
	int i;
	int neg;
	int v;
	i = 0;
	neg = 0;
	if (len == 0) return 0;
	if (s[0] == '-') { neg = 1; i = 1; }
	if (i >= len) return 0;
	v = 0;
	for (; i < len; i++) {
		if (s[i] < '0' || s[i] > '9') return 0;
		v = v * 10 + (s[i] - '0');
		if (v > 128) return 0;
	}
	if (neg) v = -v;
	if (v < -128 || v > 127) return 0;
	*value = v;
	return 1;
}

/* FIFTH PATTERN, see the file header: "move.l #IMM,Dn" with IMM
 * in the MOVEQ range, optionally preceded by a label. The label is preserved:
 * unlike patterns one/three/four, this transformation removes nothing and
 * replaces only the mnemonic text on the same line, so the branch target is
 * never endangered. */
static int phMatchMoveqCandidate(const char* line, const char** labelStart, int* labelLen,
                                   int* value, char* reg) {
	const char* p;
	const char* comma;
	const char* colon = strchr(line, ':');
	*labelLen = 0;
	if (colon != 0 && colon[1] == '\t') {
		*labelStart = line;
		*labelLen = (int)(colon - line);
		p = colon + 2;
	} else {
		if (line[0] != '\t') return 0;
		p = line + 1;
	}
	if (strncmp(p, "move.l\t#", 8) != 0) return 0;
	p += 8;
	comma = strchr(p, ',');
	if (comma == 0) return 0;
	if (comma[1] != 'd' || comma[2] < '0' || comma[2] > '7' || comma[3] != '\0') return 0;
	if (!phParseSmallImm(p, (int)(comma - p), value)) return 0;
	*reg = comma[2];
	return 1;
}

static int phFoldMoveq(void) {
	int i, folded = 0;
	for (i = 0; i < phLineCount; i++) {
		/* Je ein eigener Deklarator -- s. Kommentar in phFoldPushPop oben. */
		const char* labelStart;
		int labelLen, value;
		char reg;
		char* p;
		int n;
		if (phRemoved[i]) continue;
		if (!phMatchMoveqCandidate(phLines[i], &labelStart, &labelLen, &value, &reg)) continue;
		p = phSynth + phSynthUsed;
		if (labelLen > 0)
			n = sprintf(p, "%.*s:\tmoveq\t#%d,d%c", labelLen, labelStart, value, reg);
		else
			n = sprintf(p, "\tmoveq\t#%d,d%c", value, reg);
		phSynthUsed += n + 1;
		if (phSynthUsed >= phSynthCap) fatal("peephole: Synthesepuffer zu klein");
		phLines[i] = p;
		folded++;
	}
	return folded;
}

static void phWrite(const char* path) {
	FILE* fp;
	int i;
	fp = fopen(path, "w");
	if (!fp) fatal("peephole: unable to open assembly output");
	for (i = 0; i < phLineCount; i++) {
		if (phRemoved[i]) continue;
		fputs(phLines[i], fp);
		fputc('\n', fp);
	}
	if (ferror(fp)) fatal("peephole: output write failed");
	fclose(fp);
}

/* srcPath is the temporary file written by emitIR() for -peephole; dstPath is
   the real output file. dstPath may be created here for the FIRST and only
   time because OS-9 I$Create fails when the file already exists. srcPath is
   retained as a .tmp file, like other intermediate .i/.ir files in the chain. */
static void peepholeRun(const char* srcPath, const char* dstPath) {
	int total, roundTotal, kept, i, rounds, moveqCount;
	phLoad(srcPath);
	total = 0;
	rounds = 0;
	do {
		roundTotal = phFoldPushPop();
		roundTotal += phFoldMoveTst();
		roundTotal += phFoldLoadThenMove();
		roundTotal += phFoldDropPush();
		total += roundTotal;
		rounds++;
	} while (roundTotal > 0);
	/* Only AFTER convergence, see the fifth-pattern note: MOVEQ lines would
	   otherwise remove the "move.l\t" trigger text needed by patterns two/three. */
	moveqCount = phFoldMoveq();
	total += moveqCount;
	kept = 0;
	for (i = 0; i < phLineCount; i++) if (!phRemoved[i]) kept++;
	phWrite(dstPath);
	fprintf(stderr, "qcc_backend: peephole: %d Optimierungen in %d Durchlaeufen (%d von %d Zeilen, davon %d MOVEQ)\n",
		total, rounds, kept, phLineCount, moveqCount);
}
