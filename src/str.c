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

/* strchr findet auch das abschliessende Nullzeichen -- deshalb wird erst
   verglichen und dann auf das Ende geprueft. */
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
/* Wie strncmp, nur ohne Grenze. Verglichen wird auf unsigned char (C89). */
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
/* Haengt an und gibt das Ziel zurueck. Der Aufrufer buergt fuer den Platz --
   so steht es in C89, und eine Grenze gaebe es hier nicht zu pruefen. */
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
/* C89 GENAU: hoechstens n Zeichen, und wenn die Quelle kuerzer ist, wird mit
   Nullen AUFGEFUELLT bis n. Wenn sie nicht kuerzer ist, steht am Ende KEINE
   Null -- deshalb schreibt qcc_backend_c.cpp hinter jedem strncpy die Null
   selbst ("insP->op[OP_LEN - 1] = 0"). Wer hier die Null immer setzt, waere
   bequemer und falsch. */
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
/* Das LETZTE Vorkommen. Die abschliessende Null gehoert dazu (C89), deshalb
   laeuft die Schleife bis EINSCHLIESSLICH der Null. */
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
/* Beide stehen in string.h und deshalb hier -- mem.c ist die Speicher-
   BESCHAFFUNG (realloc), nicht die Speicherarbeit. */
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

/* Kopiert VORWAERTS. Ueberlappende Bereiche sind in C89 bei memcpy
   undefiniert (dafuer gibt es memmove); die Kette kopiert nur
   Getrenntes. Langwortweise, wenn beide Seiten und die Laenge es zulassen --
   bei den Argumentfeldern des Backends sind das wenige Byte, bei einer
   Tabellenkopie viele. */
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
/* Fuer Q9-Tools/System/grep -i (Gross-/Kleinschreibung ignorieren, s.
   ctype.h). Nur der lateinische ASCII-Bereich -- reicht fuer die Kette, wie
   ueberall sonst kein Vorratsbau. Alles ausserhalb A-Z kommt unveraendert
   zurueck, wie es C89 fuer tolower() vorschreibt. */
int qs_lower(int *a)
{
	int c;

	c = a[0];
	if (c >= 'A' && c <= 'Z')
		return c + 32;
	return c;
}
