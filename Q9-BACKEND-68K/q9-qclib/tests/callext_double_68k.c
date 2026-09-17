/* Gezielter Regressionstest: CALLEXT/CALLEXTP mit double-Argumenten gegen
 * die per xcc GEMESSENE Microware-ABI (sechs Proben, s. docs/FLOAT_PLAN_de.md,
 * "Byte-Offset-Modell"). Jeder Mock liest d0/d1/Stack als rohe 32-Bit-Werte
 * und baut eine Bitmaske -- kein einziges Feld darf fehlschlagen, sonst ist
 * das Ergebnis kleiner als der erwartete Vollwert.
 *
 * x = 3.5  -> IEEE-754 hi=0x400C0000 lo=0x00000000
 * y = 1.25 -> IEEE-754 hi=0x3FF40000 lo=0x00000000
 */
extern int printf(char*, ...);

extern int mock1(double d, int i);        /* double zuerst: komplett Register, i komplett Stack */
extern int mock2(int i, double d);        /* int zuerst: d0, double komplett Stack */
extern int mock3(double d);               /* double allein: d0:d1 */
extern int mock4(double a, double b);     /* zwei double: a Register, b Stack */
extern int mock5(int i, double d, int j); /* sticky spill: d spillt, j MUSS auch spillen (nicht d1!) */

int pruefe(char* name, int ist, int soll)
{
	if (ist == soll) {
		printf("ok %s\n", name);
		return 0;
	}
	printf("FALSCH %s ist=%d soll=%d\n", name, ist, soll);
	return 1;
}

int main(void)
{
	double x, y;
	int fails;

	x = 3.5;
	y = 1.25;
	fails = 0;

	fails = fails + pruefe("mock1 (double,int)", mock1(x, 7), 7);
	fails = fails + pruefe("mock2 (int,double)", mock2(7, x), 7);
	fails = fails + pruefe("mock3 (double)", mock3(x), 3);
	fails = fails + pruefe("mock4 (double,double)", mock4(x, y), 15);
	fails = fails + pruefe("mock5 (int,double,int) sticky-spill", mock5(7, x, 9), 15);

	printf("callext_dbl_abi fertig: %d von 5 falsch\n", fails);
	return fails;
}
