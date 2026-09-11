/*================================================================================
 * qcc_backend_peephole.c -- peephole optimizer for 68k output (-peephole)
 *
 * Operates on generated assembly text rather than IR --
 * This is the stage where Microware's o68 optimizer sits in the classic chain
 * (cc -> cpp -> c68 -> o68 -> r68 -> l68, measured on a real OS-9/68k binary
 * on 2026-09-08). Waste appears only when abstract stack IR becomes real 68k
 * memory accesses, not in the IR itself: a PUSH/POP pair is part of opcode
 * semantics, and callers rely on the operand being on the stack. Only after
 * the backend emits "move.l X,-(a7)" followed by "move.l (a7)+,Y" is the
 * removable memory round trip visible.
 *
 * FIRST PATTERN: "move.l SRC,-(a7)" immediately followed by "move.l (a7)+,DST"
 * becomes "move.l SRC,DST". This is always safe here: a push immediately
 * followed exclusively by its own pop changes A7 temporarily, but nothing else
 * observes it. The value moves unchanged from SRC to DST in one step.
 *
 * SECOND PATTERN (08.09.2026, found in the measured qr68 output distribution
 * -- 2064 occurrences, by far the largest individual finding): "move.l SRC,Dn"
 * immediately followed by "tst.l Dn" (the SAME number, a DATA register d0-d7).
 * MOVE.L sets N/Z on 68000 hardware exactly as TST.L would for the same value
 * (V/C are both cleared), so TST is only a repetition and can be removed.
 * Deliberately limit this to d0-d7, never a0-a6: "move.l SRC,An" is assembled
 * by r68/qr68 as MOVEA, the only opcode form for an address-register target,
 * and MOVEA does not set flags, so TST would be required there.
 *
 * THIRD PATTERN (same frequency list): "move.l SRC,Dn" immediately followed by
 * "move.l Dn,DST" using the same data register becomes "move.l SRC,DST".
 * This is safe for the same reason as pattern one: nothing else observes the
 * register between the two lines. DST and SRC may use any supported addressing
 * form; 68k permits memory-to-memory MOVE, and post-/pre-decrement addressing
 * evaluates its effective address exactly once in either form. The fusion does
 * not change that behavior. qr68 output was checked to contain exactly one
 * comma in each move.l line, so the rightmost comma safely separates SRC/DST.
 *
 * FOURTH PATTERN (same frequency list, 867 occurrences): "move.l
 * Dn,-(a7)" immediately followed by "addq.l #4,a7" (or "lea 4(a7),a7").
 * The IR opcode DROP emits exactly this addq.l line (compute an expression
 * and discard its result, for example a statement "f();" whose return value
 * is unused). A push immediately followed by its own discard has no net effect
 * on A7 and its value is never read. Unlike the first three patterns, nothing
 * is replaced: both lines disappear. Limit this to SRC without parentheses
 * (no "(a0)", "(a0)+", etc.); post-/pre-decrement side effects still need to
 * execute even when the value is discarded. Only a plain register or immediate
 * value can be removed safely.
 *
 * REFINEMENT of patterns one/three (08.09.2026, 776 occurrences measured in
 * the output of the first four patterns): when SRC equals DST
 * ("move.l d0,-(a7)" followed by "move.l (a7)+,d0", the same number both
 * times), fusion would produce the real but useless "move.l d0,d0". Safer and
 * smaller: remove BOTH lines, as in pattern four. This is allowed only when
 * the first line has no label, otherwise the branch target would be lost. In
 * the rare labeled case, retain the harmless but non-ideal fusion
 * "label:\tmove.l\tDn,Dn".
 *
 * FIFTH PATTERN (09.09.2026, selected by frequency versus effort, not guessed):
 * "move.l #IMM,Dn" with IMM in the range -128..127 becomes "moveq #IMM,Dn".
 * This occurred 1,928 times in qr68's output. MOVEQ is the only opcode form
 * for an immediate MOVE.L into a data register with the same flag behavior,
 * but uses 2 bytes instead of 6. Limit this to Dn (never An) and to plain
 * decimal values, so symbols or expressions are never misinterpreted.
 *
 * MUST run LAST, not in the convergence pass with the other four patterns:
 * vier: phMatchMoveIntoDataReg (Muster zwei/drei) sucht wortwoertlich den
 * Text "move.l\t" als Ausloeser. Liefe die MOVEQ-Umwandlung VORHER, saehe
 * ein anschliessendes "tst.l Dn" oder "move.l Dn,DST" sein Gegenstueck
 * nicht mehr -- die Faltungschance ginge verloren. MOVEQ-Zeilen selbst
 * bieten dafuer keine neue Faltungschance (die Quelle ist ein Sofortwert,
 * nie textgleich mit einem Zielregister), ein einzelner Durchlauf am Ende
 * reicht deshalb aus.
 *
 * MULTIPLE PASSES: removing one line often exposes the next opportunity --
 * "PUSH x / POP d0 / TST d0" first folds to "move.l x,d0", and only then is
 * "tst.l d0" adjacent. peepholeRun() therefore repeats the three patterns
 * until a pass makes no changes, as expected from a classic peephole pass.
 * Fold functions must not rely on physical adjacency (phLines[i+1]): removed
 * lines remain in the array and phNextKept() skips them.
 *
 * ARCHITECTURE for additional patterns (o68 lesson): read original lines only
 * (no mutation); replacements go into a dedicated synthesis buffer, and a
 * line is marked removed instead of physically moved (the o68s remins idea).
 * New patterns should be added as additional phFold* functions, not as special
 * cases in an existing one.
 *
 * MEMORY SIZES ARE MEASURED, NOT GUESSED: qr68's own assembler output with
 * -remotedata (the largest target-side case outside self-hosting so far) has
 * 75,273 lines / 1,588,771 bytes. The limits below provide
 * darauf reichlich Kopfraum; QCCs eigener Selbsthost-Bau (222.832 Zeilen /
 * 4.59 MB) deliberately exceeds them; -peephole is not yet wired into that
 * path, which requires a separate future memory-budget decision. Exceeding a
 * limit calls fatal(), like every other backend capacity limit, with no silent
 * truncation.
 *================================================================================*/

#define PH_MAX_LINES  100000
#define PH_TEXT_BYTES 2097152   /* 2 MB, about 35% headroom over qr68 (1.59 MB) */
#define PH_SYNTH_BYTES PH_TEXT_BYTES /* A replacement line is never longer than
                                        the two original lines combined, so the
                                        sum of all replacements fits within the
                                        original text budget. */

static char phText[PH_TEXT_BYTES];
static const char* phLines[PH_MAX_LINES];
static int phRemoved[PH_MAX_LINES];
static int phLineCount = 0;

static char phSynth[PH_SYNTH_BYTES];
static int phSynthUsed = 0;

static void phLoad(const char* path) {
	FILE* fp;
	int size, got, i;
	fp = fopen(path, "r");
	if (!fp) fatal("peephole: kann Assemblerdatei nicht lesen");
	size = 0;
	for (;;) {
		got = fread(phText + size, 1, PH_TEXT_BYTES - 1 - size, fp);
		if (got <= 0) break;
		size += got;
		if (size >= PH_TEXT_BYTES - 1) fatal("peephole: Assemblerausgabe zu gross fuer PH_TEXT_BYTES");
	}
	fclose(fp);
	phText[size] = 0;
	phLineCount = 0;
	phLines[phLineCount++] = phText;
	for (i = 0; i < size; i++) {
		if (phText[i] != '\n') continue;
		phText[i] = 0;
		if (i + 1 < size) {
			if (phLineCount >= PH_MAX_LINES) fatal("peephole: zu viele Zeilen fuer PH_MAX_LINES");
			phLines[phLineCount++] = &phText[i + 1];
		}
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

static const char* phEmitFused(const char* labelStart, int labelLen,
                                const char* srcStart, int srcLen, const char* dst) {
	char* p = phSynth + phSynthUsed;
	int n;
	if (labelLen > 0)
		n = sprintf(p, "%.*s:\tmove.l\t%.*s,%s", labelLen, labelStart, srcLen, srcStart, dst);
	else
		n = sprintf(p, "\tmove.l\t%.*s,%s", srcLen, srcStart, dst);
	phSynthUsed += n + 1;
	if (phSynthUsed >= PH_SYNTH_BYTES) fatal("peephole: Synthesepuffer zu klein");
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
		if (phSynthUsed >= PH_SYNTH_BYTES) fatal("peephole: Synthesepuffer zu klein");
		phLines[i] = p;
		folded++;
	}
	return folded;
}

static void phWrite(const char* path) {
	FILE* fp;
	int i;
	fp = fopen(path, "w");
	if (!fp) fatal("peephole: kann Ausgabedatei nicht neu schreiben");
	for (i = 0; i < phLineCount; i++) {
		if (phRemoved[i]) continue;
		fputs(phLines[i], fp);
		fputc('\n', fp);
	}
	if (ferror(fp)) fatal("peephole: Schreibfehler");
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
