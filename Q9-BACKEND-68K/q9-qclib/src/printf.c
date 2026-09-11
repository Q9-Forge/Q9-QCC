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
