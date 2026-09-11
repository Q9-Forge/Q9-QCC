/* Memory probe for the target system.
 *
 * CREATED AFTER A FAILURE: running QCC against qclib on the target
 * (test/qcc_68k.sh) brach mit "qcc: kein Speicher fuer Aktions-Log" ab --
 * realloc hatte 0 geliefert.
 *
 * Four measurements are performed in one emulator run:
 *   1. Der groesste freie Block. F$SRqMem mit -1 in d0.l liefert genau
 *      den (Handbuch: "If -1 is passed in d0.l, the largest block of free
 *      memory of the specified type is allocated").
 *   2. Die ECHTE Leiter des Parsers: erst der Eingabepuffer mit 524288
 *      Byte, dann das Aktions-Log von 1024 Eintraegen aufwaerts bis
 *      262144 -- das ist die am Host gemessene Endgroesse, ein Eintrag
 *      ist auf dem 68k 12 Byte gross. Genau diese Folge muss tragen.
 *   3. Der groesste freie Block DANACH. Er faellt um die ARENA, die
 *      qclib beim ersten realloc holt (groesster Block minus Reserve) --
 *      nicht um die Summe der Sprossen. Diese Zahl hat beim ersten
 *      Fehlschlag ein Leck ausgeschlossen und auf den Spitzenbedarf
 *      gezeigt.
 *   4. Wie weit die Leiter ueber den Bedarf hinaus traegt.
 *
 * The error code is printed as well: _os_srqmem returns it, and without
 * ihn steht am Ende nur "ging nicht".
 */

#include <stdio.h>
#include <stdlib.h>
extern int _os_srqmem(int want, int *granted, char **addr);
extern int _os_srtmem(int size, char *addr);

/* Der groesste freie Block, ohne ihn zu behalten. */
int groesster(void)
{
	char *p;
	int gr;
	int rc;

	gr = 0;
	p = 0;
	rc = _os_srqmem(-1, &gr, &p);
	if (rc != 0)
		return -rc;
	_os_srtmem(gr, p);
	return gr;
}

int main()
{
	char *eingabe;
	char *log;
	char *q;
	int vorher;
	int nachher;
	int n;
	int summe;

	vorher = groesster();
	printf("groesster Block vorher: %d\n", vorher);

	eingabe = realloc(0, 524288);
	if (eingabe == 0) {
		printf("eingabepuffer: FEHL\n");
		return 1;
	}
	printf("eingabepuffer 524288: ok\n");

	/* Die Leiter des Aktions-Logs, 12 Byte je Eintrag. */
	summe = 0;
	log = 0;
	n = 12288;
	while (n <= 3145728) {
		q = realloc(log, n);
		if (q == 0) {
			printf("log %d: FEHL\n", n);
			printf("groesster Block dabei: %d\n", groesster());
			return 1;
		}
		q[0] = 1;
		q[n - 1] = 2;
		log = q;
		summe = summe + n;
		n = n * 2;
	}
	printf("log bis 3145728: ok, summe aller sprossen %d\n", summe);

	nachher = groesster();
	printf("groesster Block nachher: %d\n", nachher);
	printf("verbraucht: %d\n", vorher - nachher);

	/* Und wie weit traegt es ueber den Bedarf hinaus? */
	while (n <= 50331648) {
		q = realloc(log, n);
		if (q == 0) {
			printf("darueber %d: FEHL (das ist in Ordnung)\n", n);
			break;
		}
		log = q;
		printf("darueber %d: ok\n", n);
		n = n * 2;
	}

	/* Zurueckgegeben wird hier nichts: qclib haelt EINE Arena und hat
	   kein free (siehe src/mem.c). Beim Prozessende gibt OS-9 sie
	   ohnehin zurueck. Der groesste freie Block ist deshalb ab dem
	   ersten realloc um die Arenagroesse kleiner -- das ist die
	   Messgroesse, nicht ein Leck. */
	printf("sonde zu ende\n");
	return 0;
}
