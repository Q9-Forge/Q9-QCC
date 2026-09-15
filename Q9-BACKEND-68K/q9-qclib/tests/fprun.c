/* Gegenprobe zur Befehlsprobe: rechnet die FPU auf dem Ziel wirklich?
 * Die Rechnungen stehen in fprun.a; hier werden nur die Ergebnisse
 * geprueft. Jeder Sollwert ist so gewaehlt, dass er bei einer falschen
 * Rechnung NICHT herauskommt -- 3 statt 4 bei 7.0/2.0 zeigt zum Beispiel,
 * dass fintrz abschneidet und nicht rundet. */

extern int printf(char*, ...);
extern int fpcalc();
extern int fpdiv();
extern int fpconv();
extern int fpless();
extern int fpfrac();
extern int fpsave();

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
	int bad;

	bad = 0;
	bad = bad + pruefe("fpcalc 1.5*2.0+1.0=4", fpcalc(), 4);
	bad = bad + pruefe("fpdiv 7.0/2.0=3.5 -> 3", fpdiv(), 3);
	bad = bad + pruefe("fpconv 123*2.0=246", fpconv(), 246);
	bad = bad + pruefe("fpless 1.5<2.0", fpless(), 1);
	bad = bad + pruefe("fpfrac (0.1+0.2)*10 -> 3", fpfrac(), 3);
	bad = bad + pruefe("fpsave Register gerettet", fpsave(), 4);
	printf("fprun fertig: %d von 6 falsch\n", bad);
	return 0;
}
