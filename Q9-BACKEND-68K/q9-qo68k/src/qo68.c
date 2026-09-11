/*===============================================================================
 * qo68k -- 68k peephole optimizer
 *
 * Purpose:
 *   Optimizes generated 68k assembly after Stack-IR lowering, preserving the
 *   observable instruction semantics required by the Q9 runtime.
 *
 * Edition history:
 *   2026-09-11  Introduced the English source-header format.
 *
 * Operates on generated assembly text, not on IR. This is the same position
 * occupied by Microware's o68 in the classic OS-9/68K toolchain. Stack
 * push/pop pairs are meaningful in the abstract IR; they become removable
 * only after the 68k backend lowers them to memory operations such as
 * "move.l X,-(a7)" followed by "move.l (a7)+,Y".
 *
 * FIRST PATTERN: "move.l SRC,-(a7)" immediately followed by
 * "move.l (a7)+,DST" becomes "move.l SRC,DST". This is safe here because
 * the push is immediately followed only by its matching pop.
 *
 * SECOND PATTERN (2026-09-08, selected from qr68's measured output):
 * "move.l SRC,Dn" immediately followed by "tst.l Dn" (the same data
 * register d0-d7). MOVE.L already sets N/Z exactly as TST.L would for the
 * same value; both clear V/C. The TST is therefore redundant. This rule is
 * intentionally limited to data registers: "move.l SRC,An" is assembled as
 * MOVEA, which does not set flags, so a following TST is meaningful.
 *
 * THIRD PATTERN: "move.l SRC,Dn" immediately followed by
 * "move.l Dn,DST" using the same data register becomes "move.l SRC,DST".
 * The register value is not observed between the two instructions. Both
 * operands may use the full addressing modes emitted by this backend,
 * including side effects; the effective address is evaluated exactly once
 * in either form. The rightmost comma separates source and destination
 * because this backend does not emit indexed operands containing commas.
 *
 * FOURTH PATTERN: "move.l Dn,-(a7)" immediately followed by
 * "addq.l #4,a7" (or "lea 4(a7),a7"). The IR DROP opcode emits this exact
 * cleanup after computing and discarding an expression value. Since the
 * pushed value is never read and A7 has no net change, both lines can be
 * removed. The source must not have side effects such as post-increment or
 * pre-decrement; a pure register or immediate value is safe to discard.
 *
 * REFINEMENT of patterns one and three: if SRC and DST are identical, the
 * merged instruction would be a useless self-move. Remove both instructions
 * instead, but only when the first line has no label; otherwise the branch
 * target must be preserved and the harmless self-move remains.
 *
 * FIFTH PATTERN: "move.l #IMM,Dn" with IMM in the range -128..127 becomes
 * "moveq #IMM,Dn". MOVEQ is two bytes instead of six and preserves the same
 * condition-code effect for this immediate source. The rule is limited to
 * data registers and plain decimal constants, so symbols and expressions
 * cannot be misinterpreted.
 *
 * The fifth pattern MUST run last, not in the convergence pass with the
 * other four. phMatchMoveIntoDataReg deliberately looks for the literal
 * "move.l\t" prefix; converting first would hide later fold opportunities.
 * MOVEQ itself cannot create another matching opportunity, so one final pass
 * is sufficient.
 *
 * MULTIPLE PASSES: removing one instruction often exposes the next pattern.
 * For example, "PUSH x / POP d0 / TST d0" first becomes "move.l x,d0" and
 * only then exposes the redundant TST. peepholeRun() therefore repeats the
 * patterns until a pass makes no change. Fold functions must not rely on
 * physical adjacency (phLines[i+1]); removed lines remain in the array and
 * phNextKept() skips them.
 *
 * ARCHITECTURE FOR FUTURE PATTERNS: read original lines without mutation,
 * place replacements in a separate synthesis buffer, and mark removed lines
 * instead of moving them physically. New patterns should be added as further
 * phFold* functions rather than as special cases in an existing matcher.
 *
 * MEMORY SIZES ARE MEASURED: qr68's own -remotedata assembly output, the
 * largest target-run case outside the self-host build, contains 75,273 lines
 * and 1,588,771 bytes. The limits below leave generous headroom. QCC's own
 * self-host build intentionally exceeds them; -peephole is not wired into
 * that path yet and needs a separate memory-budget decision. Overflow calls
 * fatal(), like every other capacity limit in this backend; it never truncates
 * silently.
 *================================================================================*/

#define PH_MAX_LINES  100000
#define PH_TEXT_BYTES 2097152   /* 2 MB, ~35% headroom over qr68 (1.59 MB) */
#define PH_SYNTH_BYTES PH_TEXT_BYTES /* A replacement is never longer than
                                        the two original lines combined, so
                                        all replacements fit within the same
                                        bound as the original text. */

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

/* Naechste NICHT gestrichene Zeile nach i, oder -1. Siehe Kommentar am
 * Dateianfang zu "mehrere Durchlaeufe": nach einer Faltung liegt die
 * logisch naechste Zeile nicht mehr zwingend bei i+1. */
static int phNextKept(int i) {
	int j = i + 1;
	while (j < phLineCount && phRemoved[j]) j++;
	return j < phLineCount ? j : -1;
}

/* Erkennt "move.l SRC,-(a7)", optional mit einem "label:\t"-Vorspann auf
 * DERSELBEN Zeile (dieses Backend haengt Labels so an, siehe emitIR()).
 * Liest NUR -- schreibt nichts in die Zeile, damit ein Fehlschlag hier
 * (Pop passt am Ende doch nicht) den Originaltext nicht beschaedigt. */
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

/* Erkennt "move.l (a7)+,DST" -- KEIN Label davor: koennte ein Sprungziel
 * sein, und dafuer gibt es hier (noch) keine Sprungziel-Nachfuehrung
 * (s. o68-Kommentar oben am Dateianfang). Lieber nicht falten als falsch. */
static int phMatchPop(const char* line, const char** dstStart) {
	if (line[0] != '\t') return 0;
	if (strncmp(line + 1, "move.l\t(a7)+,", 13) != 0) return 0;
	*dstStart = line + 1 + 13;
	return 1;
}

/* Vergleicht ein laengenbegrenztes SRC mit einem nullterminierten DST auf
 * Textgleichheit -- fuer die SRC==DST-Verfeinerung von Muster eins/drei. */
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

/* Push unmittelbar gefolgt vom eigenen Pop, s. Kommentar am Dateianfang.
 * Pop wird ZUERST geprueft (lesend, folgenlos bei Fehlschlag) -- erst wenn
 * er passt, lohnt sich die genauere Pruefung des Push davor. */
static int phFoldPushPop(void) {
	int i, folded = 0;
	for (i = 0; i < phLineCount; i++) {
		/* NICHT "const char *a, *b, *c;" -- mehrere Zeiger-Deklaratoren in
		   EINER Anweisung sind ein stiller QCC-Abbruch (FAIL, 0 Meldungen,
		   08.09.2026 gefunden; unabhaengig von "const", "char *a, *b;"
		   bricht ebenso), noch nicht behoben. Je ein eigener Deklarator. */
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
			/* SRC==DST: Push und Pop heben sich vollstaendig auf, s.
			   Verfeinerung oben -- kein Ersatzbau noetig. */
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

/* Erkennt "move.l SRC,Dn" (Dn eines von d0-d7 -- NIE a0-a6, s. Kommentar
 * am Dateianfang zu Muster zwei/drei), optional mit Label-Vorspann.
 * Liefert SOWOHL SRC (fuer Muster drei) ALS AUCH nur das Zielregister
 * (fuer Muster zwei) -- eine Funktion statt zwei fast identischer. */
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

/* Kein Ersatzbau noetig -- die Move-Zeile bleibt UNVERAENDERT stehen, nur
 * das ueberfluessige TST verschwindet. */
static int phFoldMoveTst(void) {
	int i, folded = 0;
	for (i = 0; i < phLineCount; i++) {
		/* Je ein eigener Deklarator -- s. Kommentar in phFoldPushPop oben. */
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

/* DRITTES MUSTER, s. Kommentar am Dateianfang: "move.l Dn,DST" -- dasselbe
 * Datenregister, das die vorherige Zeile (phMatchMoveIntoDataReg) gerade
 * gefuellt hat. KEIN Label davor zugelassen (koennte Sprungziel sein,
 * dieselbe Vorsicht wie bei phMatchPop). DST reicht bis zum Zeilenende --
 * derselbe Aufbau wie phMatchPop, deshalb direkt an phEmitFused
 * uebergebbar, keine eigene Ersatzbau-Funktion noetig. */
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
		/* Je ein eigener Deklarator -- s. Kommentar in phFoldPushPop oben. */
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
			/* SRC==DST, s. Verfeinerung am Dateianfang. */
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

/* VIERTES MUSTER, s. Kommentar am Dateianfang: "move.l SRC,-(a7)" OHNE
 * Klammer in SRC (kein Seiteneffekt) und OHNE Label (koennte Sprungziel
 * sein). Reine Existenzprobe -- die Zeile wird ersatzlos gestrichen, kein
 * SRC-Ruecktransport noetig. */
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

/* Liest s[0..len) als reine Dezimalzahl (optional ein fuehrendes "-"),
 * kein Zeichen ausser Ziffern erlaubt. Bricht frueh ab, sobald der Wert
 * den MOVEQ-Bereich sicher verlassen hat -- kein Ueberlaufrisiko in v. */
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

/* FUENFTES MUSTER, s. Kommentar am Dateianfang: "move.l #IMM,Dn" mit IMM
 * im MOVEQ-Bereich, optional mit Label-Vorspann (das Label bleibt beim
 * Umbau erhalten -- anders als bei Mustern eins/drei/vier wird hier
 * nichts gestrichen, nur der Mnemonic-Text derselben Zeile ersetzt, das
 * Sprungziel ist also nie in Gefahr). */
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

/* srcPath ist die von emitIR() beschriebene (temporaere, bei -peephole)
   Datei; dstPath die eigentliche Zieldatei -- siehe Kommentar in main()
   zu -peephole: dstPath darf hier zum ERSTEN und einzigen Mal angelegt
   werden (OS-9s I$Create scheitert sonst an einer schon bestehenden
   Datei). srcPath bleibt als .tmp-Datei liegen -- kein Aufrufer in
   dieser Kette raeumt Zwischendateien auf, s. .i/.ir ueberall sonst. */
/* Function: peepholeRun
 * Reads assembly, applies safe local rewrites and writes optimized assembly.
 * Parameters: srcPath Input assembly; dstPath Optimized output assembly.
 * Returns: Nothing; reports I/O failures through the optimizer diagnostic. */
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
	/* ERST NACH der Konvergenz, s. Kommentar am Dateianfang zum fuenften
	   Muster: MOVEQ-Zeilen wuerden Muster zwei/drei ihren Ausloesertext
	   "move.l\t" entziehen. */
	moveqCount = phFoldMoveq();
	total += moveqCount;
	kept = 0;
	for (i = 0; i < phLineCount; i++) if (!phRemoved[i]) kept++;
	phWrite(dstPath);
	fprintf(stderr, "qcc_backend: peephole: %d Optimierungen in %d Durchlaeufen (%d von %d Zeilen, davon %d MOVEQ)\n",
		total, rounds, kept, phLineCount, moveqCount);
}
