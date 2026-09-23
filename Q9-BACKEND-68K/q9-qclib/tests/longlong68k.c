/* long long aus C, uebersetzt mit QCC, gerechnet auf echtem 68030.
 *
 * Wie bei double68k.c ist jeder Sollwert bewusst gegen einen ECHTEN
 * Rechenfehler diskriminierend gewaehlt. Bei long long kommt eine
 * zusaetzliche Falle dazu, die es bei double nicht gibt: "(int)x" bei
 * einem Wert ausserhalb von [-2^31, 2^31-1] ist KEIN Python-int() (das
 * VM-Orakel behandelt Q2I als reinen No-op, siehe project_qcc_c89_stand.md
 * -- Nachtrag Hardwareverifikation), sondern auf dem 68k ein "move.l" der
 * UNTEREN 32 Bit, also echte Zweierkomplement-Kuerzung. Jeder Sollwert
 * hier, der (int) auf einen Wert ausserhalb dieses Bereichs anwendet, ist
 * deshalb die GEKUERZTE, nicht die rohe Zahl -- exakt das, was die
 * Hardware liefert, und exakt das, was das VM-Orakel bei genau dieser
 * Abfrageform NICHT zeigt.
 *
 * Die ganze Kette ist die eigene: qcpp, qcir, qir68k, qr68k, ql68k.
 */

extern int printf(char*, ...);

int pruefe(char* name, int ist, int soll)
{
	if (ist == soll) {
		printf("ok %s\n", name);
		return 0;
	}
	printf("FALSCH %s (ist %d, soll %d)\n", name, ist, soll);
	return 1;
}

long long g;
long long garr[3];

struct Rec { long long value; int tag; };

long long ll_square(long long x)
{
	return x * x;
}

long long ll_summe(long long a, long long b)
{
	return a + b;
}

int main()
{
	long long a, b, r;
	int bad;

	bad = 0;

	/* QMUL: Produkt passt nicht in 32 Bit. */
	a = 1000000000; b = 5;
	r = a * b;
	bad = bad + pruefe("1e9*5 hi", (int)(r >> 32), 1);
	bad = bad + pruefe("1e9*5 lo (32-Bit gekuerzt)", (int)r, 705032704);

	/* QADD: Summe traegt ueber die 32-Bit-Wortgrenze (2e9+2e9=4e9, das
	   ist zwar < 2^32, aber > INT32_MAX -- die Kuerzung klappt NEGATIV
	   um, waehrend das VM-Orakel die rohe positive Zahl zeigen wuerde). */
	a = 2000000000; b = 2000000000;
	r = a + b;
	bad = bad + pruefe("2e9+2e9 hi", (int)(r >> 32), 0);
	bad = bad + pruefe("2e9+2e9 lo (32-Bit gekuerzt, wird negativ)", (int)r, -294967296);

	/* QSUB: zurueck in den 32-Bit-Bereich -- prueft, dass QSUB nicht */
	/* selbst schon an der Wortgrenze falsch rechnet. */
	r = r - a;
	bad = bad + pruefe("(2e9+2e9)-2e9", (int)r, 2000000000);

	/* QDIV/QMOD, positiver Operand. */
	a = 1000000000; b = 7;
	r = a * b; /* 7e9 */
	b = 3;
	bad = bad + pruefe("1e9*7 % 3", (int)(r % b), 1);
	r = r / b;
	bad = bad + pruefe("1e9*7 / 3 (32-Bit gekuerzt)", (int)r, -1961633963);

	/* QDIV/QMOD, negativer Operand -- die Software-Division rechnet
	   schulmethodenbasiert auf dem BETRAG und macht das Vorzeichen am
	   Ende selbst; das ist der riskanteste Teil der 68k-Implementierung. */
	a = 1000000000; b = 7;
	r = a * b;
	r = -r;
	b = 3;
	bad = bad + pruefe("-(1e9*7) / 3 (32-Bit gekuerzt)", (int)(r / b), 1961633963);
	bad = bad + pruefe("-(1e9*7) % 3", (int)(r % b), -1);

	/* Schiebeoperationen. QSHL ueber die Wortgrenze, QSHR (arithmetisch,
	   auch bei negativem Operanden -- das war im VM-Orakel ein
	   tatsaechlich gefundener Bug bei QSHR/u64-Maskierung, s. Memory). */
	a = 1000000000;
	bad = bad + pruefe("(1e9<<3)>>32", (int)((a << 3) >> 32), 1);
	bad = bad + pruefe("1e9>>3", (int)(a >> 3), 125000000);
	a = -1000000000;
	bad = bad + pruefe("(-1e9)>>3 (arithmetisch, bleibt negativ)", (int)(a >> 3), -125000000);

	/* Bitoperationen mit negativem Zwischenergebnis (QNOT/QAND/QOR/QXOR
	   duerfen NICHT wie unsigned maskieren, sonst kippt das Vorzeichen
	   bei einer nachfolgenden Operation -- ebenfalls ein gefundener
	   VM-Orakel-Bug dieser Runde, hier auf echter Hardware nachgeprueft). */
	a = 1000000000;
	r = ~a;
	bad = bad + pruefe("~(1e9) hi (bleibt -1, kein unsigned-Kippen)", (int)(r >> 32), -1);
	bad = bad + pruefe("~(1e9) lo", (int)r, -1000000001);

	/* Vergleiche mit negativen Werten. */
	a = 5;
	if (a < 10) bad = bad + pruefe("5 < 10", 1, 1); else bad = bad + pruefe("5 < 10", 0, 1);
	if (a > 10) bad = bad + pruefe("5 > 10", 1, 0); else bad = bad + pruefe("5 > 10", 0, 0);
	a = -5;
	if (a < 0) bad = bad + pruefe("-5 < 0", 1, 1); else bad = bad + pruefe("-5 < 0", 0, 1);
	b = -3;
	if (a < b) bad = bad + pruefe("-5 < -3", 1, 1); else bad = bad + pruefe("-5 < -3", 0, 1);
	if (b > a) bad = bad + pruefe("-3 > -5", 1, 1); else bad = bad + pruefe("-3 > -5", 0, 1);

	/* Cast int<->long long, double<->long long (explizit). */
	{
		int i;
		double d;
		i = 12345;
		a = (long long)i;
		bad = bad + pruefe("(long long)12345 zurueck nach int", (int)a, 12345);
		a = -12345;
		i = (int)a;
		bad = bad + pruefe("(int) auf negative long long", i, -12345);
		d = 3.5;
		a = (long long)d;
		bad = bad + pruefe("(long long)3.5", (int)a, 3);
		a = 7;
		d = (double)a;
		bad = bad + pruefe("(double) einer long long *10", (int)(d * 10.0), 70);
	}

	/* long long als Funktionsargument UND Rueckgabewert (Boxing-Mechanismus
	   __llArg_N/__llRet). Verschachtelt, wie bei double_doppelt(double_doppelt(x)):
	   ein Puffer je AUFRUFSTELLE, sonst ueberschriebe das zweite Argument
	   das erste. */
	a = 100000;
	r = ll_square(a);
	bad = bad + pruefe("square(100000) hi", (int)(r >> 32), 2);
	bad = bad + pruefe("square(100000) lo", (int)r, 1410065408);
	r = ll_summe(a, ll_square(a));
	bad = bad + pruefe("summe(a, square(a)) ohne Kollision", (int)r, 1410165408);

	/* globale Variable, mehrfach ueberschrieben. */
	g = 1000000000;
	g = g * 7;
	bad = bad + pruefe("globales g*7 hi", (int)(g >> 32), 1);
	bad = bad + pruefe("globales g*7 lo", (int)g, -1589934592);

	/* struct-Feld (8-Byte-Ausrichtung -- xcc richtet ohnehin nur auf max.
	   2 aus, s. project_qcc_struct_abi.md; hier zaehlt, ob das Feld auf
	   echter Hardware am RICHTIGEN Byte-Offset landet, nicht nur im
	   VM-Orakel, das keine Bytes kennt). */
	{
		struct Rec s1;
		s1.value = g;
		s1.tag = 9;
		bad = bad + pruefe("struct-Feld long long: tag", s1.tag, 9);
		bad = bad + pruefe("struct-Feld long long: hi", (int)(s1.value >> 32), 1);
		bad = bad + pruefe("struct-Feld long long: lo", (int)s1.value, -1589934592);
	}

	/* Array-Element. */
	garr[1] = g;
	bad = bad + pruefe("Array-Element long long: hi", (int)(garr[1] >> 32), 1);
	bad = bad + pruefe("Array-Element long long: lo", (int)garr[1], -1589934592);

	/* sizeof. */
	bad = bad + pruefe("sizeof(long long)", (int)sizeof(long long), 8);

	/* einfache Variable ++/--: EINZIGE ++/-- Form, die fuer long long
	   unterstuetzt ist (die drei Adressformen lehnen bewusst ab, s.
	   runtests.sh). */
	a = 999999999;
	a++;
	bad = bad + pruefe("a++ auf 999999999 -> 1000000000", (int)a, 1000000000);
	b = a++;
	bad = bad + pruefe("b = a++ liefert alten Wert", (int)b, 1000000000);
	bad = bad + pruefe("a++ hat erhoeht", (int)a, 1000000001);

	printf("longlong68k fertig: %d falsch\n", bad);
	return 0;
}
