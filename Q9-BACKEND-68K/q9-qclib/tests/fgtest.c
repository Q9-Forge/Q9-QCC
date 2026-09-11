/* Pruefprogramm fuer fgets und ferror.
 *
 * WARUM NICHT IN hello.c: fuer fgets ist Microwares clib KEIN gueltiges
 * Orakel. Gemessen mit test/lineend68k.sh trennt clibs fgets an $0d, weil
 * Microwares C n auf CR abbildet; QCC bildet es auf $0a ab, und alle
 * Dateien dieser Kette entstehen damit. Das Orakel ist deshalb die
 * HOST-libc: die versteht n genauso als $0a. Dasselbe Programm laeuft
 * einmal mit clang am Host und einmal gegen qclib auf dem 68030, und beide
 * Ausgaben muessen zeichengleich sein.
 *
 * Deshalb steht hier auch kein einziger OS-9-Pfad: der Dateiname ist
 * relativ, damit beide Seiten ihn benutzen koennen.
 *
 * Geprueft werden die Faelle, in denen sich fgets-Fassungen unterscheiden:
 *   - eine gewoehnliche Zeile
 *   - eine LEERE Zeile (nur der Umbruch)
 *   - eine Zeile, die LAENGER ist als der Puffer (Abschneiden bei n-1,
 *     Fortsetzung im naechsten Aufruf)
 *   - die letzte Zeile OHNE Umbruch am Dateiende
 *   - der Aufruf nach dem Dateiende (muss 0 liefern)
 * und dass ferror danach NICHT anschlaegt.
 */

#include <stdio.h>

int main()
{
	FILE *fp;
	char muster[24];
	char buf[8];
	char *r;
	int i;
	int n;
	int k;

	/* "a" LF LF "bbbbbbbbbb" LF "cc"  -- ohne Umbruch am Ende */
	muster[0] = 97;
	muster[1] = 10;
	muster[2] = 10;
	i = 3;
	while (i < 13) {
		muster[i] = 98;
		i++;
	}
	muster[13] = 10;
	muster[14] = 99;
	muster[15] = 99;

	fp = fopen("fgtest.bin", "w");
	if (fp == 0) {
		printf("fopen w geht nicht\n");
		return 1;
	}
	n = fwrite(muster, 1, 16, fp);
	printf("geschrieben %d ferror %d\n", n, ferror(fp));
	fclose(fp);

	fp = fopen("fgtest.bin", "r");
	if (fp == 0) {
		printf("fopen r geht nicht\n");
		return 1;
	}
	i = 1;
	r = fgets(buf, 6, fp);
	while (r != 0) {
		n = 0;
		while (buf[n] != 0)
			n++;
		printf("zeile %d laenge %d:", i, n);
		k = 0;
		while (k < n) {
			printf(" %d", buf[k] & 255);
			k++;
		}
		printf("\n");
		i++;
		r = fgets(buf, 6, fp);
	}
	printf("zeilen %d ferror %d\n", i - 1, ferror(fp));
	fclose(fp);
	return 0;
}
