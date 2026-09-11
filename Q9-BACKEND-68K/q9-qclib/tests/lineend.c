/* Measurement, not pass/fail comparison: how does Microware clib split lines?
 *
 * Die Frage steht vor qclibs fgets. OS-9 nutzt herkoemmlich CR ($0d) als
 * Zeilenende, C schreibt LF ($0a) vor, und die IR-Dateien der Kette
 * entstehen mit $0a (an qclibs und clibs printf gemessen). Welches Byte
 * clibs fgets beendet, ist damit NICHT herleitbar -- also gemessen.
 *
 * Geschrieben wird eine Datei mit ALLEN drei Faellen hintereinander:
 *   A $0d B $0a C $0d $0a D
 * Danach liest fgets sie zurueck, und das Programm druckt jede Zeile als
 * Bytefolge. Aus der Aufteilung folgt die Antwort.
 *
 * This program is linked against clib (qclib does not yet provide fgets).
 */

#include <stdio.h>

int main()
{
	FILE *fp;
	char muster[9];
	char buf[64];
	int i;
	int n;
	int k;
	char *r;

	muster[0] = 65;         /* A  */
	muster[1] = 13;         /* CR */
	muster[2] = 66;         /* B  */
	muster[3] = 10;         /* LF */
	muster[4] = 67;         /* C  */
	muster[5] = 13;         /* CR */
	muster[6] = 10;         /* LF */
	muster[7] = 68;         /* D  */
	muster[8] = 0;

	fp = fopen("/dd/lineend.bin", "w");
	if (fp == 0) {
		printf("fopen w geht nicht\n");
		return 1;
	}
	n = fwrite(muster, 1, 8, fp);
	printf("geschrieben %d\n", n);
	fclose(fp);

	fp = fopen("/dd/lineend.bin", "r");
	if (fp == 0) {
		printf("fopen r geht nicht\n");
		return 1;
	}
	i = 1;
	r = fgets(buf, 60, fp);
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
		r = fgets(buf, 60, fp);
	}
	printf("zeilen insgesamt %d\n", i - 1);
	fclose(fp);
	return 0;
}
