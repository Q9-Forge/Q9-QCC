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
static char pool[524288];
static int POOL_MAX = 524288;
static int poolTop;

/* Der handgeschriebene Korpus braucht davon nicht einmal ein Viertel
   (groesste Datei: 766 KB, MWOS/.../rlm-sys-1/libfame.a); die vom
   QCC-Backend erzeugten Quellen sind mit bis zu 18 MB deutlich groesser --
   fuer die braucht es einen stroemenden Leser statt der Arena, das ist
   offen. Die Arena ist zugleich die Groesse, die das spaetere OS-9-Modul
   mitschleppt (QCCs Backend legt genullte Tabellen in den initialisierten
   Datenbereich), also nicht beliebig aufblasen. */
static char srcArena[4194304];
static int SRC_MAX = 4194304;
static int srcTop;

static int FILE_MAX = 64;
static int flName[64];
static int flStart[64];
static int flEnd[64];
static int flN;

/* Symbole */
static int SYM_MAX = 16384;
static int symName[16384];
static int symValue[16384];
static int symSect[16384];     /* s. SECT_* */
static int symDefined[16384];
static int symGlobal[16384];
static int symUsed[16384];
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
static int REF_MAX = 65536;
static int refName[65536];     /* Pool-Index des Namens (extern) oder -1 */
static int refType[65536];
static int refOffs[65536];
static int refLocal[65536];    /* 1 = lokale Referenz (eigenes Symbol) */
static int refDone[65536];     /* Merker beim Ausgeben der externen Namen */
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
static int pass;               /* Nummer des Durchlaufs, ab 1 */
static int emitting;           /* 1 = letzter Durchlauf, Ausgabe in die Puffer */
static int symMoved;           /* 1 = in diesem Durchlauf hat sich ein Wert bewegt */

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

/* Definiert (oder bestaetigt) ein Symbol. "track" heisst: eine Aenderung
   gegenueber dem letzten Durchlauf zaehlt als Bewegung -- solange sich etwas
   bewegt, sind die Adressen nicht verlaesslich und es folgt ein weiterer
   Durchlauf. Fuer "set" ist das ausgeschaltet, denn dessen Wert darf sich
   innerhalb eines Durchlaufs mehrfach aendern. */
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
/* Abschnitt eines ABGEZOGENEN verschiebbaren Anteils, sonst SECT_NONE.
   "fremd-basis" ist bei r68 KEIN Fehler, sondern zwei Referenzen auf
   denselben Offset: die externe mit $38 und die lokale mit $7c = $40|$3c.
   Bit $40 heisst also "abziehen". Genau diese Form erzeugt QCCs Backend in
   seiner Funktionstabelle, wenn dort ein externer Name steht. */
static int exNegSect;

/* 1 = im Ausdruck stand ein Name, der noch gar nicht bekannt sein KANN.
   Nur im ersten Durchlauf moeglich: ab dem zweiten ist die Symboltabelle
   vollstaendig, ein dann noch unbekannter Name ist wirklich extern.
   Solange das offen ist, zaehlt an einem Operanden nur seine Laenge --
   Wert und Bereichspruefungen kommen im naechsten Durchlauf. */
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

/* Ein Ausdruck bezieht sich entweder auf nichts (SECT_ABS) oder auf genau
   einen verschiebbaren Abschnitt. Die Verknuepfungen pruefen das: "SYM-SYM"
   im selben Abschnitt ist absolut (genau das erzeugt das QCC-Backend in
   seiner Funktionstabelle), "SYM+SYM" ist es nicht. */
static void exNeedAbs(const char *what)
{
	if (exOpen)
		return;
	if (exSect != SECT_ABS || exExtern >= 0 || exNegSect != SECT_NONE)
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
		exP = exP + 1;
		v = -exPrimary();
		exNeedAbs("unaeres Minus");
		return v;
	}
	if (exP[0] == '+') {
		exP = exP + 1;
		return exPrimary();
	}
	if (exP[0] == '~') {
		exP = exP + 1;
		v = ~exPrimary();
		exNeedAbs("unaere Negation");
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
		/* Zeichenkonstante: 'A' oder mehrere Zeichen */
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
		/* aktueller Ort */
		exP = exP + 1;
		exSect = curSect;
		return curPC;
	}
	if (isDigitCh(exP[0] & 255)) {
		exSect = SECT_ABS;
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
			/* Im ersten Durchlauf ist eine Vorwaertsreferenz normal.
			   Bleibt sie danach undefiniert, ist es ein externer
			   Name -- den traegt der Aufrufer als Referenz ein. */
			exExtern = name;
			exSect = SECT_EXTERN;
			if (pass == 1)
				exOpen = 1;
			return 0;
		}
		exSect = symSect[s];
		if (exSect == SECT_NONE)
			exSect = SECT_ABS;
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
			exNeedAbs("Multiplikation");
			exP = exP + 1;
			v = v * exPrimary();
			exNeedAbs("Multiplikation");
			continue;
		}
		if (exP[0] == '/') {
			exNeedAbs("Division");
			exP = exP + 1;
			r = exPrimary();
			exNeedAbs("Division");
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
	int ls;
	int rs;
	int lx;

	v = exMul();
	ls = exSect;
	lx = exExtern;
	while (1) {
		exSkip();
		if (exP[0] != '+' && exP[0] != '-')
			break;
		if (exP[0] == '+') {
			exP = exP + 1;
			exSect = SECT_ABS;
			exExtern = -1;
			r = exMul();
			rs = exSect;
			v = v + r;
			/* Verschiebbar darf hoechstens eine Seite sein. */
			if (!exOpen && ls != SECT_ABS && rs != SECT_ABS)
				fatal("Summe zweier verschiebbarer Groessen", "");
			if (!exOpen && lx >= 0 && exExtern >= 0)
				fatal("Summe zweier externer Namen", "");
			if (ls == SECT_ABS)
				ls = rs;
			if (lx < 0)
				lx = exExtern;
		} else {
			exP = exP + 1;
			exSect = SECT_ABS;
			exExtern = -1;
			r = exMul();
			rs = exSect;
			v = v - r;
			if (rs != SECT_ABS) {
				/* Sind BEIDE Seiten Groessen des eigenen
				   Moduls, rechnet r68 die Differenz aus und
				   gibt KEINE Referenz aus -- und zwar auch
				   ueber Abschnittsgrenzen hinweg (gemessen an
				   "dc.l dat-basis" mit dat im vsect und basis
				   im Code: Wert 0, keine Referenz). Genau
				   davon leben die Indirektionstabellen, die
				   QCCs Backend mit -largedata erzeugt.
				   Steht links dagegen ein externer Name oder
				   eine Konstante, bleibt der abgezogene Anteil
				   offen und wird eine zweite Referenz mit $40
				   im Typwort (gemessen an "dc.l fremd-basis"
				   -> $38 und $7c, und "dc.l zwei-basis" mit
				   "zwei equ 4" -> nur $7c). */
				if (exOpen) {
					ls = SECT_ABS;
					lx = -1;
				} else if (rs == SECT_EXTERN) {
					fatal("ein externer Name als abgezogener Anteil -- nicht gemessen",
					      "");
				} else if (ls == SECT_CODE || ls == SECT_IDATA ||
					   ls == SECT_UDATA) {
					ls = SECT_ABS;
				} else if (exNegSect != SECT_NONE) {
					fatal("mehr als ein abgezogener verschiebbarer Anteil",
					      "");
				} else {
					exNegSect = rs;
				}
			}
		}
	}
	exSect = ls;
	exExtern = lx;
	return v;
}

static int exShift(void)
{
	int v;

	v = exAdd();
	while (1) {
		exSkip();
		if (exP[0] == '<' && exP[1] == '<') {
			exNeedAbs("Schiebeoperator");
			exP = exP + 2;
			v = v << exAdd();
			exNeedAbs("Schiebeoperator");
			continue;
		}
		if (exP[0] == '>' && exP[1] == '>') {
			exNeedAbs("Schiebeoperator");
			exP = exP + 2;
			v = v >> exAdd();
			exNeedAbs("Schiebeoperator");
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
			exNeedAbs("UND-Verknuepfung");
			exP = exP + 1;
			v = v & exShift();
			exNeedAbs("UND-Verknuepfung");
			continue;
		}
		if (exP[0] == '!') {
			/* "!" ist bei Microware das bitweise ODER */
			exNeedAbs("ODER-Verknuepfung");
			exP = exP + 1;
			v = v | exShift();
			exNeedAbs("ODER-Verknuepfung");
			continue;
		}
		if (exP[0] == '^') {
			exNeedAbs("XOR-Verknuepfung");
			exP = exP + 1;
			v = v ^ exShift();
			exNeedAbs("XOR-Verknuepfung");
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
	exNegSect = SECT_NONE;
	exOpen = 0;
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
	/* codeN und idataN zaehlen in JEDEM Durchlauf mit, nicht nur beim
	   Ausgeben: sie sind zugleich der Ort im Abschnitt. Nur das Ablegen im
	   Puffer haengt am letzten Durchlauf. (Zaehlten sie nur dort, saesse
	   "ends" nach einem vsect die Codemarke auf 0 und alle Adressen
	   dahinter waeren im Messdurchlauf falsch.) */
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

/* Das Typwort einer Referenz setzt sich aus drei gemessenen Teilen zusammen:
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
	else if (target != SECT_UDATA && target != SECT_EXTERN)
		fatal("innerer Fehler: Referenzziel", "");
	return t;
}

/* PC-relative Referenz auf einen externen Namen: dasselbe Typwort, dazu
   $80. Gemessen an "bsr fremd", "bra fremd", "beq fremd", "lea fremd(pc),a0"
   und "move.l fremd(pc),d0" -- alle fuenf ergeben $00b0 = $80|$30, also
   relativ, Wortbreite, im Code, Ziel unbekannt. Die kurze Sprungform lehnt
   r68 dabei ab ("illegal external reference"). */
static void refPcExtern(int ext, int size)
{
	addRef(ext, 0x80 | refTypeFor(size, SECT_EXTERN), curPC, 0);
}

/* Traegt eine Referenz ein, wenn der zuletzt ausgewertete Ausdruck sich auf
   ein verschiebbares Ziel bezieht. Der Ort ist die aktuelle Stelle, also VOR
   dem Ablegen der Bytes aufzurufen. */
static void refIfRelocatable(int sect, int ext, int neg, int size)
{
	if (ext >= 0)
		addRef(ext, refTypeFor(size, SECT_EXTERN), curPC, 0);
	else if (sect == SECT_CODE || sect == SECT_IDATA || sect == SECT_UDATA)
		addRef(-1, refTypeFor(size, sect), curPC, 1);
	if (neg == SECT_CODE || neg == SECT_IDATA || neg == SECT_UDATA)
		addRef(-1, 0x40 | refTypeFor(size, neg), curPC, 1);
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
/* r68 richtet vor allem, was mindestens ein Wort breit ist, selbst auf eine
   gerade Adresse aus -- gemessen an "dc.b 1,2,3 / nop": das nop steht auf 4,
   das Fuellbyte auf 3. Das gilt fuer Befehle wie fuer dc.w/dc.l. */
static void alignEven(void)
{
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
			refIfRelocatable(exSect, exExtern, exNegSect, 1);
			emitByte(v);
		} else if (size == 'w') {
			alignEven();
			refIfRelocatable(exSect, exExtern, exNegSect, 2);
			emitWord(v);
		} else {
			alignEven();
			refIfRelocatable(exSect, exExtern, exNegSect, 4);
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

/* Gemessen: im CODE fuellt r68 mit NOP ($4E71) auf -- ein einzelnes
   ungerades Byte davor aber mit 0, denn ein NOP ist ein WORT und braucht
   selbst eine gerade Adresse. In den initialisierten Daten wird durchgehend
   mit 0 gefuellt ("d1 dc.b 1 / align 4 / d2 dc.l 7" ergibt
   "01 00 00 00 00 00 00 07"). */
static void doAlign(void)
{
	int a;

	a = 2;
	if (lnArg[0] != 0)
		a = evalExpr(lnArg);
	if (a < 1)
		fatal("align mit ungueltiger Groesse", "");
	if (curSect == SECT_CODE) {
		if ((curPC % a) != 0 && (curPC % 2) != 0)
			emitByte(0);
		while ((curPC % a) != 0)
			emitWord(0x4E71);
		return;
	}
	while ((curPC % a) != 0)
		emitByte(0);
}

/* =========================================================== Operanden === */
/* Die Adressierungsarten als Modell. Die Zahl ist NICHT das Modefeld des
   Befehlswortes -- das liefern eaModeBits()/eaRegBits().
   An r68 gemessen und deshalb hier so und nicht anders:
   - die Klammerform "(4,a5)" kennt r68 NICHT ("parenthesis needed"),
     nur "4(a5)";
   - ein nackter Ausdruck wird IMMER absolut lang, auch wenn er in 16 Bit
     passt ("move.l $1000,d0" -> 2039); ".w" am Operanden erzwingt kurz;
   - "0(a5)" bleibt die Displacementform (41ed 0000), nur "(a5)" ist die
     indirekte. r68 verkuerzt hier nichts. */
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
static int oOpen[2];           /* 1 = Wert im ersten Durchlauf noch offen */
static int oNeg[2];            /* abgezogener verschiebbarer Anteil, s. exNegSect */
static int oN;                 /* Zahl der Operanden dieser Zeile */

static char opTxt0[512];
static char opTxt1[512];
static char exBuf[1024];

/* Kopiert s[from..to) nach exBuf. */
static void subStr(const char *s, int from, int to)
{
	int i;

	if (to - from >= 1024)
		fatal("Teilausdruck zu lang", "");
	for (i = from; i < to; i++)
		exBuf[i - from] = s[i];
	exBuf[to - from] = 0;
}

/* "d3" -> 3, "a3" und "sp" -> 8+3, sonst -1. */
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
	else
		fatal("mehr als zwei Operanden: ", lnArg);
	for (i = 0; i < len; i++)
		d[i] = lnArg[from + i];
	d[len] = 0;
}

/* Zerlegt das Operandenfeld an den Kommas der obersten Ebene. Klammern und
   Anfuehrungszeichen zaehlen mit -- "move.b #',',d0" hat zwei Operanden. */
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
	oNeg[k] = SECT_NONE;
	oIdx[k] = -1;
	oIdxL[k] = 1;

	n = strLen(s);
	if (n == 0)
		fatal("leerer Operand in: ", lnOp);

	if (s[0] == '#') {
		subStr(s, 1, n);
		oVal[k] = evalExpr(exBuf);
		oSect[k] = exSect;
		oExt[k] = exExtern;
		oOpen[k] = exOpen;
		oNeg[k] = exNegSect;
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

	/* Klammerform: die zum letzten ")" gehoerende oeffnende Klammer trennt
	   Displacement und Basis. */
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
		if (blen == 2 && lowerCh(s[lp + 1] & 255) == 'p' &&
		    lowerCh(s[lp + 2] & 255) == 'c')
			isPc = 1;
		if (r >= 8 || isPc) {
			if (!isPc)
				oReg[k] = r - 8;
			if (lp > 0) {
				subStr(s, 0, lp);
				oVal[k] = evalExpr(exBuf);
				oSect[k] = exSect;
				oExt[k] = exExtern;
				oOpen[k] = exOpen;
				oNeg[k] = exNegSect;
			}
			if (comma >= 0) {
				int il;
				int ilen;

				ilen = (ie - 1) - (comma + 1);
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

	/* Nackter Ausdruck: absolut. Ohne Zusatz nimmt r68 IMMER die lange
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
	oNeg[k] = exNegSect;
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

/* Erweiterungswoerter eines Operanden, in der Reihenfolge, in der r68 sie
   ablegt: erst die des Quell-, dann die des Zieloperanden. "size" ist der
   Umfang des Befehls und zaehlt nur beim unmittelbaren Operanden. */
static void emitEa(int k, int size)
{
	int m;
	int d;

	m = oMode[k];
	if (m == AM_DN || m == AM_AN || m == AM_IND || m == AM_POST ||
	    m == AM_PRE)
		return;
	if (oOpen[k]) {
		/* Erster Durchlauf, der Name ist noch unbekannt: hier zaehlt nur
		   die Laenge. Wert, Abschnitt und Bereichsgrenzen pruefen die
		   folgenden Durchlaeufe. */
		if (m == AM_ABSL || (m == AM_IMM && size == 4))
			emitLong(0);
		else
			emitWord(0);
		return;
	}
	if (m == AM_DISP) {
		if (oExt[k] >= 0 || oSect[k] != SECT_ABS)
			fatal("verschiebbares Displacement -- Typwort nicht gemessen: ",
			      lnArg);
		if (oVal[k] < -32768 || oVal[k] > 32767)
			fatal("Displacement passt nicht in 16 Bit: ", lnArg);
		emitWord(oVal[k]);
		return;
	}
	if (m == AM_IDX || m == AM_PCIDX) {
		if (oExt[k] >= 0)
			fatal("externer Name in einer Indexform: ", lnArg);
		d = oVal[k];
		if (m == AM_PCIDX) {
			if (oSect[k] != SECT_CODE && oSect[k] != SECT_ABS)
				fatal("PC-Bezug auf einen anderen Abschnitt: ", lnArg);
			d = d - curPC;
		} else if (oSect[k] != SECT_ABS) {
			fatal("verschiebbares Displacement in einer Indexform: ",
			      lnArg);
		}
		if (d < -128 || d > 127)
			fatal("Index-Displacement passt nicht in 8 Bit: ", lnArg);
		emitWord(((oIdx[k] & 15) << 12) | (oIdxL[k] << 11) | (d & 255));
		return;
	}
	if (m == AM_PCD) {
		/* PC-relativ auf eine eigene Codestelle: der Abstand steht fest,
		   der Binder braucht dafuer KEINE Referenz (gemessen an
		   "lea start(pc),a3" -- im ROF steht dazu nichts). Auf einen
		   externen Namen dagegen schon, mit dem Wert 0. */
		if (oExt[k] >= 0) {
			refPcExtern(oExt[k], 2);
			emitWord(0);
			return;
		}
		if (oSect[k] != SECT_CODE && oSect[k] != SECT_ABS)
			fatal("PC-Bezug auf einen anderen Abschnitt: ", lnArg);
		d = oVal[k] - curPC;
		if (d < -32768 || d > 32767)
			fatal("PC-Abstand passt nicht in 16 Bit: ", lnArg);
		emitWord(d);
		return;
	}
	if (m == AM_ABSW) {
		refIfRelocatable(oSect[k], oExt[k], oNeg[k], 2);
		emitWord(oVal[k]);
		return;
	}
	if (m == AM_ABSL) {
		refIfRelocatable(oSect[k], oExt[k], oNeg[k], 4);
		emitLong(oVal[k]);
		return;
	}
	/* AM_IMM */
	if (size == 1) {
		if (oExt[k] >= 0 || oSect[k] != SECT_ABS)
			fatal("verschiebbarer Byte-Sofortwert -- nicht gemessen: ",
			      lnArg);
		emitWord(oVal[k] & 255);
		return;
	}
	if (size == 2) {
		refIfRelocatable(oSect[k], oExt[k], oNeg[k], 2);
		emitWord(oVal[k]);
		return;
	}
	refIfRelocatable(oSect[k], oExt[k], oNeg[k], 4);
	emitLong(oVal[k]);
}

/* ============================================================== Befehle == */
/* Kodierungen nach dem M68000PRM; jede erzeugte Form ist mit
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

/* Umfangsfeld von MOVE: Byte 1, Wort 3, Langwort 2. */
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

/* Bedingungsfeld: genau zwei Zeichen, sonst -1. */
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

static void needNoSize(int size)
{
	if (size != 0)
		fatal("dieser Befehl hat keinen Groessenbuchstaben: ", lnOp);
}

/* Ein Datenregister als Operand -- fuer die Befehle, die nur eines zulassen. */
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

/* Ziel eines Datenbefehls: alles ausser unmittelbar und PC-relativ. */
static void needAlterable(int k)
{
	int m;

	m = oMode[k];
	if (m == AM_IMM || m == AM_PCD || m == AM_PCIDX)
		fatal("dieser Operand kann kein Ziel sein: ", lnArg);
}

/* Kontrolladresse: fuer jsr/jmp/lea/pea. */
static void needControl(int k)
{
	int m;

	m = oMode[k];
	if (m == AM_DN || m == AM_AN || m == AM_POST || m == AM_PRE ||
	    m == AM_IMM)
		fatal("dieser Operand ist keine Kontrolladresse: ", lnArg);
}

/* Sprungbefehle. */
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
		emitEa(0, sizeBytes(size));
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
		needOps(0);
		emitWord(0x4E75);
		return;
	}
	if (baseIs(base, "nop")) {
		needNoSize(size);
		needOps(0);
		emitWord(0x4E71);
		return;
	}
	if (baseIs(base, "rte")) {
		needNoSize(size);
		needOps(0);
		emitWord(0x4E73);
		return;
	}
	if (baseIs(base, "rtr")) {
		needNoSize(size);
		needOps(0);
		emitWord(0x4E77);
		return;
	}
	if (baseIs(base, "trapv")) {
		needNoSize(size);
		needOps(0);
		emitWord(0x4E76);
		return;
	}
	if (baseIs(base, "reset")) {
		needNoSize(size);
		needOps(0);
		emitWord(0x4E70);
		return;
	}
	if (baseIs(base, "illegal")) {
		needNoSize(size);
		needOps(0);
		emitWord(0x4AFC);
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
		if (size == 'l')
			fatal("link.l (68020) ist noch nicht gemessen: ", lnOp);
		if (size != 0 && size != 'w')
			fatal("link kennt nur die Wortform: ", lnOp);
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		v = needAn(0);
		if (oMode[1] != AM_IMM)
			fatal("link braucht einen Sofortwert als Rahmengroesse: ",
			      lnArg);
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
		if (oMode[0] != AM_IMM ||
		    (!oOpen[0] && (oExt[0] >= 0 || oSect[0] != SECT_ABS)))
			fatal("moveq braucht einen festen Sofortwert: ", lnArg);
		if (!oOpen[0] && (oVal[0] < -128 || oVal[0] > 127))
			fatal("moveq-Wert passt nicht in ein Byte: ", lnArg);
		emitWord(0x7000 | (needDn(1) << 9) | (oVal[0] & 255));
		return;
	}
	if (baseIs(base, "move") || baseIs(base, "movea")) {
		if (size == 0)
			size = 'w';
		needOps(2);
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

		if (size == 0)
			size = 'w';
		needOps(2);
		parseOperand(opTxt0, 0);
		parseOperand(opTxt1, 1);
		if (oMode[0] != AM_IMM)
			fatal("die I-Form braucht einen Sofortwert: ", lnArg);
		needAlterable(1);
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
		emitEa(0, sizeBytes(size));
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
	    baseIs(base, "divs") || baseIs(base, "divu")) {
		int op;

		if (size != 0 && size != 'w')
			fatal("nur die Wortform ist gemessen (68020-Langform fehlt): ",
			      lnOp);
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
	if (curSect != SECT_CODE && curSect != SECT_IDATA)
		return 0;
	opBase(b);
	if (baseIs(b, "equ") || baseIs(b, "set") || baseIs(b, "end") ||
	    baseIs(b, "psect") || baseIs(b, "vsect") || baseIs(b, "ends") ||
	    baseIs(b, "nam") || baseIs(b, "ttl") || baseIs(b, "page") ||
	    baseIs(b, "pag") || baseIs(b, "opt") || baseIs(b, "spc") ||
	    baseIs(b, "fail") || baseIs(b, "align"))
		return 0;
	sz = opSize();
	if (baseIs(b, "dc") || baseIs(b, "dcb") || baseIs(b, "ds")) {
		if (sz == 'b')
			return 0;
		return 1;
	}
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
	statStorage = 0;
	idataPC = 0;
	udataPC = 0;
	symMoved = 0;

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

		/* Ausrichten, bevor das Label seinen Wert bekommt. */
		if (lineAligns())
			alignEven();

		/* Label setzen, bevor der Befehl den Ort veraendert. */
		if (lnLabel[0] != 0) {
			int name;

			name = intern(lnLabel);
			if (opIs("equ") || opIs("set")) {
				int v;

				v = evalExpr(lnArg);
				/* "set" darf sich innerhalb eines Durchlaufs
				   aendern und zaehlt deshalb nicht als
				   Bewegung, "equ" schon. */
				symDefine(name, v, exSect, lnGlobal,
					  opIs("equ"));
				continue;
			}
			symDefine(name, curPC, curSect, lnGlobal, 1);
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
	   NULLBYTE, falls die Laenge ungerade ist, dann NOPs. Beides gemessen
	   -- ein einzelnes "rts" ergibt codsz=4 mit $4E71 dahinter, und eine
	   Quelle, die auf einer ungeraden Laenge endet (644475), wird mit
	   genau EINEM $00 auf 644476 gebracht. Ohne den ersten Schritt kaeme
	   eine ungerade Laenge nie auf ein Vielfaches von vier. */
	if ((codeN % 4) != 0 && (codeN % 2) != 0) {
		if (codeN >= CODE_MAX)
			fatal("Codespeicher voll (CODE_MAX)", "");
		codeBuf[codeN] = 0;
		codeN++;
	}
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
		int marked[16384];

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
			if (symMoved)
				ruhig = 0;
			else
				ruhig++;
		}
		emitting = 1;
		pass++;
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
