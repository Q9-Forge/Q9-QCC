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

extern int printf(const char *fmt, ...);
extern int fprintf(char *fp, const char *fmt, ...);
extern int sprintf(char *buf, const char *fmt, ...);
extern char *fopen(const char *name, const char *mode);
extern int fclose(char *fp);
extern int fread(char *buf, int size, int n, char *fp);
extern int fwrite(const char *buf, int size, int n, char *fp);
extern int fputc(int c, char *fp);
extern int fputs(const char *s, char *fp);
extern int puts(const char *s);
extern int strlen(const char *s);
extern char *strchr(const char *s, int c);
extern int strncmp(const char *a, const char *b, int n);
extern char *realloc(char *p, int n);

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
	char *fp;
	char *p;
	char *q;
	char buf[64];
	char sbuf[64];
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
	return 0;
}
