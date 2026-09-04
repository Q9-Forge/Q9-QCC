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
/* Ein Eintrag je psect. Der ERSTE ist der Wurzel-psect -- nur er hat
   einen Typ/Sprach-Wert ungleich null, und nur aus ihm entsteht der
   Modulkopf (Handbuch Kap. 9: "mainline is the pathlist of the file
   containing the root psect"). */
#define QL_ROF    256

static int rofN;
static int rTyLan[QL_ROF];
static int rAttRev[QL_ROF];
static int rEdition[QL_ROF];
static int rStat[QL_ROF];    /* uninitialisierte Daten (ds im vsect) */
static int rIDat[QL_ROF];    /* initialisierte Daten (dc im vsect)   */
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

/* Nach dem Auslegen: wo der psect im Modul bzw. im Datenbereich liegt. */
static int bCode[QL_ROF];    /* Modulabstand des Codes            */
static int bUninit[QL_ROF];  /* Datenabstand der ds-Daten         */
static int bInit[QL_ROF];    /* Datenabstand der dc-Daten         */
static int bIDataMod[QL_ROF];/* Modulabstand der dc-Daten         */

/* Der Vorspann auf den Datenzeiger. Bei einem Programm (mod_exec) zeigt
   a6 NICHT auf den Anfang des Datenbereichs, sondern $8000 dahinter --
   so reicht ein 16-Bit-Displacement +-32K weit. Ein TREIBER bekommt
   seinen statischen Speicher dagegen direkt (in a2) und kennt keinen
   Vorspann. Gemessen: "move.l zeiger(a6),d1" mit zeiger auf $000c ergibt
   im Programm $800c, "move.w d2,$001c(a2)" im Treiber sc172 dagegen
   $001c. */
static int dataBias;

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
	if (be32(at0 + 44) != 0 || be32(at0 + 48) != 0)
		fatal("Remote-Daten sind noch nicht gemessen", "");
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

	/* Vier abschliessende Langwoerter -- sie sind in allen Proben null.
	   Sie zaehlen zur Laenge, was beim Lesen einer Bibliothek zaehlt. */
	return at + 16;
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

/* Voranstellen -- s. putIrefList(). */
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

/* Die lokalen Referenzen eines psect aufloesen. Zweierlei geschieht:
   der Wert an der Referenzstelle wird um die Basis des ZIELabschnitts
   erhoeht, und ein Langwort IN den Daten kommt zusaetzlich in die
   passende Zeigerliste -- der Lader muss es beim Laden noch einmal
   anfassen.

   Typwort wie bei qr68 (dort vollstaendig dokumentiert):
     Bit 5     die Referenz LIEGT im Code (sonst in den Daten)
     Bit 3..4  Umfang: 01 = 1, 10 = 2, 11 = 4 Byte
     Bit 2     das ZIEL ist Code; Bit 0..1 sonst der Datenabschnitt
     Bit 6/7   abziehen / relativ */
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
			base = bInit[k];       /* initialisierte Daten */
		else
			base = bUninit[k];     /* reservierte Daten     */
		/* Bit 6: der Wert geht ABGEZOGEN ein. So entsteht die
		   Differenz zweier Bezuege in einem Ausdruck -- r68 legt fuer
		   "PD_PAR-PD_OPT+M$DTyp(a1)" drei Referenzen auf denselben
		   Offset ab, eine davon mit diesem Bit. */
		if (type & 0x0040)
			base = -base;
		/* Der Zugriff auf die eigenen Daten laeuft ueber a6, und a6
		   zeigt NICHT auf den Anfang des Datenbereichs, sondern $8000
		   dahinter -- so reicht ein 16-Bit-Displacement +-32K weit.
		   Gemessen: aus "move.l zeiger(a6),d1" mit zeiger auf
		   Datenoffset $000c macht l68 $800c. Ein 32-Bit-Zeiger IN den
		   Daten bleibt unvorgespannt. */
		if (inCode && !toCode && size == 2)
			base = base + dataBias;
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
		/* Nur ein LANGWORT in den Daten ist ein Zeiger, den der Lader
		   noch einmal anfassen muss. Ein kuerzeres Feld -- etwa das
		   16-Bit-Displacement im Code -- ist hier endgueltig. */
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
			/* Bit 6: abziehen (Handbuch: "add the negative of the
			   symbols location"). */
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
				outBuf[here] = (v >> 8) & 255;
				outBuf[here + 1] = v & 255;
			} else if (size == 3) {
				v = ((outBuf[here] & 255) << 24) |
				    ((outBuf[here + 1] & 255) << 16) |
				    ((outBuf[here + 2] & 255) << 8) |
				    (outBuf[here + 3] & 255);
				patch32(here, v + val);
				/* Ein Langwort in den Daten, das auf Code oder
				   Daten zeigt, muss der Lader noch anfassen.
				   Bei einem absoluten (equ-) Symbol nicht. */
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

/* Die Globalen aller psects eintragen, mit der Basis ihres Abschnitts.
   Typwoerter nach Handbuch Kap. 6: 0 uninit. Daten, 1 init. Daten,
   4 Code, 6 equ (5 = set und $0100/$0102 = Common kommen im Korpus nicht
   vor und werden abgelehnt). */
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
	isDesc = ((rTyLan[0] >> 8) & 255) == 15;
	isDrvr = ((rTyLan[0] >> 8) & 255) == 14;
	dataBias = 0;
	if (!isDesc && !isDrvr)
		dataBias = 0x8000;

	/* --- Datenbereich auslegen: ERST alle reservierten, dann alle
	   initialisierten Daten -- und zwar psect fuer psect in der
	   Reihenfolge der Kommandozeile (Handbuch Abb. 9-2, an zwei psects
	   nachgemessen: mvar landet auf $0c, svar auf $14). --- */
	totalUninit = 0;
	for (k = 0; k < rofN; k++) {
		bUninit[k] = totalUninit;
		totalUninit = totalUninit + rStat[k];
	}
	totalInit = 0;
	for (k = 0; k < rofN; k++) {
		bInit[k] = totalUninit + totalInit;
		totalInit = totalInit + rIDat[k];
	}

	/* --- Kopf. Die Groessenfelder werden spaeter nachgetragen. --- */
	put16(0x4AFC);                 /* M$ID    */
	put16(1);                      /* M$SysRev */
	put32(0);                      /* M$Size, spaeter */
	put32(optOwner);               /* M$Owner */
	put32(0);                      /* M$Name, spaeter */
	put16(optAccess);              /* M$Accs  */
	put16(rTyLan[0]);
	put16(rAttRev[0]);
	if (optEdition >= 0)
		put16(optEdition);
	else
		put16(rEdition[0]);
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
		if (rTrap[0] == -1)
			put32(0);      /* _mexcpt */
		else
			fatal("ein gesetzter Trap-Einsprung ist noch nicht gemessen", "");
		put32(totalUninit + totalInit);   /* _mdata */
	} else if (!isDesc) {
		put32(0);              /* M$Exec, spaeter  */
		/* Fehlt der siebte psect-Parameter, traegt r68 utrap = -1
		   ein; l68 macht daraus im Modul die 0 (gemessen). */
		if (rTrap[0] == -1)
			put32(0);
		else
			fatal("ein gesetzter Trap-Einsprung ist noch nicht gemessen", "");
		put32(totalUninit + totalInit);   /* M$Data  */
		put32(rStk[0]);                   /* M$Stack */
		put32(0);                         /* M$IData, spaeter */
		put32(0);                         /* M$IRefs, spaeter */
	}

	/* Ein GERAETEDESCRIPTOR (Typ 15) hat keine mod_exec-Erweiterung:
	   sein ROF-Code IST die Kopferweiterung und liegt unmittelbar hinter
	   dem gemeinsamen Kopf auf $30; danach folgt nur der Name und der
	   CRC -- weder IData- noch IRefs-Abschnitt. */
	if (isDesc || isDrvr) {
		if (rofN != 1)
			fatal("ein Descriptor oder Treiber aus mehreren ROFs ist noch nicht gemessen", "");
		if (rIDat[0] != 0)
			fatal("initialisierte Daten gibt es bei diesem Modultyp nicht -- l68 braucht dafuer -i", "");
		bCode[0] = outLen;
		bIDataMod[0] = 0;
		for (i = 0; i < rCod[0]; i++)
			put8(inBuf[rCodeAt[0] + i]);
		nameAt = outLen;
		n = strLen(modName);
		for (i = 0; i < n; i++)
			put8(modName[i]);
		put8(0);
		if ((outLen % 2) != 0)
			put8(0);
		addGlobals(0);
		symAdd("bname", nameAt, 6);
		symAdd("_bname", nameAt, 6);
		symAdd("btext", 0, 6);
		symAdd("_btext", 0, 6);
		symAdd("etext", outLen, 6);
		symAdd("_etext", outLen, 6);
		applyLocalRefs(0);
		applyExtRefs(0);
		if (irefCodeN != 0 || irefDataN != 0)
			fatal("ein Descriptor mit Datenzeigern ist noch nicht gemessen", "");
		if (((outLen + 3) % 2) != 0)
			put8(0);
		patch32(0x0C, nameAt);
		if (isDrvr)
			patch32(0x30, bCode[0] + rEntry[0]);
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
	for (k = 0; k < rofN; k++) {
		bCode[k] = outLen;
		for (i = 0; i < rCod[k]; i++)
			put8(inBuf[rCodeAt[k] + i]);
	}

	/* --- Initialisierte Daten: <Offset><Anzahl><Bytes> --- */
	idataAt = outLen;
	put32(totalUninit);
	put32(totalInit);
	for (k = 0; k < rofN; k++) {
		bIDataMod[k] = outLen;
		for (i = 0; i < rIDat[k]; i++)
			put8(inBuf[rIDataAt[k] + i]);
	}

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
	symAdd("end", totalUninit + totalInit, 6);
	symAdd("_enddata", totalUninit + totalInit, 6);

	for (k = 0; k < rofN; k++) {
		applyLocalRefs(k);
		applyExtRefs(k);
	}

	/* --- Zeigerlisten --- */
	irefAt = outLen;
	putIrefList(irefCode, irefCodeN);
	putIrefList(irefData, irefDataN);

	/* --- Auf gerade Gesamtgroesse auffuellen, dann der CRC. --- */
	if (((outLen + 3) % 2) != 0)
		put8(0);

	patch32(0x0C, nameAt);
	patch32(0x30, bCode[0] + rEntry[0]);
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
		if (inPathN >= QL_ROF)
			fatal("zu viele Eingabedateien (QL_ROF): ", a);
		inPath[inPathN] = a;
		inPathN++;
	}
	if (inPathN == 0 || outPath == 0)
		usage();
	if (modName[0] == 0)
		nameFromPath(outPath);

	for (i = 0; i < libN; i++)
		libScan(libPath[i]);

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

	/* Der Wurzel-psect ist der erste, und nur er hat einen Typ/Sprach-
	   Wert ungleich null (Handbuch Kap. 9). */
	if (rTyLan[0] == 0)
		fatal("die erste Eingabe hat keinen Wurzel-psect (Typ/Sprache ist 0) -- nur daraus entsteht ein Modul", "");
	for (k = 1; k < rofN; k++) {
		if (rTyLan[k] != 0)
			fatal("nur die erste Eingabe darf einen Wurzel-psect haben", "");
	}
	emit();

	fp = fopen(outPath, "wb");
	if (fp == 0)
		fatal("Ausgabe nicht schreibbar: ", outPath);
	fwrite(outBuf, 1, outLen, fp);
	fclose(fp);
	return 0;
}
