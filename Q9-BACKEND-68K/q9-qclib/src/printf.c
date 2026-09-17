/*
 * q9-qclib formatted output
 *
 * Purpose:
 *   Implements printf-family formatting for the Q9 68k runtime without
 *   relying on compiler varargs support.
 *
 * Edition history:
 *   2026-09-11  Introduced the English source-header format.
 */
/* printf, fprintf, and sprintf implementation for qclib.
 *
 * The printf.a adapter packs arguments into one contiguous field:
 *
 *     args[0] = erstes Argument, args[1] = zweites, ...
 *
 * This avoids varargs and parameter-address handling in the bootstrap subset.
 * The format string is args[0] for printf and args[1] for fprintf/sprintf.
 *
 * The output sink is global state: qp_sink selects buffered OS-9 output via
 * _os_write or direct output to a string buffer. This keeps the call surface
 * compatible with the restricted compiler subset.
 *
 * _os_write(path, buffer, &count) uses path 1 for standard output and path 2
 * for diagnostics. QCC emits the Microware calling convention directly.
 *
 * Format characters are represented as numeric constants rather than
 * character literals to keep the generated source within the bootstrap subset.
 *
 * The source remains within the QCC subset: no string concatenation, nested
 * indexing through pointer fields, or function-local static storage. */

extern int _os_write(int path, char *buf, int *count);

#define QP_BUF 256

int qp_sink;                    /* 0 = OS-9 path, 1 = string buffer. */
int qp_path;                    /* Destination path when qp_sink == 0. */
char *qp_dst;                   /* Write position when qp_sink == 1. */
int qp_cnt;                     /* Emitted characters; the return value. */

char qp_buf[QP_BUF];            /* Buffer for pending output. */
int qp_len;                     /* Bytes currently used. */

/* Flush and clear the output buffer. A string sink has nothing to flush;
   qp_putc writes directly to its destination. */
/* Function: qp_flush
 * Flushes the active output sink.
 * Parameters: None.
 * Returns: Nothing. */
void qp_flush(void)
{
	int n;

	if (qp_sink != 0)
		return;
	if (qp_len <= 0) {
		qp_len = 0;
		return;
	}
	if (qp_path < 0) {
		/* Invalid path (closed file): discard bytes rather than writing to
		   an unrelated path. */
		qp_len = 0;
		return;
	}
	n = qp_len;
	_os_write(qp_path, qp_buf, &n);
	qp_len = 0;
}

/* Function: qp_putc
 * Sends one character to the active output sink.
 * Parameters: c Character value.
 * Returns: Nothing. */
void qp_putc(int c)
{
	qp_cnt = qp_cnt + 1;
	if (qp_sink != 0) {
		*qp_dst = c;
		qp_dst++;
		return;
	}
	if (qp_len >= QP_BUF)
		qp_flush();
	qp_buf[qp_len] = c;
	qp_len = qp_len + 1;
}

/* Emit at most prec characters of a string; prec < 0 means unlimited. This
   handles both %s and %.*s. */
/* Function: qp_putn
 * Emits a bounded string segment.
 * Parameters: s String; prec Maximum character count.
 * Returns: Nothing. */
void qp_putn(char *s, int prec)
{
	int i;

	if (s == 0) {
		qp_putn("(null)", -1);
		return;
	}
	i = 0;
	while (s[i] != 0) {
		if (prec >= 0 && i >= prec)
			return;
		qp_putc(s[i]);
		i++;
	}
}

/* Emit an unsigned value in base b. Digits are generated backwards, so
   store them in a small buffer and output them in reverse order. */
/* Function: qp_num
 * Emits an unsigned integer in the requested base.
 * Parameters: v Value; b Base.
 * Returns: Nothing. */
void qp_num(unsigned int v, int b)
{
	char d[12];
	int i;
	int r;

	i = 0;
	if (v == 0) {
		qp_putc(48);                    /* 0 */
		return;
	}
	while (v != 0) {
		r = v % b;
		if (r < 10)
			d[i] = 48 + r;          /* 0 */
		else
			d[i] = 97 + (r - 10);   /* a */
		v = v / b;
		i = i + 1;
	}
	while (i > 0) {
		i = i - 1;
		qp_putc(d[i]);
	}
}

/* The smallest int has no positive counterpart: -v keeps the same bit
   pattern. This still works because qp_num reads its argument unsigned;
   $80000000 becomes 2147483648. */
/* Function: qp_int
 * Emits a signed decimal integer.
 * Parameters: v Value.
 * Returns: Nothing. */
void qp_int(int v)
{
	if (v < 0) {
		qp_putc(45);                    /* - */
		qp_num(-v, 10);
		return;
	}
	qp_num(v, 10);
}

/* IEEE-754 binary64 nach Dezimaltext, fuer printf("%f") (2026-09-17).
 *
 * Die Umkehrung von tools/dec2ieee.c: dort Text->Bitmuster mit
 * Ganzzahl-Bignum (kein Gleitkomma waehrend der Selbstuebersetzung
 * verfuegbar), hier Bitmuster->Text aus demselben Grund. MUSS in dieser
 * Datei bleiben, nicht in einer eigenen: ein Aufruf ueber
 * Uebersetzungseinheiten hinweg findet sein Ziel nicht (s. qp_pathof
 * weiter unten -- dieselbe Einschraenkung, dort dokumentiert).
 *
 * %f verlangt eine FESTE Anzahl Nachkommastellen (hier sechs, der
 * C89-Standardwert) statt einer kuerzesten rundtrip-Darstellung wie %g --
 * ein einfacheres Problem: gesucht ist die Ganzzahl round(wert * 10^6).
 *
 * wert = M * 2^E, M ganz (53 Bit inkl. implizitem Bit fuer normale Zahlen,
 * 52 Bit ohne fuer subnormale), E aus [-1074, 971] (binary64-Grenzen).
 *
 *   E >= 0: M * 2^E * 10^6 ist eine EXAKTE Ganzzahl -- keine Rundung noetig.
 *   E <  0: M * 10^6 muss durch 2^(-E) geteilt werden. Der Teiler ist eine
 *           Zweierpotenz, also genuegt ein Rechts-Schub mit Pruefung des
 *           herausfallenden hoechsten Bits (round half up) -- keine
 *           allgemeine Bignum-Division noetig.
 *
 * Puffer wie in dec2ieee.c: 260 Glieder zu 16 Bit (4160 Bit) sind fuer den
 * binary64-Bereich weit mehr als noetig, aber derselbe Wert spart eine
 * zweite Abschaetzung. Das Zwischenergebnis liegt wie dort auf
 * DATEIEBENE, nicht auf dem Stack -- gut ein Kilobyte, und der Stack
 * eines OS-9-Moduls ist knapp. */

typedef struct {
	int n;
	unsigned long d[260];
} IBig;

static IBig i2dM;

static void ibigZero(IBig *a)
{
	a->n = 0;
}

static void ibigTrim(IBig *a)
{
	while (a->n > 0 && a->d[a->n - 1] == 0UL)
		a->n--;
}

static int ibigIsZero(const IBig *a)
{
	return a->n == 0;
}

static void ibigSetSmall(IBig *a, unsigned long v)
{
	ibigZero(a);
	while (v != 0UL && a->n < 260) {
		a->d[a->n] = v & 0xFFFFUL;
		a->n = a->n + 1;
		v = v >> 16;
	}
}

/* a = a * m + add (m, add < 2^16). */
static int ibigMulAddSmall(IBig *a, unsigned long m, unsigned long add)
{
	unsigned long carry;
	int i;

	carry = add;
	for (i = 0; i < a->n; i++) {
		unsigned long t;

		t = a->d[i] * m + carry;
		a->d[i] = t & 0xFFFFUL;
		carry = t >> 16;
	}
	while (carry != 0UL) {
		if (a->n >= 260)
			return 0;
		a->d[a->n] = carry & 0xFFFFUL;
		a->n = a->n + 1;
		carry = carry >> 16;
	}
	return 1;
}

static int ibigShiftLeft(IBig *a, long bits)
{
	long limbShift;
	int bitShift;
	long i;

	limbShift = bits / 16L;
	bitShift = (int) (bits % 16L);
	if (ibigIsZero(a) || bits <= 0L)
		return 1;
	if (a->n + limbShift + 1L > 260L)
		return 0;
	if (limbShift > 0L) {
		for (i = (long) a->n - 1L; i >= 0L; i--)
			a->d[i + limbShift] = a->d[i];
		for (i = 0L; i < limbShift; i++)
			a->d[i] = 0UL;
		a->n = a->n + (int) limbShift;
	}
	if (bitShift > 0) {
		unsigned long carry;

		carry = 0UL;
		for (i = limbShift; i < (long) a->n; i++) {
			unsigned long t;

			t = (a->d[i] << bitShift) | carry;
			a->d[i] = t & 0xFFFFUL;
			carry = t >> 16;
		}
		if (carry != 0UL) {
			if (a->n >= 260)
				return 0;
			a->d[a->n] = carry;
			a->n = a->n + 1;
		}
	}
	return 1;
}

/* Bit an Position `bit` (0 = niedrigstwertig), 0 ausserhalb des Bereichs. */
static int ibigGetBit(const IBig *a, long bit)
{
	long limb;

	limb = bit / 16L;
	if (bit < 0L || limb >= (long) a->n)
		return 0;
	return (int) ((a->d[limb] >> (bit % 16L)) & 1UL);
}

/* a >>= bits, OHNE Rundung -- die aufrufende Seite hat das herausfallende
   Bit vorher selbst mit ibigGetBit gelesen. */
static void ibigShiftRight(IBig *a, long bits)
{
	long limbShift;
	int bitShift;
	long i;

	limbShift = bits / 16L;
	bitShift = (int) (bits % 16L);
	if (ibigIsZero(a) || bits <= 0L)
		return;
	if (limbShift >= (long) a->n) {
		ibigZero(a);
		return;
	}
	if (limbShift > 0L) {
		for (i = 0L; i + limbShift < (long) a->n; i++)
			a->d[i] = a->d[i + limbShift];
		a->n = a->n - (int) limbShift;
	}
	if (bitShift > 0) {
		for (i = 0L; i < (long) a->n; i++) {
			unsigned long lo;
			unsigned long hiw;

			lo = a->d[i] >> bitShift;
			hiw = (i + 1L < (long) a->n) ? a->d[i + 1L] : 0UL;
			a->d[i] = (lo | (hiw << (16 - bitShift))) & 0xFFFFUL;
		}
	}
	ibigTrim(a);
}

/* Division durch einen KLEINEN Teiler (<= 65535), von den hoechstwertigen
   Gliedern her -- ein Durchlauf statt bitweiser Langdivision. Nur fuer die
   Dezimalausgabe gebraucht (Teiler 10), NICHT fuer die eigentliche
   Rundung: die laeuft ueber ibigShiftRight, weil binary64s Teiler dort
   immer eine Zweierpotenz ist. */
static unsigned long ibigDivSmall(IBig *a, unsigned long dv)
{
	unsigned long rem;
	int i;

	rem = 0UL;
	for (i = a->n - 1; i >= 0; i--) {
		unsigned long cur;

		cur = (rem << 16) | a->d[i];
		a->d[i] = cur / dv;
		rem = cur % dv;
	}
	ibigTrim(a);
	return rem;
}

/* Baut M (bis zu 53 Bit) direkt aus den Bitfeldern auf -- vier
   16-Bit-Ziffern in Basis 65536, hoechstwertige zuerst (dasselbe Muster,
   das ibigMulAddSmall fuer Dezimalliterale in dec2ieee.c benutzt, nur mit
   Basis 65536 statt 10000). */
static void ibigSetMantissa(IBig *m, unsigned long hi, unsigned long lo, int implicit)
{
	unsigned long mhi20;

	mhi20 = hi & 0xFFFFFUL;                 /* Mantissenbits 32..51 */
	ibigSetSmall(m, (unsigned long) ((implicit << 4) | ((mhi20 >> 16) & 0xFUL)));
	ibigMulAddSmall(m, 65536UL, mhi20 & 0xFFFFUL);
	ibigMulAddSmall(m, 65536UL, (lo >> 16) & 0xFFFFUL);
	ibigMulAddSmall(m, 65536UL, lo & 0xFFFFUL);
}

/* Function: qp_double
 * Formatiert ein binary64-Bitmuster als Dezimaltext mit sechs
 * Nachkommastellen (printf("%f", x) und verwandte Formen).
 * Parameters: hi Obere 32 Bit (Vorzeichen, Exponent, oberste Mantissenbits);
 *             lo Untere 32 Bit der Mantisse.
 * Returns: Nothing. */
void qp_double(unsigned long hi, unsigned long lo)
{
	int sign;
	int biased;
	long e;
	char digits[340];
	int ndig;
	unsigned long r;

	sign = (int) ((hi >> 31) & 1UL);
	biased = (int) ((hi >> 20) & 0x7FFUL);

	if (biased == 0x7FF) {
		/* Unendlich oder NaN -- in den 4109 Microware-Quellen und im
		   eigenen Bootstrap kommt das nicht vor, aber ein still
		   falscher Zahlenmuell waere schlimmer als diese Meldung. */
		if (sign)
			qp_putc(45);            /* - */
		if ((hi & 0xFFFFFUL) == 0UL && lo == 0UL)
			qp_putn("inf", -1);
		else
			qp_putn("nan", -1);
		return;
	}

	if (sign)
		qp_putc(45);                    /* - */

	if (biased == 0 && (hi & 0xFFFFFUL) == 0UL && lo == 0UL) {
		/* Null (auch -0.0: das Vorzeichen wurde oben schon gedruckt). */
		qp_putn("0.000000", -1);
		return;
	}

	ibigSetMantissa(&i2dM, hi, lo, biased != 0);
	/* e ist der Zweierexponent von M: wert = M * 2^e. 52 Bruchbits sind
	   in M schon als Ganzzahl aufgenommen, deshalb minus 52. Subnormale
	   Zahlen haben KEINEN Bias-Ausgleich (biased ist 0, der wirkliche
	   Exponent ist wie bei der kleinsten normalen Zahl: 1-1023). */
	e = (biased != 0 ? (long) biased - 1023L : 1L - 1023L) - 52L;

	if (e >= 0L) {
		/* M * 2^e * 10^6 ist exakt -- keine Rundung. */
		if (!ibigShiftLeft(&i2dM, e)) {
			qp_putn("ovfl", -1);
			return;
		}
		/* *1000000 in einem Schritt wuerde d[i]*m bei vollen 16-Bit-
		   Limbs bis zu 36 Bit brauchen -- unsigned long ist hier 32 Bit,
		   der Uebertrag ginge verloren. 100^3 = 1000000, und 65535*100
		   passt sicher in 32 Bit, daher dreimal mit dem kleinen Faktor. */
		ibigMulAddSmall(&i2dM, 100UL, 0UL);
		ibigMulAddSmall(&i2dM, 100UL, 0UL);
		ibigMulAddSmall(&i2dM, 100UL, 0UL);
	} else {
		int roundUp;

		/* *1000000 in einem Schritt wuerde d[i]*m bei vollen 16-Bit-
		   Limbs bis zu 36 Bit brauchen -- unsigned long ist hier 32 Bit,
		   der Uebertrag ginge verloren. 100^3 = 1000000, und 65535*100
		   passt sicher in 32 Bit, daher dreimal mit dem kleinen Faktor. */
		ibigMulAddSmall(&i2dM, 100UL, 0UL);
		ibigMulAddSmall(&i2dM, 100UL, 0UL);
		ibigMulAddSmall(&i2dM, 100UL, 0UL);
		/* Teilen durch 2^(-e): das herausfallende hoechste Bit
		   entscheidet ueber Aufrunden (round half up -- C89 schreibt
		   fuer %f keine bestimmte Rundungsrichtung vor, und schon die
		   Bitmuster selbst sind beim Einlesen mit round-to-even
		   entstanden, s. dec2ieee.c). */
		roundUp = ibigGetBit(&i2dM, -e - 1L);
		ibigShiftRight(&i2dM, -e);
		if (roundUp)
			ibigMulAddSmall(&i2dM, 1UL, 1UL);
	}

	/* Dezimalziffern rueckwaerts einsammeln (wie qp_num), mindestens
	   sieben (eine Stelle vor, sechs hinter dem Komma) -- kuerzere Werte
	   werden links mit Nullen aufgefuellt. */
	ndig = 0;
	while (!ibigIsZero(&i2dM) && ndig < 340) {
		r = ibigDivSmall(&i2dM, 10UL);
		digits[ndig] = (char) (48 + r);
		ndig = ndig + 1;
	}
	while (ndig < 7) {
		digits[ndig] = 48;
		ndig = ndig + 1;
	}
	while (ndig > 0) {
		ndig = ndig - 1;
		if (ndig == 5)
			qp_putc(46);             /* . */
		qp_putc(digits[ndig]);
	}
}

/* Shared formatter. fi is the format-string index; variadic arguments start
 * at fi+1.
 * at fi+1.
 *
 * Supported forms are %d %i %u %x %c %s %%, length modifier l, and precision
 * .number or .*; no other forms occur in the toolchain.
 * (counted in the QCC bootstrap source: 203 %d, 155 %s, 67 %c,
 * 30 %.*s, 7 %ld). The l length modifier is ignored deliberately: on 68k,
 * int and long are both 32 bits, so %ld and %d are identical.
 *
 * Everything else is passed through UNCHANGED; a width such as
 * %20s therefore remains text instead of being silently misaligned. A
 * precision on a number (%.3d, the C89 minimum digit count) also deliberately
 * follows this branch instead of disappearing silently.
 */
/* Function: qp_run
 * Interprets one format string and its argument frame.
 * Parameters: args Runtime arguments; fi Format-string index.
 * Returns: Number of emitted characters, or a negative format error. */
int qp_run(int *args, int fi)
{
	char *f;
	char *spec;
	int ai;
	int c;
	int prec;
	int firstArg;

	f = (char *) args[fi];
	ai = fi + 1;
	/* WEICHE FUER double-ARGUMENTE (2026-09-17): printf.a kopiert d0/d1/
	   Stack MECHANISCH in dieses Feld -- es weiss nichts von Typen. Fuer
	   ALLE Argumente ausser dem ERSTEN ist das genug: sobald ein Argument
	   nicht mehr ins Register passt (egal welcher Typ), spillen laut dem
	   gemessenen Byte-Offset-Modell (docs/FLOAT_PLAN_de.md) auch alle
	   folgenden -- ab dann liegt einfach alles hintereinander auf dem
	   Stack, ein double belegt darin zwei aufeinanderfolgende args[]-
	   Zellen (hi, lo) statt einer. NUR das ERSTE variadische Argument ist
	   ein Sonderfall: bei einem "normalen" (4-Byte-)Wert liegt es in d1
	   (args[fi+1], die bisherige Annahme), aber bei einem double passt es
	   NICHT mehr neben das Formatstring-fmt in d0 -- es geht KOMPLETT auf
	   den Stack, und d1 bleibt unbenutzter Muell. Die beiden Haelften
	   liegen dann an der Stelle, wo mechanisch ohnehin schon der Stack-
	   Anteil landet: args[fi+2]/args[fi+3] (fi+1 bleibt der ungenutzte
	   d1-Platz und wird einfach uebersprungen). */
	firstArg = 1;
	qp_cnt = 0;
	qp_len = 0;
	if (f == 0)
		return 0;
	while (*f != 0) {
		if (*f != 37) {                 /* % */
			qp_putc(*f);
			f++;
			continue;
		}
		spec = f;                       /* points to the percent sign */
		f++;
		prec = -1;
		while (*f != 0) {
			if (*f == 108) {        /* l */
				f++;
				continue;
			}
			if (*f == 46) {         /* . */
				f++;
				if (*f == 42) {                 /* * */
					prec = args[ai];
					ai = ai + 1;
					firstArg = 0;
					f++;
				} else {
					prec = 0;
					while (*f >= 48 && *f <= 57) {
						prec = prec * 10 + (*f - 48);
						f++;
					}
				}
				continue;
			}
			break;
		}
		c = *f;
		if (c == 0)
			break;
		f++;
		if (prec >= 0 && c != 115) {
			/* Precision on anything other than %s: pass it through unchanged. */
			while (spec < f) {
				qp_putc(*spec);
				spec++;
			}
		} else if (c == 37) {                   /* % */
			qp_putc(37);
		} else if (c == 100 || c == 105) {      /* d i */
			qp_int(args[ai]);
			ai = ai + 1;
			firstArg = 0;
		} else if (c == 117) {                  /* u */
			qp_num(args[ai], 10);
			ai = ai + 1;
			firstArg = 0;
		} else if (c == 120) {                  /* x */
			qp_num(args[ai], 16);
			ai = ai + 1;
			firstArg = 0;
		} else if (c == 99) {                   /* c */
			qp_putc(args[ai]);
			ai = ai + 1;
			firstArg = 0;
		} else if (c == 115) {                  /* s */
			qp_putn((char *) args[ai], prec);
			ai = ai + 1;
			firstArg = 0;
		} else if (c == 102 || c == 70) {       /* f F -- s.o., 2026-09-17 */
			if (firstArg) {
				qp_double((unsigned long) args[ai + 1], (unsigned long) args[ai + 2]);
				ai = ai + 3;
			} else {
				qp_double((unsigned long) args[ai], (unsigned long) args[ai + 1]);
				ai = ai + 2;
			}
			firstArg = 0;
		} else {
			/* Pass unknown specifications through unchanged instead of
			   silently dropping them. */
			while (spec < f) {
				qp_putc(*spec);
				spec++;
			}
		}
	}
	qp_flush();
	return qp_cnt;
}

/* Path number represented by a FILE*.
 *
 * A FILE* is the address of a table entry containing the path number (see
 * file.c). There are two special cases:
 *
 * - FILE* == 0 maps to path 2, the error channel. QCC's bootstrap source
 *   declares stderr as an unassigned pointer and calls fprintf(stderr, ...),
 *   which is otherwise undefined against Microware's clib. Diagnostics are
 *   at least readable on path 2.
 * - An entry containing 0 represents a CLOSED file. It returns -1, and
 *   qp_flush discards the bytes instead of writing to path 0.
 *
 * The same logic exists in file.c and is intentionally duplicated: QCC
 * names definitions "tc_<name>", while a call searches for the raw name, so
 * a call across translation units cannot find its target. A shared version
 * would have to be written in assembly and would be longer than this duplicate.
 */
/* Function: qp_pathof
 * Resolves a FILE handle to an OS-9 path number.
 * Parameters: fp FILE handle value.
 * Returns: OS-9 path number. */
int qp_pathof(int fp)
{
	int *slot;

	if (fp == 0)
		return 2;
	slot = (int *) fp;
	if (*slot == 0)
		return -1;
	return *slot;
}

/* Function: printf_a
 * Formats output to the standard output stream.
 * Parameters: args Runtime argument frame.
 * Returns: Formatted character count. */
int printf_a(int *args)
{
	qp_sink = 0;
	qp_path = 1;
	return qp_run(args, 0);
}

/* Never write to a closed file, and report the failure: C89 requires a
   negative return value on error. Without this check fprintf would report
   a character count while qp_flush discarded the bytes. */
/* Function: fprintf_a
 * Formats output to a selected FILE stream.
 * Parameters: args Runtime argument frame.
 * Returns: Formatted character count. */
int fprintf_a(int *args)
{
	int p;

	p = qp_pathof(args[0]);
	if (p < 0)
		return -1;
	qp_sink = 0;
	qp_path = p;
	return qp_run(args, 1);
}

/* sprintf appends the terminating NUL, which is excluded from the return
   value as required by C89. */
/* Function: sprintf_a
 * Formats output into a caller-provided character buffer.
 * Parameters: args Runtime argument frame.
 * Returns: Formatted character count. */
int sprintf_a(int *args)
{
	int n;

	qp_dst = (char *) args[0];
	if (qp_dst == 0)
		return 0;
	qp_sink = 1;
	n = qp_run(args, 1);
	*qp_dst = 0;
	qp_sink = 0;
	return n;
}
