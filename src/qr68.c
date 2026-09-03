/*
 * qr68 -- 68k-Assembler der Q9-Werkzeugkette, Ausgabe im OS-9-ROF-Format
 *
 * Aufruf:  qr68 [Optionen] <eingabe.a> <ausgabe.r>
 *
 * Ziel: Microwares r68 ersetzen. Der Praeprozessor (qcpp) und das
 * Compiler-Frontend (QCC) laufen schon auf dem Ziel; Assembler und Binder
 * sind die letzten Fremdteile der Kette.
 *
 * ---------------------------------------------------------------------------
 * DAS ROF-FORMAT, AM ORIGINAL GEMESSEN (2026-09-03)
 *
 * Es gibt genau eine Beschreibung des Formats -- MWOS/APPS/src/osk-disasm
 * (rof.c/rof.h, ein Disassembler) -- und die stimmt in einem wesentlichen
 * Punkt NICHT mit dem ueberein, was r68 schreibt: dort werden die Zaehler
 * (Globale, Externe, Referenzen) mit fread_w als 16-Bit-Woerter gelesen,
 * r68 schreibt sie aber als 32-Bit-Langwoerter. Nachgemessen mit Proben von
 * 0, 1, 2 und 3 Symbolen. Deshalb hier das gemessene Layout:
 *
 *   Kopf (56 Byte, alles big-endian):
 *     0  sync            4   $DEADFACE
 *     4  ty_lan          2   Typ/Sprache      (psect-Parameter 2)
 *     6  att_rev         2   Attribute/Rev    (psect-Parameter 3)
 *     8  valid           2   0
 *    10  series          2   Assembler-Kennung, r68 V2.9.1 schreibt 249
 *    12  rdate           6   Jahr-1900, Monat, Tag, Stunde, Minute, Sekunde
 *    18  edition         2   (psect-Parameter 4)
 *    20  statstorage     4   Groesse der statischen Daten (vsect)
 *    24  idatsz          4   Groesse der initialisierten Daten
 *    28  codsz           4   Groesse des Codes
 *    32  stksz           4   (psect-Parameter 5)
 *    36  code_begin      4   Einsprungpunkt  (psect-Parameter 6)
 *    40  utrap           4   Trap-Einsprung  (psect-Parameter 7, sonst -1)
 *    44  remotestatsiz   4
 *    48  remoteidatsiz   4
 *    52  debugsiz        4
 *
 *   danach:
 *     Psect-Name, NUL-terminiert
 *     Anzahl Globale (4)
 *       je Global:  Name (NUL-term), Typ (2), Adresse (4)
 *     Code (codsz Byte) -- r68 fuellt mit NOP ($4E71) auf ein Vielfaches
 *                          von 4 auf
 *     Initialisierte Daten (idatsz Byte)
 *     Anzahl externer Namen (4)
 *       je Name:  Name (NUL-term), Anzahl Referenzen (4),
 *                 je Referenz: Typ (2), Offset im Code (4)
 *     Anzahl lokaler Referenzen (4)
 *       je Referenz: Typ (2), Offset (4)
 *     Anzahl Common-Bloecke (4)
 *
 * Gemessene Typwoerter: Global auf einem Code-Label = $0004; externe
 * Referenz, 32 Bit absolut im Code = $0038. Weitere Faelle werden beim
 * Ausbau einzeln gemessen, nicht geraten.
 *
 * ---------------------------------------------------------------------------
 * PRUEFSTEIN
 *
 * r68 ist byteweise reproduzierbar -- bis auf die sechs Zeitstempelbytes im
 * Kopf. qr68 gilt als richtig, wenn seine Ausgabe zu der von r68
 * byteidentisch ist, diese sechs Bytes ausgenommen (test/difftest.sh).
 * Das ist ein echtes Orakel: kein selbstgeschriebener Sollwert, der erst
 * selbst richtig sein muesste.
 *
 * ---------------------------------------------------------------------------
 * STAND
 *
 * Erste Etappe: Rahmen, Symbole, Ausdruecke, psect/vsect/ends, dc/ds/align,
 * equ/set und die Befehle rts/nop/jsr -- genug, um den ROF-Schreiber gegen
 * r68 zu stellen. Die Befehlstabelle wird danach vom Korpus getrieben
 * ausgebaut (644 handgeschriebene Dateien, 207.847 Zeilen in MWOS + Q9-OS).
 * Alles, was noch nicht kodiert werden kann, bricht mit Meldung ab -- nicht
 * still uebergangen.
 *
 * Geschrieben im QCC-Subset wie qcpp (keine Unions, kein "->", kein float,
 * Arraygroessen als Literale, feste Tabellen statt malloc), damit qr68 sich
 * spaeter selbst uebersetzen laesst.
 */

/* --------------------------------------------------------------- libc ---- */
extern char *fopen(const char *path, const char *mode);
extern int fclose(char *f);
extern int fread(char *buf, int size, int n, char *f);
extern int fwrite(const char *buf, int size, int n, char *f);
extern int printf(const char *fmt, ...);
extern void exit(int code);

/* ------------------------------------------------------------ Grenzen ---- */
/* Arraygroessen als Literale (QCCs constSize kennt nur Zahlen), daneben die
   Spiegelvariable fuer die Pruefungen; selfCheck() vergleicht beides. */
static char pool[262144];
static int POOL_MAX = 262144;
static int poolTop;

static char srcArena[1048576];
static int SRC_MAX = 1048576;
static int srcTop;

static int FILE_MAX = 64;
static int flName[64];
static int flStart[64];
static int flEnd[64];
static int flN;

/* Symbole */
static int SYM_MAX = 8192;
static int symName[8192];
static int symValue[8192];
static int symSect[8192];      /* s. SECT_* */
static int symDefined[8192];
static int symGlobal[8192];
static int symUsed[8192];
static int symN;

/* Codeausgabe */
static char codeBuf[1048576];
static int CODE_MAX = 1048576;
static int codeN;

/* Initialisierte Daten (vsect) */
static char idataBuf[262144];
static int IDATA_MAX = 262144;
static int idataN;

/* Referenzen auf externe Namen und auf eigene Symbole */
static int REF_MAX = 16384;
static int refName[16384];     /* Pool-Index des Namens (extern) oder -1 */
static int refType[16384];
static int refOffs[16384];
static int refLocal[16384];    /* 1 = lokale Referenz (eigenes Symbol) */
static int refN;

static char lxTmp[4096];
static int LXTMP_MAX = 4096;

static char outBuf[8192];
static int outN;
static char *outFp;

/* Abschnittskennungen */
static int SECT_NONE = 0;
static int SECT_CODE = 1;
static int SECT_IDATA = 2;     /* vsect: initialisierte Daten */
static int SECT_UDATA = 3;     /* vsect: reservierte Daten (ds) */
static int SECT_ABS = 4;       /* equ/set: absoluter Wert */
static int SECT_EXTERN = 5;

/* Psect-Angaben */
static int psName;
static int psTyLan;
static int psAttRev;
static int psEdition;
static int psStack;
static int psEntry;
static int psTrap;
static int psSeen;

/* Im vsect haben initialisierte (dc) und reservierte (ds) Daten JE EINEN
   EIGENEN Adressraum, beide ab 0 -- an r68 gemessen: bei "d1 dc.l / d2 dc.l /
   u1 ds.b 4 / u2 ds.b 4" kommen die Adressen 0,4 und 0,4 heraus, idatsz=8 und
   statstorage=8. */
static int idataPC;
static int udataPC;
static int statStorage;        /* reservierte Groesse im vsect */

/* Zeitstempel: fest, damit die Ausgabe reproduzierbar ist. Mit -fdate=
   setzbar, damit der Differenztest gegen r68 dessen Stempel nachbilden
   kann. */
static int dtYear = 126;       /* Jahr - 1900 */
static int dtMonth = 1;
static int dtDay = 1;
static int dtHour = 0;
static int dtMin = 0;
static int dtSec = 0;

static int optVerbose;
static int pass;               /* 1 = Adressen bestimmen, 2 = ausgeben */

/* Zustand der aktuellen Zeile */
static int curFile;
static int curLine;
static int curSect;
static int curPC;              /* Offset im aktuellen Abschnitt */

/* =============================================================== Zeichen == */
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

static int internN(const char *s, int n)
{
	int i;
	int j;
	int idx;
	int same;

	/* Lineare Suche: die Symbolmengen hier sind klein genug (der groesste
	   Korpusfall hat wenige Tausend Namen), und eine Hashtabelle waere der
	   erste Kandidat, falls das je messbar bremst. */
	for (i = 0; i < poolTop; i++) {
		if (pool[i] == 0)
			continue;
		same = 1;
		for (j = 0; j < n; j++) {
			if (pool[i + j] != s[j]) {
				same = 0;
				j = n;
			}
		}
		if (same && pool[i + n] == 0)
			return i;
		while (i < poolTop && pool[i] != 0)
			i++;
	}
	if (poolTop + n + 1 >= POOL_MAX)
		fatal("Namensspeicher voll (POOL_MAX)", "");
	idx = poolTop;
	for (j = 0; j < n; j++)
		pool[idx + j] = s[j];
	pool[idx + n] = 0;
	poolTop = poolTop + n + 1;
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

	fn = "<keine Datei>";
	if (curFile >= 0 && curFile < flN)
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
		/* In Haeppchen lesen, nicht der ganze freie Rest in einem Zug:
		   auf OS-9 kam ein einzelnes grosses fread mit 0 zurueck (bei
		   qcpp gemessen). */
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

/* ================================================================ Symbole = */
static int symFind(int name)
{
	int i;

	for (i = 0; i < symN; i++) {
		if (symName[i] == name)
			return i;
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
	symName[s] = name;
	symValue[s] = 0;
	symSect[s] = SECT_NONE;
	symDefined[s] = 0;
	symGlobal[s] = 0;
	symUsed[s] = 0;
	symN++;
	return s;
}

static void symDefine(int name, int value, int sect, int global)
{
	int s;

	s = symIntern(name);
	if (symDefined[s] && pass == 1)
		fatal("Symbol doppelt definiert: ", poolAt(name));
	symValue[s] = value;
	symSect[s] = sect;
	symDefined[s] = 1;
	if (global)
		symGlobal[s] = 1;
}

/* ============================================================= Zeilenleser */
/* Liefert das logische Zeichen: CR, CR+LF und LF werden alle als LF
   gemeldet. OS-9-Textdateien enden mit CR -- ohne diese Vereinheitlichung
   ist die ganze Datei eine Zeile (bei qcpp im Emulator genau so passiert). */
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

/* Eine Zeile in lxTmp holen (ohne Umbruch). Rueckgabe 0 = Dateiende. */
static int readLine(void)
{
	int n;
	int c;

	n = 0;
	c = rdPeek();
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

/* ========================================================== Ausdruecke ==== */
/* Microware-Syntax: $hex, %binaer, @oktal, 'z' Zeichen, Dezimal; Operatoren
   + - * / & ! (oder) ^ (xor) << >> ~ und Klammern. "*" allein ist der
   aktuelle Ort. Vorrang: unaer, dann * / , dann + - , dann Schiebe, dann
   & ! ^ -- wie bei r68. */
static const char *exP;
static int exSect;             /* Abschnitt des Ergebnisses */
static int exExtern;           /* Pool-Index eines externen Namens, sonst -1 */

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
		exP = exP + 1;
		return -exPrimary();
	}
	if (exP[0] == '+') {
		exP = exP + 1;
		return exPrimary();
	}
	if (exP[0] == '~') {
		exP = exP + 1;
		return ~exPrimary();
	}
	if (exP[0] == '$') {
		exP = exP + 1;
		return exNumber(16);
	}
	if (exP[0] == '%') {
		exP = exP + 1;
		return exNumber(2);
	}
	if (exP[0] == '@') {
		exP = exP + 1;
		return exNumber(8);
	}
	if (exP[0] == 39) {
		/* Zeichenkonstante: 'A' oder mehrere Zeichen */
		exP = exP + 1;
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
		/* aktueller Ort */
		exP = exP + 1;
		exSect = curSect;
		return curPC;
	}
	if (isDigitCh(exP[0] & 255))
		return exNumber(10);

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
			/* Im ersten Durchlauf ist eine Vorwaertsreferenz normal.
			   Bleibt sie im zweiten undefiniert, ist es ein externer
			   Name -- den traegt der Aufrufer als Referenz ein. */
			exExtern = name;
			exSect = SECT_EXTERN;
			return 0;
		}
		if (symSect[s] != SECT_ABS)
			exSect = symSect[s];
		return symValue[s];
	}

	fatal("unerwartetes Zeichen im Ausdruck: ", exP);
	return 0;
}

static int exMul(void)
{
	int v;
	int r;

	v = exPrimary();
	while (1) {
		exSkip();
		if (exP[0] == '*' && exP[1] != 0) {
			exP = exP + 1;
			v = v * exPrimary();
			continue;
		}
		if (exP[0] == '/') {
			exP = exP + 1;
			r = exPrimary();
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

	v = exMul();
	while (1) {
		exSkip();
		if (exP[0] == '+') {
			exP = exP + 1;
			v = v + exMul();
			continue;
		}
		if (exP[0] == '-') {
			exP = exP + 1;
			v = v - exMul();
			continue;
		}
		break;
	}
	return v;
}

static int exShift(void)
{
	int v;

	v = exAdd();
	while (1) {
		exSkip();
		if (exP[0] == '<' && exP[1] == '<') {
			exP = exP + 2;
			v = v << exAdd();
			continue;
		}
		if (exP[0] == '>' && exP[1] == '>') {
			exP = exP + 2;
			v = v >> exAdd();
			continue;
		}
		break;
	}
	return v;
}

static int exprTop(void)
{
	int v;

	v = exShift();
	while (1) {
		exSkip();
		if (exP[0] == '&') {
			exP = exP + 1;
			v = v & exShift();
			continue;
		}
		if (exP[0] == '!') {
			/* "!" ist bei Microware das bitweise ODER */
			exP = exP + 1;
			v = v | exShift();
			continue;
		}
		if (exP[0] == '^') {
			exP = exP + 1;
			v = v ^ exShift();
			continue;
		}
		break;
	}
	return v;
}

/* Ausdruck aus einer Zeichenkette auswerten. exSect/exExtern beschreiben
   danach, worauf sich das Ergebnis bezieht. */
static int evalExpr(const char *s)
{
	int v;

	exP = s;
	exSect = SECT_ABS;
	exExtern = -1;
	v = exprTop();
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

/* ============================================================ Code legen == */
static void emitByte(int b)
{
	if (curSect == SECT_CODE) {
		if (pass == 2) {
			if (codeN >= CODE_MAX)
				fatal("Codespeicher voll (CODE_MAX)", "");
			codeBuf[codeN] = b & 255;
			codeN++;
		}
	} else if (curSect == SECT_IDATA) {
		if (pass == 2) {
			if (idataN >= IDATA_MAX)
				fatal("Datenspeicher voll (IDATA_MAX)", "");
			idataBuf[idataN] = b & 255;
			idataN++;
		}
		idataPC++;
		curPC = idataPC;
		return;
	} else if (curSect == SECT_UDATA) {
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
	if (pass != 2)
		return;
	if (refN >= REF_MAX)
		fatal("zu viele Referenzen (REF_MAX)", "");
	refName[refN] = name;
	refType[refN] = type;
	refOffs[refN] = offs;
	refLocal[refN] = local;
	refN++;
}

/* ============================================================== selfCheck = */
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
}

/* ========================================================= Zeilenzerlegung */
/* Microware-Format: Label in Spalte 1, Mnemonic eingerueckt, danach die
   Operanden, danach Kommentar. "*" in Spalte 1 ist eine Kommentarzeile.
   Ein Label mit ":" ist GLOBAL -- gemessen an r68: aus "start: rts" wird ein
   Global-Eintrag im ROF, aus "start rts" nicht. */
static char lnLabel[256];
static char lnOp[64];
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
	lnArg[0] = 0;
	lnGlobal = 0;

	i = 0;
	c = lxTmp[0] & 255;
	if (c == 0)
		return 0;
	if (c == '*' || c == ';')
		return 0;

	/* Labelfeld */
	if (!isSpaceCh(c)) {
		n = 0;
		while (lxTmp[i] != 0 && !isSpaceCh(lxTmp[i] & 255)) {
			if (n + 1 >= 256)
				fatal("Label zu lang", "");
			lnLabel[n] = lxTmp[i];
			n++;
			i++;
		}
		lnLabel[n] = 0;
		if (n > 0 && lnLabel[n - 1] == ':') {
			lnLabel[n - 1] = 0;
			lnGlobal = 1;
		}
	}

	/* Mnemonic */
	while (lxTmp[i] != 0 && isSpaceCh(lxTmp[i] & 255))
		i++;
	n = 0;
	while (lxTmp[i] != 0 && !isSpaceCh(lxTmp[i] & 255)) {
		if (n + 1 >= 64)
			fatal("Mnemonic zu lang", "");
		lnOp[n] = lowerCh(lxTmp[i] & 255);
		n++;
		i++;
	}
	lnOp[n] = 0;

	/* Operanden -- Leerzeichen beenden das Feld, ausser innerhalb von
	   Anfuehrungszeichen ("dc.b \"a b\"" muss ganz bleiben). */
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

/* Groessenbuchstabe eines Mnemonics ("move.l" -> 'l'), 0 wenn keiner. */
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

/* Mnemonic ohne Groessenbuchstaben in buf. */
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
   Die sieben Angaben landen unveraendert im ROF-Kopf; fehlt die siebte, ist
   utrap -1 (an r68 gemessen). */
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
		/* Zeichenkette? */
		if (p[i] == '"' || p[i] == 39) {
			q = p[i];
			i++;
			while (p[i] != 0 && p[i] != q) {
				emitByte(p[i] & 255);
				i++;
			}
			if (p[i] == q)
				i++;
			if (p[i] == ',')
				i++;
			continue;
		}
		start = i;
		depth = 0;
		while (p[i] != 0) {
			if (p[i] == '(')
				depth++;
			else if (p[i] == ')')
				depth--;
			else if (p[i] == ',' && depth == 0)
				break;
			i++;
		}
		len = i - start;
		for (k = 0; k < len; k++)
			lxTmp[LXTMP_MAX - 2048 + k] = p[start + k];
		lxTmp[LXTMP_MAX - 2048 + len] = 0;
		v = evalExpr(&lxTmp[LXTMP_MAX - 2048]);

		if (size == 'b') {
			emitByte(v);
		} else if (size == 'w') {
			emitWord(v);
		} else {
			/* Langwort: zeigt es auf ein eigenes oder externes
			   Symbol, braucht der Binder eine Referenz darauf. */
			if (exExtern >= 0)
				addRef(exExtern, 0x38, curPC, 0);
			else if (exSect == SECT_CODE)
				addRef(-1, 0x38, curPC, 1);
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

static void doAlign(void)
{
	int a;

	a = 2;
	if (lnArg[0] != 0)
		a = evalExpr(lnArg);
	if (a < 1)
		fatal("align mit ungueltiger Groesse", "");
	while ((curPC % a) != 0)
		emitByte(0);
}

/* ============================================================== Befehle == */
/* Erste Etappe: nur die drei Befehle, mit denen der ROF-Schreiber gegen r68
   gestellt werden kann. Alles andere bricht ab -- lieber eine klare Meldung
   als eine stille Falschkodierung. */
static void doInstruction(void)
{
	char base[64];

	opBase(base);

	if (baseIs(base, "rts")) {
		emitWord(0x4E75);
		return;
	}
	if (baseIs(base, "nop")) {
		emitWord(0x4E71);
		return;
	}
	if (baseIs(base, "rte")) {
		emitWord(0x4E73);
		return;
	}
	if (baseIs(base, "jsr")) {
		int v;

		/* Erst nur die absolute lange Form (4EB9) -- genau die, die
		   der QCC-Backend fuer Aufrufe erzeugt. */
		v = evalExpr(lnArg);
		emitWord(0x4EB9);
		if (exExtern >= 0)
			addRef(exExtern, 0x38, curPC, 0);
		else if (exSect == SECT_CODE)
			addRef(-1, 0x38, curPC, 1);
		emitLong(v);
		return;
	}

	fatal("Befehl noch nicht kodierbar: ", lnOp);
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
	statStorage = 0;
	idataPC = 0;
	udataPC = 0;

	while (readLine()) {
		if (!splitLine())
			continue;

		/* Im vsect entscheidet die Direktive der SELBEN Zeile, in
		   welchen Adressraum ein Label gehoert: "dc" in die
		   initialisierten Daten, "ds" in die reservierten. Deshalb
		   wird der Abschnitt VOR dem Label festgelegt. */
		if (curSect == SECT_IDATA || curSect == SECT_UDATA) {
			char b2[64];

			opBase(b2);
			if (baseIs(b2, "ds")) {
				curSect = SECT_UDATA;
				curPC = udataPC;
			} else if (baseIs(b2, "dc") || baseIs(b2, "dcb")) {
				curSect = SECT_IDATA;
				curPC = idataPC;
			} else if (baseIs(b2, "ends")) {
				/* faellt unten durch */
			} else if (lnOp[0] != 0 && !baseIs(b2, "equ") &&
				   !baseIs(b2, "set") && !baseIs(b2, "align")) {
				fatal("im vsect nur dc/ds/equ/set/align: ", lnOp);
			}
		}

		/* Label setzen, bevor der Befehl den Ort veraendert. */
		if (lnLabel[0] != 0) {
			int name;

			name = intern(lnLabel);
			if (opIs("equ") || opIs("set")) {
				int v;

				v = evalExpr(lnArg);
				if (pass == 1 || opIs("set")) {
					int s;

					s = symIntern(name);
					symValue[s] = v;
					symSect[s] = exSect;
					symDefined[s] = 1;
					if (lnGlobal)
						symGlobal[s] = 1;
				}
				continue;
			}
			if (pass == 1)
				symDefine(name, curPC, curSect, lnGlobal);
		}

		if (lnOp[0] == 0)
			continue;

		size = opSize();
		opBase(base);

		if (baseIs(base, "psect")) {
			doPsect();
			continue;
		}
		if (baseIs(base, "vsect")) {
			curSect = SECT_IDATA;
			curPC = idataPC;
			continue;
		}
		if (baseIs(base, "ends")) {
			if (curSect == SECT_IDATA || curSect == SECT_UDATA) {
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

		doInstruction();
	}
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

	/* r68 fuellt den Code mit NOP auf ein Vielfaches von vier auf
	   (gemessen: ein einzelnes "rts" ergibt codsz=4 mit $4E71 dahinter). */
	while ((codeN % 4) != 0) {
		if (codeN + 1 >= CODE_MAX)
			fatal("Codespeicher voll (CODE_MAX)", "");
		codeBuf[codeN] = 0x4E;
		codeBuf[codeN + 1] = 0x71;
		codeN = codeN + 2;
	}

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
	outLong(0);                    /* remotestatsiz */
	outLong(0);                    /* remoteidatsiz */
	outLong(0);                    /* debugsiz */
	outStrZ(poolAt(psName));

	/* Globale Definitionen -- ALPHABETISCH sortiert. Das ist keine
	   Kosmetik: r68 sortiert (gemessen an einer Quelle mit der Reihenfolge
	   wert/puffer/start, ausgegeben wurde puffer/start/wert), und ohne
	   dieselbe Reihenfolge gibt es keine Byteidentitaet. Sortiert wird
	   ueber die Bytewerte des Namens.
	   Typwoerter, ebenfalls gemessen: Code $0004, initialisierte Daten
	   $0001, reservierte Daten $0000. */
	outLong(nGlob);
	{
		int done;
		int best;
		int marked[8192];

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
	outLong(nExtNames);
	for (i = 0; i < refN; i++) {
		int cnt;

		if (refLocal[i])
			continue;
		seen = 0;
		for (j = 0; j < i; j++) {
			if (!refLocal[j] && refName[j] == refName[i])
				seen = 1;
		}
		if (seen)
			continue;
		cnt = 0;
		for (j = 0; j < refN; j++) {
			if (!refLocal[j] && refName[j] == refName[i])
				cnt++;
		}
		outStrZ(poolAt(refName[i]));
		outLong(cnt);
		for (j = 0; j < refN; j++) {
			if (!refLocal[j] && refName[j] == refName[i]) {
				outWord(refType[j]);
				outLong(refOffs[j]);
			}
		}
	}

	/* Lokale Referenzen */
	{
		int cnt;

		cnt = 0;
		for (i = 0; i < refN; i++) {
			if (refLocal[i])
				cnt++;
		}
		outLong(cnt);
		for (i = 0; i < refN; i++) {
			if (refLocal[i]) {
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
	printf("  -v                    gelesene/geschriebene Byteanzahl melden\n");
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

	for (i = 1; i < argc; i++) {
		char *a;

		a = argv[i];
		if (argEq(a, "-v")) {
			optVerbose = 1;
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

	/* Zwei Durchlaeufe: der erste bestimmt Adressen und Symbole, der
	   zweite gibt aus. Vorwaertsreferenzen sind damit im zweiten
	   Durchlauf aufgeloest; was dann noch offen ist, ist extern. */
	pass = 1;
	runPass();
	pass = 2;
	runPass();

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
