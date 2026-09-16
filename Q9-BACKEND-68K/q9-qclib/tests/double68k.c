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

int pruefe(char* name, int ist, int soll)
{
	if (ist == soll) {
		printf("ok %s\n", name);
		return 0;
	}
	printf("FALSCH %s\n", name);
	return 1;
}

int main()
{
	double a;
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

	printf("double68k fertig: %d von 30 falsch\n", bad);
	return 0;
}
