/*
 * qcpp -- C preprocessor for the Q9 toolchain
 *
 * Usage: qcpp [options] <input.c> <output.i>
 *
 * Purpose:
 *   Provide a self-hostable ISO C89 preprocessor for the Q9 compiler chain.
 *   The implementation is deliberately limited to the language subset that
 *   can be compiled by QCC itself and supports the OS-9 #asm extension.
 *
 * Edition history:
 *   2026-09-11  Introduced the English source-header format.
 *   2026-09-11  Replaced local stdio declarations with standard headers.
 *
 * The detailed compatibility measurements and design decisions are kept in
 * the project documentation rather than in this source header.
 *
 * Language scope: ISO C89 plus the Microware #asm/#endasm extension.
 *
 * Compatibility measurements, implementation constraints and design history
 * belong in the project documentation, not in this source header.
 */

/* --------------------------------------------------------------- libc ---- */
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------ Limits ----- */
/* Array sizes must be literals because QCC's constSize accepts only numbers
   and + - *. The mirror values below carry the same limits for range checks;
   selfCheck() compares both through sizeof so they cannot drift unnoticed. */
#define POOL_MAX 524288
static char pool[POOL_MAX];
static int poolTop;

#define SRC_MAX 2097152
static char srcArena[SRC_MAX];
static int srcTop;

#define FILE_MAX 512
static int flName[512];
static int flDir[512];
static int flStart[512];
static int flEnd[512];
static int flOnce[512];     /* File has seen "#pragma once". */
static int flN;

#define INC_MAX 64
static int isFile[64];
static int isPos[64];
static int isLine[64];
static int isCd[64];        /* #if depth while including; see main loop. */
static int isDepth;

#define PB_MAX 16384
static int pbKind[16384];
static int pbText[16384];
static int pbLine[16384];
static int pbFile[16384];
static int pbWs[16384];
static int pbN;

#define MAC_MAX 4096
static int macName[4096];
static int macFunc[4096];
static int macNPar[4096];
static int macParAt[4096];
static int macBodyAt[4096];
static int macBodyN[4096];
static int macInUse[4096];
static int macKind[4096];   /* 0 = normal, 1 = __FILE__, 2 = __LINE__ */
static int macN;

#define PAR_MAX 8192
static int parName[8192];
static int parTop;

#define MT_MAX 32768
static int mtKind[32768];
static int mtText[32768];
static int mtWs[32768];
static int mtTop;

#define AG_MAX 16384
static int agKind[16384];
static int agText[16384];
static int agWs[16384];
static int agTop;

/* Replacement storage is a stack (exTop), not a simple array starting at 0:
   substitute() recursively calls itself while prescanning an argument, so a
   shared array starting at 0 would overwrite the outer replacement. */
#define EX_MAX 16384
static int exKind[16384];
static int exText[16384];
static int exWs[16384];
static int exTop;

#define EV_MAX 4096
static int evKind[4096];
static int evText[4096];
static int evN;
static int evI;

#define CD_MAX 64
static int cdActive[64];
static int cdTaken[64];
static int cdElse[64];
static int cdDepth;

#define DIR_MAX 64
static int dirPath[64];
static int dirN;

#define HASH_BUCKETS 8192
static int hashHead[8192];
#define HASH_MAX 32768
static int hashNext[32768];
static int hashText[32768];
static int hashN;

static char lxTmp[8192];
#define LXTMP_MAX 8192

static char outBuf[8192];
static int outN;
static int outTotal;
static FILE *outFp;

/* ---------------------------------------------------------- Tokenarten ---- */
static int TK_EOF = 0;
static int TK_ID = 1;
static int TK_NUM = 2;
static int TK_STR = 3;
static int TK_CH = 4;
static int TK_PUNCT = 5;
static int TK_OTHER = 6;
static int TK_NL = 7;
static int TK_ENDMAC = 8;
static int TK_ARGEND = 9;

/* Current token. */
static int tkKind;
static int tkText;
static int tkLine;
static int tkFile;
static int tkWs;
static int tkFromPB;

/* Lexer state for the current file. */
static int lxFile;
static int lxPos;
static int lxLine;

/* Peek state: rdPeek() returns the logical character and stores the state
   AFTER consumption in pkPos/pkLine; rdTake() commits it. The lexer needs no
   ungetc, and line counting remains correct across continuations. */
static int pkCh;
static int pkPos;
static int pkLine;

/* Options. */
static int optLines;
static int optMin;
static int optAsmStrip;
static int optVerbose;
static int skipping;
static int atBOL;
static int inAsm;

/* Output state. */
static int lastFile;
static int lastLine;
static int lastCh;
static int atOutBOL;

static int textFile;   /* Pool-Index "__FILE__" usw., in setupBuiltins gesetzt */
static int textLine;
static int textDefined;
static int textEndasm;

/* ============================================================== Zeichen === */
/* Function: isSpaceCh
 * Tests whether a byte is one of the supported C whitespace characters.
 * Parameters: c Byte value.
 * Returns: Non-zero when c is whitespace. */
static int isSpaceCh(int c)
{
	/* Recognize C whitespace independently of the host locale. */
	if (c == ' ' || c == 9 || c == 11 || c == 12 || c == 13)
		return 1;
	return 0;
}

/* Function: isDigitCh
 * Tests whether a byte is an ASCII decimal digit.
 * Parameters: c Byte value.
 * Returns: Non-zero for '0' through '9'. */
static int isDigitCh(int c)
{
	/* Preprocessor numbers are ASCII tokens, not locale-dependent. */
	if (c >= '0' && c <= '9')
		return 1;
	return 0;
}

/* Function: isAlphaCh
 * Tests whether a byte can start a QCC identifier.
 * Parameters: c Byte value.
 * Returns: Non-zero for ASCII letters and underscore. */
static int isAlphaCh(int c)
{
	/* Identifier alphabet of the QCC subset: ASCII letters plus underscore. */
	if (c >= 'a' && c <= 'z')
		return 1;
	if (c >= 'A' && c <= 'Z')
		return 1;
	if (c == '_')
		return 1;
	return 0;
}

/* Function: isAlnumCh
 * Tests whether a byte can continue a QCC identifier.
 * Parameters: c Byte value.
 * Returns: Non-zero for identifier letters, digits and underscore. */
static int isAlnumCh(int c)
{
	if (isAlphaCh(c) || isDigitCh(c))
		return 1;
	return 0;
}

/* Function: strLen
 * Calculates the length of a NUL-terminated string.
 * Parameters: s Input string.
 * Returns: Number of characters before the terminating NUL. */
static int strLen(const char *s)
{
	int n;

	n = 0;
	while (s[n] != 0)
		n++;
	return n;
}

/* ================================================================= Pool === */
/* Function: poolAt
 * Returns a pointer into the interned string pool.
 * Parameters: idx Pool index.
 * Returns: Address of the string at idx. */
static char *poolAt(int idx)
{
	return &pool[idx];
}

/* Function: poolEq
 * Compares an interned pool string with an external string.
 * Parameters: idx Pool index; s External string.
 * Returns: Non-zero when both strings are equal. */
static int poolEq(int idx, const char *s)
{
	int i;

	i = 0;
	while (s[i] != 0) {
		if (pool[idx + i] != s[i])
			return 0;
		i++;
	}
	if (pool[idx + i] != 0)
		return 0;
	return 1;
}

static void fatal(const char *msg, const char *detail);

/*
 * Function: internN
 *
 * Interns a byte range in the shared string pool and returns its pool index.
 *
 * Parameters:
 *   s  Input character range.
 *   n  Number of characters, excluding the terminating NUL.
 *
 * Returns:
 *   Pool index of the unique string. Terminates the process if a table is full.
 */
static int internN(const char *s, int n)
{
	int h;
	int i;
	int e;
	int idx;
	int same;
	int j;

	h = 0;
	for (i = 0; i < n; i++)
		h = h * 31 + s[i];
	if (h < 0)
		h = -h;
	h = h % 8192;

	e = hashHead[h];
	while (e != 0) {
		idx = hashText[e - 1];
		same = 1;
		for (j = 0; j < n; j++) {
			if (pool[idx + j] != s[j]) {
				same = 0;
				j = n;
			}
		}
		if (same && pool[idx + n] == 0)
			return idx;
		e = hashNext[e - 1];
	}

	if (poolTop + n + 1 >= POOL_MAX)
		fatal("Zeichenspeicher voll (POOL_MAX)", "");
	if (hashN >= HASH_MAX)
		fatal("Namenstabelle voll (HASH_MAX)", "");
	idx = poolTop;
	for (j = 0; j < n; j++)
		pool[idx + j] = s[j];
	pool[idx + n] = 0;
	poolTop = poolTop + n + 1;

	hashText[hashN] = idx;
	hashNext[hashN] = hashHead[h];
	hashHead[h] = hashN + 1;
	hashN++;
	return idx;
}

/*
 * Function: intern
 *
 * Interns a NUL-terminated string in the shared pool.
 *
 * Parameters:
 *   s  Input string.
 *
 * Returns:
 *   Pool index of the string.
 */
static int intern(const char *s)
{
	return internN(s, strLen(s));
}

/* ============================================================ Diagnosen === */
/*
 * Function: fatal
 *
 * Reports a preprocessing error and terminates the process.
 *
 * Parameters:
 *   msg, detail  Primary and supplementary diagnostic text.
 *
 * Returns:
 *   Does not return.
 */
static void fatal(const char *msg, const char *detail)
{
	const char *fn;

	fn = "<no file>";
	if (lxFile >= 0 && lxFile < flN)
		fn = poolAt(flName[lxFile]);
	printf("qcpp: %s:%d: %s%s\n", fn, lxLine, msg, detail);
	exit(1);
}

/*
 * Function: warn
 *
 * Reports a non-fatal preprocessing diagnostic.
 *
 * Parameters:
 *   msg, detail  Primary and supplementary diagnostic text.
 *
 * Returns:
 *   Nothing.
 */
static void warn(const char *msg, const char *detail)
{
	const char *fn;

	fn = "<keine Datei>";
	if (lxFile >= 0 && lxFile < flN)
		fn = poolAt(flName[lxFile]);
	printf("qcpp: %s:%d: warning: %s%s\n", fn, lxLine, msg, detail);
}

/* ================================================================ Files === */
/* Directory part of a path without a trailing separator, or "" when absent.
   Supports both "/" (OS-9/POSIX) and "\" (SDK paths under Wine). */
/*
 * Function: dirOfPath
 *
 * Extracts and interns the directory part of a path.
 *
 * Parameters:
 *   path  Input path.
 *
 * Returns:
 *   Pool index of the directory, or the empty string when absent.
 */
static int dirOfPath(const char *path)
{
	int n;
	int cut;
	int i;

	n = strLen(path);
	cut = -1;
	for (i = 0; i < n; i++) {
		if (path[i] == '/' || path[i] == 92)
			cut = i;
	}
	if (cut < 0)
		return intern("");
	return internN(path, cut);
}

/*
 * Function: fileLoad
 *
 * Loads one source file into the shared source arena and registers its
 * include metadata.
 *
 * Parameters:
 *   path  Source file path.
 *
 * Returns:
 *   The file identifier, or -1 when the file cannot be opened.
 */
static int fileLoad(const char *path)
{
	FILE *fp;
	int got;
	int want;
	int start;
	int id;

	fp = fopen(path, "r");
	if (fp == 0)
		return -1;
	if (flN >= FILE_MAX)
		fatal("too many files (FILE_MAX)", "");

	start = srcTop;
	while (1) {
		if (srcTop >= SRC_MAX)
			fatal("Quelltextspeicher voll (SRC_MAX)", "");
		/* Read in 4 KiB chunks instead of requesting the entire remaining
		   arena. A single fread larger than 2 MiB returned zero on OS-9/68K;
		   the target clib or RBF driver does not accept that size. */
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
	if (optVerbose)
		printf("qcpp: gelesen: %d Byte aus %s\n", srcTop - start, path);

	id = flN;
	flName[id] = intern(path);
	flDir[id] = dirOfPath(path);
	flStart[id] = start;
	flEnd[id] = srcTop;
	flOnce[id] = 0;
	flN++;
	return id;
}

/* Was this path already loaded and did it contain "#pragma once"? Comparison
   uses the load path, so the same header through "./x.h" and "x.h" is not
   recognized as identical. Real preprocessors use device and inode data;
   those system calls differ across targets, so paths are compared here. */
/*
 * Function: onceSeen
 *
 * Checks whether a file has already been protected by #pragma once.
 *
 * Parameters:
 *   path  File path to check.
 *
 * Returns:
 *   Non-zero when the file was seen as a once-only include.
 */
static int onceSeen(const char *path)
{
	int name;
	int i;

	name = intern(path);
	for (i = 0; i < flN; i++) {
		if (flName[i] == name && flOnce[i])
			return 1;
	}
	return 0;
}

/* ================================================================ Lexer === */
/* Returns the logical character at the current position: line continuations
   ("\" unmittelbar vor dem Umbruch) sind uebersprungen, die dabei
   skipped newlines are counted in pkLine. -1 means end of file.
 *
 * All three newline forms are also normalized to LF so the rest of the lexer
 * only has to handle 10:
 *   LF       Unix / host
 *   CR+LF    DOS -- used by parts of the SDK sources
 *   CR       OS-9 -- used by every target text file
 * The CR-only case was found in the emulator on 2026-09-02: qcpp read the
 * complete input (590 bytes) but produced no output. Without LF the whole
 * file was treated as one line, so the first "#define" consumed the rest as
 * its macro body. The host did not expose this target-specific failure. */
/*
 * Function: rdPeek
 *
 * Peeks at the next raw source byte without consuming it.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   Next byte value, or -1 at end of input.
 */
static int rdPeek(void)
{
	int p;
	int line;
	int end;
	int c;

	p = lxPos;
	line = lxLine;
	end = flEnd[lxFile];

	while (1) {
		if (p >= end) {
			pkCh = -1;
			pkPos = p;
			pkLine = line;
			return -1;
		}
		c = srcArena[p] & 255;
		if (c == 92 && p + 1 < end && srcArena[p + 1] == 10) {
			p = p + 2;
			line++;
			continue;
		}
		/* "\" + CR + LF (DOS) and "\" + CR alone (OS-9). */
		if (c == 92 && p + 1 < end && srcArena[p + 1] == 13) {
			p = p + 2;
			if (p < end && srcArena[p] == 10)
				p = p + 1;
			line++;
			continue;
		}
		break;
	}

	if (c == 13) {
		/* Report CR and CR+LF as LF; see the lexer comment above. */
		pkCh = 10;
		pkPos = p + 1;
		if (pkPos < end && srcArena[pkPos] == 10)
			pkPos = pkPos + 1;
		pkLine = line + 1;
		return 10;
	}

	pkCh = c;
	pkPos = p + 1;
	pkLine = line;
	if (c == 10)
		pkLine = line + 1;
	return c;
}

/*
 * Function: rdTake
 *
 * Consumes one raw source byte and updates source position state.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   Nothing.
 */
static void rdTake(void)
{
	lxPos = pkPos;
	lxLine = pkLine;
}

/* Function: lexAppend
 * Appends one byte to the temporary token buffer.
 * Parameters: n Current length; c Byte to append.
 * Returns: Updated token length. */
static int lexAppend(int n, int c)
{
	if (n + 1 >= LXTMP_MAX)
		fatal("token too long (LXTMP_MAX)", "");
	lxTmp[n] = c;
	return n + 1;
}

/* Read one token from the file and update tkKind/tkText/tkLine/tkFile/tkWs. */
/*
 * Function: lexNext
 *
 * Reads the next preprocessing token from the raw input stream.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   Nothing; updates the current token state.
 */
static void lexNext(void)
{
	int c;
	int c2;
	int n;
	int ws;
	int startLine;
	int closed;

	ws = 0;

	/* Whitespace and comments. A block comment spanning lines counts as
	   whitespace; line counting continues so output remains line-accurate. */
	while (1) {
		c = rdPeek();
		if (c < 0)
			break;
		if (isSpaceCh(c)) {
			rdTake();
			ws = 1;
			continue;
		}
		if (c == '/') {
			rdTake();
			c2 = rdPeek();
			if (c2 == '*') {
				rdTake();
				startLine = lxLine;
				closed = 0;
				while (1) {
					c = rdPeek();
					if (c < 0)
						break;
					rdTake();
					if (c == '*') {
						c2 = rdPeek();
						if (c2 == '/') {
							rdTake();
							closed = 1;
							break;
						}
					}
				}
				if (!closed) {
					lxLine = startLine;
					fatal("Blockkommentar nicht geschlossen", "");
				}
				ws = 1;
				continue;
			}
			if (c2 == '/') {
				/* Line comments are not part of C89, but 22 SDK files use them;
				   accept them as a documented extension. */
				rdTake();
				while (1) {
					c = rdPeek();
					if (c < 0)
						break;
					if (c == 10)
						break;
					rdTake();
				}
				ws = 1;
				continue;
			}
			/* A real "/"; it was already consumed, so emit it as a token. */
			tkKind = TK_PUNCT;
			if (c2 == '=') {
				rdTake();
				tkText = intern("/=");
			} else {
				tkText = intern("/");
			}
			tkLine = lxLine;
			tkFile = lxFile;
			tkWs = ws;
			return;
		}
		break;
	}

	tkWs = ws;
	tkFile = lxFile;
	tkLine = lxLine;

	c = rdPeek();
	if (c < 0) {
		tkKind = TK_EOF;
		tkText = intern("");
		return;
	}

	if (c == 10) {
		rdTake();
		tkKind = TK_NL;
		tkText = intern("\n");
		return;
	}

	/* Identifier. */
	if (isAlphaCh(c)) {
		n = 0;
		while (1) {
			c = rdPeek();
			if (c < 0 || !isAlnumCh(c))
				break;
			rdTake();
			n = lexAppend(n, c);
		}
		tkKind = TK_ID;
		tkText = internN(lxTmp, n);
		return;
	}

	/* C89 3.1.8 preprocessing number: starts with a digit (or "." followed
	   by a digit) and then consumes letters, digits, dots and exponent signs.
	   This also accepts assembler numbers such as "$0515B007" in #asm blocks. */
	if (isDigitCh(c)) {
		n = 0;
		while (1) {
			c = rdPeek();
			if (c < 0)
				break;
			if (isAlnumCh(c) || c == '.') {
				rdTake();
				n = lexAppend(n, c);
				if (c == 'e' || c == 'E') {
					c2 = rdPeek();
					if (c2 == '+' || c2 == '-') {
						rdTake();
						n = lexAppend(n, c2);
					}
				}
				continue;
			}
			break;
		}
		tkKind = TK_NUM;
		tkText = internN(lxTmp, n);
		return;
	}

	/* String or character constant. */
	if (c == '"' || c == 39) {
		int quote;

		quote = c;
		rdTake();
		n = 0;
		n = lexAppend(n, quote);
		closed = 0;
		while (1) {
			c = rdPeek();
			if (c < 0)
				break;
			if (c == 10)
				break;
			rdTake();
			n = lexAppend(n, c);
			if (c == 92) {
				c2 = rdPeek();
				if (c2 >= 0 && c2 != 10) {
					rdTake();
					n = lexAppend(n, c2);
				}
				continue;
			}
			if (c == quote) {
				closed = 1;
				break;
			}
		}
		if (!closed) {
			/* In skipped code (an inactive #if branch), an unmatched quote
			   must not abort preprocessing because the text may not be C. */
			if (!skipping) {
				if (quote == '"')
					fatal("Zeichenkette nicht geschlossen", "");
				fatal("Zeichenkonstante nicht geschlossen", "");
			}
			tkKind = TK_OTHER;
			tkText = internN(lxTmp, n);
			return;
		}
		if (quote == '"')
			tkKind = TK_STR;
		else
			tkKind = TK_CH;
		tkText = internN(lxTmp, n);
		return;
	}

	/* Punctuation, longest match first. */
	rdTake();
	n = 0;
	n = lexAppend(n, c);
	c2 = rdPeek();

	if (c == '.' && c2 == '.') {
		int savePos;
		int saveLine;

		/* "..." needs two lookahead characters. Save and restore lexer state
		   instead of rewinding, which fails across skipped line continuations. */
		savePos = lxPos;
		saveLine = lxLine;
		rdTake();
		if (rdPeek() == '.') {
			rdTake();
			tkKind = TK_PUNCT;
			tkText = intern("...");
			return;
		}
		lxPos = savePos;
		lxLine = saveLine;
		tkKind = TK_PUNCT;
		tkText = intern(".");
		return;
	}

	if (c2 >= 0) {
		int two;

		two = 0;
		if (c == '<' && c2 == '<')
			two = 1;
		if (c == '>' && c2 == '>')
			two = 1;
		if (c2 == '=' && (c == '<' || c == '>' || c == '=' || c == '!' ||
				  c == '+' || c == '-' || c == '*' || c == '%' ||
				  c == '&' || c == '|' || c == '^'))
			two = 1;
		if (c == '&' && c2 == '&')
			two = 1;
		if (c == '|' && c2 == '|')
			two = 1;
		if (c == '+' && c2 == '+')
			two = 1;
		if (c == '-' && c2 == '-')
			two = 1;
		if (c == '-' && c2 == '>')
			two = 1;
		if (c == '#' && c2 == '#')
			two = 1;
		if (two) {
			rdTake();
			n = lexAppend(n, c2);
			/* Three-character operators: <<= and >>=. */
			if ((c == '<' && c2 == '<') || (c == '>' && c2 == '>')) {
				if (rdPeek() == '=') {
					rdTake();
					n = lexAppend(n, '=');
				}
			}
			tkKind = TK_PUNCT;
			tkText = internN(lxTmp, n);
			return;
		}
	}

	tkKind = TK_PUNCT;
	tkText = internN(lxTmp, n);
	return;
}

/* ============================================================= Pushback === */
/* Function: pbPush
 * Adds one token to the preprocessing pushback buffer.
 * Parameters: kind, text, line, file, ws Token fields.
 * Returns: Nothing. */
static void pbPush(int kind, int text, int line, int file, int ws)
{
	if (pbN >= PB_MAX)
		fatal("expansion stack full (PB_MAX) -- macro recursion?", "");
	pbKind[pbN] = kind;
	pbText[pbN] = text;
	pbLine[pbN] = line;
	pbFile[pbN] = file;
	pbWs[pbN] = ws;
	pbN++;
}

/* Function: pbPushCur
 * Saves the current token in the pushback buffer.
 * Parameters: None.
 * Returns: Nothing. */
static void pbPushCur(void)
{
	pbPush(tkKind, tkText, tkLine, tkFile, tkWs);
}

/* Function: nextRaw
 * Reads the next token without macro expansion.
 * Parameters: None.
 * Returns: Nothing; updates the current token. */
static void nextRaw(void)
{
	if (pbN > 0) {
		pbN--;
		tkKind = pbKind[pbN];
		tkText = pbText[pbN];
		tkLine = pbLine[pbN];
		tkFile = pbFile[pbN];
		tkWs = pbWs[pbN];
		tkFromPB = 1;
		return;
	}
	tkFromPB = 0;
	lexNext();
}

/* ======================================================== Makrotabelle === */
/* Function: macFind
 * Looks up a macro by its interned name.
 * Parameters: name Interned macro name.
 * Returns: Macro index, or -1 when not found. */
static int macFind(int name)
{
	int i;

	for (i = 0; i < macN; i++) {
		if (macName[i] == name)
			return i;
	}
	return -1;
}

/* Function: macIsParam
 * Checks whether text names a parameter of a function-like macro.
 * Parameters: m Macro index; text Interned token text.
 * Returns: Parameter index, or -1 when not a parameter. */
static int macIsParam(int m, int text)
{
	int i;

	for (i = 0; i < macNPar[m]; i++) {
		if (parName[macParAt[m] + i] == text)
			return i;
	}
	return -1;
}

/* ==================================================== Makroexpansion ===== */
static int expandOne(void);

/* Build a string from raw tokens (# operator, C89 3.8.3.2). */
/* Function: stringizeArg
 * Converts a macro argument to a C string token.
 * Parameters: at Argument start; n Argument length.
 * Returns: Interned string-token text index. */
static int stringizeArg(int at, int n)
{
	int i;
	int j;
	int len;
	const char *s;
	int k;
	int kind;

	len = 0;
	len = lexAppend(len, '"');
	for (i = 0; i < n; i++) {
		if (i > 0 && agWs[at + i])
			len = lexAppend(len, ' ');
		s = poolAt(agText[at + i]);
		kind = agKind[at + i];
		k = strLen(s);
		for (j = 0; j < k; j++) {
			/* Quotes and backslashes inside string and character constants
			   must be escaped again. */
			if (kind == TK_STR || kind == TK_CH) {
				if (s[j] == '"' || s[j] == 92)
					len = lexAppend(len, 92);
			}
			len = lexAppend(len, s[j]);
		}
	}
	len = lexAppend(len, '"');
	return internN(lxTmp, len);
}

/* ## concatenation joins two token spellings and lexes the result again. If
   the result is not a SINGLE token, C89 3.8.3.3 leaves it undefined; abort
   instead of silently approximating it. */
/* Function: pasteText
 * Concatenates two token texts for the ## operator.
 * Parameters: aText, bText Interned token text indices.
 * Returns: Interned concatenated token text index. */
static int pasteText(int aText, int bText)
{
	int n;
	const char *a;
	const char *b;
	int i;
	int k;

	n = 0;
	a = poolAt(aText);
	k = strLen(a);
	for (i = 0; i < k; i++)
		n = lexAppend(n, a[i]);
	b = poolAt(bText);
	k = strLen(b);
	for (i = 0; i < k; i++)
		n = lexAppend(n, b[i]);
	return internN(lxTmp, n);
}

/* All punctuation tokens from C89 3.1.6, so validation below checks the
   actual token rather than only estimating its length. */
static const char *punctList[48];
static int punctN;

/* Function: setupPuncts
 * Initializes the punctuation-token table.
 * Parameters: None.
 * Returns: Nothing. */
static void setupPuncts(void)
{
	punctN = 0;
	punctList[punctN++] = "[";
	punctList[punctN++] = "]";
	punctList[punctN++] = "(";
	punctList[punctN++] = ")";
	punctList[punctN++] = "{";
	punctList[punctN++] = "}";
	punctList[punctN++] = ".";
	punctList[punctN++] = "->";
	punctList[punctN++] = "++";
	punctList[punctN++] = "--";
	punctList[punctN++] = "&";
	punctList[punctN++] = "*";
	punctList[punctN++] = "+";
	punctList[punctN++] = "-";
	punctList[punctN++] = "~";
	punctList[punctN++] = "!";
	punctList[punctN++] = "/";
	punctList[punctN++] = "%";
	punctList[punctN++] = "<<";
	punctList[punctN++] = ">>";
	punctList[punctN++] = "<";
	punctList[punctN++] = ">";
	punctList[punctN++] = "<=";
	punctList[punctN++] = ">=";
	punctList[punctN++] = "==";
	punctList[punctN++] = "!=";
	punctList[punctN++] = "^";
	punctList[punctN++] = "|";
	punctList[punctN++] = "&&";
	punctList[punctN++] = "||";
	punctList[punctN++] = "?";
	punctList[punctN++] = ":";
	punctList[punctN++] = ";";
	punctList[punctN++] = "...";
	punctList[punctN++] = "=";
	punctList[punctN++] = "*=";
	punctList[punctN++] = "/=";
	punctList[punctN++] = "%=";
	punctList[punctN++] = "+=";
	punctList[punctN++] = "-=";
	punctList[punctN++] = "<<=";
	punctList[punctN++] = ">>=";
	punctList[punctN++] = "&=";
	punctList[punctN++] = "^=";
	punctList[punctN++] = "|=";
	punctList[punctN++] = ",";
	punctList[punctN++] = "#";
	punctList[punctN++] = "##";
}

/* Function: pasteCheck
 * Validates the result of token pasting.
 * Parameters: text Interned pasted token text index.
 * Returns: Non-zero when the result is a valid token. */
static int pasteCheck(int text)
{
	const char *s;
	int n;
	int i;

	s = poolAt(text);
	n = strLen(s);
	if (n == 0)
		return 1;

	if (isAlphaCh(s[0] & 255)) {
		for (i = 1; i < n; i++) {
			if (!isAlnumCh(s[i] & 255))
				return 0;
		}
		return 1;
	}
	if (isDigitCh(s[0] & 255)) {
		for (i = 1; i < n; i++) {
			if (!isAlnumCh(s[i] & 255) && s[i] != '.' &&
			    s[i] != '+' && s[i] != '-')
				return 0;
		}
		return 1;
	}
	for (i = 0; i < punctN; i++) {
		if (poolEq(text, punctList[i]))
			return 1;
	}
	return 0;
}

/* Collect raw, unexpanded arguments of a function-like macro. The argument
   count is returned; ranges are stored in argAt[]/argLen[] from the agTop
   value at entry. */
static int argAt[64];
static int argLen[64];

/* Function: collectArgs
 * Collects arguments for the current function-like macro invocation.
 * Parameters: m Macro index.
 * Returns: Non-zero on success, zero on malformed input. */
static int collectArgs(int m)
{
	int depth;
	int nargs;
	int startTop;
	int pendingWs;
	int expect;

	expect = macNPar[m];
	depth = 0;
	nargs = 0;
	startTop = agTop;
	argAt[0] = agTop;
	argLen[0] = 0;
	pendingWs = 0;

	/* The caller has already consumed the opening parenthesis. */
	while (1) {
		nextRaw();
		if (tkKind == TK_EOF)
			fatal("Klammer des Makroaufrufs nicht geschlossen", "");
		if (tkKind == TK_ENDMAC) {
			macInUse[tkText] = 0;
			continue;
		}
		if (tkKind == TK_NL) {
			pendingWs = 1;
			continue;
		}
		if (tkKind == TK_ARGEND) {
			/* If the closing parenthesis would be beyond the argument limit,
			   abort deliberately: reading past the limit is undefined in C89
			   and would make expansion order unpredictable. */
			fatal("Makroaufruf reicht ueber die Argumentgrenze hinaus", "");
		}
		if (tkKind == TK_PUNCT && poolEq(tkText, "(")) {
			depth++;
		} else if (tkKind == TK_PUNCT && poolEq(tkText, ")")) {
			if (depth == 0) {
				if (nargs > 0 || argLen[0] > 0)
					nargs++;
				break;
			}
			depth--;
		} else if (tkKind == TK_PUNCT && poolEq(tkText, ",") && depth == 0) {
			nargs++;
			if (nargs >= 64)
				fatal("zu viele Makroargumente", "");
			argAt[nargs] = agTop;
			argLen[nargs] = 0;
			pendingWs = 0;
			continue;
		}

		if (agTop >= AG_MAX)
			fatal("Argumentspeicher voll (AG_MAX)", "");
		agKind[agTop] = tkKind;
		agText[agTop] = tkText;
		agWs[agTop] = tkWs | pendingWs;
		pendingWs = 0;
		agTop++;
		argLen[nargs] = agTop - argAt[nargs];
	}

	/* With exactly one expected parameter, "F()" is an empty argument. */
	if (nargs == 0 && expect == 1) {
		nargs = 1;
		argAt[0] = startTop;
		argLen[0] = 0;
	}
	if (nargs != expect) {
		if (nargs < expect)
			fatal("zu wenige Argumente fuer Makro ", poolAt(macName[m]));
		fatal("zu viele Argumente fuer Makro ", poolAt(macName[m]));
	}
	return nargs;
}

/* Fully expand one argument during the prescan (C89 3.8.3.1). The result is
   appended to argument storage; return its start index and store its length
   in preLen. */
static int preLen;

/* Expansion is protected against direct recursion by locking the active
   macro (C89 3.8.3.4), but argument prescan can still recurse through nested
   calls such as "F(F(F(...)))". Bound the depth explicitly instead of
   allowing a target-dependent C-stack overflow. */
static int expDepth;
#define EXP_DEPTH_MAX 200

/* Function: prescanArg
 * Expands macros in one function-macro argument before substitution.
 * Parameters: at, n Argument range; line, file Source location.
 * Returns: Number of generated tokens. */
static int prescanArg(int at, int n, int line, int file)
{
	int i;
	int savePB;
	int outAt;

	expDepth++;
	if (expDepth > EXP_DEPTH_MAX)
		fatal("Makroargumente zu tief geschachtelt (EXP_DEPTH_MAX)", "");
	savePB = pbN;
	pbPush(TK_ARGEND, intern(""), line, file, 0);
	for (i = n - 1; i >= 0; i--)
		pbPush(agKind[at + i], agText[at + i], line, file, agWs[at + i]);

	outAt = agTop;
	while (1) {
		if (!expandOne())
			continue;
		if (tkKind == TK_ARGEND)
			break;
		if (agTop >= AG_MAX)
			fatal("Argumentspeicher voll (AG_MAX, Prescan)", "");
		agKind[agTop] = tkKind;
		agText[agTop] = tkText;
		agWs[agTop] = tkWs;
		agTop++;
	}
	if (pbN != savePB)
		fatal("innerer Fehler: Stapel nach Prescan nicht ausgeglichen", "");
	expDepth--;
	preLen = agTop - outAt;
	return outAt;
}

/* Substitute collected arguments into macro m: walk its body, replace
   parameters, apply # and ##, and push the result back for rescanning. */
/* Function: substitute
 * Substitutes collected arguments into a macro replacement list.
 * Parameters: m Macro index; nargs Argument count; line, file Source location;
 *             leadWs Leading-whitespace flag.
 * Returns: Nothing; writes replacement tokens to the pushback buffer. */
static void substitute(int m, int nargs, int line, int file, int leadWs)
{
	int i;
	int n;
	int bi;
	int bn;
	int p;
	int k;
	int exBase;
	int saveAg;
	int at;
	int cnt;
	int text;
	int kind;
	int ws;
	int pasteNext;
	int myAt[64];
	int myLen[64];

	/* Copy argument ranges immediately: prescanArg() may recursively call
	   collectArgs(), which would overwrite argAt/argLen. */
	for (i = 0; i < nargs; i++) {
		myAt[i] = argAt[i];
		myLen[i] = argLen[i];
	}

	bi = macBodyAt[m];
	bn = macBodyN[m];
	exBase = exTop;
	saveAg = agTop;
	pasteNext = 0;

	for (i = 0; i < bn; i++) {
		kind = mtKind[bi + i];
		text = mtText[bi + i];
		ws = mtWs[bi + i];

		/* # <Parameter> */
		if (macFunc[m] && kind == TK_PUNCT && poolEq(text, "#") &&
		    i + 1 < bn) {
			p = macIsParam(m, mtText[bi + i + 1]);
			if (p >= 0) {
				if (exTop >= EX_MAX)
					fatal("Expansionspuffer voll (EX_MAX)", "");
				exKind[exTop] = TK_STR;
				exText[exTop] = stringizeArg(myAt[p], myLen[p]);
				exWs[exTop] = ws;
				exTop++;
				i++;
				continue;
			}
		}

		/* <links> ## <rechts> */
		if (kind == TK_PUNCT && poolEq(text, "##") && exTop > exBase &&
		    i + 1 < bn) {
			int rt;
			int last;

			i++;
			rt = mtText[bi + i];
			p = -1;
			if (macFunc[m])
				p = macIsParam(m, rt);
			if (p >= 0) {
				if (myLen[p] == 0)
					continue;   /* Empty argument: append nothing. */
				rt = agText[myAt[p]];
			}
			last = exTop - 1;
			exText[last] = pasteText(exText[last], rt);
			if (!pasteCheck(exText[last]))
				fatal("## ergibt kein einzelnes Token: ",
				      poolAt(exText[last]));
			exKind[last] = TK_OTHER;
			if (isAlphaCh(pool[exText[last]] & 255))
				exKind[last] = TK_ID;
			else if (isDigitCh(pool[exText[last]] & 255))
				exKind[last] = TK_NUM;
			if (p >= 0) {
				/* Append the remaining argument tokens. */
				for (k = 1; k < myLen[p]; k++) {
					if (exTop >= EX_MAX)
						fatal("Expansionspuffer voll (EX_MAX)", "");
					exKind[exTop] = agKind[myAt[p] + k];
					exText[exTop] = agText[myAt[p] + k];
					exWs[exTop] = agWs[myAt[p] + k];
					exTop++;
				}
			}
			continue;
		}

		/* Parameter */
		p = -1;
		if (macFunc[m] && kind == TK_ID)
			p = macIsParam(m, text);
		if (p >= 0) {
			/* Operand of a following ##: use the raw argument. */
			pasteNext = 0;
			if (i + 1 < bn && mtKind[bi + i + 1] == TK_PUNCT &&
			    poolEq(mtText[bi + i + 1], "##"))
				pasteNext = 1;

			if (pasteNext) {
				at = myAt[p];
				cnt = myLen[p];
			} else {
				at = prescanArg(myAt[p], myLen[p], line, file);
				cnt = preLen;
			}
			for (k = 0; k < cnt; k++) {
				if (exTop >= EX_MAX)
					fatal("Expansionspuffer voll (EX_MAX)", "");
				exKind[exTop] = agKind[at + k];
				exText[exTop] = agText[at + k];
				exWs[exTop] = agWs[at + k];
				if (k == 0)
					exWs[exTop] = ws;
				exTop++;
			}
			continue;
		}

		if (exTop >= EX_MAX)
			fatal("Expansionspuffer voll (EX_MAX)", "");
		exKind[exTop] = kind;
		exText[exTop] = text;
		exWs[exTop] = ws;
		exTop++;
	}

	agTop = saveAg;

	/* Push the end marker first so it is read last. The macro remains locked
	   while its replacement is in the token stream (C89 3.8.3.4). */
	pbPush(TK_ENDMAC, m, line, file, 0);
	for (i = exTop - 1; i >= exBase; i--) {
		n = exWs[i];
		if (i == exBase)
			n = leadWs;
		pbPush(exKind[i], exText[i], line, file, n);
	}
	exTop = exBase;
	macInUse[m] = 1;
}

/* Read one token and expand it. Return zero when expansion consumed the
   invocation and the caller must ask again. */
/*
 * Function: expandOne
 *
 * Expands one currently available macro invocation, if applicable.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   Non-zero when the current token was left unchanged; zero when expansion
 *   consumed the macro invocation.
 */
static int expandOne(void)
{
	int m;
	int save;
	int nlCount;
	int line;
	int file;
	int ws;
	int nargs;
	int saveAg;
	char buf[32];
	int i;
	int v;
	int n;

	nextRaw();
	if (tkKind == TK_ENDMAC) {
		macInUse[tkText] = 0;
		return 0;
	}
	if (tkKind != TK_ID)
		return 1;

	m = macFind(tkText);
	if (m < 0 || macInUse[m])
		return 1;

	line = tkLine;
	file = tkFile;
	ws = tkWs;

	if (macKind[m] == 1) {
		/* __FILE__ */
		n = 0;
		n = lexAppend(n, '"');
		{
			const char *s;
			int k;

			s = poolAt(flName[file]);
			k = strLen(s);
			for (i = 0; i < k; i++) {
				if (s[i] == 92 || s[i] == '"')
					n = lexAppend(n, 92);
				n = lexAppend(n, s[i]);
			}
		}
		n = lexAppend(n, '"');
		pbPush(TK_STR, internN(lxTmp, n), line, file, ws);
		return 0;
	}
	if (macKind[m] == 2) {
		/* __LINE__ */
		v = line;
		n = 0;
		if (v == 0)
			buf[n++] = '0';
		while (v > 0) {
			buf[n++] = '0' + (v % 10);
			v = v / 10;
		}
		for (i = 0; i < n; i++)
			lxTmp[i] = buf[n - 1 - i];
		pbPush(TK_NUM, internN(lxTmp, n), line, file, ws);
		return 0;
	}

	if (!macFunc[m]) {
		substitute(m, 0, line, file, ws);
		return 0;
	}

	/* Function-like macros expand only when followed by "(". Intervening
	   newlines are allowed (C89 3.8.3) and are restored on a non-match. */
	save = pbN;
	nlCount = 0;
	while (1) {
		nextRaw();
		if (tkKind == TK_ENDMAC) {
			macInUse[tkText] = 0;
			continue;
		}
		if (tkKind == TK_NL) {
			nlCount++;
			if (nlCount > 64)
				break;
			continue;
		}
		break;
	}
	if (tkKind == TK_PUNCT && poolEq(tkText, "(")) {
		saveAg = agTop;
		nargs = collectArgs(m);
		substitute(m, nargs, line, file, ws);
		agTop = saveAg;
		return 0;
	}

	/* Not a call: restore the token and newlines; keep the macro name. */
	pbPushCur();
	for (i = 0; i < nlCount; i++)
		pbPush(TK_NL, intern("\n"), line, file, 0);
	if (pbN < save)
		fatal("innerer Fehler: Stapel unterlaufen", "");
	tkKind = TK_ID;
	tkText = macName[m];
	tkLine = line;
	tkFile = file;
	tkWs = ws;
	return 1;
}

/* Function: nextExpanded
 * Reads the next token after recursively applying macro expansion.
 * Parameters: None.
 * Returns: Nothing; updates the current token. */
static void nextExpanded(void)
{
	while (!expandOne())
		;
}

/* ============================================== #if expression evaluation == */
static int evalTernary(void);

/* When parsing without evaluation, the right operand of short-circuited
   &&/|| and the unselected ?: branch must still be traversed syntactically
   but must not calculate; "#if 0 && 1/0" is valid. */
static int evDead;

/* Unsigned state of the last evaluated subexpression. C89 3.8.1 requires
   long/unsigned-long evaluation and the usual arithmetic conversions: one
   unsigned operand makes the operation unsigned. In this 32-bit model int
   and long have the same representation; this flag supplies the meaning. */
static int evUns;

/* Function: arithDiv
 * Performs preprocessor integer division with C89 zero-divisor handling.
 * Parameters: a Dividend; b Divisor; uns Unsigned-arithmetic flag.
 * Returns: Quotient, or reports an error for division by zero. */
static int arithDiv(int a, int b, int uns)
{
	unsigned int ua;
	unsigned int ub;

	if (uns) {
		ua = a;
		ub = b;
		return ua / ub;
	}
	return a / b;
}

/* Function: arithMod
 * Computes the C89 remainder for preprocessor integers.
 * Parameters: a Dividend; b Divisor; uns Unsigned-arithmetic flag.
 * Returns: Remainder, or reports division by zero. */
static int arithMod(int a, int b, int uns)
{
	unsigned int ua;
	unsigned int ub;

	if (uns) {
		ua = a;
		ub = b;
		return ua % ub;
	}
	return a % b;
}

/* Function: arithShr
 * Performs a right shift using the selected signedness.
 * Parameters: a Value; b Shift count; uns Unsigned-arithmetic flag.
 * Returns: Shifted integer value. */
static int arithShr(int a, int b, int uns)
{
	unsigned int ua;

	if (uns) {
		ua = a;
		return ua >> b;
	}
	return a >> b;
}

/* mode: 0 = "<", 1 = ">", 2 = "<=", 3 = ">=" */
/* Function: arithCmp
 * Compares two preprocessor integers.
 * Parameters: a, b Operands; uns Unsigned-arithmetic flag; mode Comparison.
 * Returns: Integer truth value. */
static int arithCmp(int a, int b, int uns, int mode)
{
	unsigned int ua;
	unsigned int ub;

	if (uns) {
		ua = a;
		ub = b;
		if (mode == 0)
			return (ua < ub);
		if (mode == 1)
			return (ua > ub);
		if (mode == 2)
			return (ua <= ub);
		return (ua >= ub);
	}
	if (mode == 0)
		return (a < b);
	if (mode == 1)
		return (a > b);
	if (mode == 2)
		return (a <= b);
	return (a >= b);
}

/* Function: evIsPunct
 * Tests whether an expression token is punctuation text.
 * Parameters: s Token text.
 * Returns: Non-zero when s is a punctuation token. */
static int evIsPunct(const char *s)
{
	if (evI >= evN)
		return 0;
	if (evKind[evI] != TK_PUNCT)
		return 0;
	return poolEq(evText[evI], s);
}

/* Check the suffix after the digits: u/U makes the constant unsigned;
   l/L has no effect in this 32-bit model. */
/* Function: evSuffixUns
 * Detects an unsigned integer suffix in a numeric token.
 * Parameters: s Token text; from Suffix start; n Token length.
 * Returns: Non-zero when the suffix denotes unsigned arithmetic. */
static int evSuffixUns(const char *s, int from, int n)
{
	int i;

	for (i = from; i < n; i++) {
		if (s[i] == 'u' || s[i] == 'U')
			return 1;
		if (s[i] == 'l' || s[i] == 'L')
			continue;
		fatal("invalid number in #if: ", s);
	}
	return 0;
}

/* Function: evNumValue
 * Parses an interned numeric preprocessing token.
 * Parameters: text Interned token text index.
 * Returns: Integer token value. */
static int evNumValue(int text)
{
	const char *s;
	int n;
	int i;
	unsigned int v;
	int d;
	int uns;

	s = poolAt(text);
	n = strLen(s);
	v = 0;
	uns = 0;

	if (n > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		i = 2;
		while (i < n) {
			d = -1;
			if (s[i] >= '0' && s[i] <= '9')
				d = s[i] - '0';
			if (s[i] >= 'a' && s[i] <= 'f')
				d = s[i] - 'a' + 10;
			if (s[i] >= 'A' && s[i] <= 'F')
				d = s[i] - 'A' + 10;
			if (d < 0)
				break;
			v = v * 16 + d;
			i++;
		}
		uns = evSuffixUns(s, i, n);
	} else if (n > 1 && s[0] == '0') {
		i = 1;
		while (i < n) {
			if (s[i] < '0' || s[i] > '7')
				break;
			v = v * 8 + (s[i] - '0');
			i++;
		}
		uns = evSuffixUns(s, i, n);
	} else {
		i = 0;
		while (i < n) {
			if (!isDigitCh(s[i] & 255))
				break;
			v = v * 10 + (s[i] - '0');
			i++;
		}
		if (i < n && (s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
			fatal("Gleitkomma ist im #if nicht erlaubt: ", s);
		uns = evSuffixUns(s, i, n);
	}

	/* A value outside the signed range is unsigned under C89 3.1.3.2
	   (int -> long -> unsigned long; int and long coincide here). This is
	   why "#if 0xFFFFFFFF > 0" evaluates true. */
	if (v > 2147483647)
		uns = 1;
	evUns = uns;
	return v;
}

/* Function: evCharValue
 * Decodes the value of an interned character constant.
 * Parameters: text Interned token text index.
 * Returns: Character value, or zero for an invalid token. */
static int evCharValue(int text)
{
	const char *s;
	int n;
	int i;
	int v;
	int c;

	s = poolAt(text);
	n = strLen(s);
	v = 0;
	i = 1;                      /* opening quote */
	while (i < n && s[i] != 39) {
		c = s[i] & 255;
		if (c == 92) {
			i++;
			c = s[i] & 255;
			if (c == 'n')
				c = 10;
			else if (c == 't')
				c = 9;
			else if (c == 'r')
				c = 13;
			else if (c == '0')
				c = 0;
			else if (c == 'a')
				c = 7;
			else if (c == 'b')
				c = 8;
			else if (c == 'f')
				c = 12;
			else if (c == 'v')
				c = 11;
			/* Backslash, single quote and double quote remain unchanged. */
		}
		v = (v << 8) | c;
		i++;
	}
	evUns = 0;                    /* Character constants have type int (C89 3.1.3.4). */
	return v;
}

/* Function: evalPrimary
 * Evaluates a primary preprocessing expression.
 * Parameters: None.
 * Returns: Integer expression value. */
static int evalPrimary(void)
{
	int v;

	if (evI >= evN)
		fatal("#if: Ausdruck bricht ab", "");
	if (evIsPunct("(")) {
		evI++;
		v = evalTernary();
		if (!evIsPunct(")"))
			fatal("#if: \")\" fehlt", "");
		evI++;
		return v;
	}
	if (evKind[evI] == TK_NUM) {
		v = evNumValue(evText[evI]);
		evI++;
		return v;
	}
	if (evKind[evI] == TK_CH) {
		v = evCharValue(evText[evI]);
		evI++;
		return v;
	}
	if (evKind[evI] == TK_ID) {
		/* An identifier that survives to this point is not a defined macro;
		   C89 3.8.1 specifies the value zero. */
		evI++;
		evUns = 0;
		return 0;
	}
	if (evKind[evI] == TK_STR)
		fatal("#if: Zeichenketten sind hier nicht erlaubt", "");
	fatal("#if: unerwartetes Token ", poolAt(evText[evI]));
	return 0;
}

/* Function: evalUnary
 * Evaluates unary operators in a preprocessing expression.
 * Parameters: None.
 * Returns: Integer expression value. */
static int evalUnary(void)
{
	int v;

	if (evIsPunct("-")) {
		evI++;
		return -evalUnary();
	}
	if (evIsPunct("+")) {
		evI++;
		return evalUnary();
	}
	if (evIsPunct("!")) {
		evI++;
		v = evalUnary();
		evUns = 0;                    /* The result of ! is int. */
		if (v == 0)
			return 1;
		return 0;
	}
	if (evIsPunct("~")) {
		evI++;
		return ~evalUnary();
	}
	return evalPrimary();
}

	/* From here, binary operators carry signedness: save evUns immediately
	   after every subexpression because the next call overwrites it, and make
	   the result unsigned when either operand was unsigned. */
/* Function: evalMul
 * Evaluates multiplicative operators.
 * Parameters: None.
 * Returns: Integer expression value. */
static int evalMul(void)
{
	int v;
	int r;
	int lu;
	int ru;

	v = evalUnary();
	lu = evUns;
	while (1) {
		if (evIsPunct("*")) {
			evI++;
			r = evalUnary();
			ru = evUns;
			lu = lu | ru;
			v = v * r;              /* Same bit pattern for both signedness modes. */
			evUns = lu;
			continue;
		}
		if (evIsPunct("/")) {
			evI++;
			r = evalUnary();
			ru = evUns;
			lu = lu | ru;
			if (r == 0) {
				if (!evDead)
					fatal("#if: Division durch Null", "");
				v = 0;
				evUns = lu;
				continue;
			}
			v = arithDiv(v, r, lu);
			evUns = lu;
			continue;
		}
		if (evIsPunct("%")) {
			evI++;
			r = evalUnary();
			ru = evUns;
			lu = lu | ru;
			if (r == 0) {
				if (!evDead)
					fatal("#if: Rest bei Division durch Null", "");
				v = 0;
				evUns = lu;
				continue;
			}
			v = arithMod(v, r, lu);
			evUns = lu;
			continue;
		}
		break;
	}
	evUns = lu;
	return v;
}

/* Function: evalAdd
 * Evaluates additive operators.
 * Parameters: None.
 * Returns: Integer expression value. */
static int evalAdd(void)
{
	int v;
	int lu;

	v = evalMul();
	lu = evUns;
	while (1) {
		if (evIsPunct("+")) {
			evI++;
			v = v + evalMul();
			lu = lu | evUns;
			continue;
		}
		if (evIsPunct("-")) {
			evI++;
			v = v - evalMul();
			lu = lu | evUns;
			continue;
		}
		break;
	}
	evUns = lu;
	return v;
}

/* Function: evalShift
 * Evaluates left- and right-shift operators.
 * Parameters: None.
 * Returns: Integer expression value. */
static int evalShift(void)
{
	int v;
	int lu;

	/* For shifts only the left operand matters: C89 does not apply the usual
	   arithmetic conversions between the right and left operands. */
	v = evalAdd();
	lu = evUns;
	while (1) {
		if (evIsPunct("<<")) {
			evI++;
			v = v << evalAdd();      /* Same bit pattern for both signedness modes. */
			continue;
		}
		if (evIsPunct(">>")) {
			evI++;
			v = arithShr(v, evalAdd(), lu);
			continue;
		}
		break;
	}
	evUns = lu;
	return v;
}

/* Function: evalRel
 * Evaluates relational operators.
 * Parameters: None.
 * Returns: Integer truth value. */
static int evalRel(void)
{
	int v;
	int r;
	int lu;

	v = evalShift();
	lu = evUns;
	while (1) {
		if (evIsPunct("<")) {
			evI++;
			r = evalShift();
			v = arithCmp(v, r, lu | evUns, 0);
			lu = 0;                  /* A comparison result has type int. */
			continue;
		}
		if (evIsPunct(">")) {
			evI++;
			r = evalShift();
			v = arithCmp(v, r, lu | evUns, 1);
			lu = 0;
			continue;
		}
		if (evIsPunct("<=")) {
			evI++;
			r = evalShift();
			v = arithCmp(v, r, lu | evUns, 2);
			lu = 0;
			continue;
		}
		if (evIsPunct(">=")) {
			evI++;
			r = evalShift();
			v = arithCmp(v, r, lu | evUns, 3);
			lu = 0;
			continue;
		}
		break;
	}
	evUns = lu;
	return v;
}

/* Function: evalEq
 * Evaluates equality operators.
 * Parameters: None.
 * Returns: Integer truth value. */
static int evalEq(void)
{
	int v;
	int r;

	v = evalRel();
	while (1) {
		if (evIsPunct("==")) {
			evI++;
			r = evalRel();
			v = (v == r);           /* Bit patterns are sufficient for equality. */
			evUns = 0;
			continue;
		}
		if (evIsPunct("!=")) {
			evI++;
			r = evalRel();
			v = (v != r);
			evUns = 0;
			continue;
		}
		break;
	}
	return v;
}

/* Function: evalBAnd
 * Evaluates bitwise AND expressions.
 * Parameters: None.
 * Returns: Integer expression value. */
static int evalBAnd(void)
{
	int v;
	int lu;

	v = evalEq();
	lu = evUns;
	while (evIsPunct("&")) {
		evI++;
		v = v & evalEq();
		lu = lu | evUns;
	}
	evUns = lu;
	return v;
}

/* Function: evalBXor
 * Evaluates bitwise XOR expressions.
 * Parameters: None.
 * Returns: Integer expression value. */
static int evalBXor(void)
{
	int v;
	int lu;

	v = evalBAnd();
	lu = evUns;
	while (evIsPunct("^")) {
		evI++;
		v = v ^ evalBAnd();
		lu = lu | evUns;
	}
	evUns = lu;
	return v;
}

/* Function: evalBOr
 * Evaluates bitwise OR expressions.
 * Parameters: None.
 * Returns: Integer expression value. */
static int evalBOr(void)
{
	int v;
	int lu;

	v = evalBXor();
	lu = evUns;
	while (evIsPunct("|")) {
		evI++;
		v = v | evalBXor();
		lu = lu | evUns;
	}
	evUns = lu;
	return v;
}

/* Function: evalAnd
 * Evaluates bitwise AND expressions.
 * Parameters: None.
 * Returns: Integer expression value. */
static int evalAnd(void)
{
	int v;
	int r;

	v = evalBOr();
	while (evIsPunct("&&")) {
		evI++;
		if (v == 0)
			evDead++;
		r = evalBOr();
		if (v == 0) {
			evDead--;
			r = 0;
		}
		v = (v != 0 && r != 0);
		evUns = 0;
	}
	return v;
}

/* Function: evalOr
 * Evaluates bitwise OR expressions.
 * Parameters: None.
 * Returns: Integer expression value. */
static int evalOr(void)
{
	int v;
	int r;

	v = evalAnd();
	while (evIsPunct("||")) {
		evI++;
		if (v != 0)
			evDead++;
		r = evalAnd();
		if (v != 0) {
			evDead--;
			r = 0;
		}
		v = (v != 0 || r != 0);
		evUns = 0;
	}
	return v;
}

/* Function: evalTernary
 * Evaluates a conditional preprocessing expression.
 * Parameters: None.
 * Returns: Selected integer expression value. */
static int evalTernary(void)
{
	int c;
	int a;
	int b;

	c = evalOr();
	if (evIsPunct("?")) {
		int au;
		int bu;

		evI++;
		if (c == 0)
			evDead++;
		a = evalTernary();
		au = evUns;
		if (c == 0)
			evDead--;
		if (!evIsPunct(":"))
			fatal("#if: \":\" fehlt", "");
		evI++;
		if (c != 0)
			evDead++;
		b = evalTernary();
		bu = evUns;
		if (c != 0)
			evDead--;
		/* The conditional operator type is determined from both branches,
		   not only the selected branch. */
		evUns = au | bu;
		if (c != 0)
			return a;
		return b;
	}
	return c;
}

/* =============================================================== Output === */
/* Function: outFlush
 * Writes buffered preprocessor output to the destination stream.
 * Parameters: None.
 * Returns: Nothing. */
static void outFlush(void)
{
	if (outN > 0) {
		if ((int)fwrite(outBuf, 1, outN, outFp) != outN)
			fatal("Ausgabe konnte nicht geschrieben werden", "");
		outTotal = outTotal + outN;
		outN = 0;
	}
}

/* Function: outCh
 * Appends one byte to the output buffer.
 * Parameters: c Byte to append.
 * Returns: Nothing. */
static void outCh(int c)
{
	if (outN >= 8192)
		outFlush();
	outBuf[outN] = c;
	outN++;
	lastCh = c;
	if (c == 10)
		atOutBOL = 1;
	else
		atOutBOL = 0;
}

/* Function: outStr
 * Appends a NUL-terminated string to the output buffer.
 * Parameters: s String to append.
 * Returns: Nothing. */
static void outStr(const char *s)
{
	int i;

	i = 0;
	while (s[i] != 0) {
		outCh(s[i]);
		i++;
	}
}

/* Function: outNum
 * Appends a decimal integer to the output buffer.
 * Parameters: v Integer value.
 * Returns: Nothing. */
static void outNum(int v)
{
	char buf[32];
	int n;
	int i;

	n = 0;
	if (v == 0)
		buf[n++] = '0';
	if (v < 0) {
		outCh('-');
		v = -v;
	}
	while (v > 0) {
		buf[n++] = '0' + (v % 10);
		v = v / 10;
	}
	for (i = n - 1; i >= 0; i--)
		outCh(buf[i]);
}

/* Function: outLineMarker
 * Emits a source line marker for the current output position.
 * Parameters: file Source file identifier; line Source line number.
 * Returns: Nothing. */
static void outLineMarker(int file, int line)
{
	const char *s;
	int i;
	int k;

	if (!atOutBOL)
		outCh(10);
	outStr("#line ");
	outNum(line);
	outStr(" \"");
	s = poolAt(flName[file]);
	k = strLen(s);
	for (i = 0; i < k; i++) {
		if (s[i] == 92 || s[i] == '"')
			outCh(92);
		outCh(s[i]);
	}
	outStr("\"\n");
}

/* Emit one token. While the same file is active, skipped lines (directives
   and inactive #if branches) are replaced by the same number of newlines.
   This keeps diagnostics in later stages aligned with the source; -min
   enables compacting them. */
/* Function: outTok
 * Emits the current token and its required source whitespace.
 * Parameters: None.
 * Returns: Nothing. */
static void outTok(void)
{
	int i;
	int gap;
	int c0;
	int needSpace;

	if (tkFile != lastFile) {
		if (optLines)
			outLineMarker(tkFile, tkLine);
		else if (!atOutBOL)
			outCh(10);
		lastFile = tkFile;
		lastLine = tkLine;
	} else if (tkLine > lastLine) {
		gap = tkLine - lastLine;
		if (optMin && gap > 1)
			gap = 1;
		for (i = 0; i < gap; i++)
			outCh(10);
		lastLine = tkLine;
	}

	c0 = pool[tkText] & 255;
	needSpace = 0;
	if (tkWs)
		needSpace = 1;
	if (!atOutBOL) {
		/* Prevent two tokens from merging into one. Check whether the two
		   boundary characters would start a punctuation token, and also
		   protect slash before '*' or '/'. Adding whitespace between every
		   punctuation token would be simpler but would unnecessarily change
		   output. */
		if (isAlnumCh(lastCh) && isAlnumCh(c0))
			needSpace = 1;
		if (lastCh == '/' && (c0 == '*' || c0 == '/'))
			needSpace = 1;
		if (!isAlnumCh(lastCh) && !isAlnumCh(c0)) {
			int q;
			const char *pc;

			/* Take a pointer first, then index it. This avoids the double
			   indexing of a pointer array that the bootstrap QCC rejects. */
			for (q = 0; q < punctN; q++) {
				pc = punctList[q];
				if (pc[0] == lastCh && pc[1] == c0) {
					needSpace = 1;
					q = punctN;
				}
			}
			if (lastCh == '.' && c0 == '.')
				needSpace = 1;
		}
	} else {
		needSpace = 0;
	}
	if (needSpace)
		outCh(' ');
	outStr(poolAt(tkText));
}

/* ============================================================ Direktiven == */
/* Function: nextDirTok
 * Reads the next token while parsing a preprocessor directive.
 * Parameters: None.
 * Returns: Token kind. */
static int nextDirTok(void)
{
	nextRaw();
	if (tkKind == TK_NL || tkKind == TK_EOF)
		return 0;
	return 1;
}

/* Function: skipRestOfLine
 * Discards the remainder of the current directive line.
 * Parameters: None.
 * Returns: Nothing. */
static void skipRestOfLine(void)
{
	while (1) {
		nextRaw();
		if (tkKind == TK_NL || tkKind == TK_EOF)
			return;
	}
}

/* Collect the rest of a line raw (for #error, #pragma and #if). */
static int lineAt;
static int lineN;

/* Function: collectLine
 * Collects the next logical source line for directive processing.
 * Parameters: None.
 * Returns: Nothing. */
static void collectLine(void)
{
	lineAt = agTop;
	lineN = 0;
	while (1) {
		nextRaw();
		if (tkKind == TK_NL || tkKind == TK_EOF)
			break;
		if (agTop >= AG_MAX)
		fatal("line buffer full (AG_MAX)", "");
		agKind[agTop] = tkKind;
		agText[agTop] = tkText;
		agWs[agTop] = tkWs;
		agTop++;
		lineN++;
	}
}

/* Function: defineMacro
 * Adds or replaces a macro definition in the macro table.
 * Parameters: name Macro name; isFunc Function-like flag; nPar Parameter count;
 *             parAt Parameter data index; bodyAt Replacement-list index;
 *             bodyN Replacement-list length.
 * Returns: Nothing; reports a full macro table as a fatal error. */
static void defineMacro(int name, int isFunc, int nPar, int parAt, int bodyAt,
			int bodyN, int kind)
{
	int m;

	m = macFind(name);
	if (m < 0) {
		if (macN >= MAC_MAX)
			fatal("Makrotabelle voll (MAC_MAX)", "");
		m = macN;
		macN++;
	}
	macName[m] = name;
	macFunc[m] = isFunc;
	macNPar[m] = nPar;
	macParAt[m] = parAt;
	macBodyAt[m] = bodyAt;
	macBodyN[m] = bodyN;
	macInUse[m] = 0;
	macKind[m] = kind;
}

/* Function: doDefine
 * Parses and stores one #define directive.
 * Parameters: None.
 * Returns: Nothing. */
static void doDefine(void)
{
	int name;
	int isFunc;
	int nPar;
	int parAt;
	int bodyAt;
	int bodyN;
	int wsBefore;

	if (!nextDirTok())
		fatal("#define ohne Namen", "");
	if (tkKind != TK_ID)
		fatal("#define: Name erwartet, gefunden ", poolAt(tkText));
	name = tkText;

	isFunc = 0;
	nPar = 0;
	parAt = parTop;

	/* "(" immediately after the name makes the macro function-like. With
	   intervening whitespace it is an object-like macro whose body starts
	   with a parenthesis (C89 3.8.3). */
	nextRaw();
	wsBefore = tkWs;
	if (tkKind == TK_PUNCT && poolEq(tkText, "(") && !wsBefore) {
		isFunc = 1;
		while (1) {
			if (!nextDirTok())
				fatal("#define: Parameterliste nicht geschlossen", "");
			if (tkKind == TK_PUNCT && poolEq(tkText, ")"))
				break;
			if (tkKind == TK_PUNCT && poolEq(tkText, ",")) {
				if (nPar == 0)
					fatal("#define: Komma vor dem ersten Parameter", "");
				continue;
			}
			if (tkKind == TK_PUNCT && poolEq(tkText, "..."))
				fatal("#define: \"...\" in Makros ist erst C99", "");
			if (tkKind != TK_ID)
				fatal("#define: Parametername erwartet, gefunden ",
				      poolAt(tkText));
			if (parTop >= PAR_MAX)
				fatal("Parameterspeicher voll (PAR_MAX)", "");
			parName[parTop] = tkText;
			parTop++;
			nPar++;
		}
		nextRaw();
	}

	bodyAt = mtTop;
	bodyN = 0;
	while (tkKind != TK_NL && tkKind != TK_EOF) {
		if (mtTop >= MT_MAX)
			fatal("Makrospeicher voll (MT_MAX)", "");
		mtKind[mtTop] = tkKind;
		mtText[mtTop] = tkText;
		mtWs[mtTop] = tkWs;
		if (bodyN == 0)
			mtWs[mtTop] = 0;
		mtTop++;
		bodyN++;
		nextRaw();
	}

	if (bodyN > 0) {
		if (mtKind[bodyAt] == TK_PUNCT && poolEq(mtText[bodyAt], "##"))
			fatal("#define: Rumpf beginnt mit ##", "");
		if (mtKind[bodyAt + bodyN - 1] == TK_PUNCT &&
		    poolEq(mtText[bodyAt + bodyN - 1], "##"))
			fatal("#define: Rumpf endet mit ##", "");
	}

	defineMacro(name, isFunc, nPar, parAt, bodyAt, bodyN, 0);
}

/* Function: doUndef
 * Removes one macro definition from the active macro table.
 * Parameters: None.
 * Returns: Nothing. */
static void doUndef(void)
{
	int m;

	if (!nextDirTok())
		fatal("#undef ohne Namen", "");
	if (tkKind != TK_ID)
		fatal("#undef: Name erwartet, gefunden ", poolAt(tkText));
	m = macFind(tkText);
	if (m >= 0) {
		/* Disable the entry by assigning a marker that cannot be an identifier.
		   An empty name would be the internal text of TK_EOF/TK_ARGEND and
		   could match accidentally. */
		macName[m] = intern("#undef#");
		macInUse[m] = 0;
	}
	skipRestOfLine();
}

	/* Evaluate a #if/#elif expression. Resolve "defined" before macro
	   expansion; otherwise its operand would be expanded as well. */
/* Function: evalIfLine
 * Evaluates the expression following #if or #elif.
 * Parameters: None.
 * Returns: Non-zero when the conditional is active. */
static int evalIfLine(void)
{
	int i;
	int at;
	int n;
	int m;
	int j;
	int savePB;
	int name;
	int val;

	collectLine();
	at = lineAt;
	n = lineN;
	if (n == 0)
		fatal("#if ohne Ausdruck", "");

	/* Step 1: replace defined X / defined(X) with 1 or 0. */
	evN = 0;
	i = 0;
	while (i < n) {
		if (agKind[at + i] == TK_ID && agText[at + i] == textDefined) {
			j = i + 1;
			name = -1;
			if (j < n && agKind[at + j] == TK_PUNCT &&
			    poolEq(agText[at + j], "(")) {
				j++;
				if (j < n && agKind[at + j] == TK_ID) {
					name = agText[at + j];
					j++;
				}
				if (name < 0 || j >= n ||
				    agKind[at + j] != TK_PUNCT ||
				    !poolEq(agText[at + j], ")"))
					fatal("#if: defined(<name>) erwartet", "");
				j++;
			} else if (j < n && agKind[at + j] == TK_ID) {
				name = agText[at + j];
				j++;
			} else {
				fatal("#if: defined ohne Namen", "");
			}
			m = macFind(name);
			if (evN >= EV_MAX)
				fatal("Ausdruckspuffer voll (EV_MAX)", "");
			evKind[evN] = TK_NUM;
			if (m >= 0)
				evText[evN] = intern("1");
			else
				evText[evN] = intern("0");
			evN++;
			i = j;
			continue;
		}
		if (evN >= EV_MAX)
			fatal("Ausdruckspuffer voll (EV_MAX)", "");
		evKind[evN] = agKind[at + i];
		evText[evN] = agText[at + i];
		evN++;
		i++;
	}

	/* Step 2: expand the remaining tokens. */
	savePB = pbN;
	pbPush(TK_ARGEND, intern(""), lxLine, lxFile, 0);
	for (i = evN - 1; i >= 0; i--)
		pbPush(evKind[i], evText[i], lxLine, lxFile, 0);
	evN = 0;
	while (1) {
		nextExpanded();
		if (tkKind == TK_ARGEND)
			break;
		if (tkKind == TK_NL)
			continue;
		if (evN >= EV_MAX)
			fatal("Ausdruckspuffer voll (EV_MAX)", "");
		evKind[evN] = tkKind;
		evText[evN] = tkText;
		evN++;
	}
	if (pbN != savePB)
		fatal("innerer Fehler: Stapel nach #if nicht ausgeglichen", "");

	agTop = lineAt;
	evI = 0;
	evDead = 0;
	evUns = 0;
	val = evalTernary();
	if (evI != evN)
		fatal("#if: ueberzaehlige Tokens im Ausdruck", "");
	if (val != 0)
		return 1;
	return 0;
}

/* Function: condPush
 * Pushes one conditional-compilation nesting level.
 * Parameters: active Whether the new branch is active.
 * Returns: Nothing. */
static void condPush(int active)
{
	int parent;

	if (cdDepth >= CD_MAX)
		fatal("#if zu tief geschachtelt (CD_MAX)", "");
	parent = 1;
	if (cdDepth > 0)
		parent = cdActive[cdDepth - 1];
	cdActive[cdDepth] = (parent && active);
	cdTaken[cdDepth] = cdActive[cdDepth];
	cdElse[cdDepth] = 0;
	cdDepth++;
	skipping = 0;
	if (cdDepth > 0 && !cdActive[cdDepth - 1])
		skipping = 1;
}

/* Function: condUpdateSkip
 * Recomputes whether the current conditional branch is skipped.
 * Parameters: None.
 * Returns: Nothing. */
static void condUpdateSkip(void)
{
	skipping = 0;
	if (cdDepth > 0 && !cdActive[cdDepth - 1])
		skipping = 1;
}

/* Function: findInclude
 * Searches configured include directories for a header.
 * Parameters: name Header name; isAngle Angle-include flag; fromDir Parent directory.
 * Returns: File identifier, or -1 when not found. */
static int findInclude(const char *name, int isAngle, int fromDir)
{
	int i;
	int n;
	int id;
	const char *d;
	int k;

	/* Search "..." first in the including file's directory (C89 3.8.2). */
	if (!isAngle) {
		n = 0;
		d = poolAt(fromDir);
		k = strLen(d);
		if (k > 0) {
			for (i = 0; i < k; i++)
				n = lexAppend(n, d[i]);
			n = lexAppend(n, '/');
		}
		k = strLen(name);
		for (i = 0; i < k; i++)
			n = lexAppend(n, name[i]);
		lxTmp[n] = 0;
		if (onceSeen(lxTmp))
			return -2;
		id = fileLoad(lxTmp);
		if (id >= 0)
			return id;
	}

	for (i = 0; i < dirN; i++) {
		n = 0;
		d = poolAt(dirPath[i]);
		k = strLen(d);
		{
			int j;

			for (j = 0; j < k; j++)
				n = lexAppend(n, d[j]);
		}
		if (k > 0 && d[k - 1] != '/' && d[k - 1] != 92)
			n = lexAppend(n, '/');
		k = strLen(name);
		{
			int j;

			for (j = 0; j < k; j++)
				n = lexAppend(n, name[j]);
		}
		lxTmp[n] = 0;
		if (onceSeen(lxTmp))
			return -2;
		id = fileLoad(lxTmp);
		if (id >= 0)
			return id;
	}
	return -1;
}

/* Function: doInclude
 * Resolves and loads one #include directive.
 * Parameters: None.
 * Returns: Nothing. */
static void doInclude(void)
{
	int isAngle;
	int n;
	int id;
	int fromDir;
	char name[512];
	int i;
	int k;
	const char *s;

	fromDir = flDir[lxFile];
	nextRaw();

	isAngle = 0;
	n = 0;

	if (tkKind == TK_STR) {
		s = poolAt(tkText);
		k = strLen(s);
		for (i = 1; i + 1 < k; i++) {
			name[n] = s[i];
			n++;
		}
		name[n] = 0;
		skipRestOfLine();
	} else if (tkKind == TK_PUNCT && poolEq(tkText, "<")) {
		isAngle = 1;
		while (1) {
			nextRaw();
			if (tkKind == TK_NL || tkKind == TK_EOF)
				fatal("#include: \">\" fehlt", "");
			if (tkKind == TK_PUNCT && poolEq(tkText, ">"))
				break;
			s = poolAt(tkText);
			k = strLen(s);
			if (tkWs && n > 0) {
				name[n] = ' ';
				n++;
			}
			for (i = 0; i < k; i++) {
				if (n >= 500)
					fatal("#include: Name zu lang", "");
				name[n] = s[i];
				n++;
			}
		}
		name[n] = 0;
		skipRestOfLine();
	} else {
		fatal("#include: \"datei\" oder <datei> erwartet", "");
	}

	if (isDepth >= INC_MAX)
		fatal("#include zu tief geschachtelt (INC_MAX)", "");

	id = findInclude(name, isAngle, fromDir);
	if (id == -2)
		return;                       /* Already included with "#pragma once". */
	if (id < 0)
		fatal("#include: Datei nicht gefunden: ", name);

	isFile[isDepth] = lxFile;
	isPos[isDepth] = lxPos;
	isLine[isDepth] = lxLine;
	isCd[isDepth] = cdDepth;
	isDepth++;

	lxFile = id;
	lxPos = flStart[id];
	lxLine = 1;
	atBOL = 1;
}

/* Function: popInclude
 * Restores the previous source file after an include completes.
 * Parameters: None.
 * Returns: Non-zero when an include level was restored. */
static int popInclude(void)
{
	if (isDepth == 0)
		return 0;
	isDepth--;
	lxFile = isFile[isDepth];
	lxPos = isPos[isDepth];
	lxLine = isLine[isDepth];
	return 1;
}

/* Function: doErrorDir
 * Handles #error and #warning directives.
 * Parameters: isWarn Selects warning instead of fatal error.
 * Returns: Nothing. */
static void doErrorDir(int isWarn)
{
	int i;
	int n;

	collectLine();
	n = 0;
	for (i = 0; i < lineN; i++) {
		const char *s;
		int k;
		int j;

		if (i > 0)
			n = lexAppend(n, ' ');
		s = poolAt(agText[lineAt + i]);
		k = strLen(s);
		for (j = 0; j < k; j++)
			n = lexAppend(n, s[j]);
	}
	lxTmp[n] = 0;
	agTop = lineAt;
	if (isWarn) {
		warn("#warning ", lxTmp);
		return;
	}
	fatal("#error ", lxTmp);
}

/* Function: doPragma
 * Handles supported #pragma directives.
 * Parameters: None.
 * Returns: Nothing. */
static void doPragma(void)
{
	int i;

	collectLine();

	/* Honor "#pragma once" and do not pass it through, matching cc -E.
	   Without this, a protected header such as MWOS SDK SRC/DEFS/stdcomp.h
	   would be emitted twice when included through two paths. */
	if (lineN == 1 && agKind[lineAt] == TK_ID && poolEq(agText[lineAt], "once")) {
		flOnce[lxFile] = 1;
		agTop = lineAt;
		return;
	}

	/* Pass all other pragmas through unchanged; later stages decide which
	   pragmas they understand (C89 3.8.6). */
	if (!atOutBOL)
		outCh(10);
	outStr("#pragma");
	for (i = 0; i < lineN; i++) {
		/* Preserve source whitespace instead of inserting a fixed space so
		   formatting such as "warning ( disable : 4114)" is retained. */
		if (i == 0 || agWs[lineAt + i])
			outCh(' ');
		outStr(poolAt(agText[lineAt + i]));
	}
	outCh(10);
	lastLine = lxLine;
	lastFile = lxFile;
	agTop = lineAt;
}

/* Function: doLineDir
 * Handles a #line directive and updates source location state.
 * Parameters: None.
 * Returns: Nothing. */
static void doLineDir(void)
{
	int newLine;
	int id;

	collectLine();
	if (lineN < 1 || agKind[lineAt] != TK_NUM)
		fatal("#line: Zeilennummer erwartet", "");
	newLine = evNumValue(agText[lineAt]);

	if (lineN >= 2 && agKind[lineAt + 1] == TK_STR) {
		const char *s;
		int k;
		int i;
		int n;

		s = poolAt(agText[lineAt + 1]);
		k = strLen(s);
		n = 0;
		for (i = 1; i + 1 < k; i++)
			n = lexAppend(n, s[i]);
		lxTmp[n] = 0;
		if (flN >= FILE_MAX)
			fatal("zu viele Dateien (FILE_MAX)", "");
		/* Add a table entry with the same buffer but a new name so diagnostics
		   and -lines markers use the requested source name. */
		id = flN;
		flName[id] = intern(lxTmp);
		flDir[id] = flDir[lxFile];
		flStart[id] = flStart[lxFile];
		flEnd[id] = flEnd[lxFile];
		flOnce[id] = 0;
		flN++;
		lxFile = id;
	}
	agTop = lineAt;
	lxLine = newLine;
}

/* Function: emitAsmMarker
 * Emits an assembler block marker when assembler text is preserved.
 * Parameters: what Marker text.
 * Returns: Nothing. */
static void emitAsmMarker(const char *what)
{
	if (optAsmStrip)
		return;
	if (!atOutBOL)
		outCh(10);
	outStr(what);
	outCh(10);
}

	/* #asm ... #endasm: process the body as ordinary text (expand macros and
	   discard comments), matching xcc -pp. Only #endasm is recognized inside. */
/* Function: doAsm
 * Copies an OS-9 assembler block to the preprocessed output.
 * Parameters: None.
 * Returns: Nothing. */
static void doAsm(void)
{
	int save;

	emitAsmMarker("#asm");
	skipRestOfLine();
	lastLine = lxLine;
	lastFile = lxFile;
	inAsm = 1;

	while (1) {
		nextRaw();
		if (tkKind == TK_EOF)
			fatal("#asm ohne #endasm", "");
		if (tkKind == TK_NL) {
			atBOL = 1;
			continue;
		}
		if (tkKind == TK_PUNCT && poolEq(tkText, "#") && atBOL &&
		    !tkFromPB) {
			save = pbN;
			nextRaw();
			if (tkKind == TK_ID && tkText == textEndasm) {
				skipRestOfLine();
				inAsm = 0;
				emitAsmMarker("#endasm");
				lastLine = lxLine;
				lastFile = lxFile;
				return;
			}
			/* Not #endasm: both tokens belong to the body. */
			pbPushCur();
			if (pbN < save)
				fatal("innerer Fehler: Stapel unterlaufen", "");
			tkKind = TK_PUNCT;
			tkText = intern("#");
		}
		atBOL = 0;
		pbPushCur();
		nextExpanded();
		outTok();
	}
}

/* Function: directive
 * Dispatches the current preprocessor directive.
 * Parameters: None.
 * Returns: Nothing. */
static void directive(void)
{
	int name;

	nextRaw();

	if (tkKind == TK_NL || tkKind == TK_EOF)
		return;                       /* leere Direktive "#" */

	/* "# 42 \"file\"" is the line-marker form emitted by other preprocessors;
	   support it when tools are chained by treating it like #line. */
	if (tkKind == TK_NUM) {
		if (skipping) {
			skipRestOfLine();
			return;
		}
		pbPushCur();
		doLineDir();
		return;
	}

	if (tkKind != TK_ID) {
		if (skipping) {
			skipRestOfLine();
			return;
		}
		fatal("unbekannte Direktive: #", poolAt(tkText));
	}
	name = tkText;

	/* Conditional directives also take effect in skipped regions. */
	if (poolEq(name, "if")) {
		if (skipping) {
			condPush(0);
			cdTaken[cdDepth - 1] = 1;   /* never become active again */
			skipRestOfLine();
			return;
		}
		condPush(evalIfLine());
		return;
	}
	if (poolEq(name, "ifdef") || poolEq(name, "ifndef")) {
		int want;
		int m;

		want = 1;
		if (poolEq(name, "ifndef"))
			want = 0;
		if (skipping) {
			condPush(0);
			cdTaken[cdDepth - 1] = 1;
			skipRestOfLine();
			return;
		}
		if (!nextDirTok())
			fatal("#ifdef/#ifndef ohne Namen", "");
		if (tkKind != TK_ID)
			fatal("#ifdef/#ifndef: Name erwartet, gefunden ",
			      poolAt(tkText));
		m = macFind(tkText);
		skipRestOfLine();
		if (want)
			condPush(m >= 0);
		else
			condPush(m < 0);
		return;
	}
	if (poolEq(name, "elif")) {
		int parent;

		if (cdDepth == 0)
			fatal("#elif ohne #if", "");
		if (cdElse[cdDepth - 1])
			fatal("#elif nach #else", "");
		parent = 1;
		if (cdDepth > 1)
			parent = cdActive[cdDepth - 2];
		if (!parent || cdTaken[cdDepth - 1]) {
			cdActive[cdDepth - 1] = 0;
			skipRestOfLine();
			condUpdateSkip();
			return;
		}
		/* Evaluate only here: a skipped branch often contains text that is
		   not evaluable as an expression at all. */
		skipping = 0;
		if (evalIfLine()) {
			cdActive[cdDepth - 1] = 1;
			cdTaken[cdDepth - 1] = 1;
		} else {
			cdActive[cdDepth - 1] = 0;
		}
		condUpdateSkip();
		return;
	}
	if (poolEq(name, "else")) {
		int parent;

		if (cdDepth == 0)
			fatal("#else ohne #if", "");
		if (cdElse[cdDepth - 1])
			fatal("zweites #else", "");
		cdElse[cdDepth - 1] = 1;
		parent = 1;
		if (cdDepth > 1)
			parent = cdActive[cdDepth - 2];
		if (parent && !cdTaken[cdDepth - 1]) {
			cdActive[cdDepth - 1] = 1;
			cdTaken[cdDepth - 1] = 1;
		} else {
			cdActive[cdDepth - 1] = 0;
		}
		skipRestOfLine();
		condUpdateSkip();
		return;
	}
	if (poolEq(name, "endif")) {
		if (cdDepth == 0)
			fatal("#endif ohne #if", "");
		cdDepth--;
		skipRestOfLine();
		condUpdateSkip();
		return;
	}

	if (skipping) {
		skipRestOfLine();
		return;
	}

	if (poolEq(name, "define")) {
		doDefine();
		return;
	}
	if (poolEq(name, "undef")) {
		doUndef();
		return;
	}
	if (poolEq(name, "include")) {
		doInclude();
		return;
	}
	if (poolEq(name, "error")) {
		doErrorDir(0);
		return;
	}
	if (poolEq(name, "warning")) {
		doErrorDir(1);
		return;
	}
	if (poolEq(name, "pragma")) {
		doPragma();
		return;
	}
	if (poolEq(name, "line")) {
		doLineDir();
		return;
	}
	if (poolEq(name, "asm")) {
		doAsm();
		return;
	}
	if (poolEq(name, "endasm"))
		fatal("#endasm ohne #asm", "");
	if (poolEq(name, "ident") || poolEq(name, "sccs")) {
		/* Identification directives from older Unix compilers have no effect
		   on translation, so deliberately skip them. */
		skipRestOfLine();
		return;
	}

	fatal("unbekannte Direktive: #", poolAt(name));
}

/* ================================================================== main == */
/* Function: setupBuiltins
 * Initializes predefined macros and fixed date/time values.
 * Parameters: wantAnsi Enables __STDC__; noPredef Disables target defaults;
 *             dateText, timeText Replacement date and time strings.
 * Returns: Nothing. */
static void setupBuiltins(int wantAnsi, int noPredef, int dateText, int timeText)
{
	int at;

	textFile = intern("__FILE__");
	textLine = intern("__LINE__");
	textDefined = intern("defined");
	textEndasm = intern("endasm");

	defineMacro(textFile, 0, 0, 0, 0, 0, 1);
	defineMacro(textLine, 0, 0, 0, 0, 0, 2);

	at = mtTop;
	mtKind[mtTop] = TK_STR;
	mtText[mtTop] = dateText;
	mtWs[mtTop] = 0;
	mtTop++;
	defineMacro(intern("__DATE__"), 0, 0, 0, at, 1, 0);

	at = mtTop;
	mtKind[mtTop] = TK_STR;
	mtText[mtTop] = timeText;
	mtWs[mtTop] = 0;
	mtTop++;
	defineMacro(intern("__TIME__"), 0, 0, 0, at, 1, 0);

	if (!noPredef) {
		/* _OSK and _UCC: measured from the reference compiler; xcc sets exactly
		   zwei (weder _OS9000 noch __STDC__).
		   _Q9 und _Q9OS: EIGENE Kennungen der Q9-Kette. _Q9 heisst "mit
		   den Q9-Werkzeugen uebersetzt" (also qcpp/QCC statt xcc/Ultra
		   C), _Q9OS "Ziel ist Q9-OS". Damit kann Quelltext, der auf
		   beiden Ketten laufen soll, die Unterschiede benennen, statt
		   sie an _OSK zu haengen -- das gilt fuer Microware genauso und
		   taugt deshalb nicht zur Unterscheidung. */
		at = mtTop;
		mtKind[mtTop] = TK_NUM;
		mtText[mtTop] = intern("1");
		mtWs[mtTop] = 0;
		mtTop++;
		defineMacro(intern("_OSK"), 0, 0, 0, at, 1, 0);
		defineMacro(intern("_UCC"), 0, 0, 0, at, 1, 0);
		defineMacro(intern("_Q9"), 0, 0, 0, at, 1, 0);
		defineMacro(intern("_Q9OS"), 0, 0, 0, at, 1, 0);
	}
	if (wantAnsi) {
		at = mtTop;
		mtKind[mtTop] = TK_NUM;
		mtText[mtTop] = intern("1");
		mtWs[mtTop] = 0;
		mtTop++;
		defineMacro(intern("__STDC__"), 0, 0, 0, at, 1, 0);
	}
}

/* -D name / -D name=value. */
/* Function: defineFromArg
 * Parses and registers one command-line -D definition.
 * Parameters: arg Definition text without the -D prefix.
 * Returns: Nothing. */
static void defineFromArg(const char *arg)
{
	int i;
	int n;
	int name;
	int at;
	int bodyN;
	int eq;

	n = strLen(arg);
	eq = -1;
	for (i = 0; i < n; i++) {
		if (arg[i] == '=') {
			eq = i;
			i = n;
		}
	}
	if (eq < 0) {
		name = internN(arg, n);
		at = mtTop;
		if (mtTop >= MT_MAX)
			fatal("Makrospeicher voll (MT_MAX)", "");
		mtKind[mtTop] = TK_NUM;
		mtText[mtTop] = intern("1");
		mtWs[mtTop] = 0;
		mtTop++;
		defineMacro(name, 0, 0, 0, at, 1, 0);
		return;
	}
	name = internN(arg, eq);
	at = mtTop;
	bodyN = 0;
	/* Take the value as one token. Compound values such as "-Dx=a+b" pass
	   through as one piece; this is sufficient for the numeric and identifier
	   values used by this toolchain and avoids a second command-line lex pass. */
	if (eq + 1 < n) {
		if (mtTop >= MT_MAX)
			fatal("Makrospeicher voll (MT_MAX)", "");
		mtKind[mtTop] = TK_OTHER;
		mtText[mtTop] = internN(&arg[eq + 1], n - eq - 1);
		mtWs[mtTop] = 0;
		if (isAlphaCh(arg[eq + 1] & 255))
			mtKind[mtTop] = TK_ID;
		else if (isDigitCh(arg[eq + 1] & 255))
			mtKind[mtTop] = TK_NUM;
		mtTop++;
		bodyN = 1;
	}
	defineMacro(name, 0, 0, 0, at, bodyN, 0);
}

/* Function: undefFromArg
 * Removes one command-line -U definition.
 * Parameters: arg Macro name without the -U prefix.
 * Returns: Nothing. */
static void undefFromArg(const char *arg)
{
	int m;

	m = macFind(intern(arg));
	if (m >= 0)
		macName[m] = intern("#undef#");
}

/* Function: argEq
 * Compares two command-line argument strings.
 * Parameters: a, b Strings to compare.
 * Returns: Non-zero when equal. */
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

/* Function: argStarts
 * Checks whether a command-line argument has a given prefix.
 * Parameters: a Argument; pre Prefix.
 * Returns: Non-zero when the prefix matches. */
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

/* Function: usage
 * Prints command-line usage information and terminates with an error status.
 * Parameters: None.
 * Returns: Does not return. */
static void usage(void)
{
	printf("qcpp -- C-Praeprozessor der Q9-Werkzeugkette\n");
	printf("Aufruf: qcpp [Optionen] <eingabe.c> <ausgabe.i>\n");
	printf("  -D<name>[=<wert>]  Makro vorbelegen\n");
	printf("  -U<name>           Makro loeschen\n");
	printf("  -I<verzeichnis>    Suchpfad fuer #include\n");
	printf("  -ansi              __STDC__ auf 1 setzen (SDK-Header liefern\n");
	printf("                     dann Prototypen statt K&R-Deklarationen)\n");
	printf("  -nopredef          _OSK/_UCC/_Q9/_Q9OS NICHT vorbelegen\n");
	printf("  -lines             #line-Marken ausgeben\n");
	printf("  -min               uebersprungene Zeilen nicht auffuellen\n");
	printf("  -asm-strip         #asm/#endasm-Marken weglassen (wie xcc -pp)\n");
	printf("  -v                 gelesene/geschriebene Byteanzahl melden\n");
	printf("  -fdate=<text>      Wert fuer __DATE__\n");
	printf("  -ftime=<text>      Wert fuer __TIME__\n");
	exit(2);
}

/* Function: selfCheck
 * Verifies compile-time table sizes against their runtime invariants.
 * Parameters: None.
 * Returns: Nothing; terminates on an inconsistent build. */
static void selfCheck(void)
{
	/* Array sizes appear as literals in declarations because QCC accepts only
	   numbers in array bounds. Compare them with the runtime limits so the
	   duplicated values cannot drift apart. */
	int slot;

	slot = (int)sizeof(pbKind) / PB_MAX;    /* Size of one int slot */

	if ((int)sizeof(pool) != POOL_MAX)
		fatal("innerer Fehler: POOL_MAX passt nicht zu pool[]", "");
	if ((int)sizeof(srcArena) != SRC_MAX)
		fatal("innerer Fehler: SRC_MAX passt nicht zu srcArena[]", "");
	if ((int)sizeof(pbKind) != PB_MAX * slot)
		fatal("innerer Fehler: PB_MAX passt nicht zu pbKind[]", "");
	if ((int)sizeof(mtKind) != MT_MAX * slot)
		fatal("innerer Fehler: MT_MAX passt nicht zu mtKind[]", "");
	if ((int)sizeof(agKind) != AG_MAX * slot)
		fatal("innerer Fehler: AG_MAX passt nicht zu agKind[]", "");
	if ((int)sizeof(exKind) != EX_MAX * slot)
		fatal("innerer Fehler: EX_MAX passt nicht zu exKind[]", "");
	if ((int)sizeof(macName) != MAC_MAX * slot)
		fatal("innerer Fehler: MAC_MAX passt nicht zu macName[]", "");
	if ((int)sizeof(evKind) != EV_MAX * slot)
		fatal("innerer Fehler: EV_MAX passt nicht zu evKind[]", "");
	if ((int)sizeof(hashNext) != HASH_MAX * slot)
		fatal("innerer Fehler: HASH_MAX passt nicht zu hashNext[]", "");
	if ((int)sizeof(hashHead) != HASH_BUCKETS * slot)
		fatal("innerer Fehler: HASH_BUCKETS passt nicht zu hashHead[]", "");
	if ((int)sizeof(lxTmp) != LXTMP_MAX)
		fatal("innerer Fehler: LXTMP_MAX passt nicht zu lxTmp[]", "");
}

/*
 * Function: main
 *
 * Parses command-line options and runs the preprocessing pipeline.
 *
 * Parameters:
 *   argc, argv  Command-line argument count and vector.
 *
 * Returns:
 *   0 on success; a non-zero process status on error.
 */
int main(int argc, char **argv)
{
	int i;
	int k;
	const char *inPath;
	const char *outPath;
	int wantAnsi;
	int noPredef;
	int dateText;
	int timeText;
	int id;

	inPath = 0;
	outPath = 0;
	wantAnsi = 0;
	noPredef = 0;
	optLines = 0;
	optMin = 0;
	optAsmStrip = 0;

	lxFile = -1;
	lxLine = 0;
	selfCheck();
	setupPuncts();

	dateText = intern("\"Jan  1 1970\"");
	timeText = intern("\"00:00:00\"");

	/* Collect options before creating predefined macros so -D can override
	   one of the defaults. */
	for (i = 1; i < argc; i++) {
		char *a;

		a = argv[i];
		k = argStarts(a, "-fdate=");
		if (k > 0) {
			int n;
			int j;

			n = 0;
			n = lexAppend(n, '"');
			j = k;
			while (a[j] != 0) {
				n = lexAppend(n, a[j]);
				j++;
			}
			n = lexAppend(n, '"');
			dateText = internN(lxTmp, n);
			continue;
		}
		k = argStarts(a, "-ftime=");
		if (k > 0) {
			int n;
			int j;

			n = 0;
			n = lexAppend(n, '"');
			j = k;
			while (a[j] != 0) {
				n = lexAppend(n, a[j]);
				j++;
			}
			n = lexAppend(n, '"');
			timeText = internN(lxTmp, n);
			continue;
		}
		if (argEq(a, "-ansi")) {
			wantAnsi = 1;
			continue;
		}
		if (argEq(a, "-nopredef")) {
			noPredef = 1;
			continue;
		}
		if (argEq(a, "-v")) {
			optVerbose = 1;
			continue;
		}
	}

	setupBuiltins(wantAnsi, noPredef, dateText, timeText);

	/* Store each argument in a local pointer: the address of the doubly
	   indexed expression "&argv[i][k]" is rejected by QCC, while "&a[k]"
	   on char* is accepted. The local pointer also keeps this loop readable. */
	for (i = 1; i < argc; i++) {
		char *a;

		a = argv[i];
		if (argStarts(a, "-fdate=") > 0)
			continue;
		if (argStarts(a, "-ftime=") > 0)
			continue;
		if (argEq(a, "-ansi") || argEq(a, "-nopredef"))
			continue;
		if (argEq(a, "-lines")) {
			optLines = 1;
			continue;
		}
		if (argEq(a, "-min")) {
			optMin = 1;
			continue;
		}
		if (argEq(a, "-asm-strip")) {
			optAsmStrip = 1;
			continue;
		}
		if (argEq(a, "-v")) {
			optVerbose = 1;
			continue;
		}
		if (argEq(a, "-h") || argEq(a, "-?"))
			usage();
		k = argStarts(a, "-D");
		if (k > 0 && a[k] != 0) {
			defineFromArg(&a[k]);
			continue;
		}
		k = argStarts(a, "-U");
		if (k > 0 && a[k] != 0) {
			undefFromArg(&a[k]);
			continue;
		}
		k = argStarts(a, "-I");
		if (k > 0 && a[k] != 0) {
			if (dirN >= DIR_MAX)
				fatal("zu viele -I-Verzeichnisse (DIR_MAX)", "");
			dirPath[dirN] = intern(&a[k]);
			dirN++;
			continue;
		}
		if (a[0] == '-' && a[1] != 0) {
			printf("qcpp: unbekannte Option %s\n", a);
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
		printf("qcpp: Eingabe nicht lesbar: %s\n", inPath);
		exit(1);
	}
	lxFile = id;
	lxPos = flStart[id];
	lxLine = 1;

	outFp = fopen(outPath, "w");
	if (outFp == 0) {
		printf("qcpp: Ausgabe nicht schreibbar: %s\n", outPath);
		exit(1);
	}

	lastFile = id;
	lastLine = 1;
	lastCh = 10;
	atOutBOL = 1;
	atBOL = 1;

	/* Emit the first marker here: outTok() emits markers only on file changes,
	   but the input file is current from the start. Setting lastFile to -1
	   would be shorter but would lose line padding before the first token. */
	if (optLines)
		outLineMarker(id, 1);

	while (1) {
		nextRaw();
		if (tkKind == TK_EOF) {
			if (isDepth > 0) {
				/* Compare against the depth at include time, not zero: an
				   #include inside an #if branch is normal, while an #if left
				   open inside the header itself is an error. */
				if (cdDepth != isCd[isDepth - 1])
					fatal("#if in dieser Datei nicht geschlossen", "");
				popInclude();
				atBOL = 1;
				continue;
			}
			break;
		}
		if (tkKind == TK_NL) {
			atBOL = 1;
			continue;
		}
		if (tkKind == TK_PUNCT && poolEq(tkText, "#") && atBOL &&
		    !tkFromPB) {
			directive();
			atBOL = 1;
			continue;
		}
		atBOL = 0;
		if (skipping)
			continue;
		pbPushCur();
		nextExpanded();
		outTok();
	}

	if (cdDepth != 0)
		fatal("#if nicht geschlossen (fehlendes #endif)", "");

	if (!atOutBOL)
		outCh(10);
	outFlush();
	fclose(outFp);
	/* Print this message only after writing and closing. The earlier read
	   message appears at the start of the run and is not an end marker; the
	   self-host test waits for this final message before stopping the emulator. */
	if (optVerbose)
		printf("qcpp: geschrieben: %d Byte nach %s\n", outTotal, outPath);
	return 0;
}
