/*
 * qr68 -- Q9 68k assembler with OS-9 ROF output
 *
 * Usage: qr68 [options] <input.a> <output.r>
 *
 * Purpose: replace Microware r68. The preprocessor (qcpp) and
 * compiler frontend (QCC) can run on the target; assembler and linker
 * are the remaining foreign stages of the toolchain.
 *
 * Edition history:
 *   2026-09-11  Introduced the English source-header format.
 *
 * ---------------------------------------------------------------------------
 * ROF FORMAT, MEASURED AGAINST THE ORIGINAL (2026-09-03)
 *
 * The only format description is MWOS/APPS/src/osk-disasm (rof.c/rof.h, a
 * disassembler). It differs from r68 in one important detail: the counts
 * (globals, externals, references) are read there as 16-bit words with
 * fread_w, while r68 writes them as 32-bit long words. This was measured
 * with samples containing 0, 1, 2, and 3 symbols. The measured layout is:
 *
 *   Kopf (56 Byte, alles big-endian):
 *     0  sync            4   $DEADFACE
 *     4  ty_lan          2   type/language    (psect parameter 2)
 *     6  att_rev         2   attributes/rev   (psect parameter 3)
 *     8  valid           2   0
 *    10  series          2   assembler id; r68 V2.9.1 writes 249
 *    12  rdate           6   year-1900, month, day, hour, minute, second
 *    18  edition         2   (psect parameter 4)
 *    20  statstorage     4   static data size (vsect)
 *    24  idatsz          4   initialized data size
 *    28  codsz           4   code size
 *    32  stksz           4   (psect parameter 5)
 *    36  code_begin      4   entry point (psect parameter 6)
 *    40  utrap           4   trap entry (psect parameter 7, otherwise -1)
 *    44  remotestatsiz   4
 *    48  remoteidatsiz   4
 *    52  debugsiz        4
 *
 *   danach:
 *     Psect name, NUL-terminated
 *     Number of globals (4)
 *       each global: name (NUL-terminated), type (2), address (4)
 *     Code (codsz bytes), padded by r68 with NOP ($4E71) to a multiple of 4
 *     Initialized data (idatsz bytes)
 *     Number of external names (4)
 *       each name: name (NUL-terminated), reference count (4),
 *                  each reference: type (2), code offset (4)
 *     Number of local references (4)
 *       each reference: type (2), offset (4)
 *     Number of common blocks (4)
 *
 * Measured type words: global code label = $0004; external 32-bit absolute
 * code reference = $0038. Additional cases are measured individually.
 *
 * ---------------------------------------------------------------------------
 * VALIDATION
 *
 * r68 is byte-reproducible except for the six timestamp bytes in the header.
 * qr68 is considered correct when its output matches r68 byte-for-byte except
 * for those bytes (test/difftest.sh). This uses the original assembler as the
 * oracle rather than a separately authored expected output.
 *
 * ---------------------------------------------------------------------------
 * STATUS
 *
 * First stage: framework, symbols, expressions, psect/vsect/ends, dc/ds/align,
 * equ/set, and rts/nop/jsr -- enough to compare the ROF writer with r68. The
 * instruction table is expanded from the corpus (644 handwritten files,
 * 207,847 lines in MWOS + Q9-OS). Unsupported input fails with a diagnostic.
 *
 * Written in the QCC subset used by qcpp (no unions, no "->", no float,
 * literal array sizes, fixed tables instead of malloc), so qr68 can later
 * compile itself.
 */

/* --------------------------------------------------------------- libc ---- */
extern char *fopen(const char *path, const char *mode);
extern int fclose(char *f);
extern int fread(char *buf, int size, int n, char *f);
extern int fwrite(const char *buf, int size, int n, char *f);
extern int printf(const char *fmt, ...);
extern void exit(int code);

/* ------------------------------------------------------------ Limits ----- */
/* Target builds use smaller tables because QCC places zero-initialized
   fields in the module's initialized data area. Non-remote vsect data is
   limited to 64 KiB through d16(a6). These sizes cover hand-written OS-9
   sources; the Q9-OS kernel uses 232 symbols and the largest SDK driver 1428. */
#ifdef _Q9OS
#define QR_POOL     65536
#define QR_POOLHASH  1024
#define QR_PENT      8192
#define QR_SRC     262144
#define QR_SYM       4096
#define QR_SYMHASH   1024
#define QR_CODE    131072
#define QR_IDATA    32768
#define QR_REF       8192
#define QR_MACTEXT  32768
#else
#define QR_POOL    524288
#define QR_POOLHASH  4096
#define QR_PENT     32768
/* Increased from 16 to 32 MiB on 2026-09-07: building QCC as a 68k module
   produces a 20.7 MiB assembly source (277,744 lines), the largest source
   currently passed through qr68. Target sizes below remain unchanged. */
#define QR_SRC   33554432
#define QR_SYM      16384
#define QR_SYMHASH   4096
#define QR_CODE  16777216
#define QR_IDATA  8388608
#define QR_REF      65536
#define QR_MACTEXT 131072
#endif

/* ------------------------------------------------------------- Limits ---- */
/* Array sizes are literals because QCC constSize accepts only numbers; the
   mirror values are checked by selfCheck(). */
static char pool[QR_POOL];
static int POOL_MAX = QR_POOL;
static int poolTop;

/* Name hash table: poolHead[] points to the first chain entry, while
   pentOff/pentNext describe the entries. */
static int POOLHASH_MAX = QR_POOLHASH;
static int poolHead[QR_POOLHASH];
static int PENT_MAX = QR_PENT;
static int pentOff[QR_PENT];
static int pentNext[QR_PENT];
static int pentN;

/* Hand-written sources use less than a quarter of this arena, while QCC
   backend output can reach 18 MiB. A streaming reader is still needed for
   larger inputs. The arena size also affects the later OS-9 module because
   zero-initialized tables become initialized data, so it cannot grow freely. */
static char srcArena[QR_SRC];
static int SRC_MAX = QR_SRC;
static int srcTop;
/* Macro expansion storage occupies the TOP of the same arena and grows
   downward. Source files grow upward; expansions are stack storage released
   on return, so expansion can never overwrite a source file. */
static int expTop;

static int FILE_MAX = 64;
static int flName[64];
static int flStart[64];
static int flEnd[64];
static int flN;

/* Symbols */
static int SYM_MAX = QR_SYM;
static int symName[QR_SYM];
static int symValue[QR_SYM];
static int symSect[QR_SYM];     /* See SECT_* constants. */
static int symDefined[QR_SYM];
static int symGlobal[QR_SYM];
static int symUsed[QR_SYM];
static int symPass[QR_SYM];     /* Pass of the last definition; see ifdef. */
static int SYMHASH_MAX = QR_SYMHASH;
static int symHead[QR_SYMHASH];
static int symNext[QR_SYM];
/* Pool index of the external name bound to this symbol, or -1. "IRQCtrl equ
   u_icr" (MWOS/.../sc68070.a:64) binds a name to an external symbol; every
   use of IRQCtrl must then create a reference to u_icr. Otherwise the ROF
   silently lacks references and the linker never installs the address. */
static int symExt[QR_SYM];
/* "set" symbols: r68 performs EXACTLY ONE measuring pass, and its output pass
   sieht die Werte, wie sie am ENDE dieses ersten Durchlaufs standen.
   Gemessen an "dc.w A / dc.w B / A set B / B set 5": r68 legt $0000 und
   $0005 ab -- A ist beim dc.w noch das, was der erste Durchlauf hinterlassen
   hat (0, denn dort war B noch unbekannt). qr68 misst dagegen so lange, bis
   die LAENGEN stehen, und haette sonst 5. Deshalb wird der Stand nach dem
   ersten Durchlauf festgehalten und vor dem Ausgeben wiederhergestellt.
   Genau daran haengen die Descriptor-Quellen des SDK: "WrtPrecomp set
   Cylnders" steht dort VOR der Makroausdehnung, die Cylnders setzt. */
static int symIsSet[QR_SYM];
static int symSnapVal[QR_SYM];
static int symSnapSect[QR_SYM];
static int symSnapped[QR_SYM];
static int symN;

/* Code output */
static char codeBuf[QR_CODE];
static int CODE_MAX = QR_CODE;
static int codeN;

/* Initialized data (vsect). */
static char idataBuf[QR_IDATA];
static int IDATA_MAX = QR_IDATA;
static int idataN;

/* References to external names and local symbols. */
static int REF_MAX = QR_REF;
static int refName[QR_REF];     /* Name pool index (external), or -1. */
static int refType[QR_REF];
static int refOffs[QR_REF];
static int refLocal[QR_REF];    /* 1 = lokale Referenz (eigenes Symbol) */
static int refDone[QR_REF];     /* Marked while emitting external names. */
static int refN;

static char lxTmp[4096];
static int LXTMP_MAX = 4096;

static char outBuf[8192];
static int outN;
static char *outFp;

/* Section identifiers */
static int SECT_NONE = 0;
static int SECT_CODE = 1;
static int SECT_IDATA = 2;     /* vsect: initialized data */
static int SECT_UDATA = 3;     /* vsect: reserved data (ds) */
static int SECT_ABS = 4;       /* equ/set: absolute value */
static int SECT_EXTERN = 5;
static int SECT_RDATA = 6;     /* vsect remote: reserved remote data (ds) */

/* Psect parameters. */
static int psName;
static int psTyLan;
static int psAttRev;
static int psEdition;
static int psStack;
static int psEntry;
static int psTrap;
static int psSeen;

/* Initialized (dc) and reserved (ds) vsect data each have their OWN address
   space, both starting at zero. Measured with "d1 dc.l / d2 dc.l / u1 ds.b 4 /
   u2 ds.b 4": the addresses are 0,4 and 0,4, with idatsz=8 and statstorage=8. */
static int idataPC;
static int udataPC;
static int statStorage;        /* Reserved size in the vsect. */

/* "vsect remote" -- ein DRITTER Adressraum, ebenfalls ab 0.
 *
 * PURPOSE: a non-remote vsect is addressed through d16(a6) and therefore
 * fits in 64 KB; l68 rejects larger allocations. Remote data is not included
 * in that window. l68 places it in the data area AFTER initialized data.
 * Measured with 8000 bytes of non-remote data, 8 initialized bytes, and
 * 70000 remote bytes: blk -> 0, iblk -> 8000, rblk -> 8008, M$Mem = 78008.
 * The data area is shared; remote changes the order and disables the 64K check.
 *
 * Until 2026-09-07 qr68 silently discarded "remote": ROFs with and without
 * remote were byte-identical and remotestatsiz remained zero. This prevented
 * the data-model change because the result could not pass through l68.
 *
 * In the ROF, the size is stored in remotestatsiz (offset 44); symbols carry
 * type word $0002 (measured: non-remote $0000, initialized $0001, remote $0002). */
static int rdataPC;
static int remoteStatStorage;  /* Reserved size in the remote vsect. */
static int inRemoteVsect;      /* 1 while a "vsect remote" is open. */

/* Fixed timestamp for reproducible output. -fdate= overrides it so the
   differential test can reproduce the timestamp emitted by r68. */
static int dtYear = 126;       /* Year - 1900. */
static int dtMonth = 1;
static int dtDay = 1;
static int dtHour = 0;
static int dtMin = 0;
static int dtSec = 0;

static int optVerbose;
static int optBranch;          /* -b: choose branch sizes explicitly. */
static int optMpu;             /* -m<n>: target CPU. */
static int optMpuSet;          /* 1 = -m<n> was specified. */
static int pass;               /* Pass number, starting at 1. */
static int emitting;           /* 1 = final pass; emit into the buffers. */
static int symMoved;           /* 1 = a value changed during this pass. */

/* The org counter is NOT the location within a section: "org" sets it,
   "do.b/.w/.l" legt darauf Namen ab, "." liest ihn. So beschreiben die
   Definitionsdateien des SDK ihre Strukturen (1593 "do" in 127 Dateien).
   An r68 gemessen: "org 4 / A do.b 1 / B do.w 1 / C do.l 2" ergibt
   A=4, B=6, C=8 -- do.w und do.l richten vorher auf GERADE aus (nicht auf
   ihre eigene Breite), do.b nicht. Und "org" bewegt den Ort im Abschnitt
   at all: after "nop / org 8" the next label is at 2. */
static int orgPC;

/* Current source-line state. */
static int curFile;
static int curLine;
static int curSect;
static int curPC;              /* Offset im aktuellen Abschnitt */
/* Location where the CURRENT statement starts. This is exactly what "*"
   returns for the entire statement: "dc.w *,*,*" at offset 2 produces three
   values of $0002 (measured). Using the advancing location would be wrong
   from the second value onward; syscache.a:383 uses this behavior. */
static int stmtPC;

/* ============================================================= Characters == */
static int isSpaceCh(int c)
{
	if (c == ' ' || c == 9 || c == 11 || c == 12 || c == 13)
		return 1;
	return 0;
}

static int isDigitCh(int c)
{
	if (c >= '0' && c <= '9')
		return 1;
	return 0;
}

static int isAlphaCh(int c)
{
	if (c >= 'a' && c <= 'z')
		return 1;
	if (c >= 'A' && c <= 'Z')
		return 1;
	if (c == '_' || c == '.' || c == '$')
		return 1;
	return 0;
}

static int isSymCh(int c)
{
	if (isAlphaCh(c) || isDigitCh(c))
		return 1;
	return 0;
}

static int lowerCh(int c)
{
	if (c >= 'A' && c <= 'Z')
		return c + 32;
	return c;
}

static int strLen(const char *s)
{
	int n;

	n = 0;
	while (s[n] != 0)
		n++;
	return n;
}

/* ================================================================== Pool == */
static void fatal(const char *msg, const char *detail);

static char *poolAt(int idx)
{
	return &pool[idx];
}

/* Name hash kept small so it remains inexpensive on the 68030. */
static int nameHash(const char *s, int n)
{
	int h;
	int i;

	h = 0;
	for (i = 0; i < n; i++)
		h = h * 31 + (s[i] & 255);
	if (h < 0)
		h = -h;
	return h & (POOLHASH_MAX - 1);
}

static int internN(const char *s, int n)
{
	int j;
	int idx;
	int same;
	int h;
	int e;

	/* Use a hash table: linear search through the name pool was the most
	   expensive part for 12,000 names. Each chain entry stores a pool offset. */
	h = nameHash(s, n);
	e = poolHead[h];
	while (e >= 0) {
		idx = pentOff[e];
		same = 1;
		for (j = 0; j < n; j++) {
			if (pool[idx + j] != s[j]) {
				same = 0;
				j = n;
			}
		}
		if (same && pool[idx + n] == 0)
			return idx;
		e = pentNext[e];
	}
	if (poolTop + n + 1 >= POOL_MAX)
		fatal("Namensspeicher voll (POOL_MAX)", "");
	if (pentN >= PENT_MAX)
		fatal("zu viele Namen (PENT_MAX)", "");
	idx = poolTop;
	for (j = 0; j < n; j++)
		pool[idx + j] = s[j];
	pool[idx + n] = 0;
	poolTop = poolTop + n + 1;
	pentOff[pentN] = idx;
	pentNext[pentN] = poolHead[h];
	poolHead[h] = pentN;
	pentN++;
	return idx;
}

static int intern(const char *s)
{
	return internN(s, strLen(s));
}

/* ============================================================ Diagnosen === */
static void fatal(const char *msg, const char *detail)
{
	const char *fn;

	fn = "<no file>";
	if (curFile >= 0 && curFile < FILE_MAX && flName[curFile] > 0)
		fn = poolAt(flName[curFile]);
	printf("qr68: %s:%d: %s%s\n", fn, curLine, msg, detail);
	exit(1);
}

/* ================================================================ Dateien = */
static int fileLoad(const char *path)
{
	char *fp;
	int got;
	int want;
	int start;
	int id;

	fp = fopen(path, "r");
	if (fp == 0)
		return -1;
	if (flN >= FILE_MAX)
		fatal("zu viele Dateien (FILE_MAX)", "");

	start = srcTop;
	while (1) {
		if (srcTop >= SRC_MAX)
			fatal("Quelltextspeicher voll (SRC_MAX)", "");
		/* Read in chunks rather than requesting the entire remaining arena;
		   a single large fread returned zero on OS-9 (measured with qcpp). */
		want = SRC_MAX - srcTop;
		if (want > 4096)
			want = 4096;
		got = fread(&srcArena[srcTop], 1, want, fp);
		if (got <= 0)
			got = 0;
		srcTop = srcTop + got;
		if (got < want)
			break;
	}
	fclose(fp);

	id = flN;
	flName[id] = intern(path);
	flStart[id] = start;
	flEnd[id] = srcTop;
	flN++;
	if (optVerbose)
		printf("qr68: gelesen: %d Byte aus %s\n", srcTop - start, path);
	return id;
}

	/* Check whether a file is already loaded. Load each file only once even if
   multiple passes reach it through "use", otherwise the source arena grows
   on every pass. */
static int fileFind(const char *path)
{
	int name;
	int i;

	name = intern(path);
	for (i = 0; i < flN; i++) {
		if (flName[i] == name)
			return i;
	}
	return -1;
}

static int fileGet(const char *path)
{
	int id;

	id = fileFind(path);
	if (id >= 0)
		return id;
	return fileLoad(path);
}

/* ================================================================ Symbols = */
/* Symbols are found through their pool index, which is already unique, so a
   hash of that index is sufficient. */
static int symBucket(int name)
{
	int h;

	h = name & (SYMHASH_MAX - 1);
	return h;
}

static int symFind(int name)
{
	int i;

	i = symHead[symBucket(name)];
	while (i >= 0) {
		if (symName[i] == name)
			return i;
		i = symNext[i];
	}
	return -1;
}

static int symIntern(int name)
{
	int s;

	s = symFind(name);
	if (s >= 0)
		return s;
	if (symN >= SYM_MAX)
		fatal("Symboltabelle voll (SYM_MAX)", "");
	s = symN;
	symNext[s] = symHead[symBucket(name)];
	symHead[symBucket(name)] = s;
	symName[s] = name;
	symValue[s] = 0;
	symSect[s] = SECT_NONE;
	symDefined[s] = 0;
	symGlobal[s] = 0;
	symUsed[s] = 0;
	symPass[s] = 0;
	symExt[s] = -1;
	symIsSet[s] = 0;
	symSnapped[s] = 0;
	symN++;
	return s;
}

/* Define or confirm a symbol. With track enabled, changes from the previous
   pass count as movement; addresses remain provisional until stable. Tracking
   is disabled for set because its value may change several times per pass. */
static void symDefine(int name, int value, int sect, int global, int track)
{
	int s;

	s = symIntern(name);
	if (symDefined[s] && pass == 1 && track)
		fatal("Symbol doppelt definiert: ", poolAt(name));
	if (track && symDefined[s] &&
	    (symValue[s] != value || symSect[s] != sect))
		symMoved = 1;
	symValue[s] = value;
	symSect[s] = sect;
	symDefined[s] = 1;
	symPass[s] = pass;
	if (global)
		symGlobal[s] = 1;
}

/* ============================================================= Line reader */
/* Return the logical character: CR, CR+LF and LF are all reported as LF.
   OS-9 text files end in CR; without normalization the complete file becomes
   one line. */
static int rdPeekCh;
static int rdPeekPos;
static int rdPeekLine;
static int rdPos;

static int rdPeek(void)
{
	int end;
	int c;

	end = flEnd[curFile];
	if (rdPos >= end) {
		rdPeekCh = -1;
		rdPeekPos = rdPos;
		rdPeekLine = curLine;
		return -1;
	}
	c = srcArena[rdPos] & 255;
	if (c == 13) {
		rdPeekCh = 10;
		rdPeekPos = rdPos + 1;
		if (rdPeekPos < end && srcArena[rdPeekPos] == 10)
			rdPeekPos = rdPeekPos + 1;
		rdPeekLine = curLine + 1;
		return 10;
	}
	rdPeekCh = c;
	rdPeekPos = rdPos + 1;
	rdPeekLine = curLine;
	if (c == 10)
		rdPeekLine = curLine + 1;
	return c;
}

static void rdTake(void)
{
	rdPos = rdPeekPos;
	curLine = rdPeekLine;
}

/* Include stack for "use". */
static int USE_MAX = 16;
static int useFile[16];
static int usePos[16];
static int useLine[16];
static int useExp[16];         /* Expansion-stack position at entry. */
static int useDepth;

/* Read one line into lxTmp without its newline. Return 0 at the outermost
   end of file; an included-file end silently returns to its includer. */
static int readLine(void)
{
	int n;
	int c;

	n = 0;
	c = rdPeek();
	while (c < 0 && useDepth > 0) {
		useDepth--;
		curFile = useFile[useDepth];
		rdPos = usePos[useDepth];
		curLine = useLine[useDepth];
		expTop = useExp[useDepth];
		c = rdPeek();
	}
	if (c < 0)
		return 0;
	while (1) {
		c = rdPeek();
		if (c < 0)
			break;
		rdTake();
		if (c == 10)
			break;
		if (n + 1 >= LXTMP_MAX)
			fatal("Zeile zu lang (LXTMP_MAX)", "");
		lxTmp[n] = c;
		n++;
	}
	lxTmp[n] = 0;
	return 1;
}

/* ========================================================= Expressions ==== */
/* Microware syntax: $hex, %binary, @octal, 'z' character, and decimal;
   operators + - * / & (and) ! (or) << >>, unary - + ^ (not), and parentheses.
   "*" alone is the current location; "." is the org counter. Measured
   against r68: "^" is unary (not XOR), and "~" is not supported. */
static const char *exP;
static int exSect;             /* Section referenced by the result. */
static int exExtern;           /* External-name pool index, or -1. */
/* The RELOCATABLE TERMS of an expression, with signs. r68 does not resolve
   "PD_PAR-PD_OPT+M$DTyp" nicht auf, sondern legt DREI Referenzen auf
   denselben Offset ab: $0030, $0070 (das $40 heisst "abziehen") und $0030.
   Ebenso ergibt "dc.l EA+EB" zwei Referenzen und "dc.l basis+basis" zwei
   lokale. Nur eine DIFFERENZ zweier moduleigener Groessen rechnet r68 aus
   und gibt gar keine Referenz aus -- auch ueber Abschnittsgrenzen hinweg
   (gemessen an "dc.l dat-basis" mit dat im vsect: Wert 0, keine Referenz).
   Genau davon leben die Indirektionstabellen von QCCs -largedata.

   Terms are stored in three slots: 0 and 1 for instruction operands, and 2
   for the expression currently being evaluated. */
static int TERM_MAX = 8;
static int termSect[24];       /* SECT_CODE/IDATA/UDATA/EXTERN */
static int termName[24];       /* Pool-Index bei EXTERN, sonst -1 */
static int termNeg[24];        /* 1 = wird abgezogen */
static int termN[3];
static int TERM_CUR = 2;       /* Fach des laufenden Ausdrucks */

static void termAdd(int sect, int name)
{
	if (termN[2] >= TERM_MAX)
		fatal("zu viele verschiebbare Anteile in einem Ausdruck (TERM_MAX)",
		      "");
	termSect[16 + termN[2]] = sect;
	termName[16 + termN[2]] = name;
	termNeg[16 + termN[2]] = 0;
	termN[2]++;
}

/* Remove pairs consisting of an added and subtracted module-local term;
   r68 evaluates such differences. External names remain, including EA-EB. */
static void termFold(void)
{
	int i;
	int j;
	int k;

	i = 0;
	while (i < termN[2]) {
		if (termSect[16 + i] == SECT_EXTERN || termNeg[16 + i]) {
			i++;
			continue;
		}
		j = 0;
		while (j < termN[2] &&
		       (termSect[16 + j] == SECT_EXTERN || !termNeg[16 + j]))
			j++;
		if (j >= termN[2]) {
			i++;
			continue;
		}
		/* Remove i and j, starting with the higher index. */
		if (j < i) {
			k = i;
			i = j;
			j = k;
		}
		for (k = j; k + 1 < termN[2]; k++) {
			termSect[16 + k] = termSect[16 + k + 1];
			termName[16 + k] = termName[16 + k + 1];
			termNeg[16 + k] = termNeg[16 + k + 1];
		}
		termN[2]--;
		for (k = i; k + 1 < termN[2]; k++) {
			termSect[16 + k] = termSect[16 + k + 1];
			termName[16 + k] = termName[16 + k + 1];
			termNeg[16 + k] = termNeg[16 + k + 1];
		}
		termN[2]--;
		i = 0;
	}
}

/* 1 means the expression contains a name that cannot be known yet. This is
   possible only on the first pass; later an unknown name is truly external.
   While open, only operand length is counted; value and range checks wait for
   the next pass. */
static int exOpen;

static int exprTop(void);

static void exSkip(void)
{
	while (exP[0] != 0 && isSpaceCh(exP[0] & 255))
		exP = exP + 1;
}

static int exDigitVal(int c, int base)
{
	int v;

	v = -1;
	if (c >= '0' && c <= '9')
		v = c - '0';
	else if (c >= 'a' && c <= 'f')
		v = c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
		v = c - 'A' + 10;
	if (v < 0 || v >= base)
		return -1;
	return v;
}

static int exNumber(int base)
{
	int v;
	int d;

	v = 0;
	d = exDigitVal(exP[0] & 255, base);
	if (d < 0)
		fatal("Zahl erwartet", "");
	while (d >= 0) {
		v = v * base + d;
		exP = exP + 1;
		d = exDigitVal(exP[0] & 255, base);
	}
	return v;
}

/* An expression refers either to nothing (SECT_ABS) or to one relocatable
   section. The operators enforce this: SYM-SYM in one section is absolute,
   while SYM+SYM is not. */
/* Does the subexpression read since mark contain a relocatable term? Such a
   value cannot be multiplied, divided, shifted or bitwise-combined. Fold
   cancellations first because "(*-BaudTabl)/2" is an absolute constant.
   Only the current subexpression counts, not the complete expression. */
static void exNeedAbsSince(int mark, const char *what)
{
	if (exOpen)
		return;
	termFold();
	if (termN[2] > mark)
		fatal("Abschnittsbezug in diesem Ausdruck nicht moeglich: ", what);
}

static int exPrimary(void)
{
	int v;
	int n;
	int name;
	int s;

	exSkip();
	if (exP[0] == 0)
		fatal("Ausdruck bricht ab", "");

	if (exP[0] == '(') {
		exP = exP + 1;
		v = exprTop();
		exSkip();
		if (exP[0] != ')')
			fatal("\")\" fehlt im Ausdruck", "");
		exP = exP + 1;
		return v;
	}
	if (exP[0] == '-') {
		int mark;

		exP = exP + 1;
		mark = termN[2];
		v = -exPrimary();
		exNeedAbsSince(mark, "unaeres Minus");
		return v;
	}
	if (exP[0] == '+') {
		exP = exP + 1;
		return exPrimary();
	}
	if (exP[0] == '^') {
		/* In Microware syntax "^" is unary NOT, not XOR: dc.b ^$0f yields
		   $f0, while $ff^$0f is rejected. r68 does not support "~". */
		{
			int mark;

			exP = exP + 1;
			mark = termN[2];
			v = ~exPrimary();
			exNeedAbsSince(mark, "unaeres Nicht");
		}
		return v;
	}
	if (exP[0] == '$') {
		exP = exP + 1;
		exSect = SECT_ABS;
		return exNumber(16);
	}
	if (exP[0] == '%') {
		exP = exP + 1;
		exSect = SECT_ABS;
		return exNumber(2);
	}
	if (exP[0] == '@') {
		exP = exP + 1;
		exSect = SECT_ABS;
		return exNumber(8);
	}
	if (exP[0] == 39) {
		/* Character constant: 'A' or multiple characters. */
		exP = exP + 1;
		exSect = SECT_ABS;
		v = 0;
		while (exP[0] != 0 && exP[0] != 39) {
			v = (v << 8) | (exP[0] & 255);
			exP = exP + 1;
		}
		if (exP[0] == 39)
			exP = exP + 1;
		return v;
	}
	if (exP[0] == '*') {
		/* Current location in the section. Outside a section it is undefined,
		   matching r68's "undefined org" diagnostic. */
		exP = exP + 1;
		if (curSect == SECT_NONE)
			fatal("\"*\" ausserhalb eines Abschnitts", "");
		exSect = curSect;
		termAdd(curSect, -1);
		return stmtPC;
	}
	if (exP[0] == '.' && !isSymCh(exP[1] & 255)) {
		/* "." is the org counter, not the section location. "SIZE equ ."
		   returns the post-directive value, including inside a psect. */
		exP = exP + 1;
		exSect = SECT_ABS;
		return orgPC;
	}
	if (isDigitCh(exP[0] & 255)) {
		exSect = SECT_ABS;
		/* r68 accepts "0x100" in addition to "$100"; uppercase "0X10" is
		   rejected. */
		if (exP[0] == '0' && exP[1] == 'x') {
			exP = exP + 2;
			return exNumber(16);
		}
		return exNumber(10);
	}

	if (isAlphaCh(exP[0] & 255)) {
		n = 0;
		while (isSymCh(exP[0] & 255)) {
			lxTmp[LXTMP_MAX - 512 + n] = exP[0];
			n++;
			if (n >= 250)
				fatal("Symbolname zu lang", "");
			exP = exP + 1;
		}
		name = internN(&lxTmp[LXTMP_MAX - 512], n);
		s = symIntern(name);
		symUsed[s] = 1;
		if (!symDefined[s]) {
			/* A forward reference is normal on the first pass. If it remains
			   undefined afterwards, it is external and recorded by the caller. */
			exExtern = name;
			exSect = SECT_EXTERN;
			termAdd(SECT_EXTERN, name);
			if (pass == 1)
				exOpen = 1;
			return 0;
		}
		exSect = symSect[s];
		if (exSect == SECT_NONE)
			exSect = SECT_ABS;
		if (exSect == SECT_CODE || exSect == SECT_IDATA ||
		    exSect == SECT_UDATA || exSect == SECT_RDATA) {
			termAdd(exSect, -1);
		} else if (exSect == SECT_EXTERN && symExt[s] >= 0) {
			exExtern = symExt[s];
			termAdd(SECT_EXTERN, symExt[s]);
		}
		return symValue[s];
	}

	fatal("unerwartetes Zeichen im Ausdruck: ", exP);
	return 0;
}

static int exMul(void)
{
	int v;
	int r;
	int mark;

	mark = termN[2];
	v = exPrimary();
	while (1) {
		exSkip();
		if (exP[0] == '*' && exP[1] != 0) {
			exNeedAbsSince(mark, "Multiplikation");
			exP = exP + 1;
			mark = termN[2];
			v = v * exPrimary();
			exNeedAbsSince(mark, "Multiplikation");
			continue;
		}
		if (exP[0] == '/') {
			exNeedAbsSince(mark, "Division");
			exP = exP + 1;
			mark = termN[2];
			r = exPrimary();
			exNeedAbsSince(mark, "Division");
			if (r == 0)
				fatal("Division durch Null im Ausdruck", "");
			v = v / r;
			continue;
		}
		break;
	}
	return v;
}

static int exAdd(void)
{
	int v;
	int r;
	int mark;
	int i;

	v = exMul();
	while (1) {
		exSkip();
		if (exP[0] != '+' && exP[0] != '-')
			break;
		if (exP[0] == '+') {
			exP = exP + 1;
			v = v + exMul();
		} else {
			exP = exP + 1;
			mark = termN[2];
			r = exMul();
			v = v - r;
			/* Every term added on the right contributes negatively. */
			for (i = mark; i < termN[2]; i++)
				termNeg[16 + i] = !termNeg[16 + i];
		}
	}
	return v;
}

static int exShift(void)
{
	int v;
	int mark;

	mark = termN[2];
	v = exAdd();
	while (1) {
		exSkip();
		if (exP[0] == '<' && exP[1] == '<') {
			exNeedAbsSince(mark, "Schiebeoperator");
			exP = exP + 2;
			mark = termN[2];
			v = v << exAdd();
			exNeedAbsSince(mark, "Schiebeoperator");
			continue;
		}
		if (exP[0] == '>' && exP[1] == '>') {
			exNeedAbsSince(mark, "Schiebeoperator");
			exP = exP + 2;
			mark = termN[2];
			v = v >> exAdd();
			exNeedAbsSince(mark, "Schiebeoperator");
			continue;
		}
		break;
	}
	return v;
}

static int exprTop(void)
{
	int v;
	int mark;

	mark = termN[2];
	v = exShift();
	while (1) {
		exSkip();
		if (exP[0] == '&') {
			exNeedAbsSince(mark, "UND-Verknuepfung");
			exP = exP + 1;
			mark = termN[2];
			v = v & exShift();
			exNeedAbsSince(mark, "UND-Verknuepfung");
			continue;
		}
		if (exP[0] == '!' || exP[0] == '|') {
			/* In Microware syntax "!" is bitwise OR; "|" also works. Both
			   forms are accepted because SDK sources use each spelling. */
			exNeedAbsSince(mark, "ODER-Verknuepfung");
			exP = exP + 1;
			mark = termN[2];
			v = v | exShift();
			exNeedAbsSince(mark, "ODER-Verknuepfung");
			continue;
		}
		break;
	}
	return v;
}

/* Evaluate an expression string. exSect/exExtern describe the referenced
   section or external symbol afterwards. */
static int evalExpr(const char *s)
{
	int v;

	exP = s;
	exSect = SECT_ABS;
	exExtern = -1;
	exOpen = 0;
	termN[2] = 0;
	v = exprTop();
	/* The complete expression must be consumed. Without this check an unknown
	   numeric form could silently produce a wrong value. */
	exSkip();
	if (exP[0] != 0)
		fatal("Rest im Ausdruck nicht auswertbar: ", exP);
	termFold();
	/* exSect/exExtern describe the single remaining term, when there is one;
	   branch and PC-relative checks depend on this information. */
	exSect = SECT_ABS;
	exExtern = -1;
	if (termN[2] > 0) {
		exSect = termSect[16];
		if (exSect == SECT_EXTERN)
			exExtern = termName[16];
	}
	return v;
}

/* ================================================================ Ausgabe = */
static void outFlush(void)
{
	if (outN > 0) {
		if (fwrite(outBuf, 1, outN, outFp) != outN)
			fatal("Ausgabe konnte nicht geschrieben werden", "");
		outN = 0;
	}
}

static void outByte(int b)
{
	if (outN >= 8192)
		outFlush();
	outBuf[outN] = b & 255;
	outN++;
}

static void outWord(int w)
{
	outByte(w >> 8);
	outByte(w);
}

static void outLong(int l)
{
	outByte(l >> 24);
	outByte(l >> 16);
	outByte(l >> 8);
	outByte(l);
}

static void outStrZ(const char *s)
{
	int i;

	i = 0;
	while (s[i] != 0) {
		outByte(s[i]);
		i++;
	}
	outByte(0);
}

/* ============================================================ Emit code ==== */
static void emitByte(int b)
{
	/* Count codeN and idataN on every pass, not only while emitting: they are
	   also the current section locations. Store bytes only on the final pass;
	   otherwise addresses after an ends directive would be wrong during sizing. */
	if (curSect == SECT_CODE) {
		if (codeN >= CODE_MAX)
			fatal("Codespeicher voll (CODE_MAX)", "");
		if (emitting)
			codeBuf[codeN] = b & 255;
		codeN++;
	} else if (curSect == SECT_IDATA) {
		if (idataN >= IDATA_MAX)
			fatal("Datenspeicher voll (IDATA_MAX)", "");
		if (emitting)
			idataBuf[idataN] = b & 255;
		idataN++;
		idataPC++;
		curPC = idataPC;
		return;
	} else if (curSect == SECT_UDATA || curSect == SECT_RDATA) {
		fatal("Daten in einem reservierten Abschnitt", "");
	} else {
		fatal("Code oder Daten ohne psect/vsect", "");
	}
	curPC++;
}

static void emitWord(int w)
{
	emitByte(w >> 8);
	emitByte(w);
}

static void emitLong(int l)
{
	emitByte(l >> 24);
	emitByte(l >> 16);
	emitByte(l >> 8);
	emitByte(l);
}

static void addRef(int name, int type, int offs, int local)
{
	if (!emitting)
		return;
	if (refN >= REF_MAX)
		fatal("zu viele Referenzen (REF_MAX)", "");
	refName[refN] = name;
	refType[refN] = type;
	refOffs[refN] = offs;
	refLocal[refN] = local;
	refN++;
}

/* A reference type word consists of three measured parts:
     $20   die Referenz LIEGT im Code (ohne das Bit: in den init. Daten),
     $18/$10/$08   ihr Umfang -- Langwort / Wort / Byte,
     unten der ZIELabschnitt, wie bei den Globalen: Code 4, initialisierte
     Daten 1, reservierte Daten 0; ein externer Name ebenfalls 0.
   Belegt an: dc.l/dc.w/dc.b auf ein Codelabel ($3c/$34/$2c), dc.l auf
   initialisierte ($39) und auf reservierte Daten ($38), dieselben Faelle
   innerhalb des vsect ($1c/$19) und "move.w #dat,d0" ($31). */
static int refTypeFor(int size, int target)
{
	int t;

	t = 0;
	if (curSect == SECT_CODE)
		t = 0x20;
	else if (curSect != SECT_IDATA)
		fatal("Referenz in einem Abschnitt ohne gemessenes Typwort", "");
	if (size == 4)
		t = t | 0x18;
	else if (size == 2)
		t = t | 0x10;
	else if (size == 1)
		t = t | 0x08;
	else
		fatal("innerer Fehler: Referenzumfang", "");
	if (target == SECT_CODE)
		t = t | 4;
	else if (target == SECT_IDATA)
		t = t | 1;
	else if (target == SECT_RDATA)
		t = t | 2;
	else if (target != SECT_UDATA && target != SECT_EXTERN)
		fatal("innerer Fehler: Referenzziel", "");
	return t;
}

/* PC-relative reference to an external name: use the same type word plus
   $80. Gemessen an "bsr fremd", "bra fremd", "beq fremd", "lea fremd(pc),a0"
   und "move.l fremd(pc),d0" -- alle fuenf ergeben $00b0 = $80|$30, also
   relativ, Wortbreite, im Code, Ziel unbekannt. Die kurze Sprungform lehnt
   r68 dabei ab ("illegal external reference"). */
static void refPcExtern(int ext, int size)
{
	addRef(ext, 0x80 | refTypeFor(size, SECT_EXTERN), curPC, 0);
}

/* Add one reference for each relocatable expression term, all at the same
   denselben Ort -- so legt r68 es ab. Der Ort ist die aktuelle Stelle, also
   VOR dem Ablegen der Bytes aufzurufen. "slot" waehlt das Termfach:
   0/1 fuer die Operanden eines Befehls, TERM_CUR fuer den gerade
   ausgewerteten Ausdruck. */
static void refTerms(int slot, int size)
{
	int i;
	int base;
	int t;

	base = slot * TERM_MAX;
	for (i = 0; i < termN[slot]; i++) {
		if (termSect[base + i] == SECT_EXTERN) {
			t = refTypeFor(size, SECT_EXTERN);
			if (termNeg[base + i])
				t = t | 0x40;
			addRef(termName[base + i], t, curPC, 0);
		} else {
			t = refTypeFor(size, termSect[base + i]);
			if (termNeg[base + i])
				t = t | 0x40;
			addRef(-1, t, curPC, 1);
		}
	}
}

/* ============================================================== selfCheck = */
/* Hash tables start empty; empty is represented by -1 because zero is a
   valid entry. */
static void hashInit(void)
{
	int i;

	for (i = 0; i < POOLHASH_MAX; i++)
		poolHead[i] = -1;
	for (i = 0; i < SYMHASH_MAX; i++)
		symHead[i] = -1;
}

static void selfCheck(void)
{
	int slot;

	slot = (int)sizeof(symName) / SYM_MAX;
	if ((int)sizeof(pool) != POOL_MAX)
		fatal("innerer Fehler: POOL_MAX passt nicht zu pool[]", "");
	if ((int)sizeof(srcArena) != SRC_MAX)
		fatal("innerer Fehler: SRC_MAX passt nicht zu srcArena[]", "");
	if ((int)sizeof(codeBuf) != CODE_MAX)
		fatal("innerer Fehler: CODE_MAX passt nicht zu codeBuf[]", "");
	if ((int)sizeof(idataBuf) != IDATA_MAX)
		fatal("innerer Fehler: IDATA_MAX passt nicht zu idataBuf[]", "");
	if ((int)sizeof(symName) != SYM_MAX * slot)
		fatal("innerer Fehler: SYM_MAX passt nicht zu symName[]", "");
	if ((int)sizeof(refName) != REF_MAX * slot)
		fatal("innerer Fehler: REF_MAX passt nicht zu refName[]", "");
	if ((int)sizeof(lxTmp) != LXTMP_MAX)
		fatal("innerer Fehler: LXTMP_MAX passt nicht zu lxTmp[]", "");
	if ((int)sizeof(flName) != FILE_MAX * slot)
		fatal("innerer Fehler: FILE_MAX passt nicht zu flName[]", "");
	if ((int)sizeof(poolHead) != POOLHASH_MAX * slot)
		fatal("innerer Fehler: POOLHASH_MAX passt nicht zu poolHead[]", "");
	if ((int)sizeof(symHead) != SYMHASH_MAX * slot)
		fatal("innerer Fehler: SYMHASH_MAX passt nicht zu symHead[]", "");
	if ((int)sizeof(pentOff) != PENT_MAX * slot)
		fatal("innerer Fehler: PENT_MAX passt nicht zu pentOff[]", "");
	if ((int)sizeof(symNext) != SYM_MAX * slot)
		fatal("innerer Fehler: SYM_MAX passt nicht zu symNext[]", "");
}

/* ========================================================= Line splitting */
/* Microware format: label in column 1, indented mnemonic, then operands and
   Operanden, danach Kommentar. "*" in Spalte 1 ist eine Kommentarzeile.
   Ein Label mit ":" ist GLOBAL -- gemessen an r68: aus "start: rts" wird ein
   Global-Eintrag im ROF, aus "start rts" nicht. */
static char lnLabel[256];
static char lnOp[64];          /* kleingeschrieben -- Befehle sind egal welcher Schreibung */
static char lnOpRaw[64];       /* wie geschrieben -- MAKRONAMEN sind es nicht */
static char lnArg[1024];
static int lnGlobal;

static int splitLine(void)
{
	int i;
	int n;
	int c;
	int inStr;

	lnLabel[0] = 0;
	lnOp[0] = 0;
	lnOpRaw[0] = 0;
	lnArg[0] = 0;
	lnGlobal = 0;

	i = 0;
	c = lxTmp[0] & 255;
	if (c == 0)
		return 0;
	if (c == '*' || c == ';')
		return 0;

	/* Label field. It ends at whitespace OR a colon, including when the
	   auch dann, wenn das Mnemonic OHNE Trennzeichen folgt: in
	   SRC/DEFS/funcs.a:725 steht "DC_GetCluts:do.b 1", und r68 nimmt das
	   an (gemessen: "lab1:nop" ergibt $4e71 mit dem globalen Label lab1
	   auf 0). Ohne diese Regel wurde "DC_GetCluts:do.b" zum Label und
	   "1" zum Mnemonic. */
	if (!isSpaceCh(c)) {
		n = 0;
		while (lxTmp[i] != 0 && !isSpaceCh(lxTmp[i] & 255) &&
		       lxTmp[i] != ':') {
			if (n + 1 >= 256)
				fatal("Label zu lang", "");
			lnLabel[n] = lxTmp[i];
			n++;
			i++;
		}
		lnLabel[n] = 0;
		if (lxTmp[i] == ':') {
			lnGlobal = 1;
			i++;
		}
	}

	/* Mnemonic. */
	while (lxTmp[i] != 0 && isSpaceCh(lxTmp[i] & 255))
		i++;
	n = 0;
	/* The mnemonic ends at whitespace, or directly before an operand when
	   der mit einem Zeichen anfaengt, das in keinem Mnemonic vorkommt.
	   Gemessen, welche das sind:
	     "ifeq(CPUType-SYS360)"  ja   (so steht es im SDK)
	     "move.l(a0),d0"         ja   -> $2010
	     "andi.l#^$ff,d7"        ja   (ROM_CBOOT/sysinit.a:726, MVME172)
	     "moveq#7,d3"            ja
	     "bra.s*+2"              ja   (r68 meldet nur die Sprungweite)
	     "move.l-(a0),d4"        ja
	     "move.l$1234.w,d2"      NEIN -- "$" gehoert noch zum Mnemonic,
	                                    r68 meldet "bad mnemonic"
	   Das "$" ist also KEIN Trennzeichen, die anderen vier schon. */
	while (lxTmp[i] != 0 && !isSpaceCh(lxTmp[i] & 255) &&
	       lxTmp[i] != '(' && lxTmp[i] != '#' && lxTmp[i] != '*' &&
	       lxTmp[i] != '-') {
		if (n + 1 >= 64)
			fatal("Mnemonic zu lang", "");
		lnOpRaw[n] = lxTmp[i];
		lnOp[n] = lowerCh(lxTmp[i] & 255);
		n++;
		i++;
	}
	lnOp[n] = 0;
	lnOpRaw[n] = 0;

	/* Operands end at whitespace except inside quotes ("dc.b \"a b\"" must
	   remain a single field). */
	while (lxTmp[i] != 0 && isSpaceCh(lxTmp[i] & 255))
		i++;
	n = 0;
	inStr = 0;
	while (lxTmp[i] != 0) {
		c = lxTmp[i] & 255;
		if (c == '"' || c == 39) {
			if (inStr == 0)
				inStr = c;
			else if (inStr == c)
				inStr = 0;
		}
		if (inStr == 0 && isSpaceCh(c))
			break;
		if (n + 1 >= 1024)
			fatal("Operandenfeld zu lang", "");
		lnArg[n] = c;
		n++;
		i++;
	}
	lnArg[n] = 0;
	return 1;
}

static int opIs(const char *s)
{
	int i;

	i = 0;
	while (s[i] != 0) {
		if (lnOp[i] != s[i])
			return 0;
		i++;
	}
	if (lnOp[i] != 0)
		return 0;
	return 1;
}

/* Return a mnemonic size suffix ("move.l" -> 'l'), or zero when absent. */
static int opSize(void)
{
	int i;

	i = 0;
	while (lnOp[i] != 0) {
		if (lnOp[i] == '.' && lnOp[i + 1] != 0 && lnOp[i + 2] == 0)
			return lnOp[i + 1];
		i++;
	}
	return 0;
}

/* Copy the mnemonic without its size suffix to buf. */
static void opBase(char *buf)
{
	int i;

	i = 0;
	while (lnOp[i] != 0) {
		if (lnOp[i] == '.' && lnOp[i + 1] != 0 && lnOp[i + 2] == 0)
			break;
		buf[i] = lnOp[i];
		i++;
	}
	buf[i] = 0;
}

static int baseIs(const char *base, const char *s)
{
	int i;

	i = 0;
	while (s[i] != 0) {
		if (base[i] != s[i])
			return 0;
		i++;
	}
	if (base[i] != 0)
		return 0;
	return 1;
}

/* ============================================================== psect ==== */
/* psect name,ty_lan,att_rev,edition,stack,entry[,trapentry]
   The seven values are copied unchanged into the ROF header. If the seventh
   value is absent, utrap is -1 (measured against r68). */
static void doPsect(void)
{
	const char *p;
	int n;
	int vals[8];
	int nv;
	int i;
	int depth;

	if (psSeen)
		fatal("zweites psect", "");
	psSeen = 1;

	p = lnArg;
	n = 0;
	while (p[n] != 0 && p[n] != ',')
		n++;
	psName = internN(p, n);
	if (n == 0)
		fatal("psect ohne Namen", "");

	for (i = 0; i < 8; i++)
		vals[i] = 0;
	nv = 0;
	if (p[n] == ',')
		n++;
	while (p[n] != 0 && nv < 8) {
		int start;

		start = n;
		depth = 0;
		while (p[n] != 0) {
			if (p[n] == '(')
				depth++;
			else if (p[n] == ')')
				depth--;
			else if (p[n] == ',' && depth == 0)
				break;
			n++;
		}
		{
			int k;
			int len;

			len = n - start;
			for (k = 0; k < len; k++)
				lxTmp[LXTMP_MAX - 1024 + k] = p[start + k];
			lxTmp[LXTMP_MAX - 1024 + len] = 0;
		}
		vals[nv] = evalExpr(&lxTmp[LXTMP_MAX - 1024]);
		nv++;
		if (p[n] == ',')
			n++;
	}

	psTyLan = vals[0];
	psAttRev = vals[1];
	psEdition = vals[2];
	psStack = vals[3];
	psEntry = vals[4];
	if (nv >= 6)
		psTrap = vals[5];
	else
		psTrap = -1;

	curSect = SECT_CODE;
	curPC = 0;
}

/* ========================================================== dc / ds ====== */
/* r68 aligns objects at least one word wide to an even address. Measured with
   "dc.b 1,2,3 / nop": nop starts at 4 and the fill byte is at 3. */
static void alignEven(void)
{
	if (curSect == SECT_RDATA) {
		if ((rdataPC % 2) != 0) {
			rdataPC++;
			if (rdataPC > remoteStatStorage)
				remoteStatStorage = rdataPC;
			curPC = rdataPC;
		}
		return;
	}
	if (curSect == SECT_UDATA) {
		/* Reserved data is counted but not emitted. */
		if ((udataPC % 2) != 0) {
			udataPC++;
			if (udataPC > statStorage)
				statStorage = udataPC;
			curPC = udataPC;
		}
		return;
	}
	if (curSect != SECT_CODE && curSect != SECT_IDATA)
		return;
	if ((curPC % 2) != 0)
		emitByte(0);
}

static void doDc(int size)
{
	const char *p;
	int i;
	int v;
	int depth;
	int start;
	int len;
	int k;
	int q;

	p = lnArg;
	i = 0;
	if (p[0] == 0)
		fatal("dc ohne Operanden", "");

	while (p[i] != 0) {
		/* A string is recognized only inside double quotes. A character
		   literal is a NUMBER, not text; measured against r68,
		   lehnt "dc.b 'abc'" mit "value out of range" ab (es ist der
		   Wert $616263), waehrend "dc.b \"a\"+1" mit "bad operand"
		   abgelehnt wird -- auf einen Text kann man nicht rechnen.
		   Werte: 'a' -> $61, 'ab' -> $6162, 'abcd' -> $61626364,
		   und gerechnet werden darf: ')'+$80 -> $a9, 2*'a' -> $c2,
		   'a'&$0f -> $01. Genau davon lebt
		   RBF/DRVR/RAMDISK/ram.a:145 (dc.b "...Volatile",')'+$80) --
		   als Text gelesen kamen dort zwei Bytes zu viel heraus. */
		if (p[i] == '"') {
			i++;
			while (p[i] != 0 && p[i] != '"') {
				emitByte(p[i] & 255);
				i++;
			}
			if (p[i] == '"')
				i++;
			if (p[i] == ',')
				i++;
			continue;
		}
		start = i;
		depth = 0;
		q = 0;
		while (p[i] != 0) {
			if (q != 0) {
				/* A comma inside quotes is not a separator;
				   "dc.b ','+1" is one operand. */
				if (p[i] == q)
					q = 0;
			} else if (p[i] == '"' || p[i] == 39) {
				q = p[i];
			} else if (p[i] == '(') {
				depth++;
			} else if (p[i] == ')') {
				depth--;
			} else if (p[i] == ',' && depth == 0) {
				break;
			}
			i++;
		}
		len = i - start;
		for (k = 0; k < len; k++)
			lxTmp[LXTMP_MAX - 2048 + k] = p[start + k];
		lxTmp[LXTMP_MAX - 2048 + len] = 0;
		v = evalExpr(&lxTmp[LXTMP_MAX - 2048]);

		if (size == 'b') {
			refTerms(TERM_CUR, 1);
			emitByte(v);
		} else if (size == 'w') {
			alignEven();
			refTerms(TERM_CUR, 2);
			emitWord(v);
		} else {
			alignEven();
			refTerms(TERM_CUR, 4);
			emitLong(v);
		}
		if (p[i] == ',')
			i++;
	}
}

static void doDs(int size)
{
	int count;
	int bytes;

	count = evalExpr(lnArg);
	bytes = count;
	if (size == 'w')
		bytes = count * 2;
	else if (size == 'l')
		bytes = count * 4;

	if (curSect == SECT_RDATA) {
		rdataPC = rdataPC + bytes;
		if (rdataPC > remoteStatStorage)
			remoteStatStorage = rdataPC;
		curPC = rdataPC;
		return;
	}
	if (curSect == SECT_UDATA) {
		udataPC = udataPC + bytes;
		if (udataPC > statStorage)
			statStorage = udataPC;
		curPC = udataPC;
		return;
	}
	if (curSect == SECT_CODE) {
		int i;

		for (i = 0; i < bytes; i++)
			emitByte(0);
		return;
	}
	fatal("ds ohne Abschnitt", "");
}

/* Measured behavior: CODE is padded with a no-op. A preceding odd byte is
   padded with zero because the no-op is a word. Initialized data is padded
   with zero. Without -m, or below -m2, use NOP ($4E71); from -m2 onward use
   TRAPF ($51FC), the 68020 two-byte no-op. */
static int fillWord(void)
{
	if (optMpuSet && optMpu >= 2)
		return 0x51FC;
	return 0x4E71;
}

/* "do.b/.w/.l [count]" places a name at the org counter and advances it.
   Return the address assigned to the line's label. */
static int doDo(int size)
{
	int count;
	int w;
	int at;

	if (size == 0)
		size = 'w';
	w = 1;
	if (size == 'w')
		w = 2;
	else if (size == 'l')
		w = 4;
	else if (size != 'b')
		fatal("do kennt nur .b, .w und .l: ", lnOp);
	if (w > 1 && (orgPC % 2) != 0)
		orgPC++;
	count = 1;
	if (lnArg[0] != 0)
		count = evalExpr(lnArg);
	at = orgPC;
	orgPC = orgPC + count * w;
	return at;
}

static void doAlign(void)
{
	int a;

	a = 2;
	if (lnArg[0] != 0)
		a = evalExpr(lnArg);
	if (a < 1)
		fatal("align mit ungueltiger Groesse", "");
	if (curSect == SECT_RDATA) {
		while ((rdataPC % a) != 0)
			rdataPC++;
		if (rdataPC > remoteStatStorage)
			remoteStatStorage = rdataPC;
		curPC = rdataPC;
		return;
	}
	if (curSect == SECT_UDATA) {
		/* Reserved data is counted but not emitted. */
		while ((udataPC % a) != 0)
			udataPC++;
		if (udataPC > statStorage)
			statStorage = udataPC;
		curPC = udataPC;
		return;
	}
	if (curSect == SECT_CODE) {
		if ((curPC % a) != 0 && (curPC % 2) != 0)
			emitByte(0);
		while ((curPC % a) != 0)
			emitWord(fillWord());
		return;
	}
	while ((curPC % a) != 0)
		emitByte(0);
}

/* =========================================================== Operands ==== */
/* Addressing modes as an internal model. The number is NOT the instruction
   mode field; eaModeBits()/eaRegBits() provide that. Measured against r68:
   - r68 does NOT accept the parenthesized form "(4,a5)" ("parenthesis
     needed"); use "4(a5)";
   - a bare expression is ALWAYS absolute long, even if it fits in 16 bits
     ("move.l $1000,d0" -> 2039); ".w" on the operand forces short;
   - "0(a5)" remains displacement form (41ed 0000); only "(a5)" is indirect.
     r68 does not shorten this form. */
static int AM_DN = 0;
static int AM_AN = 1;
static int AM_IND = 2;
static int AM_POST = 3;
static int AM_PRE = 4;
static int AM_DISP = 5;
static int AM_IDX = 6;
static int AM_ABSW = 7;
static int AM_ABSL = 8;
static int AM_PCD = 9;
static int AM_PCIDX = 10;
static int AM_IMM = 11;

static int oMode[2];
static int oReg[2];
static int oVal[2];
static int oSect[2];
static int oExt[2];
static int oIdx[2];            /* 0..7 = dN, 8..15 = aN, -1 = keiner */
static int oIdxL[2];           /* 1 = .l, 0 = .w */
static int oIdxScale[2];       /* Zweierlogarithmus der Skalierung: 0..3 */
static int oOpen[2];           /* 1 = Wert im ersten Durchlauf noch offen */
static int oN;                 /* Zahl der Operanden dieser Zeile */

static char opTxt0[512];
static char opTxt1[512];
/* The third operand exists only for pack/unpk and cas. It has NO slot in
   KEINEN Platz in oMode[]/oReg[]/... -- deren Index 2 gehoert dem
   Zwischenspeicher des Ausdrucksauswerters (termN[2]). Die drei Befehle
   kommen ohne aus: bei pack/unpk ist der dritte Operand ein reiner
   Sofortwert, bei cas sind die ersten beiden blosse Datenregister, sodass
   der dritte in Fach 0 geparst werden kann. */
static char opTxt2[512];
/* A fourth operand is needed only by ptest ("ptestr #0,(a1),#3,a2"). */
static char opTxt3[512];
static char exBuf[1024];

/* Copy s[from..to) to exBuf. */
static void subStr(const char *s, int from, int to)
{
	int i;

	if (to - from >= 1024)
		fatal("Teilausdruck zu lang", "");
	for (i = from; i < to; i++)
		exBuf[i - from] = s[i];
	exBuf[to - from] = 0;
}

/* "d3" -> 3, "a3" and "sp" -> 8+3, otherwise -1. */
static int regNum(const char *s, int n)
{
	int c0;
	int c1;

	if (n != 2)
		return -1;
	c0 = lowerCh(s[0] & 255);
	c1 = lowerCh(s[1] & 255);
	if (c1 >= '0' && c1 <= '7') {
		if (c0 == 'd')
			return c1 - '0';
		if (c0 == 'a')
			return 8 + c1 - '0';
	}
	if (c0 == 's' && c1 == 'p')
		return 8 + 7;
	return -1;
}

static void putOperand(int k, int from, int to)
{
	int i;
	int len;
	char *d;

	len = to - from;
	if (len >= 512)
		fatal("Operand zu lang: ", lnArg);
	if (k == 0)
		d = opTxt0;
	else if (k == 1)
		d = opTxt1;
	else if (k == 2)
		d = opTxt2;
	else if (k == 3)
		d = opTxt3;
	else
		fatal("mehr als vier Operanden: ", lnArg);
	for (i = 0; i < len; i++)
		d[i] = lnArg[from + i];
	d[len] = 0;
}

/* Split the operand field at top-level commas. Parentheses and quotes count;
   "move.b #',',d0" has two operands. */
static void splitOperands(void)
{
	int i;
	int n;
	int depth;
	int q;
	int start;
	int c;

	oN = 0;
	opTxt0[0] = 0;
	opTxt1[0] = 0;
	opTxt2[0] = 0;
	opTxt3[0] = 0;
	n = strLen(lnArg);
	if (n == 0)
		return;
	start = 0;
	depth = 0;
	q = 0;
	for (i = 0; i <= n; i++) {
		c = 0;
		if (i < n)
			c = lnArg[i] & 255;
		if (i < n && q != 0) {
			if (c == q)
				q = 0;
			continue;
		}
		if (i < n && (c == '"' || c == 39)) {
			q = c;
			continue;
		}
		if (i < n && c == '(') {
			depth++;
			continue;
		}
		if (i < n && c == ')') {
			depth--;
			continue;
		}
		if (i == n || (c == ',' && depth == 0)) {
			putOperand(oN, start, i);
			oN++;
			start = i + 1;
		}
	}
}

/* Copy the terms of the last evaluated expression into an operand slot;
   otherwise the second operand would overwrite them. */
static void termCopy(int k)
{
	int i;

	termN[k] = termN[2];
	for (i = 0; i < termN[2]; i++) {
		termSect[k * TERM_MAX + i] = termSect[16 + i];
		termName[k * TERM_MAX + i] = termName[16 + i];
		termNeg[k * TERM_MAX + i] = termNeg[16 + i];
	}
}

static void parseOperand(const char *s, int k)
{
	int n;
	int i;
	int c;
	int depth;
	int lp;
	int post;
	int ie;
	int comma;
	int blen;
	int r;
	int isPc;
	int found;
	int e;

	oMode[k] = -1;
	oReg[k] = 0;
	oVal[k] = 0;
	oSect[k] = SECT_ABS;
	oExt[k] = -1;
	oOpen[k] = 0;
	termN[k] = 0;
	oIdx[k] = -1;
	oIdxL[k] = 1;
	oIdxScale[k] = 0;

	n = strLen(s);
	if (n == 0)
		fatal("leerer Operand in: ", lnOp);

	if (s[0] == '#') {
		subStr(s, 1, n);
		oVal[k] = evalExpr(exBuf);
		oSect[k] = exSect;
		oExt[k] = exExtern;
		oOpen[k] = exOpen;
		termCopy(k);
		oMode[k] = AM_IMM;
		return;
	}
	r = regNum(s, n);
	if (r >= 0) {
		if (r < 8) {
			oMode[k] = AM_DN;
			oReg[k] = r;
		} else {
			oMode[k] = AM_AN;
			oReg[k] = r - 8;
		}
		return;
	}
	if (n >= 5 && s[0] == '-' && s[1] == '(' && s[n - 1] == ')') {
		r = regNum(&s[2], n - 3);
		if (r >= 8) {
			oMode[k] = AM_PRE;
			oReg[k] = r - 8;
			return;
		}
	}

	/* Parenthesized form: the opening parenthesis matching the final ")"
	   separates displacement from base register. */
	post = 0;
	ie = n;
	if (n >= 4 && s[n - 1] == '+' && s[n - 2] == ')') {
		post = 1;
		ie = n - 1;
	}
	lp = -1;
	if (ie >= 2 && s[ie - 1] == ')') {
		depth = 0;
		found = 0;
		i = ie - 1;
		while (i >= 0 && !found) {
			c = s[i] & 255;
			if (c == ')') {
				depth++;
			} else if (c == '(') {
				depth--;
				if (depth == 0) {
					lp = i;
					found = 1;
				}
			}
			i--;
		}
	}
	if (lp >= 0) {
		comma = -1;
		depth = 0;
		for (i = lp + 1; i < ie - 1; i++) {
			c = s[i] & 255;
			if (c == '(')
				depth++;
			else if (c == ')')
				depth--;
			else if (c == ',' && depth == 0) {
				comma = i;
				break;
			}
		}
		if (comma >= 0)
			blen = comma - (lp + 1);
		else
			blen = (ie - 1) - (lp + 1);
		r = regNum(&s[lp + 1], blen);
		isPc = 0;
		if (blen >= 2 && lowerCh(s[lp + 1] & 255) == 'p' &&
		    lowerCh(s[lp + 2] & 255) == 'c') {
			if (blen == 2)
				isPc = 1;
			/* "pcr" is Microware's second spelling and behaves identically;
			   both "target(pc)" and "target(pcr)" measure from the
			   extension word. */
			else if (blen == 3 && lowerCh(s[lp + 3] & 255) == 'r')
				isPc = 1;
		}
		if (r >= 8 || isPc) {
			if (!isPc)
				oReg[k] = r - 8;
			if (lp > 0) {
				subStr(s, 0, lp);
				oVal[k] = evalExpr(exBuf);
				oSect[k] = exSect;
				oExt[k] = exExtern;
				oOpen[k] = exOpen;
				termCopy(k);
			}
			if (comma >= 0) {
				int il;
				int ilen;

				ilen = (ie - 1) - (comma + 1);
				/* Scaling by 1, 2, 4, or 8 follows the width
				   der Breite ("d0.l*4"). r68 nimmt sie erst ab
				   -m2 an, darunter meldet es "illegal addressing
				   mode" (gemessen ueber -m0..-m6). Im Korpus
				   steht sie in SRC/IO/SCF/DRVR/sc68360.a und in
				   den CPU32-Ports, die mit -m2 gebaut werden. */
				if (ilen >= 2 && s[comma + ilen - 1] == '*') {
					c = s[comma + 1 + ilen - 1] & 255;
					if (c == '1')
						oIdxScale[k] = 0;
					else if (c == '2')
						oIdxScale[k] = 1;
					else if (c == '4')
						oIdxScale[k] = 2;
					else if (c == '8')
						oIdxScale[k] = 3;
					else
						fatal("Skalierung weder *1, *2, *4 noch *8: ", s);
					if (!(optMpuSet && optMpu >= 2))
						fatal("skalierter Index erst ab -m2 -- r68 V2.9.1 lehnt ihn darunter ab: ", s);
					ilen = ilen - 2;
				}
				if (ilen > 2 && s[comma + 1 + ilen - 2] == '.') {
					c = lowerCh(s[comma + ilen] & 255);
					if (c == 'w')
						oIdxL[k] = 0;
					else if (c == 'l')
						oIdxL[k] = 1;
					else
						fatal("Indexbreite weder .w noch .l: ", s);
					ilen = ilen - 2;
				}
				il = regNum(&s[comma + 1], ilen);
				if (il < 0)
					fatal("Indexregister nicht erkannt: ", s);
				oIdx[k] = il;
				oMode[k] = AM_IDX;
				if (isPc)
					oMode[k] = AM_PCIDX;
				if (post)
					fatal("\"+\" an einer Indexform: ", s);
				return;
			}
			if (post) {
				if (isPc || lp != 0)
					fatal("Postinkrement nur als \"(aN)+\": ", s);
				oMode[k] = AM_POST;
				return;
			}
			if (isPc) {
				oMode[k] = AM_PCD;
				return;
			}
			if (lp == 0) {
				oMode[k] = AM_IND;
				return;
			}
			oMode[k] = AM_DISP;
			return;
		}
		/* Sonst war die Klammer Teil des Ausdrucks -- faellt durch. */
	}
	if (post)
		fatal("\"+\" ohne Klammerform: ", s);

	/* Bare expression: absolute. Without a suffix r68 ALWAYS selects long
	   Form, ".w" waehlt die kurze. (Ein Symbol, das selbst auf ".w" oder
	   ".l" endet, wird hier als Groessenangabe gelesen -- dieselbe
	   Zweideutigkeit hat r68.) */
	e = n;
	oMode[k] = AM_ABSL;
	if (n > 2 && s[n - 2] == '.') {
		c = lowerCh(s[n - 1] & 255);
		if (c == 'w') {
			oMode[k] = AM_ABSW;
			e = n - 2;
		} else if (c == 'l') {
			e = n - 2;
		}
	}
	subStr(s, 0, e);
	oVal[k] = evalExpr(exBuf);
	oSect[k] = exSect;
	oExt[k] = exExtern;
	oOpen[k] = exOpen;
	termCopy(k);
}

static int eaModeBits(int k)
{
	int m;

	m = oMode[k];
	if (m == AM_DN)
		return 0;
	if (m == AM_AN)
		return 1;
	if (m == AM_IND)
		return 2;
	if (m == AM_POST)
		return 3;
	if (m == AM_PRE)
		return 4;
	if (m == AM_DISP)
		return 5;
	if (m == AM_IDX)
		return 6;
	return 7;
}

static int eaRegBits(int k)
{
	int m;

	m = oMode[k];
	if (m >= AM_DN && m <= AM_IDX)
		return oReg[k];
	if (m == AM_ABSW)
		return 0;
	if (m == AM_ABSL)
		return 1;
	if (m == AM_PCD)
		return 2;
	if (m == AM_PCIDX)
		return 3;
	return 4;                      /* AM_IMM */
}

static int eaBits(int k)
{
	return (eaModeBits(k) << 3) | eaRegBits(k);
}

/* Emit an operand's extension words in r68 order: source first, then target.
   "size" is the instruction size and matters only for immediate operands. */
static void emitEa(int k, int size)
{
	int m;
	int d;

	m = oMode[k];
	if (m == AM_DN || m == AM_AN || m == AM_IND || m == AM_POST ||
	    m == AM_PRE)
		return;
	if (oOpen[k]) {
		/* First pass with an unknown name: count length only. Value, section,
		   and range checks are performed on later passes. */
		if (m == AM_ABSL || (m == AM_IMM && size == 4))
			emitLong(0);
		else
			emitWord(0);
		return;
	}
	if (m == AM_DISP) {
		/* A relocatable displacement is normal: this is how the OS-9 C ABI
		   OS-9-C-ABI ueber a6 auf die eigenen Daten zu
		   ("move.l #x,_stklimit(a6)"). Gemessen: Referenz mit
		   $30|Zielabschnitt am Displacementwort -- $0030 fuer
		   reservierte Daten, $0031 fuer initialisierte, $0030 fuer
		   einen externen Namen. */
		refTerms(k, 2);
		if (oVal[k] < -32768 || oVal[k] > 32767)
			fatal("Displacement passt nicht in 16 Bit: ", lnArg);
		emitWord(oVal[k]);
		return;
	}
	if (m == AM_IDX || m == AM_PCIDX) {
		d = oVal[k];
		if (m == AM_IDX && termN[k] > 0) {
			/* A relocatable displacement is also possible here:
			   "move.b d0,dat(a2,d5.w)" ergibt eine BYTEreferenz
			   auf das niederwertige Byte des Erweiterungswortes
			   ($0028 auf Offset 9, gemessen). So greift
			   PORTS/CB030/SCF/oxc16954.a auf seine Daten zu. */
			if (d < -128 || d > 127)
				fatal("Index-Displacement passt nicht in 8 Bit: ",
				      lnArg);
			emitByte(((oIdx[k] & 15) << 4) | (oIdxL[k] << 3) |
				 (oIdxScale[k] << 1));
			refTerms(k, 1);
			emitByte(d & 255);
			return;
		}
		if (oExt[k] >= 0)
			fatal("externer Name in einer Indexform: ", lnArg);
		if (m == AM_PCIDX) {
			if (oSect[k] != SECT_CODE && oSect[k] != SECT_ABS)
				fatal("PC-Bezug auf einen anderen Abschnitt: ", lnArg);
			/* Only a reference to a code location is calculated; a fixed value
			   is emitted directly (see AM_PCD). */
			if (termN[k] > 0)
				d = d - curPC;
		} else if (oSect[k] != SECT_ABS) {
			fatal("verschiebbares Displacement in einer Indexform: ",
			      lnArg);
		}
		if (d < -128 || d > 127)
			fatal("Index-Displacement passt nicht in 8 Bit: ", lnArg);
		emitWord(((oIdx[k] & 15) << 12) | (oIdxL[k] << 11) |
			 (oIdxScale[k] << 9) | (d & 255));
		return;
	}
	if (m == AM_PCD) {
		/* PC-relative reference to a local code location: the distance is fixed,
		   der Binder braucht dafuer KEINE Referenz (gemessen an
		   "lea start(pc),a3" -- im ROF steht dazu nichts). Auf einen
		   externen Namen dagegen schon -- und im Displacementwort steht
		   dann der KONSTANTE Anteil des Ausdrucks, nicht 0:
		   "fremd(pc)" -> $0000, "fremd+4(pc)" -> $0004,
		   "fremd-8(pc)" -> $fff8, "lea fremd+2(pc),a0" -> $0002 (alle
		   mit Referenztyp $00b0). Daran hing das zweite Langwort der
		   Vektortabelle in ROM_CBOOT/sysinit.a des Ports MVME147:
		   "move.l VectTbl+4(pc),4(a0)". */
		if (oExt[k] >= 0) {
			refPcExtern(oExt[k], 2);
			if (oVal[k] < -32768 || oVal[k] > 32767)
				fatal("PC-Abstand passt nicht in 16 Bit: ", lnArg);
			emitWord(oVal[k]);
			return;
		}
		if (oSect[k] != SECT_CODE && oSect[k] != SECT_ABS)
			fatal("PC-Bezug auf einen anderen Abschnitt: ", lnArg);
		/* Zeigt der Ausdruck auf eine CODESTELLE, rechnet r68 den
		   Abstand aus ("lea ziel(pc),a1" -> $ffec). Ist er dagegen ein
		   fester Wert, steht er UNVERAENDERT als Abstand drin:
		   "jmp 3(pc)" ergibt $0003, ebenso "lea WERT(pc),a2" mit
		   "WERT equ 6" -> $0006. Fuer "pc" und "pcr" gleichermassen --
		   den Unterschied macht der Ausdruck, nicht die Schreibweise.
		   In SYSMODS/GCLOCK/tickgeneric.a:188 steht "jmp 3(pc)". */
		d = oVal[k];
		if (termN[k] > 0)
			d = d - curPC;
		if (d < -32768 || d > 32767)
			fatal("PC-Abstand passt nicht in 16 Bit: ", lnArg);
		emitWord(d);
		return;
	}
	if (m == AM_ABSW) {
		refTerms(k, 2);
		emitWord(oVal[k]);
		return;
	}
	if (m == AM_ABSL) {
		refTerms(k, 4);
		emitLong(oVal[k]);
		return;
	}
	/* AM_IMM */
	if (size == 1) {
		/* Ein Byte-Sofortwert steht im NIEDERWERTIGEN Byte des
		   Erweiterungswortes, und eine Referenz darauf ist bytegross
		   und zeigt genau dorthin: "move.b #fremd,d0" ergibt $0028 auf
		   Offset 3 (gemessen). Die I-Formen machen es anders, die
		   bekommen deshalb von ihrer Aufrufstelle die Wortbreite --
		   "cmpi.b #-1,d0" legt $ffff ab, "move.b #-1,d0" nur $00ff. */
		emitByte(0);
		refTerms(k, 1);
		emitByte(oVal[k] & 255);
		return;
	}
	if (size == 2) {
		refTerms(k, 2);
		emitWord(oVal[k]);
		return;
	}
	refTerms(k, 4);
	emitLong(oVal[k]);
}

/* ================================================================== use == */
/* Measured against r68; intuition would suggest a different behavior:
     use datei.a     und   use "datei.a"   -> genau dieser Pfad, also
                                              relativ zum ARBEITSverzeichnis
                                              (NICHT zum Verzeichnis der
                                              einschliessenden Datei -- eine
                                              Datei in sub/ findet ihren
                                              Nachbarn nicht),
     use <datei.a>                         -> die mit -u= angegebenen
                                              Verzeichnisse.
   r68 nimmt bei <> zusaetzlich ein festes <MWOS>/OS9/SRC/DEFS. Das haengt an
   einer Umgebungsvariablen, die qr68 nicht liest -- dieses Verzeichnis muss
   man ihm also mit -u= nennen. */
static int USEDIR_MAX = 16;
static int useDirs[16];
static int useDirN;

/* Command-line symbols (-a). They are set at the start of EVERY pass so
   "ifdef" can see them. */
static int ARGDEF_MAX = 32;
static int argDefName[32];
static int argDefVal[32];
static int argDefN;

static char pathBuf[512];
static char dirBuf[512];

static void pathJoin(const char *dir, const char *name)
{
	int i;
	int n;

	n = 0;
	i = 0;
	while (dir[i] != 0) {
		if (n + 2 >= 512)
			fatal("Pfad zu lang: ", name);
		pathBuf[n] = dir[i];
		n++;
		i++;
	}
	if (n > 0 && pathBuf[n - 1] != '/') {
		pathBuf[n] = '/';
		n++;
	}
	i = 0;
	while (name[i] != 0) {
		if (n + 1 >= 512)
			fatal("Pfad zu lang: ", name);
		pathBuf[n] = name[i];
		n++;
		i++;
	}
	pathBuf[n] = 0;
}

static void doUse(void)
{
	int n;
	int i;
	int angled;
	int quoted;
	int id;
	int from;

	n = strLen(lnArg);
	if (n == 0)
		fatal("use ohne Dateinamen", "");
	angled = 0;
	quoted = 0;
	from = 0;
	/* Remove the closing character only when it is present:
	   im SDK steht "use <memc040.d)" (Tippfehler in systype.d), und r68
	   uebersetzt die Datei damit anstandslos. */
	if (lnArg[0] == '<') {
		angled = 1;
		from = 1;
		if (lnArg[n - 1] == '>' || lnArg[n - 1] == ')')
			n = n - 1;
	} else if (lnArg[0] == '"' && lnArg[n - 1] == '"') {
		quoted = 1;
		from = 1;
		n = n - 1;
	}
	for (i = from; i < n; i++)
		lxTmp[LXTMP_MAX - 3072 + i - from] = lnArg[i];
	lxTmp[LXTMP_MAX - 3072 + n - from] = 0;

	id = -1;
	if (quoted) {
		/* Quoted form searches the directory of the
		   EINSCHLIESSENDEN Datei -- gemessen: "use \"nachbar.a\""
		   findet den Nachbarn auch dann, wenn das Arbeitsverzeichnis
		   woanders liegt, waehrend das nackte "use nachbar.a" es
		   nicht tut. Genau darauf bauen die Descriptor-Quellen des
		   SDK ("use \"scfdesc.a\"" in SRC/IO/SCF/DESC/p1.a).
		   Findet sich dort nichts, wird das Arbeitsverzeichnis
		   versucht -- r68 nennt in seiner Fehlermeldung ".\name". */
		int cut;
		int k;
		const char *fn;

		fn = poolAt(flName[curFile]);
		cut = -1;
		for (k = 0; fn[k] != 0; k++) {
			if (fn[k] == '/')
				cut = k;
		}
		if (cut >= 0) {
			for (k = 0; k < cut; k++)
				dirBuf[k] = fn[k];
			dirBuf[cut] = 0;
			pathJoin(dirBuf, &lxTmp[LXTMP_MAX - 3072]);
			id = fileGet(pathBuf);
		}
	}
	if (id < 0 && angled) {
		for (i = 0; i < useDirN && id < 0; i++) {
			pathJoin(poolAt(useDirs[i]), &lxTmp[LXTMP_MAX - 3072]);
			id = fileGet(pathBuf);
		}
		if (id < 0)
			fatal("use: Datei in keinem -u=-Verzeichnis gefunden: ",
			      &lxTmp[LXTMP_MAX - 3072]);
	}
	if (id < 0) {
		id = fileGet(&lxTmp[LXTMP_MAX - 3072]);
		if (id < 0)
			fatal("use: Datei nicht lesbar: ",
			      &lxTmp[LXTMP_MAX - 3072]);
	}

	if (useDepth >= USE_MAX)
		fatal("use zu tief geschachtelt (USE_MAX): ", lnArg);
	useFile[useDepth] = curFile;
	usePos[useDepth] = rdPos;
	useLine[useDepth] = curLine;
	useExp[useDepth] = expTop;
	useDepth++;
	curFile = id;
	rdPos = flStart[id];
	curLine = 1;
}

/* ================================================================ Macros == */
/* Measured against r68 (option -x shows expansion in the listing):
     NAME macro / ... / endm     -- der Name steht im LABELfeld,
     \1 .. \9   die Argumente, TEXTUELL ersetzt, auch innerhalb von
                Anfuehrungszeichen ("dc.b \"\\5\",0" im SDK),
     \#         die Zahl der Argumente, ZWEISTELLIG dezimal ("03"),
     \@         eine laufende Nummer, FUENFSTELLIG ("lok00001"), die mit der
                ersten Ausdehnung bei 1 beginnt,
     \0         liefert nichts (r68 kennt keinen Groessenbuchstaben an einem
                Makroaufruf -- "SIZ.b" ist dort "bad mnemonic").
   Ein fehlendes Argument wird zu NICHTS -- es darf nicht abbrechen, denn
   die SDK-Makros pruefen "\#" und benutzen hoehere Argumente nur in einem
   Zweig, den die bedingte Assemblierung dann ohnehin ueberspringt.
   Makronamen sind schreibungsabhaengig ("mactest" findet "MacTest" nicht),
   deshalb wird dafuer lnOpRaw genommen und nicht lnOp. */
static int MAC_MAX = 128;
static int macName[128];
static int macStart[128];
static int macEnd[128];
static int macN;

static char macText[QR_MACTEXT];
static int MACTEXT_MAX = QR_MACTEXT;
static int macTop;

static int macDefining;        /* 1 = lines are being added to the body. */
static int macCounter;         /* For \@. */
static int repActive;           /* 1 = collecting a rept body. */
static int repCount;

static int macFind(int name)
{
	int i;

	for (i = 0; i < macN; i++) {
		if (macName[i] == name)
			return i;
	}
	return -1;
}

/* Append the RAW line to the body of the most recently started macro. */
static void macAppendLine(const char *line)
{
	int i;

	i = 0;
	while (line[i] != 0) {
		if (macTop + 2 >= MACTEXT_MAX)
			fatal("Makrospeicher voll (MACTEXT_MAX)", "");
		macText[macTop] = line[i];
		macTop++;
		i++;
	}
	macText[macTop] = 10;
	macTop++;
}

static void expPut(int c)
{
	if (expTop <= srcTop)
		fatal("Quelltextspeicher voll (SRC_MAX) beim Ausdehnen eines Makros",
		      "");
	expTop--;
	srcArena[expTop] = c;
}

/* Write body [from..to) backwards into expansion storage and substitute
   placeholders. The storage grows downward, so the result is forward-ordered. */
static void macSubstitute(int from, int to, char *argp[], int argN,
			  int serial)
{
	int i;
	int k;
	int d;
	const char *a;

	i = to;
	while (i > from) {
		i--;
		/* "\Ln" -- the LENGTH of argument n, as two decimal digits
		   (gemessen: "a0" ergibt "02", ein leeres Argument "00"). Die
		   SDK-Makros pruefen damit die Art eines Arguments:
		   "ifne \L1-2 / fail ... must be a An register". Von hinten
		   gelesen stehen hier drei Zeichen. */
		if (i > from + 1 && macText[i - 2] == '\\' &&
		    lowerCh(macText[i - 1] & 255) == 'l' &&
		    macText[i] >= '1' && macText[i] <= '9') {
			d = macText[i] - '1';
			i = i - 2;
			k = 0;
			if (d < argN)
				k = strLen(argp[d]);
			expPut('0' + (k % 10));
			expPut('0' + ((k / 10) % 10));
			continue;
		}
		if (i > from && macText[i - 1] == '\\') {
			/* The placeholder consists of two characters; they are seen
			   here in reverse order. */
			k = macText[i] & 255;
			i--;
			if (k >= '1' && k <= '9') {
				d = k - '1';
				if (d < argN) {
					a = argp[d];
					k = strLen(a);
					while (k > 0) {
						k--;
						expPut(a[k] & 255);
					}
				}
				continue;
			}
			if (k == '#') {
				expPut('0' + (argN % 10));
				expPut('0' + ((argN / 10) % 10));
				continue;
			}
			if (k == '@') {
				d = serial;
				for (k = 0; k < 5; k++) {
					expPut('0' + (d % 10));
					d = d / 10;
				}
				continue;
			}
			if (k == '0')
				continue;      /* liefert nichts */
			fatal("unbekannter Platzhalter im Makro (nur \\1..\\9, \\#, \\@, \\0)",
			      "");
		}
		expPut(macText[i] & 255);
	}
}

static char macArgBuf[1024];
static char *macArgP[9];

/* Split the invocation operand field into up to nine arguments.
   An r68 gemessen: getrennt wird an JEDEM Komma -- Klammern zaehlen NICHT
   mit. "REGMOVE2 d0,(a0,d2.w)" hat also DREI Argumente ("d0", "(a0",
   "d2.w)"), und genau darauf baut MACROS/longio.m: das Makro setzt sie mit
   "move.b \1,\2,\3" wieder zusammen und prueft vorher "\#-3".
   Anfuehrungszeichen zaehlen dagegen sehr wohl: "#',',b" sind zwei. */
static int macSplitArgs(void)
{
	int i;
	int n;
	int q;
	int out;
	int argN;
	int c;

	n = strLen(lnArg);
	argN = 0;
	out = 0;
	if (n == 0)
		return 0;
	q = 0;
	macArgP[0] = &macArgBuf[0];
	argN = 1;
	for (i = 0; i <= n; i++) {
		c = 0;
		if (i < n)
			c = lnArg[i] & 255;
		if (i < n && q != 0) {
			if (c == q)
				q = 0;
		} else if (i < n && (c == '"' || c == 39)) {
			q = c;
		} else if (i == n || c == ',') {
			if (out + 1 >= 1024)
				fatal("Makroargumente zu lang: ", lnArg);
			macArgBuf[out] = 0;
			out++;
			if (i < n) {
				if (argN >= 9)
					fatal("mehr als neun Makroargumente: ", lnArg);
				macArgP[argN] = &macArgBuf[out];
				argN++;
			}
			continue;
		}
		if (out + 1 >= 1024)
			fatal("Makroargumente zu lang: ", lnArg);
		macArgBuf[out] = c;
		out++;
	}

	/* r68 entfernt die umgebenden doppelten Anfuehrungszeichen eines
	   Arguments. Gemessen an einem Makro mit dem Rumpf
	     dc.b "\1",0 / dc.b \L1
	   und dem Aufruf M "abc": heraus kommt $61 $62 $63 $00 und die Laenge
	   $03 -- \1 ist also "abc" OHNE die Anfuehrungszeichen, und \Ln zaehlt
	   sie ebenfalls nicht mit. Setzte man sie mit ein, entstuende
	   "dc.b ""abc"",0", was r68 selbst als "bad operand" ablehnt.
	   Daran haengen alle SBF-Descriptoren: ihr Makro uebergibt den
	   Treibernamen schon in Anfuehrungszeichen
	   (SBFDesc ...,IRQPrior,"sbviper") und der Rumpf setzt ihn in
	   dc.b "\5",0 ein. */
	/* Der Zwischenzeiger ist Absicht: "macArgP[i][k]" waere ein
	   zweistufiger Index auf ein Zeigerfeld, und den kann QCC nicht
	   ("array is not two-dimensional"). */
	for (i = 0; i < argN; i++) {
		char *ap;
		int al;

		ap = macArgP[i];
		al = strLen(ap);
		if (al >= 2 && ap[0] == '"' && ap[al - 1] == '"') {
			ap[al - 1] = 0;
			macArgP[i] = ap + 1;
		}
	}
	return argN;
}

/* Expand the macro body into expansion storage and read from there using the
   same stack as "use". */
static void macExpand(int m)
{
	int argN;
	int saved;
	int slot;

	argN = macSplitArgs();
	macCounter++;
	saved = expTop;
	macSubstitute(macStart[m], macEnd[m], macArgP, argN, macCounter);

	if (useDepth >= USE_MAX)
		fatal("Makros zu tief geschachtelt (USE_MAX): ", lnOpRaw);
	if (flN > FILE_MAX - USE_MAX - 1)
		fatal("zu viele Dateien fuer den Makrostapel (FILE_MAX)", "");
	slot = FILE_MAX - 1 - useDepth;
	flName[slot] = macName[m];
	flStart[slot] = expTop;
	flEnd[slot] = saved;

	useFile[useDepth] = curFile;
	usePos[useDepth] = rdPos;
	useLine[useDepth] = curLine;
	useExp[useDepth] = saved;
	useDepth++;
	curFile = slot;
	rdPos = expTop;
	curLine = 1;
}

/* Like macExpand, but for "rept": ALL repetitions are placed in ONE
   Ausdehnung. Sie einzeln zu schieben wuerde den Stapel sprengen -- in
   ROM/COMMON/mbugboot.a steht "REPT (MBBBoundary-MBBLenB)/4", und das sind
   je nach Modulgroesse Dutzende. */
static void repExpand(int m, int count)
{
	int saved;
	int slot;
	int i;

	if (count <= 0)
		return;
	saved = expTop;
	for (i = 0; i < count; i++) {
		macCounter++;
		macSubstitute(macStart[m], macEnd[m], macArgP, 0, macCounter);
	}
	if (useDepth >= USE_MAX)
		fatal("rept zu tief geschachtelt (USE_MAX)", "");
	if (flN > FILE_MAX - USE_MAX - 1)
		fatal("zu viele Dateien fuer den Makrostapel (FILE_MAX)", "");
	slot = FILE_MAX - 1 - useDepth;
	flName[slot] = macName[m];
	flStart[slot] = expTop;
	flEnd[slot] = saved;
	useFile[useDepth] = curFile;
	usePos[useDepth] = rdPos;
	useLine[useDepth] = curLine;
	useExp[useDepth] = saved;
	useDepth++;
	curFile = slot;
	rdPos = expTop;
	curLine = 1;
}

/* ================================================ Conditional assembly ==== */
/* ifeq/ifne/ifgt/ifge/iflt/ifle <expression>, ifdef/ifndef <name>, else, endc.
   Der Ausdruck wird gegen NULL geprueft: "ifeq NULL" mit NULL=0 uebersetzt,
   "ifeq DEFINIERT" mit 1 nicht (an r68 gemessen, ebenso die Schachtelung und
   dass ein uebersprungener Block auch Unuebersetzbares enthalten darf).
   Der Operand von "endc" ist bei Microware ueblicherweise ein Kommentar --
   er wird nicht angesehen. */
static int COND_MAX = 32;
static int condActive[32];     /* 1 = this branch is assembled */
static int condAny[32];        /* 1 = a branch was already true */
static int condN;
static int condSkipN;          /* Number of levels currently skipped */

static int condDirective(const char *base)
{
	if (baseIs(base, "else") || baseIs(base, "endc"))
		return 1;
	if (base[0] != 'i' || base[1] != 'f')
		return 0;
	if (baseIs(base, "ifeq") || baseIs(base, "ifne") ||
	    baseIs(base, "ifgt") || baseIs(base, "ifge") ||
	    baseIs(base, "iflt") || baseIs(base, "ifle") ||
	    baseIs(base, "ifdef") || baseIs(base, "ifndef"))
		return 1;
	return 0;
}

static void condPush(int active)
{
	if (condN >= COND_MAX)
		fatal("bedingte Assemblierung zu tief geschachtelt (COND_MAX)", "");
	condActive[condN] = active;
	condAny[condN] = active;
	condN++;
	if (!active)
		condSkipN++;
}

static void doCond(const char *base)
{
	int v;
	int active;
	int s;

	if (baseIs(base, "endc")) {
		if (condN <= 0)
			/* r68 silently ignores an extra "endc" --
			   in MWOS/OS9/SRC/IO/SCF/DRVR/sc68990.a steht genau
			   eines (acht "if", neun "endc"), und die Datei
			   uebersetzt dort. */
			return;
		condN--;
		if (!condActive[condN])
			condSkipN--;
		return;
	}
	if (baseIs(base, "else")) {
		if (condN <= 0)
			fatal("else ohne if", "");
		if (!condActive[condN - 1])
			condSkipN--;
		active = 0;
		if (!condAny[condN - 1] && condSkipN == 0)
			active = 1;
		condActive[condN - 1] = active;
		if (active)
			condAny[condN - 1] = 1;
		else
			condSkipN++;
		return;
	}

	/* An if inside a skipped block is only counted; its expression may be
	   unevaluable. */
	if (condSkipN > 0) {
		condPush(0);
		condAny[condN - 1] = 1;
		return;
	}

	if (baseIs(base, "ifdef") || baseIs(base, "ifndef")) {
		/* "Defined" means defined during THIS pass.
		   Sonst waere die Bedingung im ersten Durchlauf anders als in
		   den folgenden (die Symboltabelle bleibt ja stehen) -- und
		   r68 entscheidet in seinem ersten Durchlauf. */
		s = symFind(intern(lnArg));
		v = 0;
		if (s >= 0 && symDefined[s] && symPass[s] == pass)
			v = 1;
		if (baseIs(base, "ifndef"))
			v = !v;
		condPush(v);
		return;
	}

	v = evalExpr(lnArg);
	if (exOpen || termN[2] > 0)
		fatal("unbekannter Name in einer Bedingung -- r68 meldet dort \"illegal external reference\" und uebersetzt den Block trotzdem: ",
		      lnArg);
	active = 0;
	if (baseIs(base, "ifeq"))
		active = (v == 0);
	else if (baseIs(base, "ifne"))
		active = (v != 0);
	else if (baseIs(base, "ifgt"))
		active = (v > 0);
	else if (baseIs(base, "ifge"))
		active = (v >= 0);
	else if (baseIs(base, "iflt"))
		active = (v < 0);
	else if (baseIs(base, "ifle"))
		active = (v <= 0);
	condPush(active);
}

/* =============================================================== Opcodes == */
/* Encodings follow the M68000PRM; every generated form is compared with
   test/insn.a gegen r68 gestellt.

   Was r68 dabei von sich aus umformt (gemessen, sonst gaebe es keine
   Byteidentitaet):
   - "add.l #4,d0" wird ADDQ, "sub.l #4,a0" wird SUBQ -- Werte 1..8, auch
     wenn das Symbol erst spaeter definiert wird. Alles ausserhalb 1..8
     bleibt ADDI/SUBI.
   - "add.l #9,a0" wird ADDA, "cmp.l a1,a0" wird CMPA, "move.l a5,a0" wird
     MOVEA: ein Adressregister als Ziel waehlt die A-Form.
   - "and.l #4,d0" wird ANDI, ebenso or/eor/cmp -- dort gibt es keine
     Kurzform.
   Was r68 NICHT umformt: "move.l #7,d0" bleibt MOVE (kein MOVEQ), "bra"
   bleibt die Wortform (es warnt nur "destination in short branch range"),
   "lea 0(a5),a0" bleibt die Displacementform. */

static int sizeBytes(int c)
{
	if (c == 'b')
		return 1;
	if (c == 'w')
		return 2;
	if (c == 'l')
		return 4;
	fatal("Groessenbuchstabe weder .b noch .w noch .l: ", lnOp);
	return 0;
}

static int sizeField(int c)
{
	if (c == 'b')
		return 0;
	if (c == 'w')
		return 1;
	if (c == 'l')
		return 2;
	fatal("Groessenbuchstabe weder .b noch .w noch .l: ", lnOp);
	return 0;
}

/* Immediate size for I-forms (addi/subi/andi/ori/eori/cmpi): a byte immediate
   is stored as a full WORD, sign-extended and with a word reference. */
static int immBytes(int c)
{
	if (c == 'b')
		return 2;
	return sizeBytes(c);
}

/* MOVE size field: byte 1, word 3, long 2. */
static int moveSizeField(int c)
{
	if (c == 'b')
		return 1;
	if (c == 'w')
		return 3;
	if (c == 'l')
		return 2;
	fatal("Groessenbuchstabe weder .b noch .w noch .l: ", lnOp);
	return 0;
}

/* Condition field: exactly two characters, otherwise -1. */
static int condOf(const char *s)
{
	int a;
	int b;

	if (strLen(s) != 2)
		return -1;
	a = lowerCh(s[0] & 255);
	b = lowerCh(s[1] & 255);
	if (a == 'h' && b == 'i')
		return 2;
	if (a == 'l' && b == 's')
		return 3;
	if (a == 'c' && b == 'c')
		return 4;
	if (a == 'h' && b == 's')
		return 4;
	if (a == 'c' && b == 's')
		return 5;
	if (a == 'l' && b == 'o')
		return 5;
	if (a == 'n' && b == 'e')
		return 6;
	if (a == 'e' && b == 'q')
		return 7;
	if (a == 'v' && b == 'c')
		return 8;
	if (a == 'v' && b == 's')
		return 9;
	if (a == 'p' && b == 'l')
		return 10;
	if (a == 'm' && b == 'i')
		return 11;
	if (a == 'g' && b == 'e')
		return 12;
	if (a == 'l' && b == 't')
		return 13;
	if (a == 'g' && b == 't')
		return 14;
	if (a == 'l' && b == 'e')
		return 15;
	return -1;
}

static void needOps(int want)
{
	if (oN != want)
		fatal("falsche Zahl von Operanden: ", lnOp);
}

/* For instructions WITHOUT operands, the third field is already the
   Kommentar -- im Korpus steht reichlich "rte   * Kommentar" ohne
   Semikolon davor. Es wird deshalb nicht geprueft, sondern verworfen.
   (Sonst waere ein Kommentar, der mit "*" beginnt, ein Operand: genau das
   ist der aktuelle Ort.) */
static void dropOps(void)
{
	oN = 0;
	opTxt0[0] = 0;
	opTxt1[0] = 0;
	opTxt2[0] = 0;
	opTxt3[0] = 0;
}

/* Special registers that occur as operand text and are NOT expressions:
   1 = ccr, 2 = sr, 3 = usp, sonst 0. Die muessen abgefangen werden, bevor
   parseOperand() sie als Symbolnamen liest. */
static int specialReg(const char *s)
{
	int n;
	char a;
	char b;
	char c;

	n = strLen(s);
	if (n < 2 || n > 3)
		return 0;
	a = lowerCh(s[0] & 255);
	b = lowerCh(s[1] & 255);
	c = 0;
	if (n == 3)
		c = lowerCh(s[2] & 255);
	if (n == 3 && a == 'c' && b == 'c' && c == 'r')
		return 1;
	/* "cc" nimmt r68 ebenfalls fuer das Bedingungsregister (nicht aber
	   "c" oder "ccrx"). In MWOS/OS9/SRC/IO/RBF/DRVR/rbvme10.a:1074 steht
	   genau das -- offenbar ein Tippfehler, den r68 klaglos uebersetzt. */
	if (n == 2 && a == 'c' && b == 'c')
		return 1;
	if (n == 2 && a == 's' && b == 'r')
		return 2;
	if (n == 3 && a == 'u' && b == 's' && c == 'p')
		return 3;
	return 0;
}

/* Like baseIs(), but case-insensitive. For keywords used as operands:
   OPERAND auftreten: Kontrollregister (movec), MMU-Register (pmove),
   Cachekennungen. r68 nimmt die in jeder Schreibung -- gemessen:
   "movec d0,DFC" ergibt $4E7B $0001, "pmove (A0),TC" ergibt $f010 $4000,
   "cinva BC" ergibt $f4d8, alles wie in Kleinschreibung. Im SDK steht
   beides; "movec d0,DFC" in ROM_CBOOT/sysinit.a zweier Ports.
   Registernamen (regNum) und ccr/sr/usp (specialReg) waren das schon,
   diese drei Tabellen nicht -- daher die Funktion.
   ACHTUNG: fuer SYMBOLnamen gilt das nicht, die sind schreibungsabhaengig
   (gemessen, s. weiter oben). kwIs() darf deshalb nur auf feste
   Schluesselwortlisten angewandt werden, nie auf einen Symbolvergleich. */
static int kwIs(const char *s, const char *lit)
{
	int i;

	i = 0;
	while (lit[i] != 0) {
		if (lowerCh(s[i] & 255) != lit[i])
			return 0;
		i++;
	}
	if (s[i] != 0)
		return 0;
	return 1;
}

/* Control registers for movec, with measured encodings (movec d0,vbr
   ergibt $4E7B $0801, movec a0,usp ergibt $8800): sfc 0, dfc 1, cacr 2,
   usp $800, vbr $801, caar $802, msp $803, isp $804.
   Dazu die des 68040/68060, an denen ROM_CBOOT/sysinit.a der Ports MVME167
   und MVME177 haengt ("movec d0,tc" -> $4E7B $0003): tc 3, itt0 4, itt1 5,
   dtt0 6, dtt1 7, mmusr $805, urp $806, srp $807, buscr 8, pcr $808.
   "srp" und "mmusr" heissen bei pmove dasselbe und bezeichnen dort etwas
   anderes -- s. mmuReg(); die beiden Tabellen sind absichtlich getrennt.
   -1 = unbekannt. */
static int controlReg(const char *s)
{
	if (kwIs(s, "sfc"))
		return 0x000;
	if (kwIs(s, "dfc"))
		return 0x001;
	if (kwIs(s, "cacr"))
		return 0x002;
	if (kwIs(s, "tc"))
		return 0x003;
	if (kwIs(s, "itt0"))
		return 0x004;
	if (kwIs(s, "itt1"))
		return 0x005;
	if (kwIs(s, "dtt0"))
		return 0x006;
	if (kwIs(s, "dtt1"))
		return 0x007;
	if (kwIs(s, "buscr"))
		return 0x008;
	if (kwIs(s, "usp"))
		return 0x800;
	if (kwIs(s, "vbr"))
		return 0x801;
	if (kwIs(s, "caar"))
		return 0x802;
	if (kwIs(s, "msp"))
		return 0x803;
	if (kwIs(s, "isp"))
		return 0x804;
	if (kwIs(s, "mmusr"))
		return 0x805;
	if (kwIs(s, "urp"))
		return 0x806;
	if (kwIs(s, "srp"))
		return 0x807;
	if (kwIs(s, "pcr"))
		return 0x808;
	return -1;
}

/* MMU registers for pmove, with measured encodings in the
   Erweiterungswort: "pmove (a0),tc" ergibt $f010 $4000, srp $4800,
   crp $4c00, tt0 $0800, tt1 $0c00, mmusr $6000. r68 nimmt "psr" als zweite
   Schreibweise fuer mmusr und lehnt "pcsr" ab. -1 = unbekannt. */
static int mmuReg(const char *s)
{
	if (kwIs(s, "tc"))
		return 0x4000;
	if (kwIs(s, "srp"))
		return 0x4800;
	if (kwIs(s, "crp"))
		return 0x4C00;
	if (kwIs(s, "tt0"))
		return 0x0800;
	if (kwIs(s, "tt1"))
		return 0x0C00;
	if (kwIs(s, "mmusr"))
		return 0x6000;
	if (kwIs(s, "psr"))
		return 0x6000;
	return -1;
}

/* Function-code field of pflush/ptest (bits 4..0), measured against r68:
   "#n" wird 1nnnn ("pflush #15,#0" -> $301f), "dN" wird 01nnn
   ("pflush d7,#0" -> $300f), "sfc" wird 00000 und "dfc" wird 00001. */
static int pmmuFc(const char *s)
{
	int r;
	int v;

	if (kwIs(s, "sfc"))
		return 0x00;
	if (kwIs(s, "dfc"))
		return 0x01;
	r = regNum(s, strLen(s));
	if (r >= 0 && r < 8)
		return 0x08 | r;
	if (s[0] != '#')
		fatal("Funktionscode weder #n noch dN, sfc, dfc: ", lnArg);
	subStr(s, 1, strLen(s));
	v = evalExpr(exBuf);
	if (exOpen)
		return 0x10;
	if (exExtern >= 0 || exSect != SECT_ABS)
		fatal("Funktionscode muss ein fester Wert sein: ", lnArg);
	if (v < 0 || v > 15)
		fatal("Funktionscode ausserhalb 0..15: ", lnArg);
	return 0x10 | v;
}

/* A fixed immediate value as operand text ("#7"), for instructions that
   dort nichts Verschiebbares zulassen. */
static int immValue(const char *s, int lo, int hi, const char *what)
{
	int v;

	if (s[0] != '#')
		fatal(what, lnArg);
	subStr(s, 1, strLen(s));
	v = evalExpr(exBuf);
	if (exOpen)
		return lo;
	if (exExtern >= 0 || exSect != SECT_ABS)
		fatal(what, lnArg);
	if (v < lo || v > hi)
		fatal(what, lnArg);
	return v;
}

/* Bit-field suffix "{offset:width}". It must be removed from the operand
   Operandentext abgeschnitten werden -- die geschweiften Klammern kennt es
   nicht. splitOperands() zaehlt sie nicht mit, das braucht es auch nicht:
   im Zusatz steht kein Komma. */
static char bfOffTxt[256];
static char bfWidTxt[256];

static int bfSplit(char *s)
{
	int n;
	int i;
	int lb;
	int colon;
	int depth;
	int j;
	int c;

	n = strLen(s);
	if (n < 1 || s[n - 1] != '}')
		return 0;
	lb = -1;
	for (i = 0; i < n && lb < 0; i++) {
		if (s[i] == '{')
			lb = i;
	}
	if (lb < 0)
		fatal("\"}\" ohne \"{\" im Operanden: ", lnArg);
	if (lb == 0)
		fatal("Bitfeldzusatz ohne Operanden davor: ", lnArg);
	colon = -1;
	depth = 0;
	for (i = lb + 1; i < n - 1 && colon < 0; i++) {
		c = s[i] & 255;
		if (c == '(')
			depth++;
		else if (c == ')')
			depth--;
		else if (c == ':' && depth == 0)
			colon = i;
	}
	if (colon < 0)
		fatal("Bitfeld ohne \":\" zwischen Offset und Breite: ", lnArg);
	if (colon - (lb + 1) >= 256 || (n - 1) - (colon + 1) >= 256)
		fatal("Bitfeldangabe zu lang: ", lnArg);
	j = 0;
	for (i = lb + 1; i < colon; i++) {
		bfOffTxt[j] = s[i];
		j++;
	}
	bfOffTxt[j] = 0;
	j = 0;
	for (i = colon + 1; i < n - 1; i++) {
		bfWidTxt[j] = s[i];
		j++;
	}
	bfWidTxt[j] = 0;
	if (bfOffTxt[0] == 0 || bfWidTxt[0] == 0)
		fatal("leere Bitfeldangabe: ", lnArg);
	s[lb] = 0;
	return 1;
}

/* One field of the bit-field suffix. "dN" denotes a data register;
   meldet bfField() ueber bfIsReg, so wie evalExpr() seine Nebenbefunde
   ueber exSect/exExtern/exOpen meldet. (QCC kann auch einen
   Zeiger-Ausgabeparameter; die Hausform ist hier nur die einheitlichere.)
   Sonst ist das Feld ein Ausdruck -- auch ein zusammengesetzter:
   "d0{WID:WID+1}" mit "WID equ 3" ergibt gemessen $00c4. r68 beschneidet
   den Wert auf 5 Bit und warnt dabei nur; "{0:32}" wird deshalb Breite 0,
   was in der Kodierung genau 32 bedeutet. Einen negativen Wert lehnt r68
   ab ("illegal addressing mode"), qr68 ebenso. */
static int bfIsReg;

static int bfField(const char *s)
{
	int r;
	int v;
	int open;
	int sect;
	int ext;

	bfIsReg = 0;
	r = regNum(s, strLen(s));
	if (r >= 0 && r < 8) {
		bfIsReg = 1;
		return r;
	}
	subStr(s, 0, strLen(s));
	v = evalExpr(exBuf);
	open = exOpen;
	sect = exSect;
	ext = exExtern;
	if (!open) {
		if (ext >= 0 || sect != SECT_ABS)
			fatal("Bitfeldangabe muss ein fester Wert sein: ", lnArg);
		if (v < 0)
			fatal("negative Bitfeldangabe: ", lnArg);
	}
	return v & 31;
}

/* Register list "d0-d7/a0-a6" -> mask in NORMAL order: bit 0 = d0
   ... Bit 7 = d7, Bit 8 = a0 ... Bit 15 = a7. Rueckgabe -1, wenn der Text
   keine Liste ist -- daran erkennt movem, welcher der beiden Operanden die
   Liste ist. */
static int regList(const char *s)
{
	int n;
	int i;
	int mask;
	int r1;
	int r2;
	int start;
	int len;
	int k;

	n = strLen(s);
	if (n == 0)
		return -1;
	mask = 0;
	i = 0;
	while (i < n) {
		start = i;
		while (i < n && s[i] != '/' && s[i] != '-')
			i++;
		len = i - start;
		r1 = regNum(&s[start], len);
		if (r1 < 0)
			return -1;
		if (i < n && s[i] == '-') {
			i++;
			start = i;
			while (i < n && s[i] != '/')
				i++;
			r2 = regNum(&s[start], i - start);
			if (r2 < 0 || r2 < r1)
				return -1;
		} else {
			r2 = r1;
		}
		for (k = r1; k <= r2; k++)
			mask = mask | (1 << k);
		if (i < n) {
			if (s[i] != '/')
				return -1;
			i++;
			if (i >= n)
				return -1;
		}
	}
	return mask;
}

/* For -(An), movem stores the mask in REVERSE order: bit 0 = a7 ... bit 15 = d0.
   Gemessen an "movem.l d0-d7/a0-a6,-(sp)" -> $FFFE gegen "(sp)+,..." ->
   $7FFF. */
static int regMaskReverse(int mask)
{
	int r;
	int i;

	r = 0;
	for (i = 0; i < 16; i++) {
		if (mask & (1 << i))
			r = r | (1 << (15 - i));
	}
	return r;
}

static void needNoSize(int size)
{
	if (size != 0)
		fatal("dieser Befehl hat keinen Groessenbuchstaben: ", lnOp);
}

/* A data register operand, for instructions that allow only one. */
static int needDn(int k)
{
	if (oMode[k] != AM_DN)
		fatal("Datenregister erwartet: ", lnArg);
	return oReg[k];
}

static int needAn(int k)
{
	if (oMode[k] != AM_AN)
		fatal("Adressregister erwartet: ", lnArg);
	return oReg[k];
}

/* Destination of a data instruction: anything except immediate and PC-relative. */
static void needAlterable(int k)
{
	int m;

	m = oMode[k];
	if (m == AM_IMM || m == AM_PCD || m == AM_PCIDX)
		fatal("dieser Operand kann kein Ziel sein: ", lnArg);
}

/* Control address, for jsr/jmp/lea/pea. */
static void needControl(int k)
{
	int m;

	m = oMode[k];
	if (m == AM_DN || m == AM_AN || m == AM_POST || m == AM_PRE ||
	    m == AM_IMM)
		fatal("dieser Operand ist keine Kontrolladresse: ", lnArg);
}

/* Branch instructions. */
static void doBranch(int cond, int size)
{
	int v;
	int d;

	int ext;

	needOps(1);
	subStr(opTxt0, 0, strLen(opTxt0));
	v = evalExpr(exBuf);
	ext = -1;
	if (!exOpen)
		ext = exExtern;
	if (ext < 0 && !exOpen && exSect != SECT_CODE && exSect != SECT_ABS)
		fatal("Sprungziel liegt nicht im Code: ", lnArg);
	if (ext >= 0 && (size == 's' || size == 'b'))
		fatal("kurzer Sprung auf einen externen Namen -- r68 lehnt das ab (\"illegal external reference\"): ",
		      lnArg);
	if (size == 'l')
		fatal("lange Sprungform nicht unterstuetzt -- r68 V2.9.1 erzeugt dafuer \"6000 00000000\", also weder das noetige $FF noch den Abstand: ",
		      lnOp);

	/* With -b, r68 chooses the size ITSELF and ignores a specified
	   angegebenen Buchstaben: "bra.w" auf ein nahes Ziel wird kurz,
	   "beq.s" auf ein fernes wird zur Wortform (beides gemessen). Ein
	   Ziel ausserhalb des Moduls bleibt die Wortform.
	   Abstand 0 -- das Ziel ist die naechste Anweisung -- laesst r68 den
	   Befehl GANZ WEG. Das ist bei bra/Bcc gleichbedeutend, bei bsr aber
	   nicht (die Ruecksprungadresse fehlt dann); dort bricht qr68 lieber
	   ab, statt eine Bedeutungsaenderung nachzubauen. */
	if (optBranch) {
		if (exOpen && ext < 0) {
			/* First pass, target still unknown: assume the SHORT form.
			   wird die KURZE Form angenommen. Das ist kein Detail,
			   sondern der Unterschied zwischen zwei Fixpunkten --
			   waere die Annahme die Wortform, blieben Spruenge
			   lang, die knapp hineinpassen, sobald sie selbst
			   kuerzer werden (an r68 gemessen: zwei bsr/bcc mit
			   Abstand 126 in sc8x30.a). Weil Spruenge danach nur
			   noch wachsen, kommt die Schleife zur Ruhe. */
			emitWord(0x6000 | (cond << 8));
			return;
		}
		if (ext < 0 && !exOpen) {
			d = v - (curPC + 2);
			if (d == 0) {
				/* Abstand 0 -- das Ziel ist die naechste
				   Anweisung. r68 laesst den Befehl mit -b
				   dann GANZ WEG (gemessen). qr68 tut das
				   NICHT, und zwar aus einem messbaren Grund:
				   die Weglassung ist selbsterfuellend. Faellt
				   der Sprung weg, rueckt sein Ziel um zwei
				   Byte heran und der Abstand BLEIBT 0 --
				   beide Zustaende sind in sich stimmig, und
				   welchen r68 trifft, haengt an seinem
				   Zwischenstand nach dem ersten Durchlauf.
				   Genau das ist nicht nachzubauen, ohne r68s
				   Zwei-Pass-Zwischenstand mitzufuehren.
				   Waehrend der Messdurchlaeufe wird die kurze
				   Form gerechnet (zwei Byte, das ist der
				   Zustand, in dem r68 die Datei sieht); steht
				   der Abstand am Ende wirklich auf 0, bricht
				   qr68 ab, statt eine falsche Kodierung
				   ($6000 waere die WORTform) auszugeben. */
				if (emitting)
					fatal("Sprung auf die unmittelbar folgende Anweisung -- r68 laesst den Befehl mit -b weg, qr68 kann das nicht stabil nachbilden: ",
					      lnArg);
				emitWord(0x6000 | (cond << 8));
				return;
			}
			if (d >= -128 && d <= 127) {
				emitWord(0x6000 | (cond << 8) | (d & 255));
				return;
			}
		}
		emitWord(0x6000 | (cond << 8));
		if (exOpen) {
			emitWord(0);
			return;
		}
		if (ext >= 0) {
			refPcExtern(ext, 2);
			emitWord(0);
			return;
		}
		d = v - curPC;
		if (d < -32768 || d > 32767)
			fatal("Sprung zu weit fuer die Wortform: ", lnArg);
		emitWord(d);
		return;
	}

	if (size == 's' || size == 'b') {
		if (exOpen) {
			emitWord(0x6000 | (cond << 8));
			return;
		}
		d = v - (curPC + 2);
		if (d < -128 || d > 127)
			fatal("kurzer Sprung zu weit: ", lnArg);
		if (d == 0)
			fatal("kurzer Sprung mit Abstand 0 (waere die Wortform): ",
			      lnArg);
		emitWord(0x6000 | (cond << 8) | (d & 255));
		return;
	}
	if (size != 0 && size != 'w')
		fatal("Sprungweite weder .s/.b noch .w: ", lnOp);
	emitWord(0x6000 | (cond << 8));
	if (exOpen) {
		emitWord(0);
		return;
	}
	if (ext >= 0) {
		refPcExtern(ext, 2);
		emitWord(0);
		return;
	}
	d = v - curPC;
	if (d < -32768 || d > 32767)
		fatal("Sprung zu weit fuer die Wortform: ", lnArg);
	emitWord(d);
}

/* add/sub/and/or/eor/cmp in allen Formen, die r68 daraus macht. */
static void doArith(const char *base, int size)
{
	int isAdd;
	int isSub;
	int isCmp;
	int isEor;
	int op;
	int iop;
	int sf;
	int q;
	int qop;
	int aop;
	int sp;

	isAdd = baseIs(base, "add");
	isSub = baseIs(base, "sub");
	isCmp = baseIs(base, "cmp");
	isEor = baseIs(base, "eor");
	op = 0;
	iop = 0;
	if (isAdd) {
		op = 0xD000;
		iop = 0x0600;
	} else if (isSub) {
		op = 0x9000;
		iop = 0x0400;
	} else if (isCmp) {
		op = 0xB000;
		iop = 0x0C00;
	} else if (isEor) {
		op = 0xB000;
		iop = 0x0A00;
	} else if (baseIs(base, "and")) {
		op = 0xC000;
		iop = 0x0200;
	} else if (baseIs(base, "or")) {
		op = 0x8000;
		iop = 0x0000;
	} else {
		fatal("innerer Fehler: doArith mit ", lnOp);
	}
	if (size == 0)
		size = 'w';
	sf = sizeField(size);
	needOps(2);

	/* Auch die GRUNDFORM darf nach ccr/sr: "and.w #$fe,ccr" wird $023c,
	   "or.w #1,ccr" wird $003c und "and.w #$fe,sr" wird $027c -- alles
	   gemessen. Ohne das wuerde "ccr" als Symbolname gelesen und die
	   Zeile vier Byte zu lang (so in PORTS/AtariST/SCF/sc_mfp_uart.a:184
	   aufgefallen). */
	sp = specialReg(opTxt1);
	if (sp == 1 || sp == 2) {
		int cop;

		if (!isAdd && !isSub && !isCmp) {
			if (size != 'w' && size != 'b')
				fatal("nach ccr/sr nur .b/.w: ", lnOp);
			parseOperand(opTxt0, 0);
			if (oMode[0] != AM_IMM)
				fatal("nach ccr/sr braucht es einen Sofortwert: ",
				      lnArg);
			cop = 0x0000;
			if (baseIs(base, "and"))
				cop = 0x0200;
			else if (baseIs(base, "eor"))
				cop = 0x0A00;
			if (sp == 1)
				emitWord(cop | 0x3C);
			else
				emitWord(cop | 0x7C);
			emitEa(0, 2);
			return;
		}
		fatal("diese Verknuepfung geht nicht nach ccr/sr: ", lnOp);
	}

	parseOperand(opTxt0, 0);
	parseOperand(opTxt1, 1);

	/* Adressregister als Ziel -> die A-Form. */
	if (oMode[1] == AM_AN) {
		if (!isAdd && !isSub && !isCmp)
			fatal("diese Verknuepfung kennt kein Adressregister als Ziel: ",
			      lnOp);
		if (size == 'b')
			fatal("die A-Form gibt es nicht als Byte: ", lnOp);
		/* Sofortwert 1..8 wird auch hier zu ADDQ/SUBQ (gemessen:
		   "sub.l #4,a0" -> 5988, "add.l #9,a0" -> ADDA). */
		if (oMode[0] == AM_IMM && (isAdd || isSub) &&
		    oExt[0] < 0 && oSect[0] == SECT_ABS &&
		    oVal[0] >= 1 && oVal[0] <= 8) {
			q = oVal[0] & 7;
			qop = 0x5000;
			if (isSub)
				qop = 0x5100;
			emitWord(qop | (q << 9) | (sf << 6) | eaBits(1));
			return;
		}
		aop = 0x0C0;
		if (size == 'l')
			aop = 0x1C0;
		emitWord(op | (oReg[1] << 9) | aop | eaBits(0));
		emitEa(0, sizeBytes(size));
		return;
	}

	/* Unmittelbare Quelle -> Q-Form (nur add/sub, Wert 1..8) oder I-Form. */
	if (oMode[0] == AM_IMM) {
		needAlterable(1);
		if ((isAdd || isSub) && oExt[0] < 0 && oSect[0] == SECT_ABS &&
		    oVal[0] >= 1 && oVal[0] <= 8) {
			q = oVal[0] & 7;
			qop = 0x5000;
			if (isSub)
				qop = 0x5100;
			emitWord(qop | (q << 9) | (sf << 6) | eaBits(1));
			emitEa(1, sizeBytes(size));
			return;
		}
		emitWord(iop | (sf << 6) | eaBits(1));
		emitEa(0, immBytes(size));
		emitEa(1, sizeBytes(size));
		return;
	}

	/* Datenregister als Quelle und ein Speicherziel -> die "Dn nach ea"-
	   Richtung. EOR kennt nur diese. */
	if (oMode[0] == AM_DN && oMode[1] != AM_DN) {
		if (isCmp)
			fatal("cmp kann nur nach einem Datenregister vergleichen: ",
			      lnArg);
		needAlterable(1);
		emitWord(op | (oReg[0] << 9) | ((sf + 4) << 6) | eaBits(1));
		emitEa(1, sizeBytes(size));
		return;
	}
	if (isEor) {
		if (oMode[0] != AM_DN)
			fatal("eor braucht ein Datenregister als Quelle: ", lnArg);
		emitWord(op | (oReg[0] << 9) | ((sf + 4) << 6) | eaBits(1));
		emitEa(1, sizeBytes(size));
		return;
	}
	if (oMode[1] != AM_DN)
		fatal("hier ist ein Datenregister als Ziel noetig: ", lnArg);
	if (oMode[0] == AM_AN && size == 'b')
		fatal("ein Adressregister ist als Byte-Quelle nicht zulaessig: ",
		      lnArg);
	emitWord(op | (oReg[1] << 9) | (sf << 6) | eaBits(0));
	emitEa(0, sizeBytes(size));
}

/* asl/asr/lsl/lsr/rol/ror/roxl/roxr. */
static void doShift(int type, int dr, int size)
{
	int cnt;

	if (oN < 1 || oN > 2)
		fatal("falsche Zahl von Operanden: ", lnOp);
	if (oN == 1) {
		/* Speicherform: um genau ein Bit, immer Wortbreite. */
		if (size != 0 && size != 'w')
			fatal("die Speicherform des Schiebebefehls ist immer ein Wort: ",
			      lnOp);
		parseOperand(opTxt0, 0);
		needAlterable(0);
		if (oMode[0] == AM_DN || oMode[0] == AM_AN)
			fatal("die Speicherform braucht eine Speicheradresse: ", lnArg);
		emitWord(0xE0C0 | (type << 9) | (dr << 8) | eaBits(0));
		emitEa(0, 2);
		return;
	}
	needOps(2);
	if (size == 0)
		size = 'w';
	parseOperand(opTxt0, 0);
	parseOperand(opTxt1, 1);
	needDn(1);
	if (oMode[0] == AM_IMM) {
		if (!oOpen[0] && (oExt[0] >= 0 || oSect[0] != SECT_ABS))
			fatal("verschiebbare Schiebeweite: ", lnArg);
		if (!oOpen[0] && (oVal[0] < 1 || oVal[0] > 8))
			fatal("Schiebeweite ausserhalb 1..8: ", lnArg);
		cnt = oVal[0] & 7;
		emitWord(0xE000 | (cnt << 9) | (dr << 8) | (sizeField(size) << 6) |
			 (type << 3) | oReg[1]);
		return;
	}
	if (oMode[0] != AM_DN)
		fatal("Schiebeweite weder Sofortwert noch Datenregister: ", lnArg);
	emitWord(0xE000 | (oReg[0] << 9) | (dr << 8) | (sizeField(size) << 6) |
		 0x20 | (type << 3) | oReg[1]);
}

/* Ein Operand, mit Umfang: clr/neg/negx/not/tst. */
static void doOneEa(int op, int size)
{
	needOps(1);
	if (size == 0)
		size = 'w';
	parseOperand(opTxt0, 0);
	needAlterable(0);
	emitWord(op | (sizeField(size) << 6) | eaBits(0));
	emitEa(0, sizeBytes(size));
}

static void doInstruction(void)
{
	char base[64];
	int size;
	int cond;
	int v;

	opBase(base);
	size = opSize();
	splitOperands();

	/* --- ohne Operanden --- */
	if (baseIs(base, "rts")) {
		needNoSize(size);
		dropOps();
		emitWord(0x4E75);
		return;
	}
	if (baseIs(base, "nop")) {
		needNoSize(size);
		dropOps();
		emitWord(0x4E71);
		return;
	}
	if (baseIs(base, "rte")) {
		needNoSize(size);
		dropOps();
		emitWord(0x4E73);
		return;
	}
	if (baseIs(base, "rtr")) {
		needNoSize(size);
		dropOps();
		emitWord(0x4E77);
		return;
	}
	if (baseIs(base, "trapv")) {
		needNoSize(size);
		dropOps();
		emitWord(0x4E76);
		return;
	}
	if (baseIs(base, "reset")) {
		needNoSize(size);
		dropOps();
		emitWord(0x4E70);
		return;
	}
	if (baseIs(base, "illegal")) {
		needNoSize(size);
		dropOps();
		emitWord(0x4AFC);
		return;
	}
	/* Der Systemaufruf: r68 hat ihn eingebaut (kein Makro aus einer
	   Include-Datei). Gemessen: "os9 F$Link" wird $4E40 (trap #0) und ein
	   WORT mit dem Aufrufcode. */
	if (baseIs(base, "os9")) {
		int code;

		needNoSize(size);
		needOps(1);
		/* Der Aufrufcode darf ein externer Name sein -- die Namen
		   stehen im SDK in einer Bibliothek, nicht in einer
		   Definitionsdatei. r68 legt dann eine Wortreferenz an
		   ($0030, gemessen an "os9 F$IRQ"). */
		subStr(opTxt0, 0, strLen(opTxt0));
		code = evalExpr(exBuf);
		emitWord(0x4E40);
		if (!exOpen)
			refTerms(TERM_CUR, 2);
		emitWord(code);
		return;
	}
	if (baseIs(base, "trap")) {
		needNoSize(size);
		needOps(1);
		parseOperand(opTxt0, 0);
		if (oMode[0] != AM_IMM ||
		    (!oOpen[0] && (oExt[0] >= 0 || oSect[0] != SECT_ABS)))
			fatal("trap braucht einen festen Sofortwert: ", lnArg);
		if (!oOpen[0] && (oVal[0] < 0 || oVal[0] > 15))
			fatal("trap-Nummer ausserhalb 0..15: ", lnArg);
		emitWord(0x4E40 | oVal[0]);
		return;
	}
	if (baseIs(base, "stop")) {
		needNoSize(size);
		needOps(1);
		parseOperand(opTxt0, 0);
		if (oMode[0] != AM_IMM ||
		    (!oOpen[0] && (oExt[0] >= 0 || oSect[0] != SECT_ABS)))
			fatal("stop braucht einen festen Sofortwert: ", lnArg);
		emitWord(0x4E72);
		emitWord(oVal[0]);
		return;
	}
	if (baseIs(base, "unlk")) {
		needNoSize(size);
		needOps(1);
		parseOperand(opTxt0, 0);
		emitWord(0x4E58 | needAn(0));
		return;
	}
	if (baseIs(base, "link")) {
		if (size != 0 && size != 'w' && size != 'l')
			fatal("link kennt nur .w und .l: ", lnOp);
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		v = needAn(0);
		if (oMode[1] != AM_IMM)
			fatal("link braucht einen Sofortwert als Rahmengroesse: ",
			      lnArg);
		if (size == 'l') {
			/* 68020, gemessen: "link.l a5,#4" -> $480d $00000004. */
			emitWord(0x4808 | v);
			emitEa(1, 4);
			return;
		}
		emitWord(0x4E50 | v);
		emitEa(1, 2);
		return;
	}

	/* --- Spruenge --- */
	if (baseIs(base, "bra")) {
		doBranch(0, size);
		return;
	}
	if (baseIs(base, "bsr")) {
		doBranch(1, size);
		return;
	}
	if (base[0] == 'b') {
		cond = condOf(&base[1]);
		if (cond >= 2) {
			doBranch(cond, size);
			return;
		}
	}
	if (baseIs(base, "dbra") || baseIs(base, "dbf")) {
		needNoSize(size);
		needOps(2);
		parseOperand(opTxt0, 0);
		v = needDn(0);
		emitWord(0x51C8 | v);
		subStr(opTxt1, 0, strLen(opTxt1));
		v = evalExpr(exBuf);
		if (exOpen) {
			emitWord(0);
			return;
		}
		if (exExtern >= 0 || (exSect != SECT_CODE && exSect != SECT_ABS))
			fatal("dbra-Ziel liegt nicht im Code: ", lnArg);
		v = v - curPC;
		if (v < -32768 || v > 32767)
			fatal("dbra-Ziel zu weit: ", lnArg);
		emitWord(v);
		return;
	}
	if (base[0] == 'd' && base[1] == 'b') {
		cond = condOf(&base[2]);
		if (cond >= 0) {
			needNoSize(size);
			needOps(2);
			parseOperand(opTxt0, 0);
			v = needDn(0);
			emitWord(0x50C8 | (cond << 8) | v);
			subStr(opTxt1, 0, strLen(opTxt1));
			v = evalExpr(exBuf);
			if (exOpen) {
				emitWord(0);
				return;
			}
			if (exExtern >= 0 ||
			    (exSect != SECT_CODE && exSect != SECT_ABS))
				fatal("dbcc-Ziel liegt nicht im Code: ", lnArg);
			v = v - curPC;
			if (v < -32768 || v > 32767)
				fatal("dbcc-Ziel zu weit: ", lnArg);
			emitWord(v);
			return;
		}
	}
	if (baseIs(base, "jsr") || baseIs(base, "jmp")) {
		int jop;

		needNoSize(size);
		needOps(1);
		parseOperand(opTxt0, 0);
		needControl(0);
		jop = 0x4E80;
		if (baseIs(base, "jmp"))
			jop = 0x4EC0;
		emitWord(jop | eaBits(0));
		emitEa(0, 4);
		return;
	}

	/* --- Adressen --- */
	if (baseIs(base, "lea")) {
		if (size != 0 && size != 'l')
			fatal("lea kennt nur das Langwort: ", lnOp);
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		needControl(0);
		v = needAn(1);
		emitWord(0x41C0 | (v << 9) | eaBits(0));
		emitEa(0, 4);
		return;
	}
	if (baseIs(base, "pea")) {
		if (size != 0 && size != 'l')
			fatal("pea kennt nur das Langwort: ", lnOp);
		needOps(1);
		parseOperand(opTxt0, 0);
		needControl(0);
		emitWord(0x4840 | eaBits(0));
		emitEa(0, 4);
		return;
	}

	/* --- Bewegen --- */
	if (baseIs(base, "moveq")) {
		if (size != 0 && size != 'l')
			fatal("moveq kennt nur das Langwort: ", lnOp);
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		if (oMode[0] != AM_IMM)
			fatal("moveq braucht einen Sofortwert: ", lnArg);
		/* r68 nimmt hier mehr als -128..127: "moveq #$ff,d0" wird
		   $70ff und "moveq #-129,d0" wird $707f, erst ab 256 meldet
		   es "value out of range" (gemessen). Der Wert wird also
		   schlicht auf ein Byte gestutzt. */
		if (termN[0] == 0 && !oOpen[0] &&
		    (oVal[0] < -256 || oVal[0] > 255))
			fatal("moveq-Wert passt nicht in ein Byte: ", lnArg);
		/* Der Wert steht im niederwertigen Byte des Befehlswortes; ein
		   externer Name dort ist erlaubt und ergibt eine BYTEreferenz
		   genau auf dieses Byte ("moveq #fremd,d1" -> $0028 auf
		   Offset 1, gemessen). */
		emitByte(0x70 | (needDn(1) << 1));
		refTerms(0, 1);
		emitByte(oVal[0] & 255);
		return;
	}
	if (baseIs(base, "movem")) {
		int mask;
		int toMem;
		int lbit;

		if (size == 0)
			size = 'w';
		if (size != 'w' && size != 'l')
			fatal("movem kennt nur .w und .l: ", lnOp);
		lbit = 0;
		if (size == 'l')
			lbit = 0x40;
		needOps(2);
		mask = regList(opTxt0);
		toMem = 1;
		if (mask < 0) {
			toMem = 0;
			mask = regList(opTxt1);
			if (mask < 0)
				fatal("movem ohne Registerliste: ", lnArg);
			parseOperand(opTxt0, 0);
		} else {
			parseOperand(opTxt1, 1);
		}
		if (toMem) {
			needAlterable(1);
			if (oMode[1] == AM_DN || oMode[1] == AM_AN ||
			    oMode[1] == AM_POST)
				fatal("movem kann dorthin nicht schreiben: ", lnArg);
			emitWord(0x4880 | lbit | eaBits(1));
			if (oMode[1] == AM_PRE)
				emitWord(regMaskReverse(mask));
			else
				emitWord(mask);
			emitEa(1, sizeBytes(size));
		} else {
			if (oMode[0] == AM_DN || oMode[0] == AM_AN ||
			    oMode[0] == AM_PRE || oMode[0] == AM_IMM)
				fatal("movem kann von dort nicht lesen: ", lnArg);
			emitWord(0x4C80 | lbit | eaBits(0));
			emitWord(mask);
			emitEa(0, sizeBytes(size));
		}
		return;
	}
	if (baseIs(base, "move") || baseIs(base, "movea")) {
		int sp0;
		int sp1;

		if (size == 0) {
			/* Ohne Groessenbuchstaben ist "move" ein Wort,
			   "movea" aber ein LANGWORT -- gemessen an
			   "movea PD_BUF(a1),a0" ($2069) gegen "move d0,d1"
			   ($3200). So steht es in den RBF-Treibern. */
			size = 'w';
			if (baseIs(base, "movea"))
				size = 'l';
		}
		needOps(2);
		/* SR, CCR und USP sind keine Ausdruecke -- gemessen:
		   "move.w sr,d0" $40c0, "move.w ccr,d0" $42c0 (68010),
		   "move.w d0,sr" $46c0, "move.w d0,ccr" $44c0,
		   "move.l usp,a0" $4e68, "move.l a0,usp" $4e60. */
		sp0 = specialReg(opTxt0);
		sp1 = specialReg(opTxt1);
		if (sp0 == 3 || sp1 == 3) {
			if (size != 'l')
				fatal("usp wird nur als Langwort bewegt: ", lnOp);
			if (sp0 == 3) {
				parseOperand(opTxt1, 1);
				emitWord(0x4E68 | needAn(1));
			} else {
				parseOperand(opTxt0, 0);
				emitWord(0x4E60 | needAn(0));
			}
			return;
		}
		if (sp0 == 1 || sp0 == 2) {
			if (size != 0 && size != 'w')
				fatal("sr/ccr werden als Wort bewegt: ", lnOp);
			parseOperand(opTxt1, 1);
			needAlterable(1);
			if (oMode[1] == AM_AN)
				fatal("sr/ccr passen nicht in ein Adressregister: ",
				      lnArg);
			if (sp0 == 2)
				emitWord(0x40C0 | eaBits(1));
			else
				emitWord(0x42C0 | eaBits(1));
			emitEa(1, 2);
			return;
		}
		if (sp1 == 1 || sp1 == 2) {
			if (size != 0 && size != 'w')
				fatal("sr/ccr werden als Wort bewegt: ", lnOp);
			parseOperand(opTxt0, 0);
			if (oMode[0] == AM_AN)
				fatal("ein Adressregister ist hier nicht zulaessig: ",
				      lnArg);
			if (sp1 == 2)
				emitWord(0x46C0 | eaBits(0));
			else
				emitWord(0x44C0 | eaBits(0));
			emitEa(0, 2);
			return;
		}
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		if (oMode[1] == AM_AN && size == 'b')
			fatal("ein Adressregister kann kein Byte-Ziel sein: ", lnArg);
		if (oMode[0] == AM_AN && size == 'b')
			fatal("ein Adressregister ist als Byte-Quelle nicht zulaessig: ",
			      lnArg);
		needAlterable(1);
		emitWord((moveSizeField(size) << 12) | (eaRegBits(1) << 9) |
			 (eaModeBits(1) << 6) | eaBits(0));
		emitEa(0, sizeBytes(size));
		emitEa(1, sizeBytes(size));
		return;
	}

	/* --- Rechnen --- */
	if (baseIs(base, "add") || baseIs(base, "sub") || baseIs(base, "and") ||
	    baseIs(base, "or") || baseIs(base, "eor") || baseIs(base, "cmp")) {
		doArith(base, size);
		return;
	}
	if (baseIs(base, "adda") || baseIs(base, "suba") || baseIs(base, "cmpa")) {
		int op;
		int aop;

		if (size == 0)
			size = 'w';
		if (size == 'b')
			fatal("die A-Form gibt es nicht als Byte: ", lnOp);
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		op = 0xD000;
		if (baseIs(base, "suba"))
			op = 0x9000;
		else if (baseIs(base, "cmpa"))
			op = 0xB000;
		aop = 0x0C0;
		if (size == 'l')
			aop = 0x1C0;
		emitWord(op | (needAn(1) << 9) | aop | eaBits(0));
		emitEa(0, sizeBytes(size));
		return;
	}
	if (baseIs(base, "addi") || baseIs(base, "subi") || baseIs(base, "andi") ||
	    baseIs(base, "ori") || baseIs(base, "eori") || baseIs(base, "cmpi")) {
		int op;
		int sp;

		if (size == 0)
			size = 'w';
		needOps(2);
		/* Nach CCR oder SR: eigene Befehlsworte, der Sofortwert ist in
		   BEIDEN Faellen ein ganzes Wort. Gemessen: "ori #1,ccr"
		   $003c $0001 (r68 warnt dabei "word sized immediate used with
		   CCR", gibt aber das Wort aus), "andi #$fe,ccr" $023c,
		   "eori #1,ccr" $0a3c, "ori.w #$700,sr" $007c,
		   "andi.w #$f8ff,sr" $027c. */
		sp = specialReg(opTxt1);
		if (sp == 1 || sp == 2) {
			if (baseIs(base, "addi") || baseIs(base, "subi") ||
			    baseIs(base, "cmpi"))
				fatal("nur ori/andi/eori gehen nach ccr/sr: ", lnOp);
			if (size != 'w' && size != 'b')
				fatal("ori/andi/eori nach ccr/sr: nur .b/.w: ", lnOp);
			parseOperand(opTxt0, 0);
			if (oMode[0] != AM_IMM)
				fatal("die I-Form braucht einen Sofortwert: ", lnArg);
			op = 0x0000;
			if (baseIs(base, "andi"))
				op = 0x0200;
			else if (baseIs(base, "eori"))
				op = 0x0A00;
			if (sp == 1)
				emitWord(op | 0x3C);
			else
				emitWord(op | 0x7C);
			emitEa(0, 2);
			return;
		}
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		if (oMode[0] != AM_IMM)
			fatal("die I-Form braucht einen Sofortwert: ", lnArg);
		needAlterable(1);
		/* Auch die AUSGESCHRIEBENE Form verkuerzt r68: "addi.b #1,d5"
		   wird $5205, also ADDQ (gemessen). Fuer andi/ori/eori/cmpi
		   gibt es keine Kurzform, die bleiben stehen. */
		if ((baseIs(base, "addi") || baseIs(base, "subi")) &&
		    termN[0] == 0 && !oOpen[0] && oVal[0] >= 1 && oVal[0] <= 8) {
			int q;
			int qop;

			q = oVal[0] & 7;
			qop = 0x5000;
			if (baseIs(base, "subi"))
				qop = 0x5100;
			emitWord(qop | (q << 9) | (sizeField(size) << 6) |
				 eaBits(1));
			emitEa(1, sizeBytes(size));
			return;
		}
		op = 0x0600;
		if (baseIs(base, "subi"))
			op = 0x0400;
		else if (baseIs(base, "andi"))
			op = 0x0200;
		else if (baseIs(base, "ori"))
			op = 0x0000;
		else if (baseIs(base, "eori"))
			op = 0x0A00;
		else if (baseIs(base, "cmpi"))
			op = 0x0C00;
		emitWord(op | (sizeField(size) << 6) | eaBits(1));
		emitEa(0, immBytes(size));
		emitEa(1, sizeBytes(size));
		return;
	}
	if (baseIs(base, "addq") || baseIs(base, "subq")) {
		int q;
		int qop;

		if (size == 0)
			size = 'w';
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		if (oMode[0] != AM_IMM ||
		    (!oOpen[0] && (oExt[0] >= 0 || oSect[0] != SECT_ABS)))
			fatal("addq/subq brauchen einen festen Sofortwert: ", lnArg);
		if (!oOpen[0] && (oVal[0] < 1 || oVal[0] > 8))
			fatal("addq/subq nur mit 1..8: ", lnArg);
		needAlterable(1);
		q = oVal[0] & 7;
		qop = 0x5000;
		if (baseIs(base, "subq"))
			qop = 0x5100;
		emitWord(qop | (q << 9) | (sizeField(size) << 6) | eaBits(1));
		emitEa(1, sizeBytes(size));
		return;
	}
	if (baseIs(base, "muls") || baseIs(base, "mulu") ||
	    baseIs(base, "divs") || baseIs(base, "divu") ||
	    baseIs(base, "divsl") || baseIs(base, "divul")) {
		int op;
		int longDiv;

		/* Die 68020-Langform: "divu.l d1,d0" -> $4c41 $0000,
		   "divs.l" setzt Bit 11, und "divu.l d1,d2:d0" (Rest in d2)
		   setzt zusaetzlich Bit 10 und traegt d2 unten ein.
		   mulu/muls.l liegen bei $4c00. Alles gemessen.
		   "divul"/"divsl" sind dasselbe Befehlswort mit 32-Bit-
		   Dividend: sie lassen Bit 10 FREI und tragen den Rest
		   trotzdem unten ein -- "divul.l d1,d2:d0" -> $4c41 $0002
		   gegen "divu.l d1,d2:d0" -> $4c41 $0402, "divsl.l" -> $0802.
		   Beide stehen in ROM_CBOOT/sysinit.a der Ports MVME167 und
		   MVME177 ("divul.l d2,d3:d1"). */
		longDiv = 0;
		if (baseIs(base, "divsl") || baseIs(base, "divul"))
			longDiv = 1;
		if (longDiv && size != 'l')
			fatal("divul/divsl gibt es nur als Langwort: ", lnOp);
		if (size == 'l') {
			int q;
			int r;
			int ext;
			int colon;
			int n2;
			int i2;

			needOps(2);
			parseOperand(opTxt0, 0);
			if (oMode[0] == AM_AN)
				fatal("ein Adressregister ist hier nicht zulaessig: ",
				      lnArg);
			colon = -1;
			n2 = strLen(opTxt1);
			for (i2 = 0; i2 < n2; i2++) {
				if (opTxt1[i2] == ':')
					colon = i2;
			}
			r = -1;
			if (colon >= 0) {
				r = regNum(opTxt1, colon);
				q = regNum(&opTxt1[colon + 1], n2 - colon - 1);
				if (r < 0 || r > 7 || q < 0 || q > 7)
					fatal("\"dr:dq\" braucht zwei Datenregister: ",
					      lnArg);
			} else {
				q = regNum(opTxt1, n2);
				if (q < 0 || q > 7)
					fatal("Datenregister erwartet: ", lnArg);
			}
			ext = q << 12;
			if (baseIs(base, "divs") || baseIs(base, "muls") ||
			    baseIs(base, "divsl"))
				ext = ext | 0x0800;
			if (r >= 0) {
				ext = ext | r;
				if (!longDiv)
					ext = ext | 0x0400;
			} else {
				/* Ohne "dr:" traegt r68 in das untere Feld
				   NICHT 0 ein, sondern noch einmal dq --
				   gemessen an "divu.l #x,d1" aus
				   SYSMODS/GCLOCK/tk162.a: $1001, nicht $1000
				   (bei d0 faellt der Unterschied nicht auf). */
				ext = ext | q;
			}
			op = 0x4C40;
			if (baseIs(base, "muls") || baseIs(base, "mulu"))
				op = 0x4C00;
			emitWord(op | eaBits(0));
			emitWord(ext);
			emitEa(0, 4);
			return;
		}
		if (size != 0 && size != 'w')
			fatal("mul/div kennen nur .w und .l: ", lnOp);
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		op = 0xC1C0;
		if (baseIs(base, "mulu"))
			op = 0xC0C0;
		else if (baseIs(base, "divs"))
			op = 0x81C0;
		else if (baseIs(base, "divu"))
			op = 0x80C0;
		if (oMode[0] == AM_AN)
			fatal("ein Adressregister ist hier nicht zulaessig: ", lnArg);
		emitWord(op | (needDn(1) << 9) | eaBits(0));
		emitEa(0, 2);
		return;
	}

	/* --- Bitbefehle --- */
	/* Gemessen: statisch "btst #2,d6" $0806 + Wort $0002, dynamisch
	   "btst d1,d6" $0306; die Art steht in Bit 7..6 (btst 0, bchg 1,
	   bclr 2, bset 3): "bset #3,d0" $08c0, "bclr #3,(a0)" $0890,
	   "bchg d1,d0" $0340. Den Umfang bestimmt der Zieloperand
	   (Datenregister lang, Speicher byteweise), nicht ein Buchstabe. */
	if (baseIs(base, "btst") || baseIs(base, "bchg") ||
	    baseIs(base, "bclr") || baseIs(base, "bset")) {
		int kind;

		if (size != 0 && size != 'b' && size != 'l')
			fatal("Bitbefehle kennen nur .b und .l: ", lnOp);
		kind = 0;
		if (baseIs(base, "bchg"))
			kind = 1;
		else if (baseIs(base, "bclr"))
			kind = 2;
		else if (baseIs(base, "bset"))
			kind = 3;
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		if (oMode[1] == AM_AN)
			fatal("ein Adressregister hat keine Bits: ", lnArg);
		if (kind != 0)
			needAlterable(1);
		if (oMode[0] == AM_IMM) {
			emitWord(0x0800 | (kind << 6) | eaBits(1));
			emitEa(0, 2);
			emitEa(1, 1);
			return;
		}
		emitWord(0x0100 | (needDn(0) << 9) | (kind << 6) | eaBits(1));
		emitEa(1, 1);
		return;
	}

	/* --- Cachebefehle (68040) --- */
	/* Gemessen: $F400 | Cache<<6 | (Zurueckschreiben ? $20 : 0) |
	   Bereich<<3 | Register. Cache: nc 0, dc 1, ic 2, bc 3;
	   Bereich: Zeile 1, Seite 2, alles 3. "cinva bc" -> $f4d8,
	   "cpushl dc,(a1)" -> $f469. */
	if (baseIs(base, "cinva") || baseIs(base, "cpusha") ||
	    baseIs(base, "cinvl") || baseIs(base, "cpushl") ||
	    baseIs(base, "cinvp") || baseIs(base, "cpushp")) {
		int cache;
		int scope;
		int push;
		int op;

		needNoSize(size);
		push = 0;
		if (base[1] == 'p')
			push = 0x20;
		scope = 3;
		if (base[strLen(base) - 1] == 'l')
			scope = 1;
		else if (base[strLen(base) - 1] == 'p')
			scope = 2;
		if (scope == 3)
			needOps(1);
		else
			needOps(2);
		cache = -1;
		if (kwIs(opTxt0, "nc"))
			cache = 0;
		else if (kwIs(opTxt0, "dc"))
			cache = 1;
		else if (kwIs(opTxt0, "ic"))
			cache = 2;
		else if (kwIs(opTxt0, "bc"))
			cache = 3;
		if (cache < 0)
			fatal("Cachekennung weder nc/dc/ic noch bc: ", lnArg);
		op = 0xF400 | (cache << 6) | push | (scope << 3);
		if (scope == 3) {
			emitWord(op);
			return;
		}
		parseOperand(opTxt1, 1);
		if (oMode[1] != AM_IND)
			fatal("Cachebefehl braucht \"(aN)\": ", lnArg);
		emitWord(op | oReg[1]);
		return;
	}

	/* --- Mit Uebertrag und BCD: die Paarform --- */
	/* Gemessen: das ZIEL steht in Bit 11..9, die Quelle unten, Bit 3
	   waehlt die Speicherform: "addx.w d2,d3" -> $d742,
	   "addx.l -(a2),-(a3)" -> $d78a. Grundworte addx $d100, subx $9100,
	   abcd $c100, sbcd $8100. Ohne Groessenbuchstaben ist es das WORT
	   ("addx d0,d1" -> $d340); abcd/sbcd rechnen immer mit einem Byte und
	   tragen gar kein Groessenfeld. */
	if (baseIs(base, "addx") || baseIs(base, "subx") ||
	    baseIs(base, "abcd") || baseIs(base, "sbcd")) {
		int op;
		int isBcd;

		isBcd = 0;
		if (baseIs(base, "abcd") || baseIs(base, "sbcd"))
			isBcd = 1;
		if (isBcd) {
			if (size != 0 && size != 'b')
				fatal("abcd/sbcd rechnen immer mit einem Byte: ", lnOp);
		} else if (size == 0) {
			size = 'w';
		}
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		op = 0xD100;
		if (baseIs(base, "subx"))
			op = 0x9100;
		else if (baseIs(base, "abcd"))
			op = 0xC100;
		else if (baseIs(base, "sbcd"))
			op = 0x8100;
		if (!isBcd)
			op = op | (sizeField(size) << 6);
		if (oMode[0] == AM_DN && oMode[1] == AM_DN) {
			emitWord(op | (oReg[1] << 9) | oReg[0]);
			return;
		}
		if (oMode[0] == AM_PRE && oMode[1] == AM_PRE) {
			emitWord(op | (oReg[1] << 9) | 8 | oReg[0]);
			return;
		}
		fatal("nur dN,dM oder -(aN),-(aM): ", lnArg);
	}

	/* Gemessen: $4800 | ea, immer ein Byte ("nbcd 8(a1)" -> $4829 $0008). */
	if (baseIs(base, "nbcd")) {
		if (size != 0 && size != 'b')
			fatal("nbcd rechnet immer mit einem Byte: ", lnOp);
		needOps(1);
		parseOperand(opTxt0, 0);
		if (oMode[0] == AM_AN)
			fatal("ein Adressregister ist hier nicht zulaessig: ", lnArg);
		needAlterable(0);
		emitWord(0x4800 | eaBits(0));
		emitEa(0, 1);
		return;
	}

	/* --- Bereichspruefung --- */
	/* Gemessen: die Breite steht in Bit 8..7, und zwar Wort 3, Langwort 2
	   ("chk.w (a0),d0" -> $4190, "chk.l (a0),d2" -> $4510). Ohne
	   Groessenbuchstaben ist es das Wort ("chk (a0),d3" -> $4790). Der
	   Sofortwert hat die Breite des Befehls: "chk.w #7,d1" -> $43bc $0007,
	   "chk.l #7,d1" -> $433c $00000007. */
	if (baseIs(base, "chk")) {
		int sz;

		if (size == 0)
			size = 'w';
		sz = 3;
		if (size == 'l')
			sz = 2;
		else if (size != 'w')
			fatal("chk kennt nur .w und .l: ", lnOp);
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		if (oMode[0] == AM_AN)
			fatal("ein Adressregister ist hier nicht zulaessig: ", lnArg);
		emitWord(0x4000 | (needDn(1) << 9) | (sz << 7) | eaBits(0));
		emitEa(0, sizeBytes(size));
		return;
	}

	/* Gemessen: $00C0 | Breite<<9 | ea, dann ein Erweiterungswort -- und
	   das steht VOR den Erweiterungswoertern des Operanden
	   ("chk2.w 8(a1),d3" -> $02e9 $3800 $0008). Im Erweiterungswort:
	   Bit 15 = Adressregister, Bit 14..12 dessen Nummer, Bit 11 = chk2
	   (ohne das Bit ist es cmp2). Ohne Groessenbuchstaben das Wort. */
	if (baseIs(base, "chk2") || baseIs(base, "cmp2")) {
		int ext;

		if (size == 0)
			size = 'w';
		if (size != 'b' && size != 'w' && size != 'l')
			fatal("chk2/cmp2 kennen .b, .w und .l: ", lnOp);
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		needControl(0);
		if (oMode[1] == AM_AN)
			ext = 0x8000 | (oReg[1] << 12);
		else
			ext = needDn(1) << 12;
		if (baseIs(base, "chk2"))
			ext = ext | 0x0800;
		emitWord(0x00C0 | (sizeField(size) << 9) | eaBits(0));
		emitWord(ext);
		emitEa(0, sizeBytes(size));
		return;
	}

	/* --- BCD packen und auspacken (68020) --- */
	/* Dieselbe Paarform wie abcd, dahinter die Korrektur als ganzes Wort:
	   "pack d2,d3,#$1234" -> $8742 $1234, "unpk -(a2),-(a3),#$3030" ->
	   $878a $3030. Grundworte pack $8140, unpk $8180. */
	if (baseIs(base, "pack") || baseIs(base, "unpk")) {
		int op;
		int adj;

		needNoSize(size);
		needOps(3);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		adj = immValue(opTxt2, -32768, 65535,
			       "pack/unpk brauchen die Korrektur als festen Sofortwert: ");
		op = 0x8140;
		if (baseIs(base, "unpk"))
			op = 0x8180;
		if (oMode[0] == AM_DN && oMode[1] == AM_DN)
			emitWord(op | (oReg[1] << 9) | oReg[0]);
		else if (oMode[0] == AM_PRE && oMode[1] == AM_PRE)
			emitWord(op | (oReg[1] << 9) | 8 | oReg[0]);
		else
			fatal("nur dN,dM oder -(aN),-(aM): ", lnArg);
		emitWord(adj);
		return;
	}

	/* --- Vergleichen und tauschen (68020) --- */
	/* Gemessen: "cas.w d0,d1,(a2)" -> $0cd2 $0040. Die Breite steht in
	   Bit 10..9, und zwar Byte 1, Wort 2, Langwort 3 -- eins mehr als das
	   uebliche Groessenfeld. Im Erweiterungswort Du in Bit 8..6, Dc unten;
	   es steht VOR den Erweiterungswoertern des Operanden
	   ("cas.w d0,d1,8(a2)" -> $0cea $0040 $0008). Ohne Groessenbuchstaben
	   das Wort. */
	if (baseIs(base, "cas")) {
		int dc;
		int du;

		if (size == 0)
			size = 'w';
		if (size != 'b' && size != 'w' && size != 'l')
			fatal("cas kennt .b, .w und .l: ", lnOp);
		needOps(3);
		dc = regNum(opTxt0, strLen(opTxt0));
		du = regNum(opTxt1, strLen(opTxt1));
		if (dc < 0 || dc > 7 || du < 0 || du > 7)
			fatal("cas braucht zwei Datenregister: ", lnArg);
		/* Der dritte Operand kommt in Fach 0 -- die beiden ersten sind
		   blosse Registernamen und brauchen keines. */
		parseOperand(opTxt2, 0);
		needAlterable(0);
		if (oMode[0] == AM_DN || oMode[0] == AM_AN)
			fatal("cas braucht eine Speicheradresse: ", lnArg);
		emitWord(0x08C0 | ((sizeField(size) + 1) << 9) | eaBits(0));
		emitWord((du << 6) | dc);
		emitEa(0, sizeBytes(size));
		return;
	}

	/* --- Haltepunkt, Rueckkehr mit Abraeumen, Modulaufruf (68020) --- */
	/* Gemessen: "bkpt #7" -> $484f, "rtd #-4" -> $4e74 $fffc,
	   "callm #255,8(a1)" -> $06e9 $00ff $0008 (die Argumentzahl steht VOR
	   der Adresse), "rtm d0" -> $06c0 und "rtm a3" -> $06cb. */
	if (baseIs(base, "bkpt")) {
		needNoSize(size);
		needOps(1);
		emitWord(0x4848 | immValue(opTxt0, 0, 7,
			 "bkpt braucht eine feste Nummer 0..7: "));
		return;
	}
	if (baseIs(base, "rtd")) {
		needNoSize(size);
		needOps(1);
		parseOperand(opTxt0, 0);
		if (oMode[0] != AM_IMM)
			fatal("rtd braucht einen Sofortwert: ", lnArg);
		emitWord(0x4E74);
		emitEa(0, 2);
		return;
	}
	if (baseIs(base, "callm")) {
		int cnt;

		needNoSize(size);
		needOps(2);
		cnt = immValue(opTxt0, 0, 255,
			       "callm braucht die Argumentzahl als festen Sofortwert 0..255: ");
		parseOperand(opTxt1, 1);
		needControl(1);
		emitWord(0x06C0 | eaBits(1));
		emitWord(cnt);
		emitEa(1, 4);
		return;
	}
	if (baseIs(base, "rtm")) {
		needNoSize(size);
		needOps(1);
		parseOperand(opTxt0, 0);
		if (oMode[0] == AM_DN)
			emitWord(0x06C0 | oReg[0]);
		else if (oMode[0] == AM_AN)
			emitWord(0x06C8 | oReg[0]);
		else
			fatal("rtm braucht ein Register: ", lnArg);
		return;
	}

	/* --- PMMU: Puffer leeren und pruefen (68030) --- */
	/* Gemessen. Beide legen $F000 | ea ab und dahinter ein
	   Erweiterungswort -- vor den Erweiterungswoertern des Operanden
	   ("ptestr #1,8(a0),#7" -> $f028 $9e11 $0008).
	   pflush: Bit 15..13 = 001, Bit 12..10 = Betriebsart, Bit 8..5 = Maske
	   (VIER Bit -- von "#0" bis "#8" waechst das Wort in Schritten von $20,
	   gemessen), Bit 4..0 = Funktionscode. Betriebsart: pflusha 1, sonst
	   4 + (pflushs ? 1 : 0) + (Adresse genannt ? 2 : 0) -- gemessen
	   $3010 / $3410 / $3810 / $3c10.
	   ptest: Bit 15..13 = 100, Bit 12..10 = Ebene, Bit 9 = lesen,
	   Bit 8 = ein Adressregister ist genannt, Bit 7..5 dessen Nummer,
	   Bit 4..0 der Funktionscode ("ptestr #0,(a1),#3,a2" -> $f011 $8f50). */
	if (baseIs(base, "pflusha")) {
		needNoSize(size);
		dropOps();
		emitWord(0xF000);
		emitWord(0x2400);
		return;
	}
	if (baseIs(base, "pflush") || baseIs(base, "pflushs")) {
		int mode;
		int fc;
		int mask;

		needNoSize(size);
		if (oN != 2 && oN != 3)
			fatal("pflush braucht Funktionscode und Maske: ", lnArg);
		mode = 4;
		if (baseIs(base, "pflushs"))
			mode = mode | 1;
		fc = pmmuFc(opTxt0);
		mask = immValue(opTxt1, 0, 15,
				"pflush braucht die Maske als festen Sofortwert 0..15: ");
		if (oN == 3) {
			mode = mode | 2;
			parseOperand(opTxt2, 0);
			needControl(0);
			emitWord(0xF000 | eaBits(0));
			emitWord(0x2000 | (mode << 10) | (mask << 5) | fc);
			emitEa(0, 4);
			return;
		}
		emitWord(0xF000);
		emitWord(0x2000 | (mode << 10) | (mask << 5) | fc);
		return;
	}
	if (baseIs(base, "ptestr") || baseIs(base, "ptestw")) {
		int lvl;
		int fc;
		int ext;
		int an;

		needNoSize(size);
		if (oN != 3 && oN != 4)
			fatal("ptest braucht Funktionscode, Adresse und Ebene: ", lnArg);
		fc = pmmuFc(opTxt0);
		lvl = immValue(opTxt2, 0, 7,
			       "ptest braucht die Ebene als festen Sofortwert 0..7: ");
		parseOperand(opTxt1, 1);
		needControl(1);
		/* r68-DEFEKT: braucht die Adressierungsart Erweiterungswoerter,
		   legt r68 an deren Stelle die EBENE ab -- die Adresse geht
		   ganz verloren. Gemessen: "ptestr #1,16(a0),#3" ergibt
		   $f028 $8e11 $0003 statt $0010, und "ptestr #1,$1234.w,#3"
		   ergibt $f039 $8e11 $00000003 statt der Adresse. Der erzeugte
		   Befehl prueft damit eine ganz andere Stelle. qr68 bricht
		   dafuer ab, statt den Defekt nachzubauen oder still davon
		   abzuweichen -- dieselbe Entscheidung wie bei bra.l. Heil ist
		   nur "(aN)", und nur das steht in echtem Code. */
		if (oMode[1] != AM_IND)
			fatal("ptest nur mit \"(aN)\" -- r68 V2.9.1 legt sonst die Ebene an die Stelle der Adresse: ", lnArg);
		ext = 0x8000 | (lvl << 10) | fc;
		if (baseIs(base, "ptestr"))
			ext = ext | 0x0200;
		if (oN == 4) {
			/* Gemessen: "ptestr #0,(a1),#3,a2" -> $f011 $8f50 --
			   Bit 8 zeigt an, dass ein Adressregister genannt ist,
			   Bit 7..5 tragen seine Nummer. */
			parseOperand(opTxt3, 0);
			an = needAn(0);
			ext = ext | 0x0100 | (an << 5);
		}
		emitWord(0xF000 | eaBits(1));
		emitWord(ext);
		return;
	}
	/* Gemessen: "psave -(a7)" -> $f127, "prestore (a7)+" -> $f15f. */
	if (baseIs(base, "psave") || baseIs(base, "prestore")) {
		int op;

		needNoSize(size);
		needOps(1);
		parseOperand(opTxt0, 0);
		op = 0xF100;
		if (baseIs(base, "prestore"))
			op = 0xF140;
		emitWord(op | eaBits(0));
		emitEa(0, 4);
		return;
	}
	/* Gemessen: "lpstop #$2700" -> $f800 $01c0 $2700 (CPU32/68060). */
	if (baseIs(base, "lpstop")) {
		needNoSize(size);
		needOps(1);
		parseOperand(opTxt0, 0);
		if (oMode[0] != AM_IMM)
			fatal("lpstop braucht einen Sofortwert: ", lnArg);
		emitWord(0xF800);
		emitWord(0x01C0);
		emitEa(0, 2);
		return;
	}

	/* --- Bitfeldbefehle (68020) --- */
	/* Gemessen: $E8C0 | Kennung<<8 | ea, gefolgt von einem
	   Erweiterungswort -- und das steht VOR den Erweiterungswoertern des
	   Operanden ("bftst 8(a0){1:2}" -> $e8e8 $0042 $0008, "bfextu
	   (a1,d1.l){d2:1},d7" -> $e9f1 $7881 $1800). Sein Aufbau:
	     Bit 14..12  Datenregister (bfextu/bfexts/bfffo das Ziel, bfins
	                 die Quelle; die vier ohne Register lassen es 0)
	     Bit 11      1 = der Offset steht in einem Datenregister
	     Bit 10..6   Offset bzw. dessen Registernummer
	     Bit 5       1 = die Breite steht in einem Datenregister
	     Bit 4..0    Breite bzw. deren Registernummer
	   Kennungen: bftst 0, bfextu 1, bfchg 2, bfexts 3, bfclr 4, bfffo 5,
	   bfset 6, bfins 7.
	   Weil das Erweiterungswort vor der Adresse liegt, zaehlt ein
	   PC-Abstand ab dem Wort DAHINTER -- "bftst lab(pc){1:2}" auf Offset
	   $2a ergibt $ffd2, also den Abstand vom Adresswort auf $2e. Das
	   ergibt sich hier von selbst, weil emitEa() mit curPC rechnet. */
	if (baseIs(base, "bftst") || baseIs(base, "bfextu") ||
	    baseIs(base, "bfchg") || baseIs(base, "bfexts") ||
	    baseIs(base, "bfclr") || baseIs(base, "bfffo") ||
	    baseIs(base, "bfset") || baseIs(base, "bfins")) {
		int kind;
		int eak;
		int dnk;
		int dn;
		int off;
		int wid;
		int doReg;
		int dwReg;
		char *eaTxt;

		needNoSize(size);
		kind = 0;
		if (baseIs(base, "bfextu"))
			kind = 1;
		else if (baseIs(base, "bfchg"))
			kind = 2;
		else if (baseIs(base, "bfexts"))
			kind = 3;
		else if (baseIs(base, "bfclr"))
			kind = 4;
		else if (baseIs(base, "bfffo"))
			kind = 5;
		else if (baseIs(base, "bfset"))
			kind = 6;
		else if (baseIs(base, "bfins"))
			kind = 7;
		dnk = -1;
		eak = 0;
		if (kind == 1 || kind == 3 || kind == 5) {
			dnk = 1;               /* "<ea>{o:b},dN" */
		} else if (kind == 7) {
			dnk = 0;               /* "dN,<ea>{o:b}" */
			eak = 1;
		}
		if (dnk < 0)
			needOps(1);
		else
			needOps(2);
		eaTxt = opTxt0;
		if (eak == 1)
			eaTxt = opTxt1;
		if (!bfSplit(eaTxt))
			fatal("Bitfeldbefehl ohne \"{offset:breite}\": ", lnArg);
		off = bfField(bfOffTxt);
		doReg = bfIsReg;
		wid = bfField(bfWidTxt);
		dwReg = bfIsReg;
		dn = 0;
		if (dnk >= 0) {
			if (dnk == 0)
				parseOperand(opTxt0, 0);
			else
				parseOperand(opTxt1, 1);
			dn = needDn(dnk);
		}
		parseOperand(eaTxt, eak);
		if (oMode[eak] != AM_DN)
			needControl(eak);
		emitWord(0xE8C0 | (kind << 8) | eaBits(eak));
		emitWord((dn << 12) | (doReg << 11) | (off << 6) |
			 (dwReg << 5) | wid);
		emitEa(eak, 4);
		return;
	}

	/* --- eine Cachezeile bewegen (move16, 68040) --- */
	/* Gemessen: die Form mit zwei Postinkrementen hat ein
	   Erweiterungswort ("move16 (a0)+,(a2)+" -> $f620 $a000: Ax unten im
	   Befehlswort, Ay in Bit 14..12, Bit 15 gesetzt), die vier Formen mit
	   absoluter Adresse dagegen keines -- dort steht die Adresse direkt
	   dahinter: "(a0)+,$12345678" -> $f600, "$12345678,(a1)+" -> $f609,
	   "(a2),$12345678" -> $f612, "$12345678,(a3)" -> $f61b. */
	if (baseIs(base, "move16")) {
		needNoSize(size);
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		if (oMode[0] == AM_POST && oMode[1] == AM_POST) {
			emitWord(0xF620 | oReg[0]);
			emitWord(0x8000 | (oReg[1] << 12));
			return;
		}
		if (oMode[0] == AM_POST && oMode[1] == AM_ABSL) {
			emitWord(0xF600 | oReg[0]);
			emitEa(1, 4);
			return;
		}
		if (oMode[0] == AM_ABSL && oMode[1] == AM_POST) {
			emitWord(0xF608 | oReg[1]);
			emitEa(0, 4);
			return;
		}
		if (oMode[0] == AM_IND && oMode[1] == AM_ABSL) {
			emitWord(0xF610 | oReg[0]);
			emitEa(1, 4);
			return;
		}
		if (oMode[0] == AM_ABSL && oMode[1] == AM_IND) {
			emitWord(0xF618 | oReg[1]);
			emitEa(0, 4);
			return;
		}
		fatal("move16 kennt nur (aN)+,(aM)+ und die vier Formen mit absoluter Adresse: ", lnArg);
	}

	/* --- MMU-Register bewegen (pmove, 68030) --- */
	/* Gemessen: $F000 | ea, dann das Erweiterungswort aus mmuReg(); Bit 9
	   gibt die Richtung an (0 = in das MMU-Register, 1 = heraus:
	   "pmove tc,(a0)" -> $f010 $4200), Bit 8 ist das FD von "pmovefd"
	   ("pmovefd (a0),tc" -> $f010 $4100). Auch hier steht das
	   Erweiterungswort VOR der Adresse ("pmove 8(a0),tc" -> $f028 $4000
	   $0008). */
	if (baseIs(base, "pmove") || baseIs(base, "pmovefd")) {
		int reg;
		int eak;

		needNoSize(size);
		needOps(2);
		reg = mmuReg(opTxt1);
		eak = 0;
		if (reg < 0) {
			reg = mmuReg(opTxt0);
			if (reg < 0)
				fatal("pmove ohne bekanntes MMU-Register: ",
				      lnArg);
			reg = reg | 0x0200;
			eak = 1;
		}
		if (baseIs(base, "pmovefd"))
			reg = reg | 0x0100;
		if (eak == 0)
			parseOperand(opTxt0, 0);
		else
			parseOperand(opTxt1, 1);
		needControl(eak);
		emitWord(0xF000 | eaBits(eak));
		emitWord(reg);
		emitEa(eak, 4);
		return;
	}

	/* --- Speicher mit Speicher vergleichen --- */
	/* Gemessen: "cmpm.l (a0)+,(a5)+" -> $bb88, der ERSTE Operand ist Ay
	   (unten), der zweite Ax (Bits 11..9). */
	if (baseIs(base, "cmpm")) {
		if (size == 0)
			size = 'w';
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		if (oMode[0] != AM_POST || oMode[1] != AM_POST)
			fatal("cmpm vergleicht nur \"(aN)+,(aM)+\": ", lnArg);
		emitWord(0xB108 | (oReg[1] << 9) | (sizeField(size) << 6) |
			 oReg[0]);
		return;
	}

	/* --- ueber ein Peripherieregister (movep) --- */
	/* Gemessen: "movep.w d1,(a0)" -> $0388 $0000, "movep.l (a0),d0" ->
	   $0148 $0000. Die Betriebsart in Bit 8..6: 4 = Wort aus dem
	   Speicher, 5 = Langwort aus dem Speicher, 6 = Wort dorthin,
	   7 = Langwort dorthin. Ein fehlendes Displacement ist 0. */
	if (baseIs(base, "movep")) {
		int opm;
		int dn;
		int an;
		int disp;

		if (size == 0)
			size = 'w';
		if (size != 'w' && size != 'l')
			fatal("movep kennt nur .w und .l: ", lnOp);
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		opm = 4;
		if (size == 'l')
			opm = opm + 1;
		if (oMode[0] == AM_DN) {
			opm = opm + 2;
			dn = oReg[0];
			if (oMode[1] != AM_DISP && oMode[1] != AM_IND)
				fatal("movep braucht \"d(aN)\" als Ziel: ", lnArg);
			an = oReg[1];
			disp = oVal[1];
		} else {
			dn = needDn(1);
			if (oMode[0] != AM_DISP && oMode[0] != AM_IND)
				fatal("movep braucht \"d(aN)\" als Quelle: ", lnArg);
			an = oReg[0];
			disp = oVal[0];
		}
		emitWord(0x0108 | (dn << 9) | (opm << 6) | an);
		if (disp < -32768 || disp > 32767)
			fatal("movep-Displacement passt nicht in 16 Bit: ", lnArg);
		emitWord(disp);
		return;
	}

	/* --- Register tauschen --- */
	/* Gemessen: "exg d0,d1" $c141, "exg a0,a1" $c149, "exg d0,a1" $c189 --
	   und "exg a1,d0" ergibt DASSELBE $c189, r68 dreht die gemischte Form
	   also so, dass das Datenregister im Rx-Feld steht. */
	if (baseIs(base, "exg")) {
		if (size != 0 && size != 'l')
			fatal("exg tauscht immer ganze Langworte: ", lnOp);
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		if (oMode[0] == AM_DN && oMode[1] == AM_DN) {
			emitWord(0xC140 | (oReg[0] << 9) | oReg[1]);
			return;
		}
		if (oMode[0] == AM_AN && oMode[1] == AM_AN) {
			emitWord(0xC148 | (oReg[0] << 9) | oReg[1]);
			return;
		}
		if (oMode[0] == AM_DN && oMode[1] == AM_AN) {
			emitWord(0xC188 | (oReg[0] << 9) | oReg[1]);
			return;
		}
		if (oMode[0] == AM_AN && oMode[1] == AM_DN) {
			emitWord(0xC188 | (oReg[1] << 9) | oReg[0]);
			return;
		}
		fatal("exg tauscht nur Register: ", lnArg);
	}

	/* --- Kontrollregister (68010) --- */
	if (baseIs(base, "movec")) {
		int cr;
		int rn;
		int ext;

		if (size != 0 && size != 'l')
			fatal("movec bewegt immer ein Langwort: ", lnOp);
		needOps(2);
		cr = controlReg(opTxt1);
		if (cr >= 0) {
			parseOperand(opTxt0, 0);
			rn = 0;
		} else {
			cr = controlReg(opTxt0);
			if (cr < 0)
				fatal("kein Kontrollregister genannt: ", lnArg);
			parseOperand(opTxt1, 1);
			rn = 1;
		}
		if (oMode[rn] == AM_DN) {
			ext = (oReg[rn] << 12) | cr;
		} else if (oMode[rn] == AM_AN) {
			ext = 0x8000 | (oReg[rn] << 12) | cr;
		} else {
			fatal("movec braucht ein Register: ", lnArg);
			ext = 0;
		}
		if (rn == 0)
			emitWord(0x4E7B);
		else
			emitWord(0x4E7A);
		emitWord(ext);
		return;
	}
	/* --- ueber die Funktionscodes (68010) --- */
	/* Gemessen: "moves.l d0,(a0)" $0e90 + $0800 (Bit 11 = Register nach
	   Speicher), "moves.l (a0),d0" $0e90 + $0000. */
	if (baseIs(base, "moves")) {
		int ext;
		int rn;

		if (size == 0)
			size = 'w';
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		if (oMode[0] == AM_DN || oMode[0] == AM_AN) {
			rn = 0;
			ext = 0x0800;
			needAlterable(1);
			emitWord(0x0E00 | (sizeField(size) << 6) | eaBits(1));
		} else if (oMode[1] == AM_DN || oMode[1] == AM_AN) {
			rn = 1;
			ext = 0;
			emitWord(0x0E00 | (sizeField(size) << 6) | eaBits(0));
		} else {
			fatal("moves braucht ein Register auf einer Seite: ", lnArg);
			return;
		}
		if (oMode[rn] == AM_AN)
			ext = ext | 0x8000;
		ext = ext | (oReg[rn] << 12);
		emitWord(ext);
		if (rn == 0)
			emitEa(1, sizeBytes(size));
		else
			emitEa(0, sizeBytes(size));
		return;
	}

	/* --- ein Operand --- */
	if (baseIs(base, "clr")) {
		doOneEa(0x4200, size);
		return;
	}
	if (baseIs(base, "neg")) {
		doOneEa(0x4400, size);
		return;
	}
	if (baseIs(base, "negx")) {
		doOneEa(0x4000, size);
		return;
	}
	if (baseIs(base, "not")) {
		doOneEa(0x4600, size);
		return;
	}
	if (baseIs(base, "tst")) {
		needOps(1);
		if (size == 0)
			size = 'w';
		parseOperand(opTxt0, 0);
		emitWord(0x4A00 | (sizeField(size) << 6) | eaBits(0));
		emitEa(0, sizeBytes(size));
		return;
	}
	if (baseIs(base, "tas")) {
		if (size != 0 && size != 'b')
			fatal("tas kennt nur das Byte: ", lnOp);
		needOps(1);
		parseOperand(opTxt0, 0);
		needAlterable(0);
		emitWord(0x4AC0 | eaBits(0));
		emitEa(0, 1);
		return;
	}
	if (baseIs(base, "swap")) {
		if (size != 0 && size != 'w')
			fatal("swap kennt nur das Wort: ", lnOp);
		needOps(1);
		parseOperand(opTxt0, 0);
		emitWord(0x4840 | needDn(0));
		return;
	}
	if (baseIs(base, "ext")) {
		needOps(1);
		parseOperand(opTxt0, 0);
		if (size == 'w')
			emitWord(0x4880 | needDn(0));
		else if (size == 'l')
			emitWord(0x48C0 | needDn(0));
		else
			fatal("ext braucht .w oder .l: ", lnOp);
		return;
	}
	if (baseIs(base, "extb")) {
		if (size != 'l')
			fatal("extb gibt es nur als .l (68020): ", lnOp);
		needOps(1);
		parseOperand(opTxt0, 0);
		emitWord(0x49C0 | needDn(0));
		return;
	}

	/* --- Schieben und Rotieren --- */
	if (baseIs(base, "asl")) {
		doShift(0, 1, size);
		return;
	}
	if (baseIs(base, "asr")) {
		doShift(0, 0, size);
		return;
	}
	if (baseIs(base, "lsl")) {
		doShift(1, 1, size);
		return;
	}
	if (baseIs(base, "lsr")) {
		doShift(1, 0, size);
		return;
	}
	if (baseIs(base, "roxl")) {
		doShift(2, 1, size);
		return;
	}
	if (baseIs(base, "roxr")) {
		doShift(2, 0, size);
		return;
	}
	if (baseIs(base, "rol")) {
		doShift(3, 1, size);
		return;
	}
	if (baseIs(base, "ror")) {
		doShift(3, 0, size);
		return;
	}

	/* --- bedingtes Setzen --- */
	if (base[0] == 's') {
		cond = condOf(&base[1]);
		/* "st" und "sf" -- immer wahr bzw. immer falsch. Die kennt
		   condOf() nicht, weil es zwei Zeichen verlangt (sonst waere
		   jedes "s?" eine Bedingung). */
		if (baseIs(base, "st"))
			cond = 0;
		else if (baseIs(base, "sf"))
			cond = 1;
		if (cond >= 0) {
			if (size != 0 && size != 'b')
				fatal("Scc kennt nur das Byte: ", lnOp);
			needOps(1);
			parseOperand(opTxt0, 0);
			needAlterable(0);
			if (oMode[0] == AM_AN)
				fatal("Scc kann kein Adressregister setzen: ", lnArg);
			emitWord(0x50C0 | (cond << 8) | eaBits(0));
			emitEa(0, 1);
			return;
		}
	}

	/* --- bedingter Ausnahmesprung (trapcc, 68020) --- */
	/* Gemessen: $50F8 | Bedingung<<8 | Form -- Form 4 ohne Operand
	   ("trapeq" -> $57fc), 2 mit Wort ("trapeq.w #7" -> $57fa $0007),
	   3 mit Langwort ("trapeq.l #7" -> $57fb $00000007). "trapt" und
	   "trapf" sind die Bedingungen 0 und 1; wie bei Scc kennt condOf()
	   die nicht, weil es zwei Zeichen verlangt. "trap" und "trapv" sind
	   weiter oben schon abgehandelt und kommen hier nicht mehr an. */
	if (base[0] == 't' && base[1] == 'r' && base[2] == 'a' &&
	    base[3] == 'p') {
		cond = condOf(&base[4]);
		if (baseIs(base, "trapt"))
			cond = 0;
		else if (baseIs(base, "trapf"))
			cond = 1;
		if (cond >= 0) {
			if (size == 0) {
				/* Ohne Groessenbuchstaben hat trapcc keinen
				   Operanden -- das dritte Feld ist dann schon
				   der Kommentar, s. dropOps(). */
				dropOps();
				emitWord(0x50FC | (cond << 8));
				return;
			}
			if (size != 'w' && size != 'l')
				fatal("trapcc kennt nur .w und .l: ", lnOp);
			needOps(1);
			parseOperand(opTxt0, 0);
			if (oMode[0] != AM_IMM)
				fatal("trapcc braucht einen Sofortwert: ", lnArg);
			if (size == 'w') {
				emitWord(0x50FA | (cond << 8));
				emitEa(0, 2);
				return;
			}
			emitWord(0x50FB | (cond << 8));
			emitEa(0, 4);
			return;
		}
	}

	fatal("Befehl noch nicht kodierbar: ", lnOp);
}

/* Legt die Zeile mindestens ein Wort ab? Dann richtet r68 vorher aus -- und
   zwar BEVOR das Label der Zeile seinen Wert bekommt. Gemessen an
   "dc.b 1 / lab: nop": lab hat den Wert 2, nicht 1. */
static int lineAligns(void)
{
	char b[64];
	int sz;

	if (lnOp[0] == 0)
		return 0;
	if (curSect == SECT_NONE || curSect == SECT_ABS ||
	    curSect == SECT_EXTERN)
		return 0;
	opBase(b);
	if (baseIs(b, "equ") || baseIs(b, "set") || baseIs(b, "end") ||
	    baseIs(b, "psect") || baseIs(b, "vsect") || baseIs(b, "ends") ||
	    baseIs(b, "endsect") ||
	    baseIs(b, "nam") || baseIs(b, "ttl") || baseIs(b, "page") ||
	    baseIs(b, "pag") || baseIs(b, "opt") || baseIs(b, "spc") ||
	    baseIs(b, "fail") || baseIs(b, "align") || baseIs(b, "use") ||
	    baseIs(b, "org") || baseIs(b, "do"))
		return 0;
	sz = opSize();
	if (baseIs(b, "dc") || baseIs(b, "dcb") || baseIs(b, "ds")) {
		if (sz == 'b')
			return 0;
		return 1;
	}
	/* Ein Makroaufruf legt selbst nichts ab -- was der Rumpf ablegt,
	   richtet sich dort aus. r68 macht es genauso: zwei Aufrufe, die je
	   ein "dc.b" ausdehnen, ergeben zwei aufeinanderfolgende Bytes. */
	if (lnOpRaw[0] != 0 && macFind(intern(lnOpRaw)) >= 0)
		return 0;
	return 1;
}

/* ============================================================ Ein Durchlauf */
static void runPass(void)
{
	int size;
	char base[64];

	curFile = 0;
	rdPos = flStart[0];
	curLine = 1;
	curSect = SECT_NONE;
	curPC = 0;
	codeN = 0;
	idataN = 0;
	refN = 0;
	/* Auch die Zustaende zuruecksetzen, die die Quelle SETZT und nicht nur
	   fortschreibt -- sonst schlaegt im zweiten Durchlauf die Wache gegen
	   ein zweites psect an. */
	psSeen = 0;
	useDepth = 0;
	orgPC = 0;
	condN = 0;
	condSkipN = 0;
	macN = 0;
	macTop = 0;
	macDefining = 0;
	repActive = 0;
	macCounter = 0;
	expTop = SRC_MAX;

	{
		int i;

		for (i = 0; i < argDefN; i++)
			symDefine(argDefName[i], argDefVal[i], SECT_ABS, 0, 0);
	}
	statStorage = 0;
	/* Die FERNdaten genauso zuruecksetzen wie die nahen -- sonst wachsen
	   sie ueber die Durchlaeufe weiter und qr68 meldet zu Recht
	   "Adressen werden nicht stabil". Genau das ist beim ersten Anlauf
	   passiert. */
	remoteStatStorage = 0;
	rdataPC = 0;
	inRemoteVsect = 0;
	idataPC = 0;
	udataPC = 0;
	symMoved = 0;

	while (readLine()) {
		if (!splitLine())
			continue;

		size = opSize();
		opBase(base);

		/* Einen Makro- oder rept-Rumpf sammeln: bis "endm"/"endr"
		   wandert JEDE Zeile roh in den Speicher, ohne sie anzusehen.
		   Das steht vor der bedingten Assemblierung, weil ein "ifeq"
		   im Rumpf erst bei der Ausdehnung gilt. Ein "macro" innerhalb
		   eines uebersprungenen Blocks wird gar nicht erst erreicht. */
		if (macDefining) {
			if (baseIs(base, "endm") && !repActive) {
				macEnd[macN - 1] = macTop;
				macDefining = 0;
				continue;
			}
			if (baseIs(base, "endr") && repActive) {
				macEnd[macN - 1] = macTop;
				macDefining = 0;
				repActive = 0;
				repExpand(macN - 1, repCount);
				macN--;
				continue;
			}
			macAppendLine(lxTmp);
			continue;
		}

		/* Bedingte Assemblierung: in einem uebersprungenen Block wird
		   nur noch nach if/else/endc gesehen -- alles andere darf dort
		   auch unuebersetzbar sein (gemessen: r68 meldet in einem
		   falschen ifdef-Zweig nicht einmal einen unbekannten Namen). */
		if (condDirective(base)) {
			doCond(base);
			continue;
		}
		if (condSkipN > 0)
			continue;

		/* Makrodefinition -- der Name steht im Labelfeld und wird
		   deshalb VOR der Labelbehandlung abgefangen. */
		if (baseIs(base, "macro")) {
			if (lnLabel[0] == 0)
				fatal("macro ohne Namen im Labelfeld", "");
			if (macN >= MAC_MAX)
				fatal("zu viele Makros (MAC_MAX): ", lnLabel);
			macName[macN] = intern(lnLabel);
			macStart[macN] = macTop;
			macEnd[macN] = macTop;
			macN++;
			macDefining = 1;
			continue;
		}
		if (baseIs(base, "rept")) {
			if (macN >= MAC_MAX)
				fatal("zu viele Makros (MAC_MAX)", "");
			repCount = evalExpr(lnArg);
			if (repCount < 0)
				fatal("rept mit negativer Anzahl: ", lnArg);
			macName[macN] = intern("*rept*");
			macStart[macN] = macTop;
			macEnd[macN] = macTop;
			macN++;
			macDefining = 1;
			repActive = 1;
			continue;
		}
		if (baseIs(base, "endm") || baseIs(base, "endr"))
			fatal("endm/endr ohne macro/rept", "");

		/* Im vsect entscheidet die Direktive der SELBEN Zeile, in
		   welchen Adressraum ein Label gehoert: "dc" in die
		   initialisierten Daten, "ds" in die reservierten. Deshalb
		   wird der Abschnitt VOR dem Label festgelegt. */
		if (curSect == SECT_IDATA || curSect == SECT_UDATA ||
		    curSect == SECT_RDATA) {
			if (baseIs(base, "ds")) {
				/* In einem "vsect remote" geht ds in den FERNbereich.
				   dc bleibt in den initialisierten Daten -- fuer
				   remote-INITIALISIERTE Daten (remoteidatsiz) gibt es
				   in dieser Kette keinen Aufrufer, und lieber nur der
				   gemessene Fall als ein geratener. */
				if (inRemoteVsect) {
					curSect = SECT_RDATA;
					curPC = rdataPC;
				} else {
					curSect = SECT_UDATA;
					curPC = udataPC;
				}
			} else if (baseIs(base, "dc") || baseIs(base, "dcb")) {
				curSect = SECT_IDATA;
				curPC = idataPC;
			} else if (baseIs(base, "ends") ||
				   baseIs(base, "endsect")) {
				/* faellt unten durch */
			} else if (lnOp[0] != 0 && !baseIs(base, "equ") &&
				   !baseIs(base, "set") && !baseIs(base, "align") &&
				   !baseIs(base, "use") && !baseIs(base, "org") &&
				   !baseIs(base, "do") && !baseIs(base, "nam") &&
				   !baseIs(base, "ttl") && !baseIs(base, "page") &&
				   !baseIs(base, "pag") && !baseIs(base, "opt") &&
				   !baseIs(base, "spc") && !baseIs(base, "end") &&
				   !baseIs(base, "fail")) {
				fatal("im vsect nur Daten und beschreibende Direktiven: ",
				      lnOp);
			}
		}

		/* Ausrichten, bevor das Label seinen Wert bekommt. */
		if (lineAligns())
			alignEven();

		/* Ab hier steht der Ort der Zeile fest -- das ist "*". */
		stmtPC = curPC;

		/* Ein ":"-Label wird nur dann GLOBAL, wenn es INNERHALB des
		   psect steht. Gemessen an einer Probe mit Labels davor, darin
		   und nach "ends": nur die inneren stehen in r68s
		   Globalenliste. Daran haengen die *stat-Dateien in SRC/DEFS,
		   die ihre Feldabstaende per "use" noch VOR der psect-Zeile
		   holen -- r68 legt fuer scfstat.a null Globale an, qr68 legte
		   21 an. Der Wert des Symbols gilt in beiden Faellen. */
		if (curSect == SECT_NONE)
			lnGlobal = 0;

		/* Label setzen, bevor der Befehl den Ort veraendert. */
		if (lnLabel[0] != 0) {
			int name;

			name = intern(lnLabel);
			if (baseIs(base, "do")) {
				/* Wie equ/set: das Label bekommt NICHT den Ort
				   im Abschnitt, sondern den org-Zaehler. */
				symDefine(name, doDo(size), SECT_ABS,
					  lnGlobal, 1);
				continue;
			}
			if (opIs("equ") || opIs("set")) {
				int v;
				int sx;

				/* Ein "set"-Symbol wird NIE global -- und ein
				   NEUES mit Doppelpunkt lehnt r68 sogar ab.
				   Gemessen (auch aus einer Include-Datei, auch
				   mit -q, auch in einer ifdef-Klammer):
				     Z:  set 2                 "illegal global
				                                symbol", keine
				                                Ausgabe
				     X   set 0 / X: set 1      angenommen, X ist
				                                NICHT global
				   Genau der zweite Fall steht im Korpus:
				   SYSMODS/INIT/init.a:165 setzt "Compat set 0",
				   und PORTS/RUSSBOX/systype.d:184 ueberschreibt
				   es mit "Compat: set $00". qr68 machte daraus
				   einen Globalen, den r68 nicht hat.
				   Bei "X: equ 1" ist der Doppelpunkt dagegen
				   ganz normal. */
				if (opIs("set") && lnGlobal) {
					int si;

					si = symFind(name);
					if (si < 0 || !symDefined[si])
						fatal("neues \"set\"-Symbol mit Doppelpunkt -- r68 meldet dafuer \"illegal global symbol\": ",
						      lnLabel);
					lnGlobal = 0;
				}
				v = evalExpr(lnArg);
				/* Steht rechts GENAU EIN externer Name, erbt
				   das Symbol ihn: "IRQCtrl equ u_icr" (so in
				   sc68070.a) muss bei jeder Benutzung wieder
				   eine Referenz auf u_icr erzeugen.
				   Bei mehreren -- im SDK kommt
				   "ILVLR4_default equ ILVLR4a+ILVLR4b+..."
				   mit lauter unbekannten Namen vor -- bleibt
				   nur der Zahlwert; r68 legt dafuer ebenfalls
				   keine Referenzen an (an sc68070.a
				   nachgeprueft: dessen Code und Referenzen
				   stimmen so byteweise). */
				if (termN[2] == 1 &&
				    termSect[16] == SECT_EXTERN) {
					sx = termName[16];
				} else {
					sx = -1;
					if (exSect == SECT_EXTERN)
						exSect = SECT_ABS;
				}
				/* "set" darf sich innerhalb eines Durchlaufs
				   aendern und zaehlt deshalb nicht als
				   Bewegung, "equ" schon. */
				symDefine(name, v, exSect, lnGlobal,
					  opIs("equ"));
				symExt[symIntern(name)] = sx;
				if (opIs("set"))
					symIsSet[symIntern(name)] = 1;
				continue;
			}
			symDefine(name, curPC, curSect, lnGlobal, 1);
		}

		if (lnOp[0] == 0)
			continue;

		if (baseIs(base, "psect")) {
			doPsect();
			continue;
		}
		if (baseIs(base, "vsect")) {
			/* "vsect remote" -- alles andere hinter vsect waere ein
			   Tippfehler, und den zu verschweigen waere genau der
			   Mangel, der hier behoben wird. */
			inRemoteVsect = 0;
			if (lnArg[0] != 0) {
				if (baseIs(lnArg, "remote"))
					inRemoteVsect = 1;
				else
					fatal("vsect kennt nur \"remote\": ", lnArg);
			}
			curSect = SECT_IDATA;
			curPC = idataPC;
			continue;
		}
		if (baseIs(base, "ends") || baseIs(base, "endsect")) {
			inRemoteVsect = 0;
			if (curSect == SECT_IDATA || curSect == SECT_UDATA ||
			    curSect == SECT_RDATA) {
				curSect = SECT_CODE;
				curPC = codeN;
			} else {
				curSect = SECT_NONE;
			}
			continue;
		}
		if (baseIs(base, "dc")) {
			if (size == 0)
				size = 'w';
			doDc(size);
			continue;
		}
		if (baseIs(base, "ds")) {
			if (size == 0)
				size = 'w';
			doDs(size);
			continue;
		}
		if (baseIs(base, "align")) {
			doAlign();
			continue;
		}
		if (baseIs(base, "use")) {
			doUse();
			continue;
		}
		if (baseIs(base, "org")) {
			orgPC = evalExpr(lnArg);
			continue;
		}
		if (baseIs(base, "do")) {
			doDo(size);
			continue;
		}
		if (baseIs(base, "end")) {
			break;
		}
		/* Nur beschreibend, ohne Wirkung auf die Ausgabe. */
		if (baseIs(base, "nam") || baseIs(base, "ttl") ||
		    baseIs(base, "page") || baseIs(base, "opt") ||
		    baseIs(base, "spc") || baseIs(base, "pag"))
			continue;
		if (baseIs(base, "fail"))
			fatal("fail: ", lnArg);
		if (baseIs(base, "equ") || baseIs(base, "set"))
			fatal("equ/set ohne Label", "");

		if (lnOpRaw[0] != 0) {
			int m;

			m = macFind(intern(lnOpRaw));
			if (m >= 0) {
				macExpand(m);
				continue;
			}
		}

		doInstruction();
	}
}

/* Haelt den Stand der "set"-Symbole nach dem ersten Durchlauf fest. */
static void symSnapshot(void)
{
	int i;

	for (i = 0; i < symN; i++) {
		if (!symIsSet[i])
			continue;
		symSnapVal[i] = symValue[i];
		symSnapSect[i] = symSect[i];
		symSnapped[i] = 1;
	}
}

/* Stellt ihn vor dem Ausgabelauf wieder her -- so sieht der dieselben Werte
   wie r68s zweiter Durchlauf. */
static void symRestore(void)
{
	int i;

	for (i = 0; i < symN; i++) {
		if (!symSnapped[i])
			continue;
		symValue[i] = symSnapVal[i];
		symSect[i] = symSnapSect[i];
	}
}

static void reportPass(void)
{
	if (!optVerbose)
		return;
	if (symMoved)
		printf("qr68: Durchlauf %d: %d Byte Code, %d Byte Daten, %d Symbole, bewegt\n",
		       pass, codeN, idataN, symN);
	else
		printf("qr68: Durchlauf %d: %d Byte Code, %d Byte Daten, %d Symbole\n",
		       pass, codeN, idataN, symN);
}

/* Vergleich zweier Namen ueber ihre Bytewerte -- fuer die alphabetische
   Reihenfolge der Globalen im ROF. */
static int nameLess(int a, int b)
{
	int i;
	int ca;
	int cb;

	i = 0;
	while (1) {
		ca = pool[a + i] & 255;
		cb = pool[b + i] & 255;
		if (ca != cb)
			return (ca < cb);
		if (ca == 0)
			return 0;
		i++;
	}
}

/* ============================================================ ROF schreiben */
static void writeRof(void)
{
	int i;
	int nGlob;
	int nExtNames;
	int j;
	int seen;

	/* r68 fuellt den Code auf ein Vielfaches von vier auf: erst ein
	   NULLBYTE, falls die Laenge ungerade ist, dann Leerbefehle. Beides
	   gemessen -- ein einzelnes "rts" ergibt codsz=4 mit $4E71 dahinter,
	   und eine Quelle, die auf einer ungeraden Laenge endet (644475),
	   wird mit genau EINEM $00 auf 644476 gebracht. Ohne den ersten
	   Schritt kaeme eine ungerade Laenge nie auf ein Vielfaches von vier.
	   Der zweite Schritt haengt an -m<n> (s. fillWord() und die
	   Optionsauswertung): mit -m0/-m1 unterbleibt er ganz. */
	if ((codeN % 2) != 0) {
		if (codeN >= CODE_MAX)
			fatal("Codespeicher voll (CODE_MAX)", "");
		codeBuf[codeN] = 0;
		codeN++;
	}
	if (!(optMpuSet && optMpu <= 1)) {
		while ((codeN % 4) != 0) {
			if (codeN + 1 >= CODE_MAX)
				fatal("Codespeicher voll (CODE_MAX)", "");
			codeBuf[codeN] = (fillWord() >> 8) & 255;
			codeBuf[codeN + 1] = fillWord() & 255;
			codeN = codeN + 2;
		}
	}

	/* Die beiden Datengroessen werden ebenfalls auf ein Vielfaches von
	   vier gebracht -- gemessen an "ds.b 1/2/3" (statstorage 4) gegen
	   "ds.b 5" (8) und "ds.b 9" (12), und ein einzelnes "dc.b 1" ergibt
	   idatsz 4 mit drei Nullbytes im Inhalt. */
	while ((idataN % 4) != 0) {
		if (idataN >= IDATA_MAX)
			fatal("Datenspeicher voll (IDATA_MAX)", "");
		idataBuf[idataN] = 0;
		idataN++;
	}
	while ((statStorage % 4) != 0)
		statStorage++;

	nGlob = 0;
	for (i = 0; i < symN; i++) {
		if (symGlobal[i] && symDefined[i])
			nGlob++;
	}

	outLong(0xDEADFACE);
	outWord(psTyLan);
	outWord(psAttRev);
	outWord(0);                    /* valid */
	outWord(249);                  /* series -- wie r68 V2.9.1 */
	outByte(dtYear);
	outByte(dtMonth);
	outByte(dtDay);
	outByte(dtHour);
	outByte(dtMin);
	outByte(dtSec);
	outWord(psEdition);
	outLong(statStorage);
	outLong(idataN);
	outLong(codeN);
	outLong(psStack);
	outLong(psEntry);
	outLong(psTrap);
	outLong(remoteStatStorage);    /* remotestatsiz */
	outLong(0);                    /* remoteidatsiz -- kein Aufrufer */
	outLong(0);                    /* debugsiz */
	outStrZ(poolAt(psName));

	/* Globale Definitionen -- ALPHABETISCH sortiert. Das ist keine
	   Kosmetik: r68 sortiert (gemessen an einer Quelle mit der Reihenfolge
	   wert/puffer/start, ausgegeben wurde puffer/start/wert), und ohne
	   dieselbe Reihenfolge gibt es keine Byteidentitaet. Sortiert wird
	   ueber die Bytewerte des Namens.
	   Typwoerter, ebenfalls gemessen: Code $0004, initialisierte Daten
	   $0001, reservierte Daten $0000, reservierte FERNdaten $0002. */
	outLong(nGlob);
	{
		int done;
		int best;
		int marked[QR_SYM];

		for (i = 0; i < symN; i++)
			marked[i] = 0;
		for (done = 0; done < nGlob; done++) {
			best = -1;
			for (i = 0; i < symN; i++) {
				if (!symGlobal[i] || !symDefined[i] || marked[i])
					continue;
				if (best < 0) {
					best = i;
					continue;
				}
				if (nameLess(symName[i], symName[best]))
					best = i;
			}
			if (best < 0)
				fatal("innerer Fehler: Globale nicht abzaehlbar", "");
			marked[best] = 1;
			outStrZ(poolAt(symName[best]));
			if (symSect[best] == SECT_CODE)
				outWord(0x0004);
			else if (symSect[best] == SECT_IDATA)
				outWord(0x0001);
			else if (symSect[best] == SECT_UDATA)
				outWord(0x0000);
			else if (symSect[best] == SECT_RDATA)
				outWord(0x0002);
			else if (symSect[best] == SECT_ABS)
				/* Ein globales equ auf einen festen Wert --
				   gemessen: Typ $0006, und in der Adresse
				   steht der Wert selbst. */
				outWord(0x0006);
			else
				fatal("globaler Typ noch nicht gemessen: ",
				      poolAt(symName[best]));
			outLong(symValue[best]);
		}
	}

	/* Code */
	for (i = 0; i < codeN; i++)
		outByte(codeBuf[i] & 255);

	/* Initialisierte Daten */
	for (i = 0; i < idataN; i++)
		outByte(idataBuf[i] & 255);

	/* Externe Namen mit ihren Referenzen: je Name ein Eintrag, darunter
	   alle Vorkommen -- so legt r68 es ab (gemessen an drei jsr auf zwei
	   verschiedene Namen). */
	nExtNames = 0;
	for (i = 0; i < refN; i++)
		refDone[i] = 0;
	for (i = 0; i < refN; i++) {
		if (refLocal[i])
			continue;
		seen = 0;
		for (j = 0; j < i; j++) {
			if (!refLocal[j] && refName[j] == refName[i])
				seen = 1;
		}
		if (!seen)
			nExtNames++;
	}
	/* Die Namen stehen ALPHABETISCH, wie die Globalen -- gemessen an einer
	   Quelle, die erst "realloc" und dann "_os_write" braucht: ausgegeben
	   wird "_os_write" zuerst ($5f vor $72). Die Referenzen unter einem
	   Namen stehen dagegen aufsteigend nach Offset. */
	outLong(nExtNames);
	{
		int done;
		int best;
		int cnt;

		for (done = 0; done < nExtNames; done++) {
			best = -1;
			for (i = 0; i < refN; i++) {
				if (refLocal[i])
					continue;
				seen = 0;
				for (j = 0; j < refN; j++) {
					if (refLocal[j])
						continue;
					if (refName[j] != refName[i])
						continue;
					if (refDone[j])
						seen = 1;
				}
				if (seen)
					continue;
				if (best < 0 ||
				    nameLess(refName[i], refName[best]))
					best = i;
			}
			if (best < 0)
				fatal("innerer Fehler: externe Namen nicht abzaehlbar", "");
			cnt = 0;
			for (j = 0; j < refN; j++) {
				if (!refLocal[j] && refName[j] == refName[best]) {
					cnt++;
					refDone[j] = 1;
				}
			}
			outStrZ(poolAt(refName[best]));
			outLong(cnt);
			for (j = 0; j < refN; j++) {
				if (!refLocal[j] && refName[j] == refName[best]) {
					outWord(refType[j]);
					outLong(refOffs[j]);
				}
			}
		}
	}

	/* Lokale Referenzen: erst die, die IM CODE liegen, dann die in den
	   initialisierten Daten -- je Gruppe mit ABSTEIGENDEM Offset.
	   Gemessen an zwei Quellen mit vsect vor bzw. hinter dem Code: die
	   Reihenfolge haengt nicht an der Quellreihenfolge, sondern an der
	   Gruppe (r68 haengt sie offenbar je Abschnitt vorne an eine Liste).
	   Die Gruppe steht im Typwort: Bit $20 = liegt im Code.
	   Die externen Referenzen dagegen stehen aufsteigend. */
	{
		int cnt;
		int inCode;

		cnt = 0;
		for (i = 0; i < refN; i++) {
			if (refLocal[i])
				cnt++;
		}
		outLong(cnt);
		for (inCode = 1; inCode >= 0; inCode--) {
			for (i = refN - 1; i >= 0; i--) {
				if (!refLocal[i])
					continue;
				if (inCode && (refType[i] & 0x20) == 0)
					continue;
				if (!inCode && (refType[i] & 0x20) != 0)
					continue;
				outWord(refType[i]);
				outLong(refOffs[i]);
			}
		}
	}

	/* Am Schluss schreibt r68 VIER Langwoerter, in allen gemessenen
	   Faellen null. Was sie bedeuten, ist offen: die einzige Beschreibung
	   des Formats (osk-disasm/rof.c) hat genau diesen Abschnitt
	   auskommentiert -- "common block variables... Do this after
	   everything else is done". Sobald ein Fall auftritt, in dem sie nicht
	   null sind, wird er gemessen; blind gefuellt wird hier nichts, die
	   Nullen sind das Messergebnis. */
	outLong(0);
	outLong(0);
	outLong(0);
	outLong(0);
	outFlush();
}

/* ================================================================== main == */
static int argEq(const char *a, const char *b)
{
	int i;

	i = 0;
	while (a[i] != 0 && b[i] != 0) {
		if (a[i] != b[i])
			return 0;
		i++;
	}
	if (a[i] != b[i])
		return 0;
	return 1;
}

static int argStarts(const char *a, const char *pre)
{
	int i;

	i = 0;
	while (pre[i] != 0) {
		if (a[i] != pre[i])
			return 0;
		i++;
	}
	return i;
}

static void usage(void)
{
	printf("qr68 -- 68k-Assembler der Q9-Kette, Ausgabe als OS-9-ROF\n");
	printf("Aufruf: qr68 [Optionen] <eingabe.a> <ausgabe.r>\n");
	printf("    oder qr68 [Optionen] -o=<ausgabe.r> <eingabe.a>\n");
	printf("  -o=<datei>, -O=<datei> Ausgabedatei (wie r68 -- so rufen die\n");
	printf("                        SDK-Makefiles den Assembler auf)\n");
	printf("  -v                    gelesene/geschriebene Byteanzahl melden\n");
	printf("  -u=<verz>             Suchverzeichnis fuer \"use <datei>\"\n");
	printf("  -a<sym>[=<wert>]      Symbol setzen (ohne Wert: 1)\n");
	printf("  -b                    Sprungweiten selbst waehlen (wie r68 -b)\n");
	printf("  -q                    angenommen und ignoriert (r68-Kompatibilitaet)\n");
	printf("  -fdate=J,M,T,S,Mi,Se  Zeitstempel im ROF-Kopf setzen\n");
	printf("                        (J = Jahr-1900; fuer den Vergleich mit r68)\n");
	exit(2);
}

static void parseDate(const char *s)
{
	int vals[6];
	int nv;
	int v;
	int i;

	for (i = 0; i < 6; i++)
		vals[i] = 0;
	nv = 0;
	i = 0;
	while (s[i] != 0 && nv < 6) {
		v = 0;
		while (s[i] >= '0' && s[i] <= '9') {
			v = v * 10 + (s[i] - '0');
			i++;
		}
		vals[nv] = v;
		nv++;
		if (s[i] == ',')
			i++;
		else if (s[i] != 0)
			fatal("-fdate= erwartet Zahlen mit Komma", "");
	}
	if (nv != 6)
		fatal("-fdate= braucht sechs Werte: Jahr-1900,Monat,Tag,Stunde,Minute,Sekunde", "");
	dtYear = vals[0];
	dtMonth = vals[1];
	dtDay = vals[2];
	dtHour = vals[3];
	dtMin = vals[4];
	dtSec = vals[5];
}

/* Function: main
 * Parses assembler options, assembles the input and writes one ROF object.
 * Parameters: argc, argv Command-line argument count and vector.
 * Returns: Process status, zero on success. */
int main(int argc, char **argv)
{
	int i;
	int k;
	const char *inPath;
	const char *outPath;
	int id;

	inPath = 0;
	outPath = 0;
	curFile = -1;
	curLine = 0;
	selfCheck();
	hashInit();

	for (i = 1; i < argc; i++) {
		char *a;

		a = argv[i];
		if (argEq(a, "-v")) {
			optVerbose = 1;
			continue;
		}
		if (argEq(a, "-b")) {
			optBranch = 1;
			continue;
		}
		if (argEq(a, "-qb") || argEq(a, "-bq")) {
			optBranch = 1;
			continue;
		}
		if (argEq(a, "-bt") || argEq(a, "-y") || argEq(a, "-j") ||
		    argStarts(a, "-p") > 0) {
			/* Diese Schalter aendern die Ausgabe und sind nicht
			   gemessen: -bt/-y machen Spruenge lang, -j legt eine
			   Sprungtabelle an, -p richtet alle org aus. */
			printf("qr68: Schalter %s aendert die Ausgabe und ist nicht nachgebildet\n",
			       a);
			exit(2);
		}
		if (a[0] == '-' && a[1] == 'm' && a[2] != 0) {
			/* -m<n> waehlt die Ziel-CPU -- und das AENDERT DIE
			   AUSGABE, anders als hier lange angenommen. Gemessen
			   an einem psect mit einem einzelnen "rts":
			     ohne -m      codsz=4, aufgefuellt mit $4e71 (nop)
			     -m0 / -m1    codsz=2, GAR NICHT auf 4 aufgefuellt
			     -m2 .. -m6   codsz=4, aufgefuellt mit $51fc
			   $51fc ist "trapf", der 2-Byte-Leerbefehl des 68020.
			   Dasselbe gilt fuer "align" mitten im Code. Ab -m2
			   nimmt r68 ausserdem den SKALIERTEN INDEX an, darunter
			   lehnt es ihn ab ("illegal addressing mode").
			   Daran haengen SYSCACHE (cache030/040/349 werden mit
			   -m3/-m4 gebaut) und die CPU32-Ports (-m2). */
			optMpu = 0;
			k = 2;
			while (a[k] >= '0' && a[k] <= '9') {
				optMpu = optMpu * 10 + (a[k] - '0');
				k++;
			}
			if (a[k] != 0)
				fatal("-m erwartet eine Zahl: ", a);
			optMpuSet = 1;
			continue;
		}
		if (a[0] == '-' && a[1] == 'd' && a[2] != 0) {
			/* -d<n> ist die Zeilenzahl je Listenseite -- betrifft
			   nur das Listing. */
			continue;
		}
		if (argEq(a, "-l") || argEq(a, "-g") || argEq(a, "-e") ||
		    argEq(a, "-s") || argEq(a, "-n") || argEq(a, "-x") ||
		    argEq(a, "-c") || argEq(a, "-f") || argEq(a, "-r")) {
			/* Listing- und Meldungsschalter von r68: angenommen
			   und uebergangen, damit die Aufrufe der SDK-Makefiles
			   unveraendert laufen. */
			continue;
		}
		if (a[0] == '-' && a[1] == 'q') {
			/* r68 unterdrueckt damit Warnungen; qr68 gibt ohnehin
			   nur Fehler aus. Angenommen, damit die Aufrufe der
			   SDK-Makefiles unveraendert laufen -- und zwar auch
			   MIT angehaengtem Text: das Makefile von
			   CPU32/PORTS/QUADS/ROM_CBOOT schreibt "-qQUADS360"
			   (offenbar ein vertipptes "-a"), und r68 verwirft den
			   Zusatz stillschweigend. Gemessen: "-qQUADS360"
			   definiert das Symbol QUADS360 NICHT, es wirkt genau
			   wie ein nacktes "-q". ("-qb" faengt der Zweig
			   darueber ab.) */
			continue;
		}
		if (a[0] == '-' && a[1] == 'a' && a[2] != 0) {
			/* -a<sym>[=<wert>] oder -a=<sym>[=<wert>]: Symbol von
			   der Kommandozeile. Ohne Wert ist es 1 (gemessen). */
			k = 2;
			if (a[2] == '=')
				k = 3;
			if (argDefN >= ARGDEF_MAX)
				fatal("zu viele -a-Symbole (ARGDEF_MAX)", "");
			{
				int j;
				int val;

				j = k;
				while (a[j] != 0 && a[j] != '=')
					j++;
				argDefName[argDefN] = internN(&a[k], j - k);
				val = 1;
				if (a[j] == '=')
					val = evalExpr(&a[j + 1]);
				argDefVal[argDefN] = val;
				argDefN++;
			}
			continue;
		}
		k = argStarts(a, "-u=");
		if (k > 0 && a[k] != 0) {
			if (useDirN >= USEDIR_MAX)
				fatal("zu viele -u=-Verzeichnisse (USEDIR_MAX)", "");
			useDirs[useDirN] = intern(&a[k]);
			useDirN++;
			continue;
		}
		k = argStarts(a, "-o=");
		if (k == 0)
			k = argStarts(a, "-O=");
		if (k > 0 && a[k] != 0) {
			/* So benennen die SDK-Makefiles die Ausgabe -- und zwar
			   ausnahmslos: von den 300 Makefiles, die r68 aufrufen,
			   benutzt keines die Stellung. Beide Schreibungen kommen
			   vor ("-o=$(RDIR)/$@" und "-O=$@"), und die Stellung
			   relativ zur Quelle ist r68 egal (gemessen). Ohne
			   Ausgabeangabe schreibt r68 GAR NICHTS -- es gibt keinen
			   Vorgabenamen; qr68 bleibt dabei, das als Aufruffehler
			   zu melden, statt still nichts zu tun. */
			if (outPath != 0) {
				printf("qr68: Ausgabedatei zweimal angegeben: %s\n", a);
				exit(2);
			}
			outPath = &a[k];
			continue;
		}
		k = argStarts(a, "-fdate=");
		if (k > 0 && a[k] != 0) {
			parseDate(&a[k]);
			continue;
		}
		if (argEq(a, "-h") || argEq(a, "-?"))
			usage();
		if (a[0] == '-' && a[1] != 0) {
			printf("qr68: unbekannte Option %s\n", a);
			usage();
		}
		if (inPath == 0)
			inPath = a;
		else if (outPath == 0)
			outPath = a;
		else
			usage();
	}
	if (inPath == 0 || outPath == 0)
		usage();

	id = fileLoad(inPath);
	if (id < 0) {
		printf("qr68: Eingabe nicht lesbar: %s\n", inPath);
		exit(1);
	}

	/* Mehrere Messdurchlaeufe, dann einer zum Ausgeben. Zwei feste
	   Durchlaeufe reichen NICHT: r68 verkuerzt "add.l #4,d0" zu ADDQ, und
	   zwar auch dann, wenn der Wert erst weiter unten definiert wird
	   (gemessen). Die Befehlslaenge haengt damit an Symbolwerten, ein
	   Vorwaertsbezug kann also alle Adressen dahinter verschieben.
	   Abgebrochen wird erst, wenn sich zwei Durchlaeufe hintereinander
	   nichts mehr bewegt -- ein einzelner sauberer Durchlauf genuegt
	   nicht, denn der erste kennt die Vorwaertsbezuege noch gar nicht und
	   meldet deshalb faelschlich Ruhe. */
	{
		int ruhig;

		emitting = 0;
		pass = 0;
		ruhig = 0;
		while (ruhig < 2) {
			pass++;
			if (pass > 12)
				fatal("Adressen werden nicht stabil (mehr als 12 Durchlaeufe)",
				      "");
			runPass();
			reportPass();
			if (pass == 1)
				symSnapshot();
			if (symMoved)
				ruhig = 0;
			else
				ruhig++;
		}
		emitting = 1;
		pass++;
		symRestore();
		runPass();
		reportPass();
	}

	if (!psSeen)
		fatal("kein psect in der Quelle", "");

	outFp = fopen(outPath, "w");
	if (outFp == 0) {
		printf("qr68: Ausgabe nicht schreibbar: %s\n", outPath);
		exit(1);
	}
	writeRof();
	fclose(outFp);
	if (optVerbose)
		printf("qr68: geschrieben: %d Byte Code, %d Byte Daten nach %s\n",
		       codeN, idataN, outPath);
	return 0;
}
