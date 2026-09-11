/*
 * q9-qclib string and memory functions
 *
 * Purpose:
 *   Implements the C string and byte-memory operations required by the Q9
 *   compiler tools on the 68k target.
 *
 * Edition history:
 *   2026-09-11  Introduced the English source-header format.
 */
/* Die Stringfunktionen von qclib -- die C-Rumpfe.
 *
 * strlen, strchr, strncmp brauchte QCCs erzeugter Parser; strcmp, strcat,
 * strncpy, strrchr, strtok, strtol, memset und memcpy kamen 2026-09-07 fuer
 * qcc_backend_c.cpp dazu (dort nachgezaehlt: strcmp 154-mal, strncpy 9-mal,
 * memset 6-mal, strtok und strrchr je zweimal, strcat, memcpy und strtol je
 * einmal). Nichts davon ist Vorratsbau -- jede Funktion hat einen Aufrufer
 * in der Kette.
 *
 * Bauform wie bei den anderen Einheiten: der Adapter in str.a hat die
 * Argumente vorher zu EINEM zusammenhaengenden Feld gemacht, der Rumpf
 * liest a[0], a[1], ... (siehe file.a fuer die Begruendung).
 *
 * VERGLICHEN WIRD AUF unsigned char, so schreibt es C89 fuer strncmp und
 * strchr vor: "as if converted to unsigned char". Auf dem 68k ist char
 * vorzeichenBEHAFTET, deshalb steht hier ueberall "& 255" -- ohne das
 * waere ein Byte ab $80 kleiner als jedes ASCII-Zeichen. Der erzeugte
 * Parser vergleicht mit strncmp Quelltext, in dem solche Bytes vorkommen
 * koennen.
 *
 * KEINE Nullzeigerpruefung: strlen(0) und strncmp(0,...) sind in C89
 * undefiniert, und ein stillschweigendes "0" waere hier das Schlimmste,
 * was passieren kann -- bei strncmp heisst 0 "gleich", der Parser wuerde
 * also ein Schluesselwort erkennen, wo keines steht. Ein Zugriff auf 0
 * faellt auf dem 68030 dagegen laut als PMMU-Fehler auf.
 *
 * Quelle im QCC-Subset: keine Zeichenkettenverkettung, kein
 * tab[i][k] auf Zeigerfeldern, kein static.
 */

/* Function: qs_len
 * Computes the length of a NUL-terminated string.
 * Parameters: a Runtime argument frame containing the string.
 * Returns: String length. */
int qs_len(int *a)
{
	char *s;
	int n;

	s = (char *) a[0];
	n = 0;
	while (s[n] != 0)
		n++;
	return n;
}

/* strchr also matches the terminating NUL, so compare first and check for
   the end afterwards. */
/* Function: qs_chr
 * Finds the first occurrence of a byte in a string.
 * Parameters: a Runtime argument frame containing string and byte.
 * Returns: Matching address, or null. */
char *qs_chr(int *a)
{
	char *s;
	int c;

	s = (char *) a[0];
	c = a[1] & 255;
	while (1) {
		if ((*s & 255) == c)
			return s;
		if (*s == 0)
			return 0;
		s++;
	}
}

/* Function: qs_ncmp
 * Compares at most n bytes of two strings.
 * Parameters: a Runtime argument frame containing both strings and n.
 * Returns: Negative, zero or positive comparison result. */
int qs_ncmp(int *a)
{
	char *x;
	char *y;
	int n;
	int i;
	int cx;
	int cy;

	x = (char *) a[0];
	y = (char *) a[1];
	n = a[2];
	i = 0;
	while (i < n) {
		cx = x[i] & 255;
		cy = y[i] & 255;
		if (cx != cy) {
			if (cx < cy)
				return -1;
			return 1;
		}
		if (cx == 0)
			return 0;
		i++;
	}
	return 0;
}

/* ---------------------------------------------------------------- strcmp */
/* Like strncmp, but without a length limit. Compare as unsigned char (C89). */
/* Function: qs_cmp
 * Compares two NUL-terminated strings.
 * Parameters: a Runtime argument frame containing both strings.
 * Returns: Negative, zero or positive comparison result. */
int qs_cmp(int *a)
{
	char *x;
	char *y;
	int i;
	int cx;
	int cy;

	x = (char *) a[0];
	y = (char *) a[1];
	i = 0;
	while (1) {
		cx = x[i] & 255;
		cy = y[i] & 255;
		if (cx != cy) {
			if (cx < cy)
				return -1;
			return 1;
		}
		if (cx == 0)
			return 0;
		i++;
	}
}

/* ---------------------------------------------------------------- strcat */
/* Append to the destination and return it. The caller provides sufficient
   space, as required by C89; no bound can be checked here. */
/* Function: qs_cat
 * Appends one string to another.
 * Parameters: a Runtime argument frame containing destination and source.
 * Returns: Destination string address. */
char *qs_cat(int *a)
{
	char *dst;
	char *src;
	int n;
	int i;

	dst = (char *) a[0];
	src = (char *) a[1];
	n = 0;
	while (dst[n] != 0)
		n++;
	i = 0;
	while (src[i] != 0) {
		dst[n + i] = src[i];
		i++;
	}
	dst[n + i] = 0;
	return dst;
}

/* --------------------------------------------------------------- strncpy */
/* Exact C89 behavior: copy at most n bytes and pad with NULs when the source
   is shorter. If the source is at least n bytes long, no terminating NUL is
   written; callers that need one must add it themselves. */
/* Function: qs_ncpy
 * Copies at most n bytes between strings.
 * Parameters: a Runtime argument frame containing destination, source and n.
 * Returns: Destination string address. */
char *qs_ncpy(int *a)
{
	char *dst;
	char *src;
	int n;
	int i;

	dst = (char *) a[0];
	src = (char *) a[1];
	n = a[2];
	i = 0;
	while (i < n && src[i] != 0) {
		dst[i] = src[i];
		i++;
	}
	while (i < n) {
		dst[i] = 0;
		i++;
	}
	return dst;
}

/* --------------------------------------------------------------- strrchr */
/* Find the last occurrence. The terminating NUL is included (C89), so the
   scan continues through that byte. */
/* Function: qs_rchr
 * Finds the last occurrence of a byte in a string.
 * Parameters: a Runtime argument frame containing string and byte.
 * Returns: Matching address, or null. */
char *qs_rchr(int *a)
{
	char *s;
	int c;
	char *fund;
	int i;

	s = (char *) a[0];
	c = a[1] & 255;
	fund = 0;
	i = 0;
	while (1) {
		if ((s[i] & 255) == c)
			fund = s + i;
		if (s[i] == 0)
			return fund;
		i++;
	}
}

/* ---------------------------------------------------------------- strtok */
/* Der Zustand zwischen zwei Aufrufen: die Stelle HINTER dem letzten Fund.
   strtok ist damit nicht wiedereintrittsfaehig -- so ist die Funktion in C89
   definiert, das ist keine Einschraenkung dieser Fassung. */
char *qs_tokp;

/* Steht c in der Trennerliste? */
int qs_istrenner(char *delim, int c)
{
	int i;

	i = 0;
	while (delim[i] != 0) {
		if ((delim[i] & 255) == c)
			return 1;
		i++;
	}
	return 0;
}

/* Function: qs_tok
 * Splits a string into delimiter-separated tokens.
 * Parameters: a Runtime argument frame containing string and delimiters.
 * Returns: Next token address, or null. */
char *qs_tok(int *a)
{
	char *s;
	char *delim;
	char *anfang;

	s = (char *) a[0];
	delim = (char *) a[1];
	if (s != 0)
		qs_tokp = s;            /* neuer Durchlauf */
	if (qs_tokp == 0)
		return 0;               /* Fortsetzung nach dem Ende */

	while (*qs_tokp != 0 && qs_istrenner(delim, *qs_tokp & 255))
		qs_tokp++;              /* fuehrende Trenner weg */
	if (*qs_tokp == 0) {
		qs_tokp = 0;
		return 0;
	}
	anfang = qs_tokp;
	while (*qs_tokp != 0 && !qs_istrenner(delim, *qs_tokp & 255))
		qs_tokp++;
	if (*qs_tokp != 0) {
		*qs_tokp = 0;           /* das Feld abschliessen -- strtok SCHREIBT */
		qs_tokp++;
	} else {
		qs_tokp = 0;            /* am Ende angekommen */
	}
	return anfang;
}

/* ---------------------------------------------------------------- strtol */
/* Basis 2..36 und 0 (dann entscheidet das Praefix: 0x -> 16, 0 -> 8, sonst
   10), fuehrender Leerraum und ein Vorzeichen werden ueberlesen. end zeigt
   danach auf das erste nicht verbrauchte Zeichen; ohne eine einzige Ziffer
   auf den ANFANG -- so verlangt es C89, und qcc_backend_c.cpp prueft genau
   das ("*end != 0" heisst: keine Zahl).
 *
 * UEBERLAUF wird GEKAPPT (auf 2147483647 bzw. -2147483648) und die Ziffern
 * werden weiter verbraucht, damit end richtig steht. C89 will zusaetzlich
 * errno = ERANGE; qclib hat kein errno, und das ist hier ausgeschrieben statt
 * verschwiegen. */
/* Function: qs_tol
 * Converts a decimal string to a signed long value.
 * Parameters: a Runtime argument frame containing string, end pointer and base.
 * Returns: Converted value. */
int qs_tol(int *a)
{
	char *s;
	char **endp;
	int base;
	char *p;
	int neg;
	int wert;
	int ziffern;
	int ueber;
	int c;
	int d;

	s = (char *) a[0];
	endp = (char **) a[1];
	base = a[2];
	if (s == 0) {
		if (endp != 0)
			*endp = 0;
		return 0;
	}
	p = s;
	while (*p == 32 || (*p >= 9 && *p <= 13))
		p++;
	neg = 0;
	if (*p == '+') {
		p++;
	} else if (*p == '-') {
		neg = 1;
		p++;
	}
	if (base == 0) {
		if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
			base = 16;
			p = p + 2;
		} else if (*p == '0') {
			base = 8;
		} else {
			base = 10;
		}
	} else if (base == 16) {
		if (*p == '0' && (p[1] == 'x' || p[1] == 'X'))
			p = p + 2;
	}
	wert = 0;
	ziffern = 0;
	ueber = 0;
	while (*p != 0) {
		c = *p & 255;
		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'z')
			d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'Z')
			d = c - 'A' + 10;
		else
			break;
		if (d >= base)
			break;
		if (wert > (2147483647 - d) / base)
			ueber = 1;
		else
			wert = wert * base + d;
		ziffern++;
		p++;
	}
	if (ziffern == 0) {
		if (endp != 0)
			*endp = s;
		return 0;
	}
	if (endp != 0)
		*endp = p;
	if (ueber != 0) {
		if (neg != 0)
			return -2147483647 - 1;
		return 2147483647;
	}
	if (neg != 0)
		return -wert;
	return wert;
}

/* ------------------------------------------------------- memset / memcpy */
/* Both functions are declared in string.h and belong here; mem.c handles
   allocation (realloc), not byte operations. */
/* Function: qs_set
 * Fills a byte range with one value.
 * Parameters: a Runtime argument frame containing destination, value and n.
 * Returns: Destination address. */
char *qs_set(int *a)
{
	char *d;
	int c;
	int n;
	int i;

	d = (char *) a[0];
	c = a[1] & 255;
	n = a[2];
	i = 0;
	while (i < n) {
		d[i] = c;
		i++;
	}
	return d;
}

/* Copy forward. Overlapping regions are undefined for memcpy in C89 (use
   memmove instead); this toolchain copies only disjoint ranges. Use longword
   copies when both addresses and the length permit it. */
/* Function: qs_cpy
 * Copies a byte range from source to destination.
 * Parameters: a Runtime argument frame containing destination, source and n.
 * Returns: Destination address. */
char *qs_cpy(int *a)
{
	char *d;
	char *q;
	int n;
	int i;
	int *ld;
	int *lq;

	d = (char *) a[0];
	q = (char *) a[1];
	n = a[2];
	if ((((int) d) & 3) == 0 && (((int) q) & 3) == 0) {
		ld = (int *) d;
		lq = (int *) q;
		i = 0;
		while (i < n / 4) {
			ld[i] = lq[i];
			i++;
		}
		i = (n / 4) * 4;
	} else {
		i = 0;
	}
	while (i < n) {
		d[i] = q[i];
		i++;
	}
	return d;
}

/* --------------------------------------------------------------- tolower */
/* Used by Q9 tools/system/grep -i. Only the ASCII Latin range is needed by
   the toolchain; bytes outside A-Z are returned unchanged as required by
   C89 tolower(). */
/* Function: qs_lower
 * Converts one ASCII uppercase byte to lowercase.
 * Parameters: a Runtime argument frame containing the byte.
 * Returns: Converted byte. */
int qs_lower(int *a)
{
	int c;

	c = a[0];
	if (c >= 'A' && c <= 'Z')
		return c + 32;
	return c;
}
