/* fopen, fclose, fread, fwrite, puts, fputc, fputs fuer qclib.
 *
 * Der FILE* der C-Ebene ist hier die ADRESSE eines Tabelleneintrags, und
 * der Eintrag enthaelt die OS-9-Pfadnummer. Microwares FILE-Struktur mit
 * ihren dreizehn Feldern (_ptr/_base/_end/_flag/_fd/_ungetc/...) wird
 * NICHT nachgebaut: sie ist nur dann bindend, wenn fremder Code sie liest
 * -- und der einzige fremde Leser waere Microwares eigene clib, gegen die
 * niemand gleichzeitig bindet. Was q9_cstart.a von `_iob` erwartet, ist
 * blosser Platz (siehe iob.a).
 *
 * Bewusst NICHT gepuffert: die Werkzeuge der Kette lesen und schreiben in
 * grossen Bloecken (qr68 und ql68 holen ihre Eingabe mit einem einzigen
 * fread), da traegt eine Pufferschicht nichts bei und kostet nur Speicher
 * und Fehlermoeglichkeiten.
 *
 * Quelle im QCC-Subset: keine Zeichenkettenverkettung, kein
 * tab[i][k] auf Zeigerfeldern, kein static.
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

/* Je offener Datei ein Eintrag mit der Pfadnummer; 0 heisst frei.
   Pfad 0 ist die Standardeingabe und kann hier nicht vorkommen, weil
   fopen sie nie liefert -- die 0 ist also als "frei" eindeutig. */
int qf_path[QF_MAX];

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
	return (char *) &qf_path[frei];
}

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
	if (rc != 0 || got <= 0)
		return 0;
	return got / size;
}

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
	if (rc != 0 || done <= 0)
		return 0;
	return done / size;
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
