/* strlen, strchr, strncmp fuer qclib -- die C-Rumpfe.
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
