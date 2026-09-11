/*
 * q9-qclib file I/O
 *
 * Purpose:
 *   Provides the Q9 FILE abstraction and unbuffered OS-9 file operations.
 *   A FILE handle identifies an entry in the Q9 path table rather than a
 *   Microware FILE layout.
 *
 * Edition history:
 *   2026-09-11  Introduced the English source-header format.
 */
/*
 * The public FILE handle is the address of a Q9 path-table entry. The entry
 * stores the OS-9 path number; Microware's private FILE layout is not copied.
 * The implementation is intentionally unbuffered because the toolchain reads
 * and writes large blocks, making an additional buffer unnecessary.
 *
 * The source stays within the QCC subset: no string concatenation, no indexed
 * access through pointer fields and no static local variables.
 */

extern int _os_open(char *name, int mode, int *path);
extern int _os_create(char *name, int mode, int *path, int perms);
extern int _os_close(int path);
extern int _os_read(int path, char *buf, int *count);
extern int _os_write(int path, char *buf, int *count);

#define QF_MAX   16     /* gleichzeitig offene Dateien */

/* Zugriffsmodi, aus MWOS/OS9/SRC/DEFS/modes.h:
   FAM_READ 0x01, FAM_WRITE 0x02. */
#define QF_READ  1
#define QF_WRITE 2

/* One path-table entry per open file; zero means unused. */
int qf_path[QF_MAX];

/* Per-file state fields, indexed like qf_path.
 *
 * WARUM ES SIE GIBT: fgets liest ZEILENweise. Ohne Puffer waere das ein
 * Systemaufruf je BYTE, und das Backend liest eine IR-Datei von ueber einem
 * Megabyte -- rund eine Million I$Read auf einem 68030. Deshalb holt fgets
 * einen Block und gibt die Zeilen daraus heraus.
 *
 * Der Puffer entsteht erst beim ersten fgets (ueber realloc, also aus der
 * Arena in mem.c) und nur fuer die Dateien, die zeilenweise gelesen werden.
 * fread bleibt davon unberuehrt und geht weiter direkt zum System -- die
 * Werkzeuge der Kette holen ihre Eingabe in EINEM fread, da traegt eine
 * Pufferschicht nichts bei.
 *
 * Do not mix fgets and fread on the same stream: fread would skip bytes that
 * are already buffered. The toolchain does not do this. */
char *qf_rbuf[QF_MAX];          /* Lesepuffer, 0 = noch keiner */
int qf_rlen[QF_MAX];            /* gueltige Bytes darin */
int qf_rpos[QF_MAX];            /* Leseposition darin */
int qf_err[QF_MAX];             /* Fehlerkennung fuer ferror */

#define QF_RBUF 1024

extern char *realloc(char *p, int n);

/* Der Tabellenindex hinter einem FILE*.
   Ein FILE* IST die Adresse von qf_path[i]; gesucht wird durch Vergleich
   statt durch Zeigerarithmetik -- QF_MAX ist 16, und ein "(fp - &qf_path[0])
   / 4" waere eine Annahme ueber die Feldgroesse mehr. -1 = kein gueltiger
   Eintrag. */
/* Function: qf_index
 * Finds the table entry represented by a FILE handle.
 * Parameters: fp FILE handle.
 * Returns: Table index, or -1 when invalid. */
int qf_index(char *fp)
{
	int i;

	if (fp == 0)
		return -1;
	i = 0;
	while (i < QF_MAX) {
		if ((char *) &qf_path[i] == fp)
			return i;
		i++;
	}
	return -1;
}

/* Refills one per-file read buffer. */
/* Function: qf_fill
 * Fills the read buffer for an open file.
 * Parameters: i File table index.
 * Returns: Number of bytes read, or a negative error status. */
int qf_fill(int i)
{
	char *buf;
	int n;
	int rc;

	buf = qf_rbuf[i];
	if (buf == 0) {
		buf = realloc(0, QF_RBUF);
		if (buf == 0) {
			qf_err[i] = 1;
			return 0;
		}
		qf_rbuf[i] = buf;
	}
	n = QF_RBUF;
	rc = _os_read(qf_path[i], buf, &n);
	if (rc != 0) {
		/* End of file is not an error; OS-9 reports E$EOF (211). */
		if (rc != 211)
			qf_err[i] = 1;
		qf_rlen[i] = 0;
		qf_rpos[i] = 0;
		return 0;
	}
	if (n <= 0) {
		qf_rlen[i] = 0;
		qf_rpos[i] = 0;
		return 0;
	}
	qf_rlen[i] = n;
	qf_rpos[i] = 0;
	return 1;
}

/* Function: qf_open
 * Opens or creates a file using the requested mode.
 * Parameters: a IR argument frame containing path and mode.
 * Returns: FILE handle, or null on failure. */
char *qf_open(int *a)
{
	char *name;
	char *mode;
	int i;
	int frei;
	int p;
	int rc;
	int m;

	name = (char *) a[0];
	mode = (char *) a[1];
	frei = -1;
	for (i = 0; i < QF_MAX; i++) {
		if (qf_path[i] == 0)
			frei = i;
	}
	if (frei < 0 || name == 0 || mode == 0)
		return 0;

	m = mode[0];
	p = 0;
	if (m == 'r') {
		rc = _os_open(name, QF_READ, &p);
	} else if (m == 'w') {
		/* Neu anlegen. Die Dateirechte $03 (Besitzer lesen und
		   schreiben) sind NICHT an Microwares fopen nachgemessen --
		   sobald ein Vergleichslauf sie prueft, gehoert der Wert
		   hierher korrigiert. */
		rc = _os_create(name, QF_WRITE, &p, 0x03);
	} else {
		/* "a", "r+", "w+" sind nicht gemessen. Lieber nichts
		   liefern als still den falschen Modus oeffnen. */
		return 0;
	}
	if (rc != 0 || p == 0)
		return 0;
	qf_path[frei] = p;
	qf_rlen[frei] = 0;
	qf_rpos[frei] = 0;
	qf_err[frei] = 0;
	/* qf_rbuf bleibt stehen: ein einmal geholter Puffer wird beim naechsten
	   fopen desselben Platzes wiederverwendet. Freigeben kann qclib nicht
	   (mem.c hat keine Freigabeliste), und ihn liegen zu lassen ist besser
	   als bei jedem fopen einen neuen zu holen. */
	return (char *) &qf_path[frei];
}

/* Function: qf_close
 * Closes a Q9 file handle and releases its table entry.
 * Parameters: a IR argument frame containing the handle.
 * Returns: Zero on success, or an OS-9 error code. */
int qf_close(int *a)
{
	char *fp;
	int *slot;
	int rc;

	fp = (char *) a[0];
	if (fp == 0)
		return -1;
	slot = (int *) fp;
	if (*slot == 0)
		return -1;
	rc = _os_close(*slot);
	*slot = 0;
	if (rc != 0)
		return -1;
	return 0;
}

/* Liefert die Zahl der vollstaendig gelesenen ELEMENTE, nicht der Bytes
   -- so steht es in C89, und die Kette ruft durchweg mit size = 1. */
/* Function: qf_read
 * Reads complete items from a Q9 file.
 * Parameters: a IR argument frame containing buffer, size, count and handle.
 * Returns: Number of items read. */
int qf_read(int *a)
{
	char *buf;
	int size;
	int n;
	char *fp;
	int *slot;
	int want;
	int got;
	int rc;
	int i;

	buf = (char *) a[0];
	size = a[1];
	n = a[2];
	fp = (char *) a[3];
	if (fp == 0 || buf == 0 || size <= 0 || n <= 0)
		return 0;
	slot = (int *) fp;
	if (*slot == 0)
		return 0;
	want = size * n;
	got = want;
	rc = _os_read(*slot, buf, &got);
	if (rc != 0) {
		/* 211 ist E$EOF und kein Fehler (MWOS/SRC/DEFS/errno.h). */
		if (rc != 211) {
			i = qf_index(fp);
			if (i >= 0)
				qf_err[i] = 1;
		}
		return 0;
	}
	if (got <= 0)
		return 0;
	return got / size;
}

/* Function: qf_write
 * Writes complete items to a Q9 file.
 * Parameters: a IR argument frame containing buffer, size, count and handle.
 * Returns: Number of items written. */
int qf_write(int *a)
{
	char *buf;
	int size;
	int n;
	char *fp;
	int *slot;
	int want;
	int done;
	int rc;
	int i;

	buf = (char *) a[0];
	size = a[1];
	n = a[2];
	fp = (char *) a[3];
	if (fp == 0 || buf == 0 || size <= 0 || n <= 0)
		return 0;
	slot = (int *) fp;
	if (*slot == 0)
		return 0;
	want = size * n;
	done = want;
	rc = _os_write(*slot, buf, &done);
	if (rc != 0) {
		/* Den Fehler MERKEN, sonst hat ferror nichts zu melden: ein
		   Schreibfehler waere allein am kleineren Rueckgabewert erkennbar,
		   und den prueft kaum ein Aufrufer. qcc_backend_c.cpp fragt nach
		   dem Schreiben genau danach. */
		i = qf_index(fp);
		if (i >= 0)
			qf_err[i] = 1;
		return 0;
	}
	if (done <= 0)
		return 0;
	return done / size;
}

/* fgets: bis zum Zeilenumbruch EINSCHLIESSLICH, hoechstens n-1 Zeichen, immer
   mit abschliessender Null. Liefert 0, wenn nichts mehr kommt.
 *
 * DAS ZEILENENDE IST $0a, UND DAS WEICHT VON clib AB -- gemessen mit
 * test/lineend68k.sh: Microwares fgets trennt an $0d und laesst $0a
 * durchlaufen. Beides ist in sich stimmig, denn der Unterschied steckt im
 * COMPILER: Microwares C bildet '\n' auf CR ab, QCC auf LF. Alle Dateien
 * dieser Kette entstehen mit $0a (an printf nachgemessen), also muss fgets
 * hier an $0a trennen. Fuer DIESE Funktion ist der Gegenlauf gegen clib
 * deshalb kein gueltiges Orakel.
 *
 * Folge fuer die Pruefstaende: IR-Dateien mit ToolShed "copy -r" (roh) ins
 * Abbild bringen, nicht mit "copy -l" -- das setzt OS-9-Zeilenenden. */
/* Function: qf_gets
 * Reads one line into the caller's buffer.
 * Parameters: a IR argument frame containing buffer, limit and handle.
 * Returns: Buffer on success, or null at EOF/error. */
char *qf_gets(int *a)
{
	char *dst;
	int n;
	char *fp;
	int i;
	int k;
	char *buf;
	int c;

	dst = (char *) a[0];
	n = a[1];
	fp = (char *) a[2];
	i = qf_index(fp);
	if (dst == 0 || n <= 1 || i < 0 || qf_path[i] == 0)
		return 0;
	k = 0;
	while (k < n - 1) {
		if (qf_rpos[i] >= qf_rlen[i]) {
			if (qf_fill(i) == 0)
				break;
		}
		buf = qf_rbuf[i];
		c = buf[qf_rpos[i]] & 255;
		qf_rpos[i] = qf_rpos[i] + 1;
		dst[k] = c;
		k++;
		if (c == 10)
			break;
	}
	if (k == 0)
		return 0;               /* nichts gelesen: Ende */
	dst[k] = 0;
	return dst;
}

/* ferror: die Fehlerkennung der Datei. Gesetzt wird sie dort, wo ein
   Systemaufruf fehlschlaegt -- ohne diese Kennung waere ein Schreibfehler
   nur ein kleinerer Rueckgabewert, den kein Aufrufer prueft. */
/* Function: qf_error
 * Returns the pending error state of a Q9 file handle.
 * Parameters: a IR argument frame containing the handle.
 * Returns: Non-zero when an I/O error occurred. */
int qf_error(int *a)
{
	int i;

	i = qf_index((char *) a[0]);
	if (i < 0)
		return 1;               /* kein gueltiger Strom ist selbst ein Fehler */
	return qf_err[i];
}

/* Die Pfadnummer hinter einem FILE*. FILE* == 0 geht auf Pfad 2, den
   Fehlerkanal -- QCCs Bootstrap-Quelle erklaert "stderr" als nie
   zugewiesenen Zeiger und ruft damit fprintf/fputs/fputc darauf. Ein
   Eintrag mit 0 ist eine GESCHLOSSENE Datei und gibt -1.

   Dieselben Zeilen stehen in printf.c. Sie sind bewusst DOPPELT: QCC
   benennt eine Definition "tc_<name>", ein Aufruf sucht aber den nackten
   Namen -- ein Aufruf ueber die Uebersetzungseinheit hinweg findet sein
   Ziel also nicht. Eine gemeinsame Fassung muesste in Assembler stehen
   und waere laenger als die Wiederholung. */
/* Function: qf_pathof
 * Resolves a Q9 FILE handle to its OS-9 path number.
 * Parameters: fp Handle value.
 * Returns: OS-9 path number, or the diagnostic path for null. */
int qf_pathof(int fp)
{
	int *slot;

	if (fp == 0)
		return 2;
	slot = (int *) fp;
	if (*slot == 0)
		return -1;
	return *slot;
}

/* fputc gibt das geschriebene Zeichen zurueck, so steht es in C89 --
   nicht 0. Geschrieben wird EIN Byte ungepuffert; die Kette ruft fputc
   nur in Diagnosen, wo es auf Geschwindigkeit nicht ankommt. */
/* Function: qf_putc
 * Writes one character to a Q9 stream.
 * Parameters: a IR argument frame containing character and handle.
 * Returns: Character on success, or EOF-style failure. */
int qf_putc(int *a)
{
	char eins[4];
	int p;
	int n;
	int rc;

	p = qf_pathof(a[1]);
	if (p < 0)
		return -1;
	eins[0] = a[0];
	n = 1;
	rc = _os_write(p, eins, &n);
	if (rc != 0)
		return -1;
	return a[0] & 255;
}

/* fputs haengt KEINEN Zeilenumbruch an -- anders als puts. Geschrieben
   wird direkt aus der uebergebenen Zeichenkette, ohne Umkopieren: die
   Laenge steht ja fest, und _os_write nimmt jeden Puffer. */
/* Function: qf_puts_f
 * Writes a string without appending a newline.
 * Parameters: a IR argument frame containing string and handle.
 * Returns: Non-negative success status, or failure. */
int qf_puts_f(int *a)
{
	char *s;
	int p;
	int n;
	int rc;

	s = (char *) a[0];
	if (s == 0)
		return -1;
	p = qf_pathof(a[1]);
	if (p < 0)
		return -1;
	n = 0;
	while (s[n] != 0)
		n++;
	if (n == 0)
		return 0;
	rc = _os_write(p, s, &n);
	if (rc != 0)
		return -1;
	return 0;
}

/* puts haengt einen Zeilenumbruch an -- anders als fputs. Geschrieben
   wird in einem Stueck, damit zwischen Text und Umbruch nichts anderes
   dazwischenkommt.

   DAS ZEILENENDE IST $0a, NICHT $0d. Hier stand zuerst $0d, abgeleitet
   aus q9_cstart.a ("move.b #CR,-1(a1)") -- eine Ableitung, keine
   Messung, und sie war falsch: der Gegenlauf gegen clib zeigte
   "puts gehtgeschrieben 11" statt zweier Zeilen. Massgeblich ist, was
   printf fuer "\n" ablegt, und das ist $0a; damit bricht das Terminal
   um. Der eigene Test hatte den Fehler durchgelassen, weil er nur auf
   den Text prueft, nicht auf den Umbruch. */
/* Function: qf_puts
 * Writes a string followed by a newline.
 * Parameters: a IR argument frame containing the string.
 * Returns: Non-negative success status, or failure. */
int qf_puts(int *a)
{
	char *s;
	char zeile[256];
	int n;
	int rc;

	s = (char *) a[0];
	if (s == 0)
		return -1;
	n = 0;
	while (s[n] != 0 && n < 254) {
		zeile[n] = s[n];
		n++;
	}
	zeile[n] = 0x0a;
	n++;
	rc = _os_write(1, zeile, &n);
	if (rc != 0)
		return -1;
	return 0;
}
