/* printf fuer qclib -- der C-Rumpf.
 *
 * Der Adapter in printf.a hat die Argumente vorher zu EINEM
 * zusammenhaengenden Feld gemacht (siehe dort, an l68/QCC nachgemessen):
 *
 *     args[0] = Formatzeichenkette
 *     args[1] = erstes variadisches Argument
 *     args[2] = zweites, ...
 *
 * Damit braucht es hier weder varargs (die QCC nicht DEFINIEREN kann)
 * noch die Adresse eines Parameters -- nur ein gewoehnliches Feld.
 *
 * Die Ausgabe geht ueber den Systemaufruf, nicht ueber eine
 * Pufferschicht: error_code _os_write(path_id, const void*, u_int32*),
 * count ist ein IN/OUT-ZEIGER, Pfad 1 ist die Standardausgabe.
 * QCC uebersetzt den Aufruf in genau die Microware-Konvention
 * (d0 = Pfad, d1 = Puffer, &count auf dem Stack).
 *
 * Quelle bleibt im QCC-Subset: keine Zeichenkettenverkettung, kein
 * tab[i][k] auf Zeigerfeldern, kein static (das ist bei QCC wirkungslos).
 */

extern int _os_write(int path, char *buf, int *count);

#define QP_BUF 256

char qp_buf[QP_BUF];            /* Sammelpuffer der laufenden Zeile */
int qp_len;                     /* belegt */

/* Den Sammelpuffer ausgeben und leeren. */
void qp_flush(void)
{
	int n;

	if (qp_len <= 0) {
		qp_len = 0;
		return;
	}
	n = qp_len;
	_os_write(1, qp_buf, &n);
	qp_len = 0;
}

void qp_putc(int c)
{
	if (qp_len >= QP_BUF)
		qp_flush();
	qp_buf[qp_len] = c;
	qp_len = qp_len + 1;
}

void qp_puts(char *s)
{
	if (s == 0) {
		qp_puts("(null)");
		return;
	}
	while (*s != 0) {
		qp_putc(*s);
		s++;
	}
}

/* Vorzeichenlos zur Basis b. Die Ziffern entstehen rueckwaerts, deshalb
   erst in ein kleines Feld und dann verkehrt herum heraus. */
void qp_num(unsigned int v, int b)
{
	char d[12];
	int i;
	int r;

	i = 0;
	if (v == 0) {
		qp_putc('0');
		return;
	}
	while (v != 0) {
		r = v % b;
		if (r < 10)
			d[i] = '0' + r;
		else
			d[i] = 'a' + (r - 10);
		v = v / b;
		i = i + 1;
	}
	while (i > 0) {
		i = i - 1;
		qp_putc(d[i]);
	}
}

void qp_int(int v)
{
	if (v < 0) {
		qp_putc('-');
		qp_num(-v, 10);
		return;
	}
	qp_num(v, 10);
}

int printf_a(int *args)
{
	char *f;
	int ai;
	int c;

	f = (char *) args[0];
	ai = 1;
	qp_len = 0;
	if (f == 0)
		return 0;
	while (*f != 0) {
		if (*f != '%') {
			qp_putc(*f);
			f++;
			continue;
		}
		f++;
		c = *f;
		if (c == 0)
			break;
		f++;
		if (c == '%') {
			qp_putc('%');
		} else if (c == 'd' || c == 'i') {
			qp_int(args[ai]);
			ai = ai + 1;
		} else if (c == 'u') {
			qp_num(args[ai], 10);
			ai = ai + 1;
		} else if (c == 'x') {
			qp_num(args[ai], 16);
			ai = ai + 1;
		} else if (c == 'c') {
			qp_putc(args[ai]);
			ai = ai + 1;
		} else if (c == 's') {
			qp_puts((char *) args[ai]);
			ai = ai + 1;
		} else {
			/* Unbekannte Angabe unveraendert durchreichen, statt
			   still etwas zu verschlucken. */
			qp_putc('%');
			qp_putc(c);
		}
	}
	qp_flush();
	return 0;
}
