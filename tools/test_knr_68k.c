/* K&R-Funktionsdefinitionen auf dem ECHTEN 68k-Ziel (C89 6.7.1).
 *
 * Warum zusaetzlich zu runtests.sh: die Host-VM qccvm.py modelliert Speicher
 * als typisierte Zellen, nicht als Bytes. Ein K&R-Parameter bekommt seinen Typ
 * in tc_krend NACHTRAEGLICH zugewiesen (aus der Deklaration hinter der
 * Klammer) -- eine falsche BREITE (char/short als 4 Byte gelesen) oder eine
 * falsche Byte-Reihenfolge faellt der VM deshalb gar nicht auf. Auf dem
 * big-endian 68030 faellt sie sofort auf.
 *
 * Die Faelle sind so gewaehlt, dass ein plausibler Fehler einen ANDEREN Wert
 * liefert und nicht zufaellig denselben: Deklarationen in vertauschter
 * Reihenfolge, gemischte Typbreiten in einer Liste, negative Werte (Vorzeichen-
 * erweiterung), und jeweils ein ANSI-Zwilling als direkter Vergleich.
 *
 * Ausgabeformat: je Fall "<id>:<wert>\n", die id VOR dem Fall gedruckt, damit
 * ein Absturz am letzten Marker lokalisierbar ist statt nur als fehlende Zeile.
 */

static void mark(int id) { putint(id); putchar(58); }
static void val(int v)   { putint(v); putchar(10); }

struct P { int x; int y; };

/* --- schlichte Grundform --- */
int kadd(a, b)
int a;
int b;
{
	return a + b;
}

/* --- mehrere Deklaratoren in EINER Deklaration --- */
int kmul(a, b)
int a, b;
{
	return a * b;
}

/* --- Deklarationen in ANDERER Reihenfolge als die Klammer.
       Ein Emitter, der stumpf in Deklarationsreihenfolge anlegt, rechnet
       hier 7-30 = -23 statt 30-7 = 23. --- */
int ksub(a, b)
int b;
int a;
{
	return a - b;
}

/* --- gemischte Breiten, Reihenfolge zusaetzlich vertauscht.
       Falsche Breite oder falsche Zuordnung ergibt einen anderen Wert. --- */
int kmix(c, s, i)
short s;
int i;
char c;
{
	return (int)c * 100 + (int)s * 10 + i;
}

/* --- Schmale Typen mit negativem Wert. GEMESSEN (2026-09-16): QCC erweitert
       hier NICHT vorzeichenrichtig -- "(int)c" auf ein char-Parameter mit -3
       liefert 253, nicht -3. Das ist ein VORBESTEHENDER, allgemeiner Mangel:
       der ANSI-Zwilling aneg liefert exakt dasselbe. Deshalb steht hier
       bewusst KEIN absoluter Sollwert (der wuerde einen fremden Fehler als
       K&R-Erwartung festschreiben), sondern die DIFFERENZ gegen ANSI. Sie
       muss 0 sein: was auch immer QCC mit schmalen Typen tut, ueber K&R muss
       es dasselbe tun wie ueber ANSI. Faellt der Mangel spaeter, bleibt
       dieser Test gruen -- er prueft Gleichheit, nicht das Verhalten. */
int kneg(c, s)
char c;
short s;
{
	return (int)c + (int)s;
}
int aneg(char c, short s) { return (int)c + (int)s; }

/* --- unsigned char, ebenfalls gegen den ANSI-Zwilling geprueft --- */
int kuc(u)
unsigned char u;
{
	return (int)u;
}
int auc(unsigned char u) { return (int)u; }

/* --- Zeiger --- */
int klen(s)
char *s;
{
	int n;
	n = 0;
	while (*s) { n = n + 1; s = s + 1; }
	return n;
}

/* --- Array-Schreibweise ist ein Zeiger --- */
int kidx(b)
char b[];
{
	return b[1];
}

/* --- Zeiger auf struct --- */
int kptr(p)
struct P *p;
{
	return p->x + p->y;
}

/* --- struct per Wert --- */
int kval(p)
struct P p;
{
	return p.x * 10 + p.y;
}

/* --- double als K&R-Parameter, mit ANSI-Zwilling zum Vergleich --- */
int kdbl(d)
double d;
{
	return (int)(d * 4.0);
}
int adbl(double d) { return (int)(d * 4.0); }

/* --- ein Name OHNE Deklaration ist in C89 implizit int --- */
int kimp(a, b)
int a;
{
	return a + b;
}

/* --- Rekursion ueber eine K&R-Funktion --- */
int kfak(n)
int n;
{
	if (n < 2) return 1;
	return n * kfak(n - 1);
}

/* --- K&R ruft ANSI und umgekehrt --- */
int ansi3(int v) { return v * 3; }
int kcall(v)
int v;
{
	return ansi3(v) + 1;
}

/* --- void-Rueckgabe als K&R --- */
static int gsum;
void kvoid(a)
int a;
{
	gsum = gsum + a;
}

/* --- unsigned int --- */
int kuns(u)
unsigned int u;
{
	return (int)(u / 2);
}

int main(void)
{
	struct P s;
	char buf[4];

	s.x = 3; s.y = 4;
	buf[0] = 120; buf[1] = 121; buf[2] = 0;

	/* 1  Grundform */
	mark(1);  val(kadd(3, 4));
	/* 2  mehrere Deklaratoren in einer Zeile */
	mark(2);  val(kmul(6, 7));
	/* 3  Deklarationen vertauscht -- der eigentliche Waechter */
	mark(3);  val(ksub(30, 7));
	/* 4  gemischte Breiten, vertauscht */
	mark(4);  val(kmix(5, 6, 7));
	/* 5  schmale Typen negativ: K&R MUSS sich wie ANSI verhalten -> Differenz 0 */
	mark(5);  val(kneg(-3, -4) - aneg(-3, -4));
	/* 6  unsigned char: ebenfalls gleich wie ANSI */
	mark(6);  val(kuc(200) - auc(200));
	/* 7  Zeiger */
	mark(7);  val(klen("hallo"));
	/* 8  Array-Schreibweise */
	mark(8);  val(kidx(buf));
	/* 9  Zeiger auf struct */
	mark(9);  val(kptr(&s));
	/* 10 struct per Wert */
	mark(10); val(kval(s));
	/* 11 double als K&R */
	mark(11); val(kdbl(1.5));
	/* 12 derselbe Wert ueber ANSI -- muss identisch sein */
	mark(12); val(adbl(1.5));
	/* 13 implizit int */
	mark(13); val(kimp(40, 2));
	/* 14 Rekursion */
	mark(14); val(kfak(5));
	/* 15 K&R ruft ANSI */
	mark(15); val(kcall(5));
	/* 16 void-Rueckgabe */
	gsum = 0;
	mark(16); kvoid(11); kvoid(22); val(gsum);
	/* 17 unsigned */
	mark(17); val(kuns(100));
	/* 18 K&R-Funktion als Argument einer anderen */
	mark(18); val(kadd(kmul(2, 3), 4));
	/* 19 viele Parameter (Reihenfolge ueber die Klammer) */
	mark(19); val(kmix(1, 2, 3));
	/* 20 negativer int */
	mark(20); val(kadd(-10, 3));

	return 0;
}
