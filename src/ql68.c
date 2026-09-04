/*
 * ql68 -- der Binder der Q9-Werkzeugkette.
 *
 * Aufruf:  ql68 [Optionen] <eingabe.r> -O=<modul>
 *
 * Liest eine ROF-Datei (Microware Edition 9.1, wie sie qr68 und r68
 * schreiben) und erzeugt daraus ein ladbares OS-9/68k-Modul.
 *
 * ALLES HIER IST AN l68 GEMESSEN, nicht aus der Dokumentation abgeleitet.
 * Die Dokumentation (MWOS/DOC/PDF/ultrac_use.pdf Kap. 6 und 9,
 * 68k_tech.pdf) gibt die Struktur vor; die Bytes kommen aus dem Vergleich.
 * Was gemessen wurde, steht im README.
 *
 * Geschrieben in derselben Teilmenge wie qr68 und qcpp, damit sich ql68
 * spaeter selbst uebersetzen laesst: kein Union, kein "->", kein float,
 * Arraygroessen als Literale, feste Tabellen statt malloc, KEINE
 * aneinandergereihten Stringliterale und KEIN zweistufiger Index auf ein
 * Zeigerfeld (beides kann QCC nicht).
 */

int printf(const char *fmt, ...);
int puts(const char *s);
void exit(int code);
char *fopen(const char *path, const char *mode);
int fclose(char *fp);
int fread(char *buf, int size, int n, char *fp);
int fwrite(const char *buf, int size, int n, char *fp);

#define QL_IN     (4 * 1024 * 1024)   /* Eingabepuffer  */
#define QL_OUT    (4 * 1024 * 1024)   /* Ausgabepuffer  */
#define QL_IREF   16384               /* Zeiger je Liste */

static char inBuf[QL_IN];
static int inLen;
static char outBuf[QL_OUT];
static int outLen;

/* ------------------------------------------------------------------ ROF */
static int rofTyLan;
static int rofAttRev;
static int rofEdition;
static int rofStatStorage;   /* uninitialisierte Daten (ds im vsect) */
static int rofIDatSz;        /* initialisierte Daten (dc im vsect)   */
static int rofCodSz;
static int rofStkSz;
static int rofEntry;
static int rofTrap;
static int rofNameAt;        /* Offset des psect-Namens in inBuf     */
static int rofCodeAt;        /* Offset des Codes in inBuf            */
static int rofIDataAt;       /* Offset der init. Daten in inBuf      */
static int rofLocalAt;       /* Offset der lokalen Referenzliste     */
static int rofLocalN;
static int rofExtAt;         /* Offset der externen Referenzliste    */
static int rofExtN;

static char modName[256];
static int optOwner = 0x00010000;  /* M$Owner, so ohne -gu= (gemessen) */
static int optAccess = 0x0555;     /* M$Accs,  so ohne -p=  (gemessen) */
static int optEdition = -1;        /* -e=: ueberschreibt den psect-Wert */

/* Zeigerlisten fuer den IRefs-Abschnitt. */
static int irefCode[QL_IREF];
static int irefCodeN;
static int irefData[QL_IREF];
static int irefDataN;

#define QL_SYM    8192
#define QL_POOL   (256 * 1024)
#define QL_LIB    16

/* Symboltabelle fuer alles, was externe Referenzen aufloesen kann: die
   Globalen der Bibliotheken und die vom Binder selbst gesetzten Symbole. */
static char symPool[QL_POOL];
static int symPoolTop;
static int symName[QL_SYM];    /* Index in symPool */
static int symValue[QL_SYM];
static int symType[QL_SYM];    /* Typwort wie im ROF: 6 = equ, 4 = Code ... */
static int symN;

static char libBuf[QL_IN];
static int libLen;
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

/* Der ROF-Kopf ist 56 Byte, die Zaehler sind 32 Bit -- das ist Edition
   9.1. (Edition 9.0 haette 16-Bit-Zaehler; osk-disasm/rof.c liest die
   und passt deshalb nicht. Bestaetigt durch Microwares rdump, das fuer
   qr68-Ausgaben "CPU/ROF type: 680x0/9.1" meldet.) */
static void rofParse(void)
{
	int at;
	int n;
	int i;

	if (inLen < 56)
		fatal("Eingabe ist zu kurz fuer einen ROF-Kopf", "");
	if ((inBuf[0] & 255) != 0xDE || (inBuf[1] & 255) != 0xAD ||
	    (inBuf[2] & 255) != 0xFA || (inBuf[3] & 255) != 0xCE)
		fatal("keine ROF-Datei (Sync ist nicht $DEADFACE)", "");

	rofTyLan = be16(4);
	rofAttRev = be16(6);
	if (be16(8) != 0)
		fatal("der ROF ist als fehlerhaft gekennzeichnet", "");
	rofEdition = be16(18);
	rofStatStorage = be32(20);
	rofIDatSz = be32(24);
	rofCodSz = be32(28);
	rofStkSz = be32(32);
	rofEntry = be32(36);
	rofTrap = be32(40);
	if (be32(44) != 0 || be32(48) != 0)
		fatal("Remote-Daten sind noch nicht gemessen", "");
	if (be32(52) != 0)
		fatal("Debuginformationen sind noch nicht gemessen", "");

	at = 56;
	rofNameAt = at;
	at = skipName(at);

	/* Globale Definitionen ueberspringen -- fuer ein einzelnes Modul
	   werden sie nicht gebraucht (sie dienen dem Aufloesen zwischen
	   psects). */
	n = be32(at);
	at = at + 4;
	for (i = 0; i < n; i++) {
		at = skipName(at);
		at = at + 6;               /* Typwort und Wert */
	}

	rofCodeAt = at;
	at = at + rofCodSz;
	rofIDataAt = at;
	at = at + rofIDatSz;

	rofExtN = be32(at);
	rofExtAt = at + 4;
	at = rofExtAt;
	for (i = 0; i < rofExtN; i++) {
		at = skipName(at);
		at = at + 4 + be32(at) * 6;
	}

	rofLocalN = be32(at);
	rofLocalAt = at + 4;
}

/* Eine Bibliothek ist KEIN Sonderformat, sondern eine Folge von ROFs --
   nachgemessen an MWOS/OS9/68000/LIB/sys.l: sieben ROFs hintereinander,
   1747 Globale, alle vom Typ $0006 (equ), und die Laengenrechnung landet
   genau auf dem Dateiende. Genau das meint die Dokumentation mit
   "sys.l ... contains only equ symbol definitions".

   Die Laenge eines ROF ist: 56 + Name + Globale + Code + init. Daten +
   externe Referenzen + lokale Referenzen + VIER abschliessende
   Langwoerter. */
static int libAt;            /* Lesezeiger, waehrend libBuf geparst wird */

static int libBe32(int at)
{
	return ((libBuf[at] & 255) << 24) | ((libBuf[at + 1] & 255) << 16) |
	       ((libBuf[at + 2] & 255) << 8) | (libBuf[at + 3] & 255);
}

static int libSkipName(int at)
{
	while (at < libLen && libBuf[at] != 0)
		at++;
	return at + 1;
}

static void libScan(const char *path)
{
	char *fp;
	int at;
	int n;
	int i;
	int cods;
	int idat;

	fp = fopen(path, "rb");
	if (fp == 0)
		fatal("Bibliothek nicht lesbar: ", path);
	libLen = fread(libBuf, 1, QL_IN, fp);
	fclose(fp);

	at = 0;
	while (at + 56 <= libLen) {
		if ((libBuf[at] & 255) != 0xDE || (libBuf[at + 1] & 255) != 0xAD ||
		    (libBuf[at + 2] & 255) != 0xFA || (libBuf[at + 3] & 255) != 0xCE)
			fatal("in der Bibliothek steht kein ROF-Sync: ", path);
		idat = libBe32(at + 24);
		cods = libBe32(at + 28);
		libAt = at + 56;
		libAt = libSkipName(libAt);
		n = libBe32(libAt);
		libAt = libAt + 4;
		for (i = 0; i < n; i++) {
			int nameAt;
			int t;
			int v;

			nameAt = libAt;
			libAt = libSkipName(libAt);
			t = ((libBuf[libAt] & 255) << 8) | (libBuf[libAt + 1] & 255);
			v = libBe32(libAt + 2);
			libAt = libAt + 6;
			if (t != 6)
				fatal("in einer Bibliothek ist bisher nur ein equ-Symbol gemessen: ", &libBuf[nameAt]);
			symAdd(&libBuf[nameAt], v, t);
		}
		libAt = libAt + cods + idat;
		n = libBe32(libAt);
		libAt = libAt + 4;
		for (i = 0; i < n; i++) {
			libAt = libSkipName(libAt);
			libAt = libAt + 4 + libBe32(libAt) * 6;
		}
		n = libBe32(libAt);
		libAt = libAt + 4 + n * 6;
		at = libAt + 16;
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

/* Kopfparitaet: das Einerkomplement des XOR aller Kopfworte von $00 bis
   $2d. Gemessen und an l68s Ausgabe nachgerechnet. */
static int headerParity(void)
{
	int p;
	int o;

	p = 0;
	for (o = 0; o < 0x2E; o = o + 2)
		p = p ^ (((outBuf[o] & 255) << 8) | (outBuf[o + 1] & 255));
	return (~p) & 0xFFFF;
}

/* 24-Bit-CRC, Polynom $800063, Startwert $FFFFFF. Der abgelegte Wert ist
   das KOMPLEMENT des Ergebnisses ueber das Modul ohne die drei CRC-Bytes.
   Probe: rechnet man ueber das ganze Modul EINSCHLIESSLICH CRC, kommt
   $800FE3 heraus -- die Konstante CRCCON aus module.h ist also der
   Sollrest, nicht das Polynom. Beides an l68 nachgerechnet. */
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
   Die Liste ist AUFSTEIGEND nach Offset sortiert -- der ROF liefert seine
   lokalen Referenzen absteigend, l68 dreht sie also um (gemessen an zwei
   Codezeigern auf $0000 und $0008). */
static void putIrefList(int *offs, int n)
{
	int i;
	int j;
	int msw;
	int t;

	for (i = 0; i < n; i++) {
		for (j = i + 1; j < n; j++) {
			if (offs[j] < offs[i]) {
				t = offs[i];
				offs[i] = offs[j];
				offs[j] = t;
			}
		}
	}

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

/* Die lokalen Referenzen des ROF durchgehen und dabei zweierlei tun:
   den Wert an der Referenzstelle um die Basis des Zielabschnitts
   erhoehen, und -- wenn die Stelle in den initialisierten Daten liegt --
   ihren Datenoffset in die passende Zeigerliste eintragen.

   Das Typwort ist dasselbe wie bei qr68 (dort vollstaendig dokumentiert):
     Bit 5     die Referenz LIEGT im Code (sonst in den Daten)
     Bit 3..4  Umfang: 01 = 1, 10 = 2, 11 = 4 Byte
     Bit 2     das ZIEL ist Code (sonst Daten)
     Bit 6/7   abziehen / relativ */
static void applyLocalRefs(int codeBase, int dataInitBase, int idataAt)
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

	for (i = 0; i < rofLocalN; i++) {
		at = rofLocalAt + i * 6;
		type = be16(at);
		offs = be32(at + 2);
		if (type & 0x00C0)
			fatal("abziehende oder relative lokale Referenz ist noch nicht gemessen", "");
		size = (type >> 3) & 3;
		if (size < 1 || size > 3)
			fatal("Referenz ohne gemessenen Umfang", "");
		inCode = (type >> 5) & 1;
		toCode = (type >> 2) & 1;
		base = dataInitBase;
		if (toCode)
			base = codeBase;
		/* Der Zugriff auf die eigenen Daten laeuft ueber a6, und a6
		   zeigt NICHT auf den Anfang des Datenbereichs, sondern
		   $8000 dahinter -- so reicht ein 16-Bit-Displacement
		   +-32K weit. Gemessen: aus "move.l zeiger(a6),d1" mit
		   zeiger auf Datenoffset $000c macht l68 $800c. Der Bias
		   gilt nur fuer dieses Displacement im Code; ein 32-Bit-
		   Zeiger IN den Daten bleibt unvorgespannt (p3 dc.l p1 mit
		   p1 auf 0 ergibt 0). */
		if (inCode && !toCode && size == 2)
			base = base + 0x8000;
		if (inCode)
			here = codeBase + offs;
		else
			here = idataAt + offs;
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
		/* Nur ein LANGWORT in den Daten ist ein Zeiger, den der Lader
		   noch einmal anfassen muss. Ein kuerzeres Feld -- etwa das
		   16-Bit-Displacement in "move.l zeiger(a6),d1" -- steht im
		   Code und wird hier endgueltig aufgeloest. */
		if (!inCode && size == 3) {
			/* Der Lader muss diesen Zeiger beim Laden noch einmal
			   anpassen -- deshalb kommt sein Datenoffset in die
			   Liste. */
			if (toCode) {
				if (irefCodeN >= QL_IREF)
					fatal("zu viele Codezeiger (QL_IREF)", "");
				irefCode[irefCodeN] = rofStatStorage + offs;
				irefCodeN++;
			} else {
				if (irefDataN >= QL_IREF)
					fatal("zu viele Datenzeiger (QL_IREF)", "");
				irefData[irefDataN] = rofStatStorage + offs;
				irefDataN++;
			}
		}
	}
}

/* Die externen Referenzen aufloesen. Der Wert des Symbols wird an der
   Referenzstelle AUFADDIERT -- der Assembler hat dort schon den konstanten
   Anteil des Ausdrucks abgelegt (bei qr68 gemessen und dokumentiert).
   Der Umfang steht wie bei den lokalen Referenzen in Bit 3..4, Bit 5 sagt,
   ob die Stelle im Code liegt. */
static void applyExtRefs(int codeBase, int idataAt)
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

	at = rofExtAt;
	for (i = 0; i < rofExtN; i++) {
		nameAt = at;
		at = skipName(at);
		nrefs = be32(at);
		at = at + 4;
		si = symFind(&inBuf[nameAt]);
		if (si < 0)
			fatal("unaufgeloester Name: ", &inBuf[nameAt]);
		if (symType[si] != 6)
			fatal("bisher ist nur ein absolutes (equ-) Symbol als externer Bezug gemessen: ", &inBuf[nameAt]);
		val = symValue[si];
		for (j = 0; j < nrefs; j++) {
			type = be16(at);
			offs = be32(at + 2);
			at = at + 6;
			if (type & 0x00C0)
				fatal("abziehende oder relative externe Referenz ist noch nicht gemessen: ", &inBuf[nameAt]);
			size = (type >> 3) & 3;
			inCode = (type >> 5) & 1;
			if (inCode)
				here = codeBase + offs;
			else
				here = idataAt + offs;
			if (size == 1) {
				v = outBuf[here] & 255;
				outBuf[here] = (v + val) & 255;
			} else if (size == 2) {
				v = ((outBuf[here] & 255) << 8) | (outBuf[here + 1] & 255);
				v = v + val;
				outBuf[here] = (v >> 8) & 255;
				outBuf[here + 1] = v & 255;
			} else if (size == 3) {
				v = ((outBuf[here] & 255) << 24) |
				    ((outBuf[here + 1] & 255) << 16) |
				    ((outBuf[here + 2] & 255) << 8) |
				    (outBuf[here + 3] & 255);
				patch32(here, v + val);
			} else {
				fatal("externe Referenz ohne gemessenen Umfang: ", &inBuf[nameAt]);
			}
		}
	}
}

static void emit(void)
{
	int isDesc;
	int nameAt;
	int codeAt;
	int idataAt;
	int irefAt;
	int i;
	int n;
	int crc;

	/* Typ 15 = Gerraetedescriptor (Devic), s. module.h. */
	isDesc = ((rofTyLan >> 8) & 255) == 15;

	/* --- Kopf. Die Groessenfelder werden spaeter nachgetragen. --- */
	put16(0x4AFC);                 /* M$ID    */
	put16(1);                      /* M$SysRev */
	put32(0);                      /* M$Size, spaeter */
	put32(optOwner);               /* M$Owner */
	put32(0);                      /* M$Name, spaeter */
	put16(optAccess);              /* M$Accs */
	put16(rofTyLan);
	put16(rofAttRev);
	if (optEdition >= 0)
		put16(optEdition);
	else
		put16(rofEdition);
	put32(0);                      /* M$Usage  */
	put32(0);                      /* M$Symbol */
	put16(0);                      /* M$Ident  */
	for (i = 0; i < 6; i++)
		put8(0);                   /* Reserve  */
	put32(0);                      /* M$HdExt  */
	put16(0);                      /* M$HdExtSz */
	put16(0);                      /* M$Parity, spaeter */
	if (!isDesc)
		put32(0);              /* M$Exec, spaeter  */
	if (!isDesc) {
		/* Fehlt der siebte psect-Parameter, traegt r68 utrap = -1 ein;
		   l68 macht daraus im Modul die 0 (gemessen). */
		if (rofTrap == -1)
			put32(0);
		else
			fatal("ein gesetzter Trap-Einsprung ist noch nicht gemessen", "");
		put32(rofStatStorage + rofIDatSz);   /* M$Data */
		put32(rofStkSz);               /* M$Stack */
		put32(0);                      /* M$IData, spaeter */
		put32(0);                      /* M$IRefs, spaeter */
	}

	/* Ein GERAETEDESCRIPTOR (Typ 15) hat keine mod_exec-Erweiterung:
	   sein ROF-Code IST die Kopferweiterung (Portadresse, Vektor,
	   IRQ-Ebene, Modus, die Namensoffsets fuer Dateimanager und Treiber)
	   samt Rumpf, und er liegt unmittelbar hinter dem gemeinsamen Kopf
	   auf $30. Danach folgt nur noch der Name und der CRC -- KEIN
	   IData- und kein IRefs-Abschnitt.

	   Gemessen an SRC/IO/SCF/DESC/term.a: die Namensoffsets im Code
	   springen von $0034/$0038/$003e auf $0064/$0068/$006e, also genau
	   um die Codebasis $30 weiter. */
	if (isDesc) {
		codeAt = outLen;
		for (i = 0; i < rofCodSz; i++)
			put8(inBuf[rofCodeAt + i]);
		nameAt = outLen;
		n = strLen(modName);
		for (i = 0; i < n; i++)
			put8(modName[i]);
		put8(0);
		if ((outLen % 2) != 0)
			put8(0);
		applyLocalRefs(codeAt, 0, 0);
		symAdd("bname", nameAt, 6);
		symAdd("_bname", nameAt, 6);
		symAdd("btext", 0, 6);
		symAdd("_btext", 0, 6);
		symAdd("etext", outLen, 6);
		symAdd("_etext", outLen, 6);
		applyExtRefs(codeAt, 0);
		if (irefCodeN != 0 || irefDataN != 0)
			fatal("ein Descriptor mit Datenzeigern ist noch nicht gemessen", "");
		if (((outLen + 3) % 2) != 0)
			put8(0);
		patch32(0x0C, nameAt);
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

	/* --- Code --- */
	codeAt = outLen;
	for (i = 0; i < rofCodSz; i++)
		put8(inBuf[rofCodeAt + i]);

	/* --- Initialisierte Daten: <Offset><Anzahl><Bytes> --- */
	idataAt = outLen;
	put32(rofStatStorage);
	put32(rofIDatSz);
	for (i = 0; i < rofIDatSz; i++)
		put8(inBuf[rofIDataAt + i]);

	/* Erst jetzt stehen die Basen fest. Die initialisierten Daten
	   liegen im Datenbereich HINTER den uninitialisierten -- gemessen. */
	applyLocalRefs(codeAt, rofStatStorage, idataAt + 8);

	/* Die Symbole, die erst der Binder kennt (Tabelle 9-8/9-9 des
	   Handbuchs). Sie werden NACH den Bibliotheken eingetragen und
	   verdecken damit eine gleichnamige Definition von dort. */
	symAdd("bname", nameAt, 6);
	symAdd("_bname", nameAt, 6);
	symAdd("btext", 0, 6);
	symAdd("_btext", 0, 6);
	symAdd("etext", outLen, 6);
	symAdd("_etext", outLen, 6);
	symAdd("end", rofStatStorage + rofIDatSz, 6);
	symAdd("_enddata", rofStatStorage + rofIDatSz, 6);

	applyExtRefs(codeAt, idataAt + 8);

	/* --- Zeigerlisten --- */
	irefAt = outLen;
	putIrefList(irefCode, irefCodeN);
	putIrefList(irefData, irefDataN);

	/* --- Auf gerade Gesamtgroesse auffuellen, dann der CRC. --- */
	if (((outLen + 3) % 2) != 0)
		put8(0);

	patch32(0x0C, nameAt);
	patch32(0x30, codeAt + rofEntry);
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
	puts("Aufruf: ql68 [Optionen] <eingabe.r> -O=<modul>");
	puts("  -O=<datei>, -o=<datei>  Ausgabemodul (wie l68)");
	puts("  -n=<name>               Modulname (sonst aus dem Ausgabenamen)");
	puts("  -l=<datei>              Bibliothek (eine Folge von ROFs)");
	puts("  -gu=<gruppe>.<nutzer>   Eigentuemer des Moduls");
	puts("  -p=<hex>                Zugriffsrechte im Modulkopf");
	puts("  -e=<n>                  Editionsnummer");
	exit(2);
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

int main(int argc, char **argv)
{
	const char *inPath;
	const char *outPath;
	char *fp;
	int i;
	int k;

	inPath = 0;
	outPath = 0;
	modName[0] = 0;
	for (i = 1; i < argc; i++) {
		char *a;

		a = argv[i];
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
		if (a[0] == '-' && a[1] != 0) {
			printf("ql68: unbekannte Option %s\n", a);
			usage();
		}
		if (inPath == 0)
			inPath = a;
		else
			fatal("ql68 bindet bisher nur einen einzelnen ROF: ", a);
	}
	if (inPath == 0 || outPath == 0)
		usage();
	if (modName[0] == 0)
		nameFromPath(outPath);

	fp = fopen(inPath, "rb");
	if (fp == 0)
		fatal("Eingabe nicht lesbar: ", inPath);
	inLen = fread(inBuf, 1, QL_IN, fp);
	fclose(fp);

	for (i = 0; i < libN; i++)
		libScan(libPath[i]);

	rofParse();
	if (rofTyLan == 0)
		fatal("der ROF hat keinen Wurzel-psect (Typ/Sprache ist 0) -- nur daraus entsteht ein Modul", "");
	emit();

	fp = fopen(outPath, "wb");
	if (fp == 0)
		fatal("Ausgabe nicht schreibbar: ", outPath);
	fwrite(outBuf, 1, outLen, fp);
	fclose(fp);
	return 0;
}
