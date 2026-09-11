/* Das Pruefprogramm der Bibliothek.
 *
 * Es wird ZWEIMAL gebunden -- gegen qclib und gegen Microwares clib --
 * und beide Module laufen im selben Emulatorlauf (test/vsclib.sh). Was
 * hier steht, muss also gegen BEIDE Bibliotheken dasselbe ausgeben.
 *
 * Daraus folgen zwei Regeln fuer neue Faelle:
 *
 * 1. NUR NORMIERTES VERHALTEN pruefen. strncmp gibt in C89 bloss ein
 *    VORZEICHEN zurueck ("greater than, equal to, or less than zero"),
 *    keinen bestimmten Wert -- clib liefert die Bytedifferenz, qclib
 *    -1/0/1. Deshalb geht alles durch vorz() statt direkt in die
 *    Ausgabe.
 * 2. KEINE Zeiger drucken. Die Adressen unterscheiden sich zwangslaeufig.
 *
 * Der stderr-Fall (FILE* == 0) steht bewusst NICHT hier: qclib legt ihn
 * auf Pfad 2, clib laeuft damit ins Ungewisse -- vergleichen liesse sich
 * das nicht. Er gehoert in einen Test, der nur qclib faehrt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int vorz(int v)
{
	if (v < 0)
		return -1;
	if (v > 0)
		return 1;
	return 0;
}

int main()
{
	FILE *fp;
	char *p;
	char *q;
	char buf[64];
	char sbuf[64];
	char tbuf[20];
	char *ende;
	int n;
	int i;
	int gut;

	printf("Hallo %s\n", "Welt");
	printf("%d %d %d\n", 42, -7, 0);
	printf("hex %x, Zeichen %c, Prozent %%\n", 255, 65);
	puts("puts geht");

	fp = fopen("/dd/qftest.txt", "w");
	if (fp == 0) {
		printf("fopen w geht nicht\n");
		return 1;
	}
	n = fwrite("Hallo Datei", 1, 11, fp);
	printf("geschrieben %d\n", n);
	fclose(fp);

	fp = fopen("/dd/qftest.txt", "r");
	if (fp == 0) {
		printf("fopen r geht nicht\n");
		return 1;
	}
	n = fread(buf, 1, 63, fp);
	i = n;
	if (i < 0)
		i = 0;
	buf[i] = 0;
	printf("gelesen %d: %s\n", n, buf);
	fclose(fp);

	/* Die drei Stringfunktionen. strchr gibt einen Zeiger IN die
	   Zeichenkette zurueck, der Rest der Zeichenkette ist also der
	   Nachweis; der Fehlschlag wird als 0/1 geprueft, nicht gedruckt. */
	printf("str %d %s %d %d %d\n", strlen("Hallo Datei"),
	       strchr("Hallo Datei", 68), vorz(strncmp("ab", "ab", 2)),
	       vorz(strncmp("ab", "ac", 2)), vorz(strncmp("ac", "ab", 2)));
	printf("strchr0 %d\n", strchr("abc", 122) == 0);

	/* sprintf, samt der Genauigkeit .* und der Laengenangabe l. */
	sprintf(sbuf, "%d|%s|%c|%.*s", 42, "xy", 65, 3, "abcdef");
	printf("sprintf %s\n", sbuf);
	sprintf(sbuf, "%ld", -123456);
	printf("langzahl %s\n", sbuf);

	/* realloc muss den Inhalt mitnehmen -- der erzeugte Parser
	   verdoppelt damit sein Aktions-Log. Erst 100 Byte fuellen, dann auf
	   4000 wachsen lassen und alles nachpruefen. */
	p = realloc(0, 100);
	if (p == 0) {
		printf("realloc 1 geht nicht\n");
		return 1;
	}
	i = 0;
	while (i < 100) {
		p[i] = 33 + (i % 60);
		i++;
	}
	q = realloc(p, 4000);
	if (q == 0) {
		printf("realloc 2 geht nicht\n");
		return 1;
	}
	gut = 1;
	i = 0;
	while (i < 100) {
		if ((q[i] & 255) != 33 + (i % 60))
			gut = 0;
		i++;
	}
	/* Und der neue Teil muss beschreibbar sein, sonst war die gewaehrte
	   Groesse eine Luege. */
	i = 100;
	while (i < 4000) {
		q[i] = 7;
		i++;
	}
	printf("realloc %d %d\n", gut, q[3999] & 255);

	/* fprintf, fputs und fputc in eine Datei und wieder zurueck. */
	fp = fopen("/dd/qftest2.txt", "w");
	if (fp == 0) {
		printf("fopen w2 geht nicht\n");
		return 1;
	}
	fprintf(fp, "fp %d %s %c\n", 7, "sieben", 55);
	n = fputs("fputs ohne Umbruch", fp);
	i = fputc(33, fp);
	fclose(fp);
	printf("rueckgaben %d %d\n", n >= 0, i);

	/* Die acht Funktionen, die qcc_backend_c.cpp braucht. fgets und ferror
	   stehen NICHT hier: fuer fgets ist clib kein gueltiges Orakel (es
	   trennt an $0d, siehe test/lineend68k.sh), und ferror liesse sich
	   ohne einen echten Schreibfehler nicht vergleichen. */
	printf("strcmp %d %d %d\n", vorz(strcmp("ab", "ab")),
	       vorz(strcmp("ab", "ac")), vorz(strcmp("b", "a")));

	sbuf[0] = 0;
	strcat(sbuf, "abc");
	strcat(sbuf, "de");
	printf("strcat %s\n", sbuf);

	/* strncpy fuellt eine kuerzere Quelle mit Nullen AUF -- bis n, nicht
	   weiter. Das '#' an Stelle 5 muss also stehen bleiben. */
	memset(sbuf, 35, 8);
	strncpy(sbuf, "xy", 5);
	printf("strncpy %d %d %d %d %d %d\n", sbuf[0] & 255, sbuf[1] & 255,
	       sbuf[2] & 255, sbuf[3] & 255, sbuf[4] & 255, sbuf[5] & 255);

	printf("strrchr %s\n", strrchr("a/b/c", 47));

	/* strtok SCHREIBT in seinen Puffer, deshalb ein eigenes Feld und kein
	   Zeichenkettenliteral. Zwei Trenner hintereinander duerfen kein
	   leeres Feld ergeben. */
	strncpy(tbuf, "eins zwei  drei", 19);
	tbuf[19] = 0;
	p = strtok(tbuf, " ");
	n = 0;
	while (p != 0) {
		printf("tok %d %s\n", n, p);
		n++;
		p = strtok(0, " ");
	}

	printf("strtol %d %d %d\n", strtol("42", &ende, 10),
	       strtol("-7", &ende, 10), strtol("ff", &ende, 16));
	i = strtol("123abc", &ende, 10);
	printf("strtol-ende %d %s\n", i, ende);

	memset(sbuf, 46, 6);
	memcpy(sbuf, "AB", 2);
	printf("memcpy %d %d %d\n", sbuf[0] & 255, sbuf[1] & 255, sbuf[2] & 255);

	fp = fopen("/dd/qftest2.txt", "r");
	if (fp == 0) {
		printf("fopen r2 geht nicht\n");
		return 1;
	}
	n = fread(buf, 1, 63, fp);
	i = n;
	if (i < 0)
		i = 0;
	buf[i] = 0;
	printf("zurueck %d: %s\n", n, buf);
	fclose(fp);
	/* EINE SCHLUSSMARKE, an der die Emulatorlaeufe erkennen, dass das
	   Programm durch ist. Ohne sie warteten sie auf den PROMPT -- und der
	   steht nach dem Login noch im Puffer, trifft also sofort und der
	   Escape killt das Modul, bevor es etwas ausgibt. Das ist am
	   2026-09-07 einmal passiert (0 von 14 Zeilen), war beim naechsten
	   Lauf wieder gruen und damit ein WACKLER -- schlimmer als ein
	   Fehlschlag. Dieselbe Falle steht in Q9-QCC/q9-cpp/test/
	   run_selfhost_68k.exp ausgeschrieben. */
	printf("hello fertig\n");
	return 0;
}
