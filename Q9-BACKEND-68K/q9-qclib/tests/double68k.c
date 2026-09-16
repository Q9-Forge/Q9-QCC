/* double aus C, uebersetzt mit QCC, gerechnet auf echtem 68030.
 *
 * Jeder Sollwert ist so gewaehlt, dass er bei falscher Rechnung NICHT
 * herauskommt: 3 statt 4 bei 7.0/2.0 zeigt, dass (int) Richtung null
 * abschneidet und nicht rundet; (0.1+0.2)*10 faengt ab, ob die
 * Nachkommastellen ueberhaupt durch die Kette kommen.
 *
 * Die ganze Kette ist die eigene: qcpp, qcir, qir68k, qr68, ql68 -- und
 * gerechnet wird von OS-9s F-Line-Trap, denn der 68030 hat keine FPU.
 */

extern int printf(char*, ...);

double g;
/* INITIALISIERER AN GLOBALEN double (2026-09-16). Der Wert steht als
   Bitmuster in den Daten -- auf dem 68k big-endian, zwei dc.l, hi zuerst.
   Ob die Reihenfolge stimmt, zeigt nur die echte Maschine: im VM-Orakel
   setzt Python den double selbst zusammen. */
double gi = 4.5;
double gneg = -2.5;
double gpi = 3.14159;
double gint = 5;
/* Exponentschreibweise, auch im globalen Initialisierer */
double gexp = 1.5e3;
double gexpneg = 2.5E-2;

int pruefe(char* name, int ist, int soll)
{
	if (ist == soll) {
		printf("ok %s\n", name);
		return 0;
	}
	printf("FALSCH %s\n", name);
	return 1;
}

int d_param(double x)
{
	return (int)(x * 10.0);
}

int d_zwei(double a, double b)
{
	return (int)((a + b) * 10.0);
}

int d_gemischt(double a, int b)
{
	return (int)(a * 10.0) + b;
}

int d_schreib(double x)
{
	x = x + 1.0;
	return (int)(x * 10.0);
}

double d_ret()
{
	return 3.5;
}

double d_doppelt(double x)
{
	return x * 2.0;
}

double d_summe(double a, double b)
{
	return a + b;
}

double d_fak(double x)
{
	if (x < 1.5) return 1.0;
	return x * d_fak(x - 1.0);
}

/* STRUCTS MIT double-FELD (2026-09-16). Das VM-Orakel kann den GEMISCHTEN
   Fall nicht abbilden: es fuehrt einen Block als Liste typisierter Zellen und
   rechnet den Index als offset/groesse -- ein double bei Offset 4 laesst sich
   darin nicht von einem int unterscheiden. Auf dem Ziel ist genau dieses
   Layout richtig: xcc richtet double auf ZWEI Byte aus (68k-Wortausrichtung),
   und QCC stimmt damit ueberein. Hier wird es deshalb auf echter Hardware
   geprueft -- der einzige Ort, an dem es sich zeigen kann. */
struct DblOnly { double d; };
struct IntDbl  { int n; double d; };
struct DblInt  { double d; int n; };

int s_nimm(struct DblOnly s)
{
	return (int)(s.d * 10.0);
}

struct DblOnly s_gib()
{
	struct DblOnly s;
	s.d = 3.5;
	return s;
}

int d_ptr(double* p)
{
	return (int)((*p) * 10.0);
}

void d_setze(double* p)
{
	*p = 9.5;
}

int main()
{
	double a;
	double* dp;
	double b;
	double s;
	int i;
	int bad;

	bad = 0;

	a = 1.5;
	b = 2.5;
	bad = bad + pruefe("1.5*2.5=3.75 -> 3", (int)(a * b), 3);
	bad = bad + pruefe("1.5+2.5=4", (int)(a + b), 4);
	bad = bad + pruefe("2.5-1.5=1", (int)(b - a), 1);

	a = 7.0;
	b = 2.0;
	bad = bad + pruefe("7.0/2.0=3.5 -> 3", (int)(a / b), 3);

	a = -2.5;
	bad = bad + pruefe("(int)-2.5 = -2, nicht -3", (int)a, -2);

	a = 0.1;
	b = 0.2;
	s = a + b;
	bad = bad + pruefe("(0.1+0.2)*10 -> 3", (int)(s * 10.0), 3);

	a = 1.5;
	b = 2.5;
	if (a < b) bad = bad + pruefe("1.5 < 2.5", 1, 1);
	else bad = bad + pruefe("1.5 < 2.5", 0, 1);
	if (b > a) bad = bad + pruefe("2.5 > 1.5", 1, 1);
	else bad = bad + pruefe("2.5 > 1.5", 0, 1);
	if (a == 1.5) bad = bad + pruefe("1.5 == 1.5", 1, 1);
	else bad = bad + pruefe("1.5 == 1.5", 0, 1);

	/* globale Variable */
	g = 3.75;
	bad = bad + pruefe("globale double", (int)g, 3);

	/* ueber eine Schleife hinweg -- der Wert muss im Rahmen ueberleben */
	s = 0.0;
	i = 0;
	while (i < 4) {
		s = s + 1.5;
		i = i + 1;
	}
	bad = bad + pruefe("viermal 1.5 addiert = 6", (int)s, 6);

	/* Konversion in beide Richtungen */
	i = 7;
	a = (double)i;
	a = a * 0.5;
	bad = bad + pruefe("(double)7 * 0.5 -> 3", (int)a, 3);

	/* GEMISCHT MIT GANZZAHLEN (2026-09-16). Beide Stellungen, denn der
	   linke Operand liegt beim Emittieren schon unter dem rechten: einmal
	   I2D, einmal I2DUNDER. Die Faelle mit "-" und "/" sind nicht
	   kommutativ -- eine vertauschte Konversion faellt dort auf. */
	a = 1.5;
	bad = bad + pruefe("a+1 = 2.5 -> 2", (int)(a + 1), 2);
	bad = bad + pruefe("1+a = 2.5 -> 2", (int)(1 + a), 2);
	a = 8.0;
	bad = bad + pruefe("a-10 = -2", (int)(a - 10), -2);
	bad = bad + pruefe("10-a = 2", (int)(10 - a), 2);
	bad = bad + pruefe("32/a = 4", (int)(32 / a), 4);
	bad = bad + pruefe("a*4 = 32", (int)(a * 4), 32);
	if (a > 7) bad = bad + pruefe("8.0 > 7", 1, 1);
	else bad = bad + pruefe("8.0 > 7", 0, 1);

	/* Zuweisung wandelt um, in beide Richtungen */
	a = 5;
	bad = bad + pruefe("double a = 5", (int)a, 5);
	i = 0;
	i = 2.9;
	bad = bad + pruefe("int i = 2.9 -> 2", i, 2);

	/* ++/-- AUF double (2026-09-16). Der Fehler dahinter: 'd' fiel im
	   Frontend in den char-Auffangzweig (LOADC/STOREC) und das Inkrement
	   blieb WIRKUNGSLOS -- ohne jede Meldung. Die Sollwerte sind mit *10
	   gewaehlt, damit die Nachkommastelle mitgeprueft wird: ein blosses
	   (int)a haette auch beim falschen Ergebnis noch gestimmt.
	   Auf dem 68030 zaehlt das doppelt, denn hier rechnet die F-Line-Trap
	   mit 80 Bit, nicht Pythons 64 -- das VM-Orakel allein genuegt nicht. */
	a = 1.5;
	a++;
	bad = bad + pruefe("a++ auf 1.5 -> 2.5", (int)(a * 10.0), 25);
	a = 1.5;
	++a;
	bad = bad + pruefe("++a auf 1.5 -> 2.5", (int)(a * 10.0), 25);
	a = 5.5;
	a--;
	bad = bad + pruefe("a-- auf 5.5 -> 4.5", (int)(a * 10.0), 45);

	/* Postfix liefert den ALTEN, Praefix den NEUEN Wert -- ohne diese
	   beiden Faelle ist die Choreographie ungeprueft. */
	a = 1.5;
	b = a++;
	bad = bad + pruefe("b = a++ liefert alt (1.5)", (int)(b * 10.0), 15);
	a = 1.5;
	b = ++a;
	bad = bad + pruefe("b = ++a liefert neu (2.5)", (int)(b * 10.0), 25);

	/* global: LOADGD/STOREGD ist eine eigene Emissionsstelle */
	g = 1.5;
	g++;
	bad = bad + pruefe("globales g++ -> 2.5", (int)(g * 10.0), 25);

	/* Als ANWEISUNG verworfen muss DDROP acht Byte abraeumen, nicht DROP
	   einen Slot -- schief laeuft der Stapel erst beim Weiterrechnen. */
	a = 1.5;
	a++;
	a++;
	a++;
	b = a + 4.5;
	bad = bad + pruefe("dreimal a++, dann +4.5 = 9.0", (int)(b * 10.0), 90);

	/* DOUBLE AN FUNKTIONSGRENZEN (2026-09-16). Acht Byte passen nicht in
	   einen Parameter-Slot (im Rahmen fest vier Byte), also geht der Wert
	   ueber einen globalen Puffer und uebergeben wird dessen Adresse; der
	   Aufgerufene packt beim Eintritt in einen lokalen Block aus. Auf echter
	   Hardware zaehlt das doppelt: hier liegt der Puffer im -remotedata-vsect
	   und die Rechnung macht OS-9s F-Line-Trap. */
	bad = bad + pruefe("f(double) nimmt 3.5 an", d_param(3.5), 35);
	bad = bad + pruefe("zwei double-Parameter", d_zwei(1.5, 2.5), 40);
	bad = bad + pruefe("double und int gemischt", d_gemischt(1.5, 7), 22);
	bad = bad + pruefe("int-Argument wird gewandelt", d_param(3), 30);
	bad = bad + pruefe("Parameter ist beschreibbar", d_schreib(3.5), 45);
	a = d_ret();
	bad = bad + pruefe("double als Rueckgabe", (int)(a * 10.0), 35);
	a = d_doppelt(1.25);
	bad = bad + pruefe("double rein und raus", (int)(a * 10.0), 25);
	a = d_doppelt(d_doppelt(1.25));
	bad = bad + pruefe("verschachtelt f(f(x))", (int)(a * 10.0), 50);
	/* Ein Puffer je AUFRUFSTELLE: sonst ueberschriebe das Auswerten des
	   zweiten Arguments das bereits abgelegte erste. */
	a = d_summe(1.0, d_doppelt(2.0));
	bad = bad + pruefe("f(1.0, g(2.0)) ohne Kollision", (int)(a * 10.0), 50);
	/* Rekursion traegt nur, weil der Aufgerufene beim Eintritt kopiert. */
	a = d_fak(4.0);
	bad = bad + pruefe("Rekursion 4*3*2*1", (int)a, 24);

	/* ZUSAMMENGESETZTE ZUWEISUNG (C89 3.3.16.2) */
	a = 1.5;
	a += 2.0;
	bad = bad + pruefe("a += 2.0", (int)(a * 10.0), 35);
	a = 9.0;
	a /= 2.0;
	bad = bad + pruefe("a /= 2.0", (int)(a * 10.0), 45);
	a = 1.5;
	a *= 4;
	bad = bad + pruefe("a *= 4 (int rechts)", (int)(a * 10.0), 60);
	i = 7;
	i += 1.5;
	bad = bad + pruefe("int i += 1.5 -> 8", i, 8);

	bad = bad + pruefe("globaler Initialisierer 4.5", (int)(gi * 10.0), 45);
	bad = bad + pruefe("globaler Initialisierer -2.5", (int)(gneg * 10.0), -25);
	bad = bad + pruefe("globaler Initialisierer 3.14159", (int)(gpi * 100.0), 314);
	bad = bad + pruefe("globaler Initialisierer 5 (ganz)", (int)(gint * 10.0), 50);
	/* beschreibbar bleibt er auch */
	gi = gi + 1.0;
	bad = bad + pruefe("Initialisierter global ist schreibbar", (int)(gi * 10.0), 55);

	/* EXPONENTSCHREIBWEISE (2026-09-16). Gescheitert war sie am Lexer, nicht
	   am Umrechner -- "TOKEN floatLit" fehlte, und "1e2" zerfiel in ein
	   number- und ein ident-Token. Die Form MIT Vorzeichen ("1e-2") ging
	   deshalb schon vorher; beide gehoeren in den Test. */
	a = 1e2;
	bad = bad + pruefe("1e2 = 100", (int)a, 100);
	a = 1.5e3;
	bad = bad + pruefe("1.5e3 = 1500", (int)a, 1500);
	a = 5e-1;
	bad = bad + pruefe("5e-1 = 0.5", (int)(a * 10.0), 5);
	a = 2.5E-2;
	bad = bad + pruefe("2.5E-2 = 0.025", (int)(a * 1000.0), 25);
	bad = bad + pruefe("globaler Init 1.5e3", (int)gexp, 1500);
	bad = bad + pruefe("globaler Init 2.5E-2", (int)(gexpneg * 1000.0), 25);

	/* ZEIGER AUF EIN LOKALES double (2026-09-16). "&a" lieferte die
	   SLOT-Adresse statt der des Blocks -- der Zeiger zeigte ins Leere, und
	   zwar ohne Meldung. Auf echter Hardware zaehlt das doppelt: hier ist
	   der Rahmen wirklich ein Rahmen, und eine falsche Adresse trifft
	   fremde Daten statt einer Python-Liste. */
	a = 1.5;
	dp = &a;
	bad = bad + pruefe("*p liest 1.5", (int)((*dp) * 10.0), 15);
	*dp = 2.5;
	bad = bad + pruefe("*p = 2.5 schreibt durch", (int)(a * 10.0), 25);
	bad = bad + pruefe("f(&a) nimmt den Zeiger", d_ptr(&a), 25);
	d_setze(&a);
	bad = bad + pruefe("Aufgerufener schreibt zurueck", (int)(a * 10.0), 95);
	gi = 1.5;
	dp = &gi;
	bad = bad + pruefe("Zeiger auf globales double", (int)((*dp) * 10.0), 15);

	{
		struct DblOnly s1;
		struct DblOnly s2;
		struct IntDbl m1;
		struct IntDbl m2;
		struct DblInt r1;

		s1.d = 1.5;
		s2 = s1;
		bad = bad + pruefe("struct{double} kopieren", (int)(s2.d * 10.0), 15);
		bad = bad + pruefe("struct{double} als Parameter", s_nimm(s1), 15);
		s2 = s_gib();
		bad = bad + pruefe("struct{double} als Rueckgabe", (int)(s2.d * 10.0), 35);

		/* Der gemischte Fall -- das double liegt bei Offset 4. */
		m1.n = 7;
		m1.d = 2.5;
		bad = bad + pruefe("struct{int,double} lesen", m1.n * 100 + (int)(m1.d * 10.0), 725);
		m2 = m1;
		bad = bad + pruefe("struct{int,double} kopieren", m2.n * 100 + (int)(m2.d * 10.0), 725);
		r1.d = 1.5;
		r1.n = 9;
		bad = bad + pruefe("struct{double,int} lesen", r1.n * 100 + (int)(r1.d * 10.0), 915);
		bad = bad + pruefe("sizeof struct{int,double}", (int)sizeof(struct IntDbl), 12);
	}

	printf("double68k fertig: %d von 67 falsch\n", bad);
	return 0;
}
