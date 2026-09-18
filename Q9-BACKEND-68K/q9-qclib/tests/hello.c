/* Library comparison test.
 *
 * It is linked TWICE, once against qclib and once against Microware clib,
 * und beide Module laufen im selben Emulatorlauf (test/vsclib.sh). Was
 * hier steht, muss also gegen BEIDE Bibliotheken dasselbe ausgeben.
 *
 * This gives two rules for new cases:
 *
 * 1. Test only STANDARDIZED behavior. C89 specifies that strncmp returns
 *    VORZEICHEN zurueck ("greater than, equal to, or less than zero"),
 *    keinen bestimmten Wert -- clib liefert die Bytedifferenz, qclib
 *    -1/0/1. Deshalb geht alles durch vorz() statt direkt in die
 *    Ausgabe.
 * 2. Do not print pointers; addresses necessarily differ.
 *
 * The stderr case (FILE* == 0) is intentionally NOT here: qclib maps it
 * auf Pfad 2, clib laeuft damit ins Ungewisse -- vergleichen liesse sich
 * das nicht. Er gehoert in einen Test, der nur qclib faehrt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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

	/* The three string functions. strchr returns a pointer INTO the
	   Zeichenkette zurueck, der Rest der Zeichenkette ist also der
	   Nachweis; der Fehlschlag wird als 0/1 geprueft, nicht gedruckt. */
	printf("str %d %s %d %d %d\n", strlen("Hallo Datei"),
	       strchr("Hallo Datei", 68), vorz(strncmp("ab", "ab", 2)),
	       vorz(strncmp("ab", "ac", 2)), vorz(strncmp("ac", "ab", 2)));
	printf("strchr0 %d\n", strchr("abc", 122) == 0);

	/* sprintf, including .* precision and the l length modifier. */
	sprintf(sbuf, "%d|%s|%c|%.*s", 42, "xy", 65, 3, "abcdef");
	printf("sprintf %s\n", sbuf);
	sprintf(sbuf, "%ld", -123456);
	printf("langzahl %s\n", sbuf);

	/* realloc must preserve the content -- the generated parser
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
	/* The new portion must also be writable, otherwise the granted
	   Groesse eine Luege. */
	i = 100;
	while (i < 4000) {
		q[i] = 7;
		i++;
	}
	printf("realloc %d %d\n", gut, q[3999] & 255);

	/* fprintf, fputs, and fputc to a file and back. */
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

	/* The eight functions required by qcc_backend_c.cpp. fgets and ferror
	   stehen NICHT hier: fuer fgets ist clib kein gueltiges Orakel (es
	   trennt an $0d, siehe test/lineend68k.sh), und ferror liesse sich
	   ohne einen echten Schreibfehler nicht vergleichen. */
	printf("strcmp %d %d %d\n", vorz(strcmp("ab", "ab")),
	       vorz(strcmp("ab", "ac")), vorz(strcmp("b", "a")));

	sbuf[0] = 0;
	strcat(sbuf, "abc");
	strcat(sbuf, "de");
	printf("strcat %s\n", sbuf);

	/* strncpy pads a shorter source with zeros up to n, not
	   weiter. Das '#' an Stelle 5 muss also stehen bleiben. */
	memset(sbuf, 35, 8);
	strncpy(sbuf, "xy", 5);
	printf("strncpy %d %d %d %d %d %d\n", sbuf[0] & 255, sbuf[1] & 255,
	       sbuf[2] & 255, sbuf[3] & 255, sbuf[4] & 255, sbuf[5] & 255);

	printf("strrchr %s\n", strrchr("a/b/c", 47));

	/* strtok WRITES into its buffer, so use a dedicated field and no
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

	/* Gap-closing batch 1 (2026-09-18, STATUS.md): ctype completion,
	   strcpy, putchar/putc. abort() is NOT here -- it does not return,
	   so it gets its own dedicated test instead of this shared one. */
	printf("ctype %d %d %d %d %d %d %d %d\n",
	       isdigit('5'), isdigit('a'), isupper('A'), isupper('a'),
	       islower('a'), islower('A'), isxdigit('f'), isxdigit('g'));
	printf("ctype2 %d %d %d %d %d\n",
	       iscntrl(9), iscntrl('a'), isgraph(' '), isgraph('a'),
	       ispunct('.'));
	printf("toupper %c %c %c\n", toupper('a'), toupper('A'), toupper('9'));

	strcpy(sbuf, "kopiert");
	printf("strcpy %s\n", sbuf);

	putchar('X');
	putchar('\n');

	fp = fopen("/dd/qftest3.txt", "w");
	if (fp == 0) {
		printf("fopen w3 geht nicht\n");
		return 1;
	}
	n = putc('Y', fp);
	fclose(fp);
	printf("putc %d\n", n);

	/* ONE END MARKER used by emulator runs to detect that the
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
