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
/* Der Ausdehnungsspeicher fuer Makros liegt am OBEREN Ende derselben Arena
   und waechst nach unten: eingelesene Dateien wachsen von unten,
   Ausdehnungen sind ein Stapel und werden beim Verlassen wieder
   freigegeben. So kann eine Ausdehnung nie eine Datei ueberschreiben. */
static int expTop;

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
static int symPass[16384];     /* Durchlauf der letzten Definition, s. ifdef */
/* Pool-Index eines externen Namens, auf den dieses Symbol steht, sonst -1.
   "IRQCtrl equ u_icr" (so in MWOS/.../sc68070.a:64) bindet einen Namen an
   einen EXTERNEN -- jede Benutzung von IRQCtrl muss danach wieder eine
   Referenz auf u_icr erzeugen. Ohne das fehlen im ROF stillschweigend
   Referenzen, und der Binder setzt die Adresse nie ein. */
static int symExt[16384];
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
static int optBranch;          /* -b: Sprungweiten selbst waehlen */
static int pass;               /* Nummer des Durchlaufs, ab 1 */
static int emitting;           /* 1 = letzter Durchlauf, Ausgabe in die Puffer */
static int symMoved;           /* 1 = in diesem Durchlauf hat sich ein Wert bewegt */

/* Der org-Zaehler ist NICHT der Ort im Abschnitt: "org" setzt ihn,
   "do.b/.w/.l" legt darauf Namen ab, "." liest ihn. So beschreiben die
   Definitionsdateien des SDK ihre Strukturen (1593 "do" in 127 Dateien).
   An r68 gemessen: "org 4 / A do.b 1 / B do.w 1 / C do.l 2" ergibt
   A=4, B=6, C=8 -- do.w und do.l richten vorher auf GERADE aus (nicht auf
   ihre eigene Breite), do.b nicht. Und "org" bewegt den Ort im Abschnitt
   ueberhaupt nicht: nach "nop / org 8" steht das naechste Label auf 2. */
static int orgPC;

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

/* Schon eingelesen? Eine Datei wird nur EINMAL in die Arena geholt, auch
   wenn mehrere Durchlaeufe sie mehrfach ueber "use" erreichen -- sonst
   liefe der Quelltextspeicher mit jedem Durchlauf weiter voll. */
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
	symPass[s] = 0;
	symExt[s] = -1;
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
	symPass[s] = pass;
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

/* Einschlussstapel fuer "use". */
static int USE_MAX = 16;
static int useFile[16];
static int usePos[16];
static int useLine[16];
static int useExp[16];         /* Stand des Ausdehnungsspeichers beim Eintritt */
static int useDepth;

/* Eine Zeile in lxTmp holen (ohne Umbruch). Rueckgabe 0 = Dateiende der
   aeussersten Datei; das Ende einer eingeschlossenen Datei kehrt still zur
   einschliessenden zurueck. */
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

/* ========================================================== Ausdruecke ==== */
/* Microware-Syntax: $hex, %binaer, @oktal, 'z' Zeichen, Dezimal; Operatoren
   + - * / & (und) ! (oder) << >> und unaer - + ^ (Nicht), dazu Klammern.
   "*" allein ist der aktuelle Ort, "." der org-Zaehler.
   An r68 gemessen: "^" ist UNAER (kein XOR -- "$ff^$0f" ist ein Fehler),
   und "~" gibt es nicht. */
static const char *exP;
static int exSect;             /* Abschnitt des Ergebnisses */
static int exExtern;           /* Pool-Index eines externen Namens, sonst -1 */
/* Die VERSCHIEBBAREN ANTEILE eines Ausdrucks, mit Vorzeichen. r68 loest
   "PD_PAR-PD_OPT+M$DTyp" nicht auf, sondern legt DREI Referenzen auf
   denselben Offset ab: $0030, $0070 (das $40 heisst "abziehen") und $0030.
   Ebenso ergibt "dc.l EA+EB" zwei Referenzen und "dc.l basis+basis" zwei
   lokale. Nur eine DIFFERENZ zweier moduleigener Groessen rechnet r68 aus
   und gibt gar keine Referenz aus -- auch ueber Abschnittsgrenzen hinweg
   (gemessen an "dc.l dat-basis" mit dat im vsect: Wert 0, keine Referenz).
   Genau davon leben die Indirektionstabellen von QCCs -largedata.

   Die Anteile liegen in drei Faechern: 0 und 1 fuer die beiden Operanden
   eines Befehls, 2 fuer den gerade ausgewerteten Ausdruck. */
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

/* Kuerzt Paare aus einem addierten und einem abgezogenen MODULEIGENEN
   Anteil weg -- r68 rechnet solche Differenzen aus. Externe Namen bleiben
   stehen, auch "EA-EB". */
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
		/* i und j herausnehmen, hoeheres zuerst */
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
/* Hat der gerade gelesene Teilausdruck -- alles ab "mark" -- einen
   verschiebbaren Anteil? Dann darf er nicht multipliziert, geteilt,
   geschoben oder verundet werden. Vorher wird gekuerzt, denn
   "(*-BaudTabl)/2" steht so im SDK und IST eine Konstante. Und es zaehlt
   nur der Teilausdruck, nicht der ganze: "\1+\1+\1+\1+256*4" (aus
   MACROS/os9svc.m) hat vier verschiebbare Summanden und trotzdem eine
   erlaubte Multiplikation. */
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
		/* "^" ist bei Microware das UNAERE Nicht, kein XOR: gemessen
		   ergibt "dc.b ^$0f" ein $f0, waehrend "$ff^$0f" mit
		   "illegal expression terminator" abgelehnt wird. Ein "~"
		   kennt r68 gar nicht ("bad operand"). */
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
		/* aktueller Ort im Abschnitt. Ausserhalb eines Abschnitts gibt
		   es ihn nicht -- r68 meldet dort "undefined org". */
		exP = exP + 1;
		if (curSect == SECT_NONE)
			fatal("\"*\" ausserhalb eines Abschnitts", "");
		exSect = curSect;
		termAdd(curSect, -1);
		return curPC;
	}
	if (exP[0] == '.' && !isSymCh(exP[1] & 255)) {
		/* "." ist der org-Zaehler, nicht der Ort im Abschnitt
		   (gemessen: "SIZE equ ." nach do-Direktiven liefert deren
		   Endstand, und zwar auch innerhalb eines psect). */
		exP = exP + 1;
		exSect = SECT_ABS;
		return orgPC;
	}
	if (isDigitCh(exP[0] & 255)) {
		exSect = SECT_ABS;
		/* "0x100" kennt r68 neben "$100" -- so steht es in
		   MWOS/OS9/SRC/IO/SCF/DRVR/sccd2401.a:1151. Nur klein
		   geschrieben: "0X10" lehnt r68 ab. */
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
			/* Im ersten Durchlauf ist eine Vorwaertsreferenz normal.
			   Bleibt sie danach undefiniert, ist es ein externer
			   Name -- den traegt der Aufrufer als Referenz ein. */
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
		    exSect == SECT_UDATA) {
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
			/* Alles, was rechts dazugekommen ist, geht negativ ein. */
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
		if (exP[0] == '!') {
			/* "!" ist bei Microware das bitweise ODER */
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

/* Ausdruck aus einer Zeichenkette auswerten. exSect/exExtern beschreiben
   danach, worauf sich das Ergebnis bezieht. */
static int evalExpr(const char *s)
{
	int v;

	exP = s;
	exSect = SECT_ABS;
	exExtern = -1;
	exOpen = 0;
	termN[2] = 0;
	v = exprTop();
	/* Der Ausdruck muss GANZ aufgebraucht sein. Ohne diese Pruefung
	   liefert ein unbekanntes Zahlenformat still einen falschen Wert --
	   "0x100" ergab, bevor es unterstuetzt war, klaglos 0. */
	exSkip();
	if (exP[0] != 0)
		fatal("Rest im Ausdruck nicht auswertbar: ", exP);
	termFold();
	/* exSect/exExtern beschreiben den EINEN Anteil, wenn es genau einen
	   gibt -- daran haengen die Pruefungen fuer Spruenge und
	   PC-relative Formen. */
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

/* Traegt je verschiebbarem Anteil des Ausdrucks EINE Referenz ein, alle auf
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
		lnOpRaw[n] = lxTmp[i];
		lnOp[n] = lowerCh(lxTmp[i] & 255);
		n++;
		i++;
	}
	lnOp[n] = 0;
	lnOpRaw[n] = 0;

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
	if (curSect == SECT_UDATA) {
		/* Im reservierten Bereich wird nichts abgelegt, nur gezaehlt
		   (gemessen: "u1 ds.b 1 / u2 ds.w 1" ergibt u2 = 2). */
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
/* "do.b/.w/.l [anzahl]" -- legt einen Namen auf den org-Zaehler und schiebt
   ihn weiter. Liefert die Adresse, die das Label der Zeile bekommt. */
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
	if (curSect == SECT_UDATA) {
		/* Dort wird nichts abgelegt, nur gezaehlt. */
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

/* Uebernimmt die Anteile des zuletzt ausgewerteten Ausdrucks in das Fach
   eines Operanden -- der zweite Operand wuerde sie sonst ueberschreiben. */
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
		if (blen >= 2 && lowerCh(s[lp + 1] & 255) == 'p' &&
		    lowerCh(s[lp + 2] & 255) == 'c') {
			if (blen == 2)
				isPc = 1;
			/* "pcr" ist Microwares zweite Schreibweise dafuer und
			   verhaelt sich genauso -- gemessen: "ziel(pc)" und
			   "ziel(pcr)" ergeben beide den Abstand vom
			   Erweiterungswort. */
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
		/* Ein verschiebbares Displacement ist normal: so greift die
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
/* An r68 gemessen, denn geraten haette man es anders:
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

/* Symbole von der Kommandozeile (-a). Sie werden zu Beginn JEDES Durchlaufs
   gesetzt, damit "ifdef" sie sieht. */
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
	/* Das schliessende Zeichen wird nur weggenommen, wenn es da ist:
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
		/* Die Anfuehrungsform sucht im Verzeichnis der
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

/* ================================================================ Makros = */
/* An r68 gemessen (Option -x zeigt die Ausdehnung im Listing):
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

static char macText[131072];
static int MACTEXT_MAX = 131072;
static int macTop;

static int macDefining;        /* 1 = Zeilen wandern in den Rumpf */
static int macCounter;         /* fuer \@ */
static int repActive;          /* 1 = Rumpf einer rept sammeln */
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

/* Haengt die ROHE Zeile an den Rumpf des zuletzt begonnenen Makros. */
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

/* Schreibt den Rumpf [from..to) rueckwaerts in den Ausdehnungsspeicher und
   ersetzt dabei die Platzhalter. Rueckwaerts, weil der Speicher von oben
   nach unten waechst -- das Ergebnis steht danach vorwaerts richtig. */
static void macSubstitute(int from, int to, const char *argp[], int argN,
			  int serial)
{
	int i;
	int k;
	int d;
	const char *a;

	i = to;
	while (i > from) {
		i--;
		/* "\Ln" -- die LAENGE des Arguments n, zweistellig dezimal
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
			/* Der Platzhalter besteht aus zwei Zeichen; er wird
			   hier von hinten gesehen. */
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
static const char *macArgP[9];

/* Zerlegt das Operandenfeld des Aufrufs in bis zu neun Argumente.
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
	return argN;
}

/* Dehnt den Makrorumpf in den Ausdehnungsspeicher aus und liest ab dann von
   dort -- ueber denselben Stapel wie "use". */
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

/* ==================================================== bedingt uebersetzen */
/* ifeq/ifne/ifgt/ifge/iflt/ifle <ausdruck>, ifdef/ifndef <name>, else, endc.
   Der Ausdruck wird gegen NULL geprueft: "ifeq NULL" mit NULL=0 uebersetzt,
   "ifeq DEFINIERT" mit 1 nicht (an r68 gemessen, ebenso die Schachtelung und
   dass ein uebersprungener Block auch Unuebersetzbares enthalten darf).
   Der Operand von "endc" ist bei Microware ueblicherweise ein Kommentar --
   er wird nicht angesehen. */
static int COND_MAX = 32;
static int condActive[32];     /* 1 = dieser Zweig wird uebersetzt */
static int condAny[32];        /* 1 = ein Zweig war schon wahr */
static int condN;
static int condSkipN;          /* Zahl der Ebenen, die gerade ueberspringen */

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
			/* r68 uebergeht ein "endc" zu viel stillschweigend --
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

	/* Ein if innerhalb eines uebersprungenen Blocks wird nur gezaehlt --
	   sein Ausdruck darf unauswertbar sein. */
	if (condSkipN > 0) {
		condPush(0);
		condAny[condN - 1] = 1;
		return;
	}

	if (baseIs(base, "ifdef") || baseIs(base, "ifndef")) {
		/* "definiert" heisst: in DIESEM Durchlauf schon definiert.
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

/* Umfang des Sofortwertes bei den I-Formen (addi/subi/andi/ori/eori/cmpi):
   ein Byte-Sofortwert wird dort als ganzes WORT abgelegt, mit Vorzeichen und
   mit einer Wortreferenz -- gemessen an "cmpi.b #-1,d0" ($ffff) gegen
   "move.b #-1,d0" ($00ff). */
static int immBytes(int c)
{
	if (c == 'b')
		return 2;
	return sizeBytes(c);
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

/* Fuer Befehle OHNE Operanden ist das dritte Feld der Zeile schon der
   Kommentar -- im Korpus steht reichlich "rte   * Kommentar" ohne
   Semikolon davor. Es wird deshalb nicht geprueft, sondern verworfen.
   (Sonst waere ein Kommentar, der mit "*" beginnt, ein Operand: genau das
   ist der aktuelle Ort.) */
static void dropOps(void)
{
	oN = 0;
	opTxt0[0] = 0;
	opTxt1[0] = 0;
}

/* Sonderregister, die als Operandentext auftreten und KEIN Ausdruck sind:
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

/* Kontrollregister fuer movec, mit den gemessenen Kennungen (movec d0,vbr
   ergibt $4E7B $0801, movec a0,usp ergibt $8800): sfc 0, dfc 1, cacr 2,
   usp $800, vbr $801, caar $802, msp $803, isp $804. -1 = unbekannt. */
static int controlReg(const char *s)
{
	if (baseIs(s, "sfc"))
		return 0x000;
	if (baseIs(s, "dfc"))
		return 0x001;
	if (baseIs(s, "cacr"))
		return 0x002;
	if (baseIs(s, "usp"))
		return 0x800;
	if (baseIs(s, "vbr"))
		return 0x801;
	if (baseIs(s, "caar"))
		return 0x802;
	if (baseIs(s, "msp"))
		return 0x803;
	if (baseIs(s, "isp"))
		return 0x804;
	return -1;
}

/* Registerliste "d0-d7/a0-a6" -> Maske in der NORMALEN Ordnung: Bit 0 = d0
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

/* Bei -(An) legt movem die Maske UMGEKEHRT ab: Bit 0 = a7 ... Bit 15 = d0.
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

	/* Mit -b waehlt r68 die Weite SELBST und uebergeht dabei einen
	   angegebenen Buchstaben: "bra.w" auf ein nahes Ziel wird kurz,
	   "beq.s" auf ein fernes wird zur Wortform (beides gemessen). Ein
	   Ziel ausserhalb des Moduls bleibt die Wortform.
	   Abstand 0 -- das Ziel ist die naechste Anweisung -- laesst r68 den
	   Befehl GANZ WEG. Das ist bei bra/Bcc gleichbedeutend, bei bsr aber
	   nicht (die Ruecksprungadresse fehlt dann); dort bricht qr68 lieber
	   ab, statt eine Bedeutungsaenderung nachzubauen. */
	if (optBranch) {
		if (exOpen && ext < 0) {
			/* Erster Durchlauf, das Ziel ist noch unbekannt: hier
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
				int r;

				macEnd[macN - 1] = macTop;
				macDefining = 0;
				repActive = 0;
				for (r = 0; r < repCount; r++)
					macExpand(macN - 1);
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
		if (curSect == SECT_IDATA || curSect == SECT_UDATA) {
			if (baseIs(base, "ds")) {
				curSect = SECT_UDATA;
				curPC = udataPC;
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
			curSect = SECT_IDATA;
			curPC = idataPC;
			continue;
		}
		if (baseIs(base, "ends") || baseIs(base, "endsect")) {
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
		if (argEq(a, "-b")) {
			optBranch = 1;
			continue;
		}
		if (argEq(a, "-qb") || argEq(a, "-bq")) {
			optBranch = 1;
			continue;
		}
		if (argEq(a, "-q")) {
			/* r68 unterdrueckt damit Warnungen; qr68 gibt ohnehin
			   nur Fehler aus. Angenommen, damit die Aufrufe der
			   SDK-Makefiles unveraendert laufen. */
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
