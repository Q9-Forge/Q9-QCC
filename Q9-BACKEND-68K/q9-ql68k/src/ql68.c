/*
 * ql68 -- Q9 ROF linker for OS-9/68k.
 *
 * Usage: ql68 [options] <input.r> -O=<module>
 *
 * Reads ROF objects and produces a loadable OS-9/68k module.
 *
 * Edition history:
 *   2026-09-11  Introduced the English source-header format.
 *
 * Everything here is measured against l68 rather than inferred from prose.
 * The documentation defines the structure; byte-level behavior comes from
 * comparison tests. Measured details are recorded in the README.
 *
 * Written in the same subset as qr68 and qcpp so ql68 can later self-host:
 * no unions, no "->", no floating point, literal array sizes, fixed tables
 * instead of malloc, no adjacent string literals and no two-stage indexing
 * of pointer arrays (QCC does not support the latter two forms).
 */

extern int printf(const char *fmt, ...);
extern int puts(const char *s);
extern void exit(int code);
extern char *fopen(const char *path, const char *mode);
extern int fclose(char *fp);
extern int fread(char *buf, int size, int n, char *fp);
extern int fwrite(const char *buf, int size, int n, char *fp);

/* Inputs and libraries share inBuf; qcpp.r alone is already 4.4 MiB because
   QCC places zero-initialized fields in the initialized data area and qcpp
   uses fixed global tables. Host memory is inexpensive; target-sized limits
   are selected below for _Q9OS. */
/* Keep sizes as literals rather than expressions: QCC's constSize accepts
   only numbers in this context. Host builds can be generous, while target
   builds must keep zero-initialized global buffers small because they become
   part of the module's initialized data area. */
#ifdef _Q9OS
#define QL_IN       524288            /* Inputs and libraries. */
#define QL_OUT      524288            /* Output buffer. */
#else
#define QL_IN     33554432            /* Inputs and libraries. */
#define QL_OUT    33554432            /* Output buffer. */
#endif
#define QL_IREF   16384               /* Zeiger je Liste */

static char inBuf[QL_IN];
static int inLen;
static char outBuf[QL_OUT];
static int outLen;

/* ------------------------------------------------------------------ ROF */
/* One entry per psect. The first is the root psect; only it has a non-zero
   type/language value and only it supplies the module header. */
#define QL_ROF    256

static int rofN;
static int rofRoot;          /* Root psect index. */
static int rTyLan[QL_ROF];
static int rAttRev[QL_ROF];
static int rEdition[QL_ROF];
static int rStat[QL_ROF];    /* uninitialisierte Daten (ds im vsect) */
static int rIDat[QL_ROF];    /* initialisierte Daten (dc im vsect)   */
static int rRem[QL_ROF];     /* reservierte FERNdaten (ds im vsect remote) */
static int rCod[QL_ROF];
static int rStk[QL_ROF];
static int rEntry[QL_ROF];
static int rTrap[QL_ROF];
static int rNameAt[QL_ROF];  /* Offsets IN inBuf */
static int rCodeAt[QL_ROF];
static int rIDataAt[QL_ROF];
static int rGlobAt[QL_ROF];
static int rGlobN[QL_ROF];
static int rExtAt[QL_ROF];
static int rExtN[QL_ROF];
static int rLocalAt[QL_ROF];
static int rLocalN[QL_ROF];

/* Psect locations after layout in the module and data areas. */
static int bCode[QL_ROF];    /* Code module offset. */
static int bUninit[QL_ROF];  /* ds-data offset. */
static int bInit[QL_ROF];    /* dc-data offset. */
static int bRemote[QL_ROF];  /* Remote-data offset. */
static int bIDataMod[QL_ROF];/* dc-data module offset. */

/* Der Vorspann auf den Datenzeiger. Bei einem Programm (mod_exec) zeigt
   a6 NICHT auf den Anfang des Datenbereichs, sondern $8000 dahinter --
   so reicht ein 16-Bit-Displacement +-32K weit. Ein TREIBER bekommt
   seinen statischen Speicher dagegen direkt (in a2) und kennt keinen
   Vorspann. Gemessen: "move.l zeiger(a6),d1" mit zeiger auf $000c ergibt
   im Programm $800c, "move.w d2,$001c(a2)" im Treiber sc172 dagegen
   $001c. */
static int dataBias;         /* Data-pointer bias for program modules. */

/* -r=<base>: raw binary output instead of a module. -1 disables it. */
static int optRaw = -1;
/* In der rohen Ausgabe bekommt ein Codebezug IM CODE die Basis
   aufaddiert, ein Zeiger IN DEN DATEN dagegen nicht -- den setzt erst der
   Startcode ueber die Zeigerliste. Gemessen an einem Label auf Codeoffset
   6: im Code wird daraus $1006, in den Daten bleibt es $0006. */
static int rawCodeBias;

static char modName[256];
static int optOwner = 65536;       /* M$Owner $00010000, ohne -gu= (gemessen) */
static int optAccess = 1365;       /* M$Accs  $0555,     ohne -p=  (gemessen) */
static int optEdition = -1;        /* -e=: ueberschreibt den psect-Wert */
/* -M=<n>[K]: stack-size increment. The number is always in KiB, so -M=1
   and -M=1K both add 1024, while -M=100 adds 102400 (measured). */
static int optStackAdd;
/* -b=<n>: align code and data starts to n (n = 2, 4, 8, 16). Measured:
   every code section is aligned and padded with zero bytes, but pointer
   lists are not. */
static int optAlign = 1;
/* -x=<n>: align only the beginning of the code area. Measured with an entry
   at code offset 4: -x=8 starts code at $50 while the entry remains at $54.
   Only the first psect's code area is aligned. */
static int optXAlign = 1;
/* -S: keep the module in memory. Measured effect: add bit $4000 to the
   attribute word ($8000 -> $c000). */
static int optSticky = 0;
/* -R=<n>: revision number in the low byte of the same word. */
static int optRevision = -1;

/* Pointer lists for the IRefs section. */
static int irefCode[QL_IREF];
static int irefCodeN;
static int irefData[QL_IREF];
static int irefDataN;

/* Symboltabelle. Der Bedarf ist GEMESSEN, nicht geschaetzt: QCCs eigener
   Parser (stage2.r aus qcc_backend -os9 -largedata) bringt 14.193
   Globale mit 325.828 Byte Namenstext -- QCC macht aus jeder Sprungmarke
   ein Globalsymbol. Dazu q9_cstart.r (55) und qclib.l (433), zusammen
   14.681 Namen und 334.019 Byte. Die alten 8.192/262.144 reichten fuer
   das SDK (Assembler, kleine Module), aber nicht fuer die eigene Kette.

   Am ZIEL bleibt es bei den alten Massen: dort begrenzt schon QL_IN das
   Ganze auf 512-KB-Eingaben, und jedes zusaetzliche Feld waechst 1:1 ins
   Modul (QCCs Backend legt genullte Felder in den INITIALISIERTEN
   Datenbereich). Ein grosszuegiges QL_SYM waere dort nur Ballast. */
#ifdef _Q9OS
#define QL_SYM    8192
#define QL_POOL   262144
#else
#define QL_SYM    32768
#define QL_POOL  1048576
#endif
#define QL_LIB    16
#define QL_ARGS   1024              /* Argumente nach dem Aufloesen von -z= */
#define QL_ZBUF    65536            /* Text der -z=-Dateien */
#define QL_JT     1024              /* Eintraege der Sprungtabelle */

/* --- Die Sprungtabelle von -a ------------------------------------------
   Ein `bsr.w ziel` reicht nur +-32K weit. Liegt das Ziel weiter, legt l68
   mit -a einen 6 Byte langen Eintrag `jmp $xxxxxxxx` ($4ef9) in den
   INITIALISIERTEN DATEN an und macht aus dem Aufruf `jsr d16(a6)`
   ($4eae); das Displacement ist der Datenabstand des Eintrags MIT dem
   $8000-Bias. Alles an `l68 -a -j` nachgemessen, das seine Rechnung
   selbst druckt.

   ql68 legt nur die WIRKLICH gebrauchten Eintraege an. l68 dagegen
   schaetzt: `-j` meldet etwa "guess=11 Actual=7", und die vier
   ueberzaehligen bleiben als `4ef9 00000000` stehen und vergroessern den
   Datenbereich. Der Grund ist strukturell -- l68 muss die Tabellengroesse
   festlegen, BEVOR die Bibliothekssuche entschieden hat, welche Module
   dazukommen und wo sie liegen. Gemessen: bei gewoehnlichen ROF-Eingaben
   rechnet l68 exakt (guess == Actual bei 1, 2, 3 und 5 fernen Aufrufen),
   nur mit Bibliotheken schaetzt es hoch (dort blieb guess=6 stehen,
   gleich ob 1, 2 oder 4 Aufrufe -- die Schaetzung reagiert gar nicht auf
   ihre Zahl). ql68 kennt beim Layout alle Eingaben und Bibliotheken und
   braucht deshalb nicht zu schaetzen. Byteidentitaet bleibt damit ueberall
   pruefbar, wo l68 selbst exakt ist; bei Bibliotheksfaellen weicht ql68
   bewusst ab -- die Tabelle ist dann kleiner, das Modul aber richtig.

   Die Tabelle verschiebt den CODE nicht (sie liegt in den Daten), deshalb
   genuegen zwei Durchlaeufe: einer zaehlt, einer schreibt. */
static int optJumpTab;         /* -a */
static int jtPlan;             /* 1 = Zaehllauf */
static int jtSym[QL_JT];       /* je SYMBOL ein Eintrag, nicht je Aufruf */
static int jtN;
static int jtBase;             /* Datenabstand der Tabelle */
static int jtAt;               /* Dateioffset der Tabelle */

static int jtFind(int si)
{
	int i;

	for (i = 0; i < jtN; i++) {
		if (jtSym[i] == si)
			return i;
	}
	return -1;
}

/* Symbol table for resolving external references: library globals and
   symbols defined by the linker itself. */
static char symPool[QL_POOL];
static int symPoolTop;
static int symName[QL_SYM];    /* Index in symPool */
static int symValue[QL_SYM];
static int symType[QL_SYM];    /* Typwort wie im ROF: 6 = equ, 4 = Code ... */
static int symN;

#define QL_LIBROF 2048          /* Module in allen Bibliotheken zusammen */
static const char *libPath[QL_LIB];
static int libN;

static void fatal(const char *msg, const char *detail)
{
	printf("ql68: %s%s\n", msg, detail);
	exit(1);
}

static int strLen(const char *s)
{
	int n;

	n = 0;
	while (s[n] != 0)
		n++;
	return n;
}

/* --------------------------------------------------------------- Lesen */
static int be16(int at)
{
	return ((inBuf[at] & 255) << 8) | (inBuf[at + 1] & 255);
}

static int be32(int at)
{
	return ((inBuf[at] & 255) << 24) | ((inBuf[at + 1] & 255) << 16) |
	       ((inBuf[at + 2] & 255) << 8) | (inBuf[at + 3] & 255);
}

/* Ueberspringt einen nullterminierten Namen und liefert den Offset
   dahinter. */
static int skipName(int at)
{
	while (at < inLen && inBuf[at] != 0)
		at++;
	return at + 1;
}

static int strEq(const char *a, const char *b)
{
	int i;

	i = 0;
	while (a[i] != 0 && a[i] == b[i])
		i++;
	return a[i] == b[i];
}

static void symAdd(const char *name, int value, int type)
{
	int n;
	int i;

	if (symN >= QL_SYM)
		fatal("zu viele Symbole (QL_SYM)", "");
	n = strLen(name);
	if (symPoolTop + n + 1 >= QL_POOL)
		fatal("Symbolnamen zu lang (QL_POOL)", "");
	symName[symN] = symPoolTop;
	for (i = 0; i <= n; i++)
		symPool[symPoolTop + i] = name[i];
	symPoolTop = symPoolTop + n + 1;
	symValue[symN] = value;
	symType[symN] = type;
	symN++;
}

/* -1 = unbekannt. Die zuletzt eingetragene Definition gewinnt, damit die
   vom Binder gesetzten Symbole eine gleichnamige aus der Bibliothek
   verdecken. */
static int symFind(const char *name)
{
	int i;

	for (i = symN - 1; i >= 0; i--) {
		if (strEq(&symPool[symName[i]], name))
			return i;
	}
	return -1;
}

/* The ROF header is 56 bytes and uses 32-bit counts, which is edition 9.1.
   Edition 9.0 used 16-bit counts and is not compatible with this parser. */
static int rofParse(int at0)
{
	int at;
	int i;
	int k;

	if (rofN >= QL_ROF)
		fatal("zu viele ROFs (QL_ROF)", "");
	k = rofN;
	rofN++;

	if (at0 + 56 > inLen)
		fatal("Eingabe ist zu kurz fuer einen ROF-Kopf", "");
	if ((inBuf[at0] & 255) != 0xDE || (inBuf[at0 + 1] & 255) != 0xAD ||
	    (inBuf[at0 + 2] & 255) != 0xFA || (inBuf[at0 + 3] & 255) != 0xCE)
		fatal("keine ROF-Datei (Sync ist nicht $DEADFACE)", "");

	rTyLan[k] = be16(at0 + 4);
	rAttRev[k] = be16(at0 + 6);
	if (be16(at0 + 8) != 0)
		fatal("der ROF ist als fehlerhaft gekennzeichnet", "");
	rEdition[k] = be16(at0 + 18);
	rStat[k] = be32(at0 + 20);
	rIDat[k] = be32(at0 + 24);
	rCod[k] = be32(at0 + 28);
	rStk[k] = be32(at0 + 32);
	rEntry[k] = be32(at0 + 36);
	rTrap[k] = be32(at0 + 40);
	/* remotestatsiz: reserved remote data. It does not count against the
	   64 KiB limit and follows initialized data in the data area. The
	   remoteidatsiz field at offset 48 remains unsupported because qr68 does
	   not generate it. */
	rRem[k] = be32(at0 + 44);
	if (be32(at0 + 48) != 0)
		fatal("remote INITIALISIERTE Daten sind noch nicht gemessen", "");
	if (be32(at0 + 52) != 0)
		fatal("Debuginformationen sind noch nicht gemessen", "");

	at = at0 + 56;
	rNameAt[k] = at;
	at = skipName(at);

	rGlobN[k] = be32(at);
	rGlobAt[k] = at + 4;
	at = rGlobAt[k];
	for (i = 0; i < rGlobN[k]; i++) {
		at = skipName(at);
		at = at + 6;               /* Typwort und Wert */
	}

	rCodeAt[k] = at;
	at = at + rCod[k];
	rIDataAt[k] = at;
	at = at + rIDat[k];

	rExtN[k] = be32(at);
	rExtAt[k] = at + 4;
	at = rExtAt[k];
	for (i = 0; i < rExtN[k]; i++) {
		at = skipName(at);
		at = at + 4 + be32(at) * 6;
	}

	rLocalN[k] = be32(at);
	rLocalAt[k] = at + 4;
	at = rLocalAt[k] + rLocalN[k] * 6;

	/* Four trailing longwords; all observed objects contain zero here. They
	   are part of the object length and therefore matter when scanning libs. */
	return at + 16;
}

/* A library is simply a sequence of ROFs, not a special format. The length
   of one ROF is 56 + name + globals + code + initialized data + external
   references + local references + four trailing longwords. */
/* Sizes read while indexing a library. */
static int libRofAt[QL_LIBROF];   /* Offset des ROF in inBuf */
static int libRofN;

/* Skip the ROF at at and return the following offset. */
static int rofSkip(int at)
{
	int n;
	int i;
	int cods;
	int idat;

	idat = be32(at + 24);
	cods = be32(at + 28);
	at = skipName(at + 56);
	n = be32(at);
	at = at + 4;
	for (i = 0; i < n; i++) {
		at = skipName(at);
		at = at + 6;
	}
	at = at + cods + idat;
	n = be32(at);
	at = at + 4;
	for (i = 0; i < n; i++) {
		at = skipName(at);
		at = at + 4 + be32(at) * 6;
	}
	n = be32(at);
	return at + 4 + n * 6 + 16;
}

/* Is a name referenced by an included ROF but not defined there? In that
   case the linker must fetch the defining module from a library. */
static int nameIsOpen(const char *name)
{
	int k;
	int i;
	int at;
	int gebraucht;

	gebraucht = 0;
	for (k = 0; k < rofN && !gebraucht; k++) {
		at = rExtAt[k];
		for (i = 0; i < rExtN[k]; i++) {
			if (strEq(&inBuf[at], name)) {
				gebraucht = 1;
				break;
			}
			at = skipName(at);
			at = at + 4 + be32(at) * 6;
		}
	}
	if (!gebraucht)
		return 0;
	for (k = 0; k < rofN; k++) {
		at = rGlobAt[k];
		for (i = 0; i < rGlobN[k]; i++) {
			if (strEq(&inBuf[at], name))
				return 0;
			at = skipName(at);
			at = at + 6;
		}
	}
	return 1;
}

/* Does the ROF at at define this name? Ignore equ symbols: they are already
   in the symbol table and must not pull a constant-only psect into the
   module. */
static int rofDefines(int at, const char *name)
{
	int n;
	int i;
	int t;

	at = skipName(at + 56);
	n = be32(at);
	at = at + 4;
	for (i = 0; i < n; i++) {
		int nameAt;

		nameAt = at;
		at = skipName(at);
		t = be16(at);
		at = at + 6;
		if (t != 6 && strEq(&inBuf[nameAt], name))
			return 1;
	}
	return 0;
}

/* Read and index a library. Linking happens later in libLink(), and only
   required modules are included. Keep the library in the same buffer as
   inputs so rofParse() can parse it without conversion. Register equ symbols
   immediately because constant-only libraries still provide needed values. */
static void libScan(const char *path)
{
	char *fp;
	int at;
	int n;
	int i;
	int got;

	fp = fopen(path, "rb");
	if (fp == 0)
		fatal("Bibliothek nicht lesbar: ", path);
	at = inLen;
	got = fread(&inBuf[inLen], 1, QL_IN - inLen, fp);
	fclose(fp);
	if (got <= 0) {
		/* Distinguish an actually empty file from a full input buffer where
		   fread cannot read any additional bytes. */
		if (inLen >= QL_IN)
			fatal("Eingabepuffer voll (QL_IN): ", path);
		fatal("Bibliothek ist leer: ", path);
	}
	inLen = inLen + got;

	while (at + 56 <= inLen) {
		if ((inBuf[at] & 255) != 0xDE || (inBuf[at + 1] & 255) != 0xAD ||
		    (inBuf[at + 2] & 255) != 0xFA || (inBuf[at + 3] & 255) != 0xCE)
			fatal("in der Bibliothek steht kein ROF-Sync: ", path);
		if (libRofN >= QL_LIBROF)
			fatal("zu viele Module in den Bibliotheken (QL_LIBROF)", "");
		libRofAt[libRofN] = at;
		libRofN++;

		n = be32(skipName(at + 56));
		i = skipName(at + 56) + 4;
		while (n > 0) {
			int nameAt;
			int t;
			int v;

			nameAt = i;
			i = skipName(i);
			t = be16(i);
			v = be32(i + 2);
			i = i + 6;
			if (t == 6)
				symAdd(&inBuf[nameAt], v, t);
			n--;
		}
		at = rofSkip(at);
	}
}

/* Die Bibliothekssuche -- BEDARFSGESTEUERT, nicht in
   Bibliotheksreihenfolge. An l68 nachgemessen: bei einer Bibliothek in
   der Reihenfolge (qprintf, printf_p, qiob, qos9) bindet l68
   qiob, qos9, qprintf, printf_p ein. Es arbeitet also die offenen
   Referenzen der Reihe nach ab und holt zu jeder das Modul, das sie
   definiert: _initarg (aus dem Startcode) holt qiob, dessen _os_exit
   holt qos9, dann printf aus dem Programm qprintf, dessen tc_printf_a
   printf_p.
   Wer in Bibliotheksreihenfolge einbindet, bekommt dieselbe Groesse,
   aber andere Adressen -- und damit ein anderes Modul.
   Die Ordnungsregel des Handbuchs ("the order in which the psects appear
   in a simple library file is important") bleibt davon unberuehrt: sie
   sagt, welche Module GEFUNDEN werden, nicht in welcher Folge sie
   eingebunden werden. */
static void libLink(void)
{
	int fortschritt;
	int k;
	int i;
	int j;
	int at;

	fortschritt = 1;
	while (fortschritt) {
		fortschritt = 0;
		/* Find the first unresolved name in included-ROF order and then in
		   reference order within each ROF. */
		for (k = 0; k < rofN && !fortschritt; k++) {
			at = rExtAt[k];
			for (i = 0; i < rExtN[k]; i++) {
				int nameAt;

				nameAt = at;
				at = skipName(at);
				at = at + 4 + be32(at) * 6;
				if (!nameIsOpen(&inBuf[nameAt]))
					continue;
				for (j = 0; j < libRofN; j++) {
					if (rofDefines(libRofAt[j], &inBuf[nameAt])) {
						rofParse(libRofAt[j]);
						fortschritt = 1;
						break;
					}
				}
				if (fortschritt)
					break;
			}
		}
	}
}

/* -------------------------------------------------------------- Ausgabe */
static void put8(int v)
{
	if (outLen >= QL_OUT)
		fatal("Ausgabepuffer voll (QL_OUT)", "");
	outBuf[outLen] = v & 255;
	outLen++;
}

static void put16(int v)
{
	put8(v >> 8);
	put8(v);
}

/* Pad with zero bytes up to the next alignment boundary. */
static void alignTo(int n)
{
	while ((outLen % n) != 0)
		put8(0);
}

/* Round up to the next multiple of n. */
static int alignUp(int v, int n)
{
	while ((v % n) != 0)
		v++;
	return v;
}

static void put32(int v)
{
	put8(v >> 24);
	put8(v >> 16);
	put8(v >> 8);
	put8(v);
}

static void patch32(int at, int v)
{
	outBuf[at] = (v >> 24) & 255;
	outBuf[at + 1] = (v >> 16) & 255;
	outBuf[at + 2] = (v >> 8) & 255;
	outBuf[at + 3] = v & 255;
}

/* Header parity: one's complement of the XOR of all header words from $00
   through $2d. Measured and verified against l68 output. */
static int headerParity(void)
{
	int p;
	int o;

	p = 0;
	for (o = 0; o < 0x2E; o = o + 2)
		p = p ^ (((outBuf[o] & 255) << 8) | (outBuf[o + 1] & 255));
	return (~p) & 0xFFFF;
}

/* 24-bit CRC with polynomial $800063 and initial value $FFFFFF. Store the
   complement of the result over the module excluding the three CRC bytes.
   Verifying the complete module including CRC yields the expected residue
   $800FE3. */
static int moduleCrc(int len)
{
	int crc;
	int i;
	int k;

	crc = 0xFFFFFF;
	for (i = 0; i < len; i++) {
		crc = crc ^ ((outBuf[i] & 255) << 16);
		for (k = 0; k < 8; k++) {
			if (crc & 0x800000)
				crc = ((crc << 1) ^ 0x800063) & 0xFFFFFF;
			else
				crc = (crc << 1) & 0xFFFFFF;
		}
	}
	return crc ^ 0xFFFFFF;
}

/* Eine Zeigerliste des IRefs-Abschnitts ausgeben: Gruppen aus
   <msw><Anzahl><lsw...>, beendet durch eine Gruppe der Anzahl 0.

   Die Reihenfolge ist NICHT sortiert, sondern umgekehrte
   Begegnungsreihenfolge: l68 stellt jeden neuen Eintrag VORNE an. Der ROF
   liefert seine lokalen Referenzen absteigend nach Offset, und bei einem
   einzelnen psect sieht das Ergebnis deshalb aufsteigend aus -- ein
   Trugschluss, den erst der zweite psect aufdeckt. Gemessen: zwei psects
   mit je einem Codezeiger auf Datenoffset $10 (Wurzel) und $18 (zweiter)
   ergeben die Liste $18, $10. Die Eintraege werden hier beim Sammeln
   vorangestellt, die Liste steht also schon richtig. */
static void putIrefList(int *offs, int n)
{
	int i;
	int j;
	int msw;

	i = 0;
	while (i < n) {
		msw = (offs[i] >> 16) & 0xFFFF;
		j = i;
		while (j < n && ((offs[j] >> 16) & 0xFFFF) == msw)
			j++;
		put16(msw);
		put16(j - i);
		while (i < j) {
			put16(offs[i] & 0xFFFF);
			i++;
		}
	}
	put16(0);
	put16(0);
}

/* Prepend an entry; see putIrefList(). */
static void irefAdd(int *offs, int *n, int value)
{
	int i;

	if (*n >= QL_IREF)
		fatal("zu viele Zeiger (QL_IREF)", "");
	for (i = *n; i > 0; i--)
		offs[i] = offs[i - 1];
	offs[0] = value;
	*n = *n + 1;
}

/* Resolve local references of one psect. Add the target-section base to the
   value at each reference site, and add each data longword to the appropriate
   pointer list so the loader can adjust it again at load time.

   Typwort wie bei qr68 (dort vollstaendig dokumentiert):
     Bit 5     die Referenz LIEGT im Code (sonst in den Daten)
     Bit 3..4  Umfang: 01 = 1, 10 = 2, 11 = 4 Byte
     Bit 2     das ZIEL ist Code; Bit 0..1 sonst der Datenabschnitt
     Bit 6/7   abziehen / relativ */
/* Do not guard intermediate field width; this behavior is measured.
   Ein Ausdruck wie "move.b PD_PAR-PD_OPT+M$DTyp(a1),d0" (so woertlich in
   den SCF-Treibern) erzeugt DREI Referenzen auf DASSELBE
   Byte-Displacement. ql68 verrechnet sie nacheinander und kappt dabei
   jedes Mal auf die Feldbreite. Das ist kein Verlust, sondern Rechnen
   modulo 256: (a-b+c) mod 256 kommt richtig heraus, gleich an welcher
   Stelle gekappt wird -- und l68 rechnet genauso, ohne etwas zu melden.
   Ein Waechter auf den ZWISCHENwert schlaegt hier falsch an. Der Versuch
   hat acht zuvor byteidentische SDK-Module zerlegt (sc68990, sc147,
   sc162, sc167, sc172, sc177, sc68360, ram): PD_PAR allein ist $1005c,
   erst der Abzug von PD_OPT macht daraus ein kleines Displacement.
   Der Endwert eines Feldes steht erst fest, wenn ALLE Referenzen darauf
   abgearbeitet sind -- eine Einzelreferenz taugt nicht als Pruefstelle.

   Fuer einen wirklich zu weiten Bezug legt l68 mit -a eine Sprungtabelle
   an (gemessen: aus "bsr sub1" wird "jsr d16(a6)", und in den
   initialisierten Daten steht ein 6 Byte langer Eintrag "jmp $xxxxxxxx",
   dessen Adresse in der Code-Zeigerliste mitgefuehrt wird). Das bleibt
   bewusst nicht nachgebaut: im ganzen SDK-Korpus kommt der Fall nicht
   vor, alle 227 Aufrufe sind ohne Sprungtabelle byteidentisch. */
static void applyLocalRefs(int k)
{
	int i;
	int at;
	int type;
	int offs;
	int size;
	int inCode;
	int toCode;
	int base;
	int here;
	int v;

	for (i = 0; i < rLocalN[k]; i++) {
		at = rLocalAt[k] + i * 6;
		type = be16(at);
		offs = be32(at + 2);
		if (type & 0x0080)
			fatal("relative lokale Referenz ist noch nicht gemessen", "");
		size = (type >> 3) & 3;
		if (size < 1 || size > 3)
			fatal("Referenz ohne gemessenen Umfang", "");
		inCode = (type >> 5) & 1;
		toCode = (type >> 2) & 1;
		if (toCode)
			base = bCode[k];
		else if (type & 1)
			base = bInit[k];       /* Initialized data. */
		else if (type & 2)
			base = bRemote[k];     /* Reserved remote data. */
		else
			base = bUninit[k];     /* Reserved data. */
		/* Bit 6: subtract the value. This produces the
		   Differenz zweier Bezuege in einem Ausdruck -- r68 legt fuer
		   "PD_PAR-PD_OPT+M$DTyp(a1)" drei Referenzen auf denselben
		   Offset ab, eine davon mit diesem Bit. */
		if (type & 0x0040)
			base = -base;
		/* Access to local data uses a6, and a6
		   zeigt NICHT auf den Anfang des Datenbereichs, sondern $8000
		   dahinter -- so reicht ein 16-Bit-Displacement +-32K weit.
		   Gemessen: aus "move.l zeiger(a6),d1" mit zeiger auf
		   Datenoffset $000c macht l68 $800c. Ein 32-Bit-Zeiger IN den
		   Daten bleibt unvorgespannt. */
		if (inCode && !toCode && (size == 2 || size == 3))
			base = base + dataBias;
		if (inCode && toCode)
			base = base + rawCodeBias;
		if (inCode)
			here = bCode[k] + offs;
		else
			here = bIDataMod[k] + offs;
		if (size == 1) {
			v = outBuf[here] & 255;
			outBuf[here] = (v + base) & 255;
		} else if (size == 2) {
			v = ((outBuf[here] & 255) << 8) | (outBuf[here + 1] & 255);
			v = v + base;
			outBuf[here] = (v >> 8) & 255;
			outBuf[here + 1] = v & 255;
		} else {
			v = ((outBuf[here] & 255) << 24) |
			    ((outBuf[here + 1] & 255) << 16) |
			    ((outBuf[here + 2] & 255) << 8) |
			    (outBuf[here + 3] & 255);
			patch32(here, v + base);
		}
		/* Only a data longword is a pointer that the loader must adjust again.
		   A shorter field, such as a 16-bit code displacement, is final here. */
		if (!inCode && size == 3 && !(type & 0x0040)) {
			if (toCode)
				irefAdd(irefCode, &irefCodeN, bInit[k] + offs);
			else
				irefAdd(irefData, &irefDataN, bInit[k] + offs);
		}
	}
}

/* Den Wert eines Symbols aufloesen. Absolute (equ-) Symbole gelten wie
   sie sind; Code- und Datensymbole bekommen die Basis ihres psect. */
static int symResolve(int si)
{
	return symValue[si];
}

/* Die externen Referenzen eines psect aufloesen. Der Wert des Symbols
   wird an der Referenzstelle AUFADDIERT -- der Assembler hat dort schon
   den konstanten Anteil des Ausdrucks abgelegt (bei qr68 gemessen).
   Ist Bit 7 gesetzt, ist der Bezug RELATIV zur Referenzstelle: gemessen
   an "jsr sub1(pc)" -- sub1 liegt auf $5a, das Erweiterungswort auf $52,
   abgelegt wird $0008. */
/* Ein Aufruf, der nicht mehr in sein Wort passt. Ohne -a bricht l68 hier
   ab ("The value of symbol 'x' ($...) is too large for a word operand") --
   ql68 tut dasselbe, statt still ein falsches Displacement abzulegen.
   Mit -a bekommt das Symbol einen Tabelleneintrag, und der Aufruf wird
   umgeschrieben. */
/* Die Eintraege fuellen: "jmp $xxxxxxxx" auf das aufgeloeste Symbol.
   Beide Formen von -a teilen sich denselben Eintrag -- der Sprung laeuft
   durch den jmp, der Adresszugriff liest das Feld dahinter (s. farCall). */
static void putJumpTable(void)
{
	int i;

	for (i = 0; i < jtN; i++) {
		outBuf[jtAt + i * 6] = 0x4e;
		outBuf[jtAt + i * 6 + 1] = 0xf9;
		patch32(jtAt + i * 6 + 2, symResolve(jtSym[i]));
		/* Die Zieladresse steht in den DATEN und muss vom Lader
		   angepasst werden -- als Code- oder als Datenzeiger, je
		   nachdem, worauf sie zeigt. */
		if (symType[jtSym[i]] == 4)
			irefAdd(irefCode, &irefCodeN, jtBase + i * 6 + 2);
		else
			irefAdd(irefData, &irefDataN, jtBase + i * 6 + 2);
	}
}

static void farCall(int si, int here, const char *name)
{
	int e;
	int op;
	int disp;
	int reg;

	if (!optJumpTab)
		fatal("Bezug zu weit fuer ein Wort, l68 braucht dafuer -a: ",
		      name);
	e = jtFind(si);
	if (e < 0) {
		if (jtN >= QL_JT)
			fatal("zu viele Sprungtabelleneintraege (QL_JT)", "");
		e = jtN;
		jtSym[jtN] = si;
		jtN++;
	}
	if (jtPlan)
		return;

	/* Two forms, both measured against l68, share the same
	   ROF-Typwort $00b0 (im Code, 2 Byte, relativ). Sie sind also nur am
	   OPCODE zu unterscheiden:

	     bsr.w ziel        $6100  ->  jsr d16(a6)         $4eae
	     lea d16(pc),An    $41fa  ->  movea.l d16(a6),An  $206e | reg<<9

	   Der Tabelleneintrag ist in beiden Faellen derselbe
	   ("jmp $xxxxxxxx"), nur das Displacement zeigt woandershin: der
	   SPRUNG geht auf den Eintragsanfang und laeuft durch den jmp, die
	   ADRESSE dagegen wird aus dem Adressfeld dahinter geladen
	   (Eintrag + 2). Genau dafuer fuehrt l68s "-j"-Karte zwei Spalten --
	   "Indx" den Eintragsanfang, "Roff"/"Data Offset" das Adressfeld.

	   Nachgemessen an qr68 gegen qclib: die sechs Bezuege auf
	   tc_extcall_tmp wurden alle zu "movea.l $8282(a6),a0", waehrend der
	   Eintrag selbst bei $8280 steht. */
	op = ((outBuf[here - 2] & 255) << 8) | (outBuf[here - 1] & 255);
	if (op == 0x6100) {
		outBuf[here - 2] = 0x4E;
		outBuf[here - 1] = 0xAE;
		disp = jtBase + e * 6;
	} else if ((op & 0xF1FF) == 0x41FA) {
		reg = (op >> 9) & 7;
		op = 0x206E | (reg << 9);
		outBuf[here - 2] = (op >> 8) & 255;
		outBuf[here - 1] = op & 255;
		disp = jtBase + e * 6 + 2;
	} else {
		/* The manual mentions only distant BSRs and LEAs for -a. Other
		   forms are unmeasured, so abort instead of guessing. */
		fatal("-a kennt bisher nur bsr.w und lea d16(pc): ", name);
		return;
	}
	disp = disp + 0x8000;             /* Datenabstand mit dem a6-Vorspann */
	outBuf[here] = (disp >> 8) & 255;
	outBuf[here + 1] = disp & 255;
}

static void applyExtRefs(int k)
{
	int i;
	int j;
	int at;
	int nameAt;
	int nrefs;
	int si;
	int type;
	int offs;
	int size;
	int inCode;
	int here;
	int v;
	int val;
	int rel;

	at = rExtAt[k];
	for (i = 0; i < rExtN[k]; i++) {
		nameAt = at;
		at = skipName(at);
		nrefs = be32(at);
		at = at + 4;
		si = symFind(&inBuf[nameAt]);
		if (si < 0)
			fatal("unaufgeloester Name: ", &inBuf[nameAt]);
		for (j = 0; j < nrefs; j++) {
			type = be16(at);
			offs = be32(at + 2);
			at = at + 6;
			rel = (type >> 7) & 1;
			size = (type >> 3) & 3;
			inCode = (type >> 5) & 1;
			if (inCode)
				here = bCode[k] + offs;
			else
				here = bIDataMod[k] + offs;
			val = symResolve(si);
			if (inCode && symType[si] == 4)
				val = val + rawCodeBias;
			else if (inCode && symType[si] != 6 &&
				 (size == 2 || size == 3))
				val = val + dataBias;
			/* Bit 6: subtract (the manual says to add the negative of the
			   symbol's location). */
			if (type & 0x0040)
				val = -val;
			if (rel)
				val = val - here;
			if (size == 1) {
				v = outBuf[here] & 255;
				outBuf[here] = (v + val) & 255;
			} else if (size == 2) {
				v = ((outBuf[here] & 255) << 8) | (outBuf[here + 1] & 255);
				v = v + val;
				/* A PC-relative code reference that no longer fits in the
				   word is the case handled by -a. Check only after all
				   contributions have been added; an intermediate check is wrong. */
				if (rel && inCode && (v < -32768 || v > 32767)) {
					farCall(si, here, &inBuf[nameAt]);
					continue;
				}
				outBuf[here] = (v >> 8) & 255;
				outBuf[here + 1] = v & 255;
			} else if (size == 3) {
				v = ((outBuf[here] & 255) << 24) |
				    ((outBuf[here + 1] & 255) << 16) |
				    ((outBuf[here + 2] & 255) << 8) |
				    (outBuf[here + 3] & 255);
				patch32(here, v + val);
				/* A data longword pointing to code or data needs another loader
				   adjustment; an absolute equ symbol does not. */
				if (!inCode && symType[si] != 6 && !(type & 0x0040)) {
					if (symType[si] == 4)
						irefAdd(irefCode, &irefCodeN, bInit[k] + offs);
					else
						irefAdd(irefData, &irefDataN, bInit[k] + offs);
				}
			} else {
				fatal("externe Referenz ohne gemessenen Umfang: ", &inBuf[nameAt]);
			}
		}
	}
}

/* Register all psect globals with their section base. Type words follow the
   OS-9 format: 0 uninitialized data, 1 initialized data, 4 code and 6 equ.
   Unsupported types are rejected. */
static void addGlobals(int k)
{
	int at;
	int i;
	int nameAt;
	int t;
	int v;

	at = rGlobAt[k];
	for (i = 0; i < rGlobN[k]; i++) {
		nameAt = at;
		at = skipName(at);
		t = be16(at);
		v = be32(at + 2);
		at = at + 6;
		if (t == 4)
			symAdd(&inBuf[nameAt], bCode[k] + v, t);
		else if (t == 1)
			symAdd(&inBuf[nameAt], bInit[k] + v, t);
		else if (t == 0)
			symAdd(&inBuf[nameAt], bUninit[k] + v, t);
		else if (t == 2)
			symAdd(&inBuf[nameAt], bRemote[k] + v, t);
		else if (t == 6)
			symAdd(&inBuf[nameAt], v, t);
		else
			fatal("Globales mit ungemessenem Typwort: ", &inBuf[nameAt]);
	}
}

static void emit(void)
{
	int isDesc;
	int isDrvr;
	int nameAt;
	int idataAt;
	int irefAt;
	int totalUninit;
	int totalInit;
	int totalRemote;
	int i;
	int k;
	int n;
	int crc;

	/* Der Modulaufbau haengt am TYP (s. module.h):
	     Typ 15 (Devic)  mod_dev    -- gar keine feste Erweiterung
	     Typ 14 (Drivr)  mod_driver -- nur _mexec/_mexcpt/_mdata (12 Byte)
	     sonst           mod_exec   -- 24 Byte mit Stack, IData, IRefs
	   Bei Descriptor und Treiber liefert der ROF-CODE den Rest der
	   Erweiterung, und es gibt weder IData- noch IRefs-Abschnitt --
	   die Strukturen haben diese Felder nicht. Der Name steht dort
	   HINTER dem Code, bei mod_exec davor.
	   Gemessen an sc8x30.a (Treiber sc172): _mexec = $3c zeigt auf die
	   Routinentabelle, die die ersten 14 Codebytes sind; _mdata = $114
	   sind die 276 Byte ds; der Name liegt auf $664 = $3c + 1576. */
	/* Wie gross die Kopferweiterung ist, entscheidet die SPRACHE, nicht
	   der Typ -- an den gebundenen SDK-Modulen nachgemessen, indem der
	   Codeanfang aus M$Name minus Codegroesse zurueckgerechnet wurde:

	     Sprache 0  keine Erweiterung, Code auf $30, Name dahinter
	                (Descriptoren "0f00", aber auch das Init-Modul
	                "0c00" -- Sprache 0 heisst "nicht ausfuehrbar",
	                so ein Modul braucht keine Einsprungfelder)
	     sonst      12 Byte _mexec/_mexcpt/_mdata, Code auf $3c,
	                Name dahinter (Treiber "0e01", Systemmodule "0c01",
	                Unterprogramme "0201")
	     Typ 1      mod_exec mit 24 Byte, Name VOR dem Code, dazu
	                IData- und IRefs-Abschnitt

	   Die Typliste, die hier zuerst stand, war eine Fehlverallgemeinerung:
	   Typ 12 kommt in BEIDEN Formen vor, je nach Sprache. */
	i = (rTyLan[rofRoot] >> 8) & 255;
	isDesc = (rTyLan[rofRoot] & 255) == 0;
	isDrvr = !isDesc && i != 1;
	dataBias = 0;
	rawCodeBias = 0;
	if (!isDesc && !isDrvr)
		dataBias = -0x8000;

	/* --- Lay out data: all reserved data first, then all
	   initialisierten Daten -- und zwar psect fuer psect in der
	   Reihenfolge der Kommandozeile (Handbuch Abb. 9-2, an zwei psects
	   nachgemessen: mvar landet auf $0c, svar auf $14). --- */
	/* With -b=<n>, every psect block gets its own boundary, counted in
	   zwar im DATENbereich gezaehlt, nicht im Dateiabstand (gemessen:
	   bei -b=16 stehen die dc-Daten auf Dateiabstand $78, jeder Block
	   ist trotzdem 16 lang). Ohne -b= ist optAlign 1 und alignUp
	   aendert nichts. */
	totalUninit = 0;
	for (k = 0; k < rofN; k++) {
		bUninit[k] = totalUninit;
		totalUninit = totalUninit + alignUp(rStat[k], optAlign);
	}
	totalInit = 0;
	for (k = 0; k < rofN; k++) {
		bInit[k] = totalUninit + totalInit;
		totalInit = totalInit + alignUp(rIDat[k], optAlign);
	}
	/* Die Sprungtabelle liegt am ENDE der initialisierten Daten -- an
	   l68 gemessen: dort endete sie genau auf M$Data. Im Zaehllauf ist
	   jtN noch 0; das macht nichts, weil die Tabelle in den DATEN liegt
	   und die Codelagen nicht verschiebt. Genau deshalb genuegen zwei
	   Durchlaeufe. */
	jtBase = totalUninit + totalInit;
	totalInit = totalInit + jtN * 6;

	/* --- DIE FERNDATEN, hinter allem anderen -------------------------
	   An l68 gemessen (ein psect mit 8000 nicht-remote, 8 Byte
	   initialisiert, 70000 remote):
	     blk  (nicht remote)  -> Datenoffset     0
	     iblk (initialisiert) -> Datenoffset  8000
	     rblk (remote)        -> Datenoffset  8008
	     M$Mem = 78008
	   Die Reihenfolge ist also: nicht-remote uninitialisiert,
	   initialisiert, remote. Der Datenbereich ist derselbe wie ohne
	   remote -- "remote" schaltet nur die 64-KB-Pruefung ab und schiebt
	   die Ferndaten aus dem 16-Bit-Fenster heraus.

	   DIE SPRUNGTABELLE BLEIBT AM ENDE DER INITIALISIERTEN DATEN, auch
	   mit Ferndaten -- ebenfalls gemessen (zwei Eintraege, mit remote bei
	   Datenoffset 4 und 10 direkt hinter 4 Byte initialisierten Daten,
	   ohne remote bei 8004 und 8010). Deshalb steht dieser Block HINTER
	   der Tabellengroesse. Im Zaehllauf ist jtN noch 0 und die Fernbasen
	   sitzen zu tief; das macht nichts, weil nur der zweite Durchlauf
	   schreibt und jtN dann feststeht. */
	totalRemote = 0;
	for (k = 0; k < rofN; k++) {
		bRemote[k] = totalUninit + totalInit + totalRemote;
		totalRemote = totalRemote + alignUp(rRem[k], optAlign);
	}

	/* --- DIE 64-KB-GRENZE DER NICHT-REMOTE-DATEN ------------------------
	   Ein nicht-remoter vsect wird ueber "d16(a6)" angesprochen. Mit dem
	   $8000-Vorspann deckt ein 16-Bit-Displacement genau die Offsets
	   0..65535 ab (0 wird -$8000, 65535 wird $7fff) -- deshalb liegt die
	   Grenze bei 64 KB und nicht bei 32.

	   AN l68 GEMESSEN, nicht abgeleitet:
	     65536 Byte  -> Modul, 65540 Byte -> Abbruch
	     65000 + 400 initialisiert -> Modul
	     65400 + 400 initialisiert -> Abbruch
	   Es zaehlt also die SUMME aus reservierten und initialisierten Daten,
	   und erlaubt ist "kleiner oder gleich 65536". Die Sprungtabelle liegt
	   in den initialisierten Daten und zaehlt mit; deshalb steht die
	   Pruefung HINTER ihrer Groesse. Die Grenze gilt auch bei -r= und ist
	   von -M= unabhaengig (beides an l68 nachgemessen). l68 meldet
	   "**** fatal - non-remote data allocation exceeds 64k bytes".

	   WARUM DAS HIER FEHLTE UND WAS ES ANRICHTETE: ql68 hat den Fall
	   klaglos gebaut -- 102 Byte Modul mit M$Mem = 70000, also ein Kopf,
	   der stimmt, und Code, der seine Daten nicht erreichen kann. Genau
	   diese Luecke hat beim Datenmodell-Versuch (2026-09-06) eine
	   Machbarkeitsprobe gruen aussehen lassen, die mit l68 sofort
	   aufgefallen waere. Eine Machbarkeitsprobe muss durch BEIDE Binder.

	   REMOTE-Daten waeren ausgenommen ("non-remote" bei l68) -- ql68
	   bricht bei ihnen ohnehin schon vorher ab, alles hier ist also
	   nicht-remote. */
	if (totalUninit + totalInit > 65536)
		fatal("mehr als 64 KB nicht-remote Daten -- ein d16(a6) reicht nicht so weit", "");

	/* --- Rohe Binaerausgabe: kein Kopf, kein Name, kein CRC. Der Code
	   liegt ab Dateianfang, dahinter IData und IRefs wie sonst auch.
	   Auch der $8000-Vorspann auf die Daten entfaellt -- den legt in
	   dieser Betriebsart der Startcode selbst an (Handbuch Kap. 9:
	   "the appropriate register must also point to the beginning of a
	   global/static RAM area ... Some processors may require biasing"). */
	if (optRaw >= 0) {
		dataBias = 0;
		rawCodeBias = optRaw;
		for (k = 0; k < rofN; k++) {
			alignTo(optAlign);
			bCode[k] = outLen;
			for (i = 0; i < rCod[k]; i++)
				put8(inBuf[rCodeAt[k] + i]);
		}
		alignTo(optAlign);
		idataAt = outLen;
		put32(totalUninit);
		put32(totalInit);
		for (k = 0; k < rofN; k++) {
			bIDataMod[k] = outLen;
			for (i = 0; i < rIDat[k]; i++)
				put8(inBuf[rIDataAt[k] + i]);
			for (i = rIDat[k]; i < alignUp(rIDat[k], optAlign); i++)
				put8(0);
		}
		for (k = 0; k < rofN; k++)
			addGlobals(k);
		symAdd("bname", 0, 6);
		symAdd("_bname", 0, 6);
		symAdd("btext", 0, 6);
		symAdd("_btext", 0, 6);
		symAdd("etext", idataAt, 6);
		symAdd("_bidata", idataAt, 6);
		/* Typ 0 (Daten), nicht 6 (equ): "end" bezeichnet das Ende des
		   DATENbereichs und bekommt deshalb den a6-Vorspann wie jeder
		   andere Datenbezug -- an l68 gemessen, das aus $250 im Code
		   $ffff8250 macht. Mit Typ 6 blieb der Vorspann aus. */
		/* "end" bezeichnet das Ende des GANZEN Datenbereichs, Ferndaten
		   eingeschlossen -- an l68 gemessen: mit 4 Byte initialisiert und
		   8000 Byte remote steht end auf 8004. */
		symAdd("end", totalUninit + totalInit + totalRemote, 0);
		symAdd("_enddata", totalUninit + totalInit + totalRemote, 0);
		for (k = 0; k < rofN; k++) {
			applyLocalRefs(k);
			applyExtRefs(k);
		}
		irefAt = outLen;
		putIrefList(irefCode, irefCodeN);
		putIrefList(irefData, irefDataN);
		symAdd("edata", irefAt, 6);
		symAdd("_birefs", irefAt, 6);
		/* Ist GENAU EINE der beiden Listen leer, haengt l68 vier
		   Nullbytes an -- an sieben Faellen gemessen (keine, nur
		   Code-, nur Daten-, beide Zeigerarten, je ein bis drei
		   Stueck). Sind beide leer oder beide gefuellt, kommt nichts.
		   Im Modulaufbau gibt es das NICHT; dort wird stattdessen auf
		   gerade Gesamtlaenge aufgefuellt. Herleiten laesst sich das
		   nicht, es ist eine Eigenheit von l68. */
		if ((irefCodeN == 0) != (irefDataN == 0))
			put32(0);
		return;
	}

	/* --- Kopf. Die Groessenfelder werden spaeter nachgetragen. --- */
	put16(0x4AFC);                 /* M$ID    */
	put16(1);                      /* M$SysRev */
	put32(0);                      /* M$Size, spaeter */
	put32(optOwner);               /* M$Owner */
	put32(0);                      /* M$Name, spaeter */
	put16(optAccess);              /* M$Accs  */
	put16(rTyLan[rofRoot]);
	n = rAttRev[rofRoot];
	if (optSticky)
		n = n | 0x4000;
	if (optRevision >= 0)
		n = (n & 0xFF00) | (optRevision & 255);
	put16(n);
	if (optEdition >= 0)
		put16(optEdition);
	else
		put16(rEdition[rofRoot]);
	put32(0);                      /* M$Usage  */
	put32(0);                      /* M$Symbol */
	put16(0);                      /* M$Ident  */
	for (i = 0; i < 6; i++)
		put8(0);                   /* Reserve  */
	put32(0);                      /* M$HdExt  */
	put16(0);                      /* M$HdExtSz */
	put16(0);                      /* M$Parity, spaeter */
	if (isDrvr) {
		put32(0);              /* _mexec, spaeter */
		put32(0);              /* _mexcpt, ggf. spaeter */
		put32(totalUninit + totalInit + totalRemote);   /* _mdata */
	} else if (!isDesc) {
		put32(0);              /* M$Exec, spaeter  */
		/* M$Excpt, spaeter. Fehlt der siebte psect-Parameter, traegt
		   r68 utrap = -1 ein und l68 macht daraus im Modul die 0
		   (gemessen). Ist er gesetzt, gilt dieselbe Rechnung wie fuer
		   M$Exec: Codebasis + utrap. An q9_cstart.a nachgemessen --
		   utrap $180 im ROF, Codebasis $54, M$Excpt $1d4 im Modul. */
		put32(0);
		put32(totalUninit + totalInit + totalRemote);   /* M$Data  */
		put32(rStk[rofRoot] + optStackAdd);      /* M$Stack */
		put32(0);                         /* M$IData, spaeter */
		put32(0);                         /* M$IRefs, spaeter */
	}

	/* Ein GERAETEDESCRIPTOR (Typ 15) hat keine mod_exec-Erweiterung:
	   sein ROF-Code IST die Kopferweiterung und liegt unmittelbar hinter
	   dem gemeinsamen Kopf auf $30; danach folgt nur der Name und der
	   CRC -- weder IData- noch IRefs-Abschnitt. */
	if (isDesc || isDrvr) {
		if (totalInit != 0)
			fatal("initialisierte Daten gibt es bei diesem Modultyp nicht -- l68 braucht dafuer -i", "");
		/* Auch hier duerfen es mehrere psects sein -- die Uhrenmodule
		   des SDK werden aus tickgeneric.r und dem portspezifischen
		   Teil gebunden. Der Code folgt der Reihenfolge der
		   Kommandozeile, wie bei mod_exec. */
		for (k = 0; k < rofN; k++) {
			bCode[k] = outLen;
			bIDataMod[k] = 0;
			for (i = 0; i < rCod[k]; i++)
				put8(inBuf[rCodeAt[k] + i]);
		}
		nameAt = outLen;
		n = strLen(modName);
		for (i = 0; i < n; i++)
			put8(modName[i]);
		put8(0);
		if ((outLen % 2) != 0)
			put8(0);
		for (k = 0; k < rofN; k++)
			addGlobals(k);
		symAdd("bname", nameAt, 6);
		symAdd("_bname", nameAt, 6);
		symAdd("btext", 0, 6);
		symAdd("_btext", 0, 6);
		symAdd("etext", outLen, 6);
		symAdd("_etext", outLen, 6);
		for (k = 0; k < rofN; k++) {
			applyLocalRefs(k);
			applyExtRefs(k);
		}
		if (irefCodeN != 0 || irefDataN != 0)
			fatal("ein Descriptor mit Datenzeigern ist noch nicht gemessen", "");
		if (((outLen + 3) % 2) != 0)
			put8(0);
		patch32(0x0C, nameAt);
		if (isDrvr) {
			patch32(0x30, bCode[rofRoot] + rEntry[rofRoot]);
			if (rTrap[rofRoot] != -1)
				patch32(0x34, bCode[rofRoot] + rTrap[rofRoot]);
		}
		patch32(0x04, outLen + 3);
		outBuf[0x2E] = (headerParity() >> 8) & 255;
		outBuf[0x2F] = headerParity() & 255;
		crc = moduleCrc(outLen);
		put8(crc >> 16);
		put8(crc >> 8);
		put8(crc);
		return;
	}

	/* --- Name, auf gerade aufgefuellt --- */
	nameAt = outLen;
	n = strLen(modName);
	for (i = 0; i < n; i++)
		put8(modName[i]);
	put8(0);
	if ((outLen % 2) != 0)
		put8(0);

	/* --- Code aller psects, in Reihenfolge der Kommandozeile --- */
	alignTo(optXAlign);
	for (k = 0; k < rofN; k++) {
		alignTo(optAlign);
		bCode[k] = outLen;
		for (i = 0; i < rCod[k]; i++)
			put8(inBuf[rCodeAt[k] + i]);
	}

	/* --- Initialisierte Daten: <Offset><Anzahl><Bytes> --- */
	alignTo(optAlign);
	idataAt = outLen;
	put32(totalUninit);
	put32(totalInit);
	for (k = 0; k < rofN; k++) {
		bIDataMod[k] = outLen;
		for (i = 0; i < rIDat[k]; i++)
			put8(inBuf[rIDataAt[k] + i]);
		for (i = rIDat[k]; i < alignUp(rIDat[k], optAlign); i++)
			put8(0);
	}
	/* Platz fuer die Sprungtabelle; gefuellt wird sie erst, wenn die
	   Referenzen abgearbeitet sind und die Zielwerte feststehen. */
	jtAt = outLen;
	for (i = 0; i < jtN * 6; i++)
		put8(0);

	/* Erst jetzt stehen alle Basen fest. */
	for (k = 0; k < rofN; k++)
		addGlobals(k);

	/* Die Symbole, die erst der Binder kennt (Handbuch Tab. 9-8/9-9).
	   Sie werden NACH den Bibliotheken und Globalen eingetragen und
	   verdecken damit eine gleichnamige Definition. */
	symAdd("bname", nameAt, 6);
	symAdd("_bname", nameAt, 6);
	symAdd("btext", 0, 6);
	symAdd("_btext", 0, 6);
	symAdd("etext", outLen, 6);
	symAdd("_etext", outLen, 6);
	/* Typ 0 (Daten), nicht 6 (equ): "end" bezeichnet das Ende des
	   DATENbereichs und bekommt deshalb den a6-Vorspann wie jeder
	   andere Datenbezug -- an l68 gemessen, das aus $250 im Code
	   $ffff8250 macht. Mit Typ 6 blieb der Vorspann aus. */
	symAdd("end", totalUninit + totalInit + totalRemote, 0);
	symAdd("_enddata", totalUninit + totalInit + totalRemote, 0);

	for (k = 0; k < rofN; k++) {
		applyLocalRefs(k);
		applyExtRefs(k);
	}

	/* --- Zeigerlisten --- */
	irefAt = outLen;
	putJumpTable();
	putIrefList(irefCode, irefCodeN);
	putIrefList(irefData, irefDataN);

	/* --- Auf gerade Gesamtgroesse auffuellen, dann der CRC. --- */
	if (((outLen + 3) % 2) != 0)
		put8(0);

	patch32(0x0C, nameAt);
	patch32(0x30, bCode[rofRoot] + rEntry[rofRoot]);
	if (rTrap[rofRoot] != -1)
		patch32(0x34, bCode[rofRoot] + rTrap[rofRoot]);
	patch32(0x40, idataAt);
	patch32(0x44, irefAt);
	patch32(0x04, outLen + 3);

	outBuf[0x2E] = (headerParity() >> 8) & 255;
	outBuf[0x2F] = headerParity() & 255;

	crc = moduleCrc(outLen);
	put8(crc >> 16);
	put8(crc >> 8);
	put8(crc);
}

/* ---------------------------------------------------------------- main */
static void usage(void)
{
	puts("ql68 -- Binder der Q9-Kette, erzeugt OS-9/68k-Module aus ROF");
	puts("Aufruf: ql68 [Optionen] <wurzel.r> [<weitere.r> ...] -O=<modul>");
	puts("  -O=<datei>, -o=<datei>  Ausgabemodul (wie l68)");
	puts("  -n=<name>               Modulname (sonst aus dem Ausgabenamen)");
	puts("  -l=<datei>              Bibliothek (eine Folge von ROFs)");
	puts("  -gu=<gruppe>.<nutzer>   Eigentuemer des Moduls");
	puts("  -p=<hex>                Zugriffsrechte im Modulkopf");
	puts("  -e=<n>                  Editionsnummer");
	puts("  -r[=<hex>]              rohe Binaerausgabe ab dieser Adresse (Vorgabe 0)");
	puts("  -z=<datei>              Dateinamen und Optionen daraus lesen, je Zeile eine");
	puts("  -M=<n>[K]               Stackzuschlag, die Zahl zaehlt in K");
	puts("  -b=<n>                  Code und Daten auf n ausrichten (2/4/8/16)");
	puts("  -x=<n>                  nur den Codeanfang ausrichten (2/4/8/16)");
	puts("  -a                      Sprungtabelle fuer zu weite Aufrufe");
	puts("  -S                      Modul bleibt im Speicher (sticky)");
	puts("  -R=<n>                  Revisionsnummer (unter 256)");
	exit(2);
}

static int argStarts(const char *a, const char *p);

/* Schalter, die das Modul NICHT veraendern -- angenommen und uebergangen,
   damit die Aufrufe der SDK-Makefiles unveraendert durchlaufen. Jeder
   einzelne ist gegen den Lauf OHNE ihn nachgemessen (test/optstest.sh):
     -m[=]  Modulkarte, wahlweise in eine Datei
     -s[=]  dieselbe Karte mit Symbolen
     -w     Karte alphabetisch statt nach Adressen
     -j     Sprungtabellenrechnung drucken
     -g     STB-Modul fuer den Debugger daneben (eigene Datei)
     -v     geschwaetzig
     -c     ANSI-treues Verhalten
     -i     initialisierte Daten im Systemmodul zulassen
     -q     still
     -f=    zusaetzliche DATEIrechte -- nicht der Modulkopf, dafuer -p=
     -mt<x> Umgang mit thread-fremdem Code
   l68 nimmt die Einzelbuchstaben auch als BUENDEL: "-swam" der
   ROM-Makefiles ist -s -w -a -m, "-gwj" druckt die Sprungtabellenkarte
   (an l68 nachgemessen).
   -a bleibt als BUCHSTABE in der Liste, weil "-swam" der ROM-Makefiles
   sonst nicht mehr durchlaeuft -- es wird beim Auswerten aber eigens
   herausgegriffen und schaltet die Sprungtabelle ein (s. farCall).
   Im SDK-Korpus wirkt es nie; die eigene Kette kommt ohne es nicht aus. */
static int harmlessOpt(const char *a)
{
	int i;

	if (a[1] == 0)
		return 0;
	if (argStarts(a, "-m=") || argStarts(a, "-s=") ||
	    argStarts(a, "-f=") || argStarts(a, "-mt"))
		return 1;
	for (i = 1; a[i] != 0; i++) {
		if (a[i] != 's' && a[i] != 'w' && a[i] != 'a' && a[i] != 'm' &&
		    a[i] != 'j' && a[i] != 'g' && a[i] != 'v' && a[i] != 'c' &&
		    a[i] != 'i' && a[i] != 'q')
			return 0;
	}
	return 1;
}

static int argStarts(const char *a, const char *p)
{
	int i;

	i = 0;
	while (p[i] != 0) {
		if (a[i] != p[i])
			return 0;
		i++;
	}
	return i;
}

/* Der Modulname kommt aus dem AUSGABENAMEN, nicht aus dem psect --
   gemessen: zwei Laeufe mit verschiedenen -O= unterscheiden sich genau im
   Namen und im CRC darueber. Verzeichnisanteile fallen weg. */
static void nameFromPath(const char *path)
{
	int i;
	int start;
	int n;

	n = strLen(path);
	start = 0;
	for (i = 0; i < n; i++) {
		if (path[i] == '/' || path[i] == '\\')
			start = i + 1;
	}
	i = 0;
	while (start + i < n && i < 255) {
		modName[i] = path[start + i];
		i++;
	}
	modName[i] = 0;
}

/* --- Die Argumentliste, in der -z= schon aufgeloest ist --- */
static char *argList[QL_ARGS];
static int argN;
static char zBuf[QL_ZBUF];
static int zLen;

static void argAdd(char *a);

/* -z=<datei>: JEDE ZEILE ist EIN Eintrag. An l68 gemessen -- steht
   "od.r -M=8K" in einer Zeile, sucht l68 eine Datei dieses Namens
   ("can't open file, od.r -M=8K"). Dateinamen und Optionen duerfen sich
   mischen, und die Reihenfolge ist egal: zwei Laeufe mit vertauschten
   Zeilen ergaben dieselben Bytes. Die Zeilen treten an die Stelle des
   -z=, ein -z= darin wird wieder aufgeloest. */
static void loadZ(const char *path)
{
	char *fp;
	int got;
	int start;
	int i;

	fp = fopen(path, "rb");
	if (fp == 0)
		fatal("-z=: Datei nicht lesbar: ", path);
	got = fread(&zBuf[zLen], 1, QL_ZBUF - zLen - 1, fp);
	fclose(fp);
	if (got <= 0)
		return;
	i = zLen;
	start = i;
	zLen = zLen + got;
	zBuf[zLen] = 0;
	zLen++;
	while (i < zLen) {
		if (zBuf[i] == '\r' || zBuf[i] == '\n' || zBuf[i] == 0) {
			zBuf[i] = 0;
			if (i > start)
				argAdd(&zBuf[start]);
			start = i + 1;
		}
		i++;
	}
}

static void argAdd(char *a)
{
	int k;

	if (a[0] == '-' && a[1] == 'z') {
		k = argStarts(a, "-z=");
		if (k > 0 && a[k] != 0) {
			loadZ(&a[k]);
			return;
		}
		if (a[2] == 0)
			fatal("-z ohne Datei liest die Standardeingabe; ql68 kennt nur fopen -- bitte -z=<datei>", "");
	}
	if (argN >= QL_ARGS)
		fatal("zu viele Argumente (QL_ARGS)", "");
	argList[argN] = a;
	argN++;
}

/* Function: main
 * Parses linker options, resolves ROF symbols and writes one OS-9 module.
 * Parameters: argc, argv Command-line argument count and vector.
 * Returns: Process status, zero on success. */
int main(int argc, char **argv)
{
	const char *inPath[QL_ROF];
	int inPathN;
	const char *outPath;
	char *fp;
	int at;
	int i;
	int k;

	inPathN = 0;
	outPath = 0;
	modName[0] = 0;
	/* Erst die Argumentliste aufbauen -- dabei loest argAdd jedes -z=
	   an Ort und Stelle auf. Danach unterscheidet sich eine Zeile aus
	   der Datei in nichts mehr von einem Wort der Kommandozeile. */
	argN = 0;
	zLen = 0;
	for (i = 1; i < argc; i++)
		argAdd(argv[i]);
	for (i = 0; i < argN; i++) {
		char *a;

		a = argList[i];
		k = argStarts(a, "-O=");
		if (k == 0)
			k = argStarts(a, "-o=");
		if (k > 0 && a[k] != 0) {
			outPath = &a[k];
			continue;
		}
		k = argStarts(a, "-n=");
		if (k > 0 && a[k] != 0) {
			nameFromPath(&a[k]);
			continue;
		}
		k = argStarts(a, "-l=");
		if (k > 0 && a[k] != 0) {
			if (libN >= QL_LIB)
				fatal("zu viele Bibliotheken (QL_LIB)", "");
			libPath[libN] = &a[k];
			libN++;
			continue;
		}
		k = argStarts(a, "-gu=");
		if (k > 0 && a[k] != 0) {
			/* "-gu=<gruppe>.<nutzer>" -- die beiden Zahlen bilden
			   die obere und untere Haelfte von M$Owner. */
			int g;
			int u;

			g = 0;
			while (a[k] >= '0' && a[k] <= '9') {
				g = g * 10 + (a[k] - '0');
				k++;
			}
			if (a[k] != '.')
				fatal("-gu= erwartet <gruppe>.<nutzer>: ", a);
			k++;
			u = 0;
			while (a[k] >= '0' && a[k] <= '9') {
				u = u * 10 + (a[k] - '0');
				k++;
			}
			optOwner = ((g & 0xFFFF) << 16) | (u & 0xFFFF);
			continue;
		}
		k = argStarts(a, "-p=");
		if (k > 0 && a[k] != 0) {
			/* HEXADEZIMAL, so steht es in der Hilfe von l68. */
			int v;
			int c;

			v = 0;
			while (a[k] != 0) {
				c = a[k];
				if (c >= '0' && c <= '9')
					v = v * 16 + (c - '0');
				else if (c >= 'a' && c <= 'f')
					v = v * 16 + (c - 'a' + 10);
				else if (c >= 'A' && c <= 'F')
					v = v * 16 + (c - 'A' + 10);
				else
					fatal("-p= erwartet eine Hexzahl: ", a);
				k++;
			}
			optAccess = v & 0xFFFF;
			continue;
		}
		k = argStarts(a, "-M=");
		if (k > 0 && a[k] != 0) {
			int v;

			v = 0;
			while (a[k] >= '0' && a[k] <= '9') {
				v = v * 10 + (a[k] - '0');
				k++;
			}
			if (a[k] == 'K' || a[k] == 'k')
				k++;
			if (a[k] != 0)
				fatal("-M= erwartet eine Zahl, wahlweise mit K: ", a);
			optStackAdd = v * 1024;
			continue;
		}
		k = argStarts(a, "-b=");
		if (k > 0 && a[k] != 0) {
			int v;

			v = 0;
			while (a[k] >= '0' && a[k] <= '9') {
				v = v * 10 + (a[k] - '0');
				k++;
			}
			if (a[k] != 0)
				fatal("-b= erwartet eine Zahl: ", a);
			if (v != 2 && v != 4 && v != 8 && v != 16)
				fatal("-b= kennt nur 2, 4, 8 und 16: ", a);
			optAlign = v;
			continue;
		}
		k = argStarts(a, "-r=");
		if (k > 0 && a[k] != 0) {
			int v;
			int c;

			v = 0;
			while (a[k] != 0) {
				c = a[k];
				if (c >= '0' && c <= '9')
					v = v * 16 + (c - '0');
				else if (c >= 'a' && c <= 'f')
					v = v * 16 + (c - 'a' + 10);
				else if (c >= 'A' && c <= 'F')
					v = v * 16 + (c - 'A' + 10);
				else
					fatal("-r= erwartet eine Hexzahl: ", a);
				k++;
			}
			optRaw = v;
			continue;
		}
		if (a[0] == '-' && a[1] == 'r' && a[2] == 0) {
			/* l68 schreibt "-r[=<base>] ... default=0". An l68
			   gemessen: "-r" und "-r=0" liefern dieselben 54
			   Byte. */
			optRaw = 0;
			continue;
		}
		k = argStarts(a, "-e=");
		if (k > 0 && a[k] != 0) {
			int v;

			v = 0;
			while (a[k] >= '0' && a[k] <= '9') {
				v = v * 10 + (a[k] - '0');
				k++;
			}
			optEdition = v;
			continue;
		}
		k = argStarts(a, "-R=");
		if (k > 0 && a[k] != 0) {
			int v;

			v = 0;
			while (a[k] >= '0' && a[k] <= '9') {
				v = v * 10 + (a[k] - '0');
				k++;
			}
			if (a[k] != 0 || v > 255)
				fatal("-R= erwartet eine Zahl unter 256: ", a);
			optRevision = v;
			continue;
		}
		k = argStarts(a, "-x=");
		if (k > 0 && a[k] != 0) {
			int v;

			v = 0;
			while (a[k] >= '0' && a[k] <= '9') {
				v = v * 10 + (a[k] - '0');
				k++;
			}
			if (a[k] != 0)
				fatal("-x= erwartet eine Zahl: ", a);
			if (v != 2 && v != 4 && v != 8 && v != 16)
				fatal("-x= kennt nur 2, 4, 8 und 16: ", a);
			optXAlign = v;
			continue;
		}
		k = argStarts(a, "-t=");
		if (k > 0 && a[k] != 0) {
			/* Nur OS-9/68k. Die uebrigen Ziele von l68 sind ganz
			   andere Modulformate -- lieber abbrechen als still das
			   falsche Format schreiben. */
			if (!argStarts(&a[k], "os9_68k"))
				fatal("ql68 kennt nur -t=os9_68k: ", a);
			continue;
		}
		if (a[0] == '-' && a[1] == 'S' && a[2] == 0) {
			optSticky = 1;
			continue;
		}
		if (a[0] == '-' && harmlessOpt(a)) {
			/* -a schaltet die Sprungtabelle ein, auch als
			   Buchstabe in einem Buendel ("-swam"). Nur bei den
			   reinen Buchstabenformen nachsehen -- in "-m=pfad_a"
			   oder "-f=..." waere ein 'a' blosser Text. */
			if (argStarts(a, "-m=") == 0 && argStarts(a, "-s=") == 0 &&
			    argStarts(a, "-f=") == 0 && argStarts(a, "-mt") == 0) {
				int b;

				for (b = 1; a[b] != 0; b++) {
					if (a[b] == 'a')
						optJumpTab = 1;
				}
			}
			continue;
		}
		if (a[0] == '-' && a[1] != 0) {
			printf("ql68: unbekannte Option %s\n", a);
			usage();
		}
		if (inPathN >= QL_ROF)
			fatal("zu viele Eingabedateien (QL_ROF): ", a);
		inPath[inPathN] = a;
		inPathN++;
	}
	if (inPathN == 0 || outPath == 0)
		usage();
	if (modName[0] == 0)
		nameFromPath(outPath);

	/* Alle Eingabedateien hintereinander in denselben Puffer -- eine
	   Datei kann selbst mehrere ROFs enthalten (so sind die Bibliotheken
	   aufgebaut), deshalb wird bis zum Dateiende weitergelesen. */
	inLen = 0;
	for (i = 0; i < inPathN; i++) {
		int start;
		int got;

		fp = fopen(inPath[i], "rb");
		if (fp == 0)
			fatal("Eingabe nicht lesbar: ", inPath[i]);
		start = inLen;
		got = fread(&inBuf[inLen], 1, QL_IN - inLen, fp);
		fclose(fp);
		inLen = inLen + got;
		at = start;
		while (at < inLen)
			at = rofParse(at);
	}

	/* Erst jetzt die Bibliotheken: verzeichnen, dann in EINEM Durchgang
	   einbinden, was gebraucht wird. Vorher war das umgekehrt -- das ging
	   nur, solange eine Bibliothek blosse Konstanten lieferte. */
	for (i = 0; i < libN; i++)
		libScan(libPath[i]);
	libLink();

	/* Den Wurzel-psect suchen: er ist der EINZIGE mit einem Typ/Sprach-
	   Wert ungleich null. Er steht NICHT zwangslaeufig vorn -- die
	   SDK-Makefiles schreiben etwa
	     l68 ... ..\..\68000\LIB\scfstat.l RELS\sc172.r -O=...
	   und reichen damit erst eine reine Symboldatei ein. */
	rofRoot = -1;
	for (k = 0; k < rofN; k++) {
		if (rTyLan[k] != 0) {
			if (rofRoot >= 0)
				fatal("mehr als ein Wurzel-psect in den Eingaben", "");
			rofRoot = k;
		}
	}
	if (rofRoot < 0)
		fatal("keine der Eingaben hat einen Wurzel-psect (Typ/Sprache ist 0) -- nur daraus entsteht ein Modul", "");
	/* Erst zaehlen, dann schreiben. Der Zaehllauf stellt fest, wie viele
	   Aufrufe zu weit sind; die Tabelle liegt in den DATEN und verschiebt
	   den Code nicht, deshalb ist der zweite Lauf endgueltig. Ohne -a
	   bricht farCall schon im ersten Lauf ab. */
	if (optJumpTab) {
		int symKeep;
		int poolKeep;

		symKeep = symN;
		poolKeep = symPoolTop;
		jtPlan = 1;
		emit();
		jtPlan = 0;
		if (jtN > 0) {
			outLen = 0;
			symN = symKeep;
			symPoolTop = poolKeep;
			irefCodeN = 0;
			irefDataN = 0;
			emit();
		}
	} else {
		emit();
	}

	fp = fopen(outPath, "wb");
	if (fp == 0)
		fatal("Ausgabe nicht schreibbar: ", outPath);
	fwrite(outBuf, 1, outLen, fp);
	fclose(fp);
	return 0;
}
