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
/* printf, fprintf und sprintf fuer qclib -- der C-Rumpf.
 *
 * Der Adapter in printf.a hat die Argumente vorher zu EINEM
 * zusammenhaengenden Feld gemacht (siehe dort, an l68/QCC nachgemessen):
 *
 *     args[0] = erstes Argument, args[1] = zweites, ...
 *
 * Damit braucht es hier weder varargs (die QCC nicht DEFINIEREN kann)
 * noch die Adresse eines Parameters -- nur ein gewoehnliches Feld. Wo die
 * Formatzeichenkette steht, unterscheidet die drei Funktionen: bei printf
 * ist es args[0], bei fprintf und sprintf args[1].
 *
 * DIE SENKE IST EIN ZUSTAND, kein Argument: qp_sink schaltet zwischen
 * OS-9-Pfad (gepuffert, ueber _os_write) und Zeichenkettenpuffer (direkt).
 * Ein Argument waere sauberer, aber jede Ebene mehr kostet in diesem
 * Subset eine weitere Funktion; verschachtelt oder nebenlaeufig wird hier
 * nichts.
 *
 * error_code _os_write(path_id, const void*, u_int32*) -- count ist ein
 * IN/OUT-ZEIGER, Pfad 1 ist die Standardausgabe, Pfad 2 der Fehlerkanal.
 * QCC uebersetzt den Aufruf in genau die Microware-Konvention
 * (d0 = Pfad, d1 = Puffer, &count auf dem Stack).
 *
 * ZEICHEN STEHEN HIER ALS ZAHL (37 statt eines Zeichenliterals). Die
 * Formatzeichen kommen so dicht vor, dass eine falsche Anfuehrung erst im
 * Zielprogramm auffiele; die Bedeutung steht jeweils als Kommentar
 * dahinter.
 *
 * Quelle bleibt im QCC-Subset: keine Zeichenkettenverkettung, kein
 * tab[i][k] auf Zeigerfeldern, kein static (das ist bei QCC wirkungslos).
 */

extern int _os_write(int path, char *buf, int *count);

#define QP_BUF 256

int qp_sink;                    /* 0 = OS-9-Pfad, 1 = Zeichenkette */
int qp_path;                    /* Zielpfad, wenn qp_sink == 0 */
char *qp_dst;                   /* Schreibmarke, wenn qp_sink == 1 */
int qp_cnt;                     /* abgelegte Zeichen -- der Rueckgabewert */

char qp_buf[QP_BUF];            /* Sammelpuffer der laufenden Ausgabe */
int qp_len;                     /* belegt */

/* Den Sammelpuffer ausgeben und leeren. Bei der Zeichenkettensenke gibt
   es nichts zu leeren -- dort schreibt qp_putc direkt ans Ziel. */
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
		/* Kein gueltiger Pfad (geschlossene Datei). Lieber die Bytes
		   verwerfen als sie auf einem fremden Pfad ausgeben. */
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

/* Zeichenkette ausgeben, hoechstens prec Zeichen; prec < 0 = ohne Grenze.
   Damit deckt eine Schleife %s und %.*s ab. */
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

/* Vorzeichenlos zur Basis b. Die Ziffern entstehen rueckwaerts, deshalb
   erst in ein kleines Feld und dann verkehrt herum heraus. */
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

/* Der kleinste int hat kein positives Gegenstueck: -v ergibt dasselbe
   Bitmuster. Das geht hier trotzdem richtig aus, weil qp_num sein
   Argument VORZEICHENLOS liest -- aus $80000000 wird 2147483648. */
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

/* Der gemeinsame Formatierer. fi ist der Index der Formatzeichenkette,
 * die variadischen Argumente folgen ab fi+1.
 *
 * Erkannt werden %d %i %u %x %c %s %% sowie die Laengenangabe l und eine
 * Genauigkeit .Zahl bzw. .* -- mehr kommt in der Kette nicht vor
 * (nachgezaehlt an QCCs Bootstrap-Quelle: 203 %d, 155 %s, 67 %c,
 * 30 %.*s, 7 %ld). Die Laengenangabe l wird UEBERGANGEN, und das ist
 * keine Nachlaessigkeit: auf dem 68k sind int und long beide 32 Bit,
 * %ld und %d sind dasselbe.
 *
 * Alles andere wird UNVERAENDERT durchgereicht -- eine Breitenangabe wie
 * %20s erscheint also als Text, statt still falsch ausgerichtet zu
 * werden. Auch eine Genauigkeit an einer Zahl (%.3d, in C89 die
 * Mindestziffernzahl) faellt bewusst in diesen Zweig, statt
 * stillschweigend zu verschwinden.
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

	f = (char *) args[fi];
	ai = fi + 1;
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
		spec = f;                       /* zeigt auf das Prozentzeichen */
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
			/* Genauigkeit an etwas anderem als %s: durchreichen. */
			while (spec < f) {
				qp_putc(*spec);
				spec++;
			}
		} else if (c == 37) {                   /* % */
			qp_putc(37);
		} else if (c == 100 || c == 105) {      /* d i */
			qp_int(args[ai]);
			ai = ai + 1;
		} else if (c == 117) {                  /* u */
			qp_num(args[ai], 10);
			ai = ai + 1;
		} else if (c == 120) {                  /* x */
			qp_num(args[ai], 16);
			ai = ai + 1;
		} else if (c == 99) {                   /* c */
			qp_putc(args[ai]);
			ai = ai + 1;
		} else if (c == 115) {                  /* s */
			qp_putn((char *) args[ai], prec);
			ai = ai + 1;
		} else {
			/* Unbekannte Angabe unveraendert durchreichen, statt
			   still etwas zu verschlucken. */
			while (spec < f) {
				qp_putc(*spec);
				spec++;
			}
		}
	}
	qp_flush();
	return qp_cnt;
}

/* Die Pfadnummer hinter einem FILE*.
 *
 * Ein FILE* ist hier die Adresse eines Tabelleneintrags mit der
 * Pfadnummer (siehe file.c). ZWEI Sonderfaelle:
 *
 * - FILE* == 0 geht auf Pfad 2, den Fehlerkanal. QCCs Bootstrap-Quelle
 *   erklaert "stderr" als nie zugewiesenen Zeiger und ruft damit
 *   fprintf(stderr, ...) -- gegen Microwares clib laeuft das ins
 *   Ungewisse. Auf Pfad 2 sind die Diagnosen wenigstens zu lesen.
 * - Ein Eintrag mit 0 ist eine GESCHLOSSENE Datei. Der gibt -1, und
 *   qp_flush verwirft die Bytes, statt auf Pfad 0 zu schreiben.
 *
 * Dieselben Zeilen stehen in file.c. Sie sind bewusst DOPPELT: QCC
 * benennt eine Definition "tc_<name>", ein Aufruf sucht aber den nackten
 * Namen -- ein Aufruf ueber die Uebersetzungseinheit hinweg findet sein
 * Ziel also nicht. Eine gemeinsame Fassung muesste in Assembler stehen
 * und waere laenger als die Wiederholung.
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

/* Auf eine geschlossene Datei wird NICHT geschrieben, und der Aufruf
   meldet das auch: C89 verlangt bei einem Fehler einen negativen
   Rueckgabewert. Ohne diese Zeile haette fprintf die Zeichenzahl
   gemeldet, waehrend qp_flush die Bytes verwirft -- ein stiller
   Erfolgsbericht fuer eine Ausgabe, die nie stattfand. */
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

/* sprintf haengt die abschliessende Null an; sie zaehlt nicht zum
   Rueckgabewert, so steht es in C89. */
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
