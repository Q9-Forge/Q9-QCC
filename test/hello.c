extern int printf(const char *fmt, ...);
extern char *fopen(const char *name, const char *mode);
extern int fclose(char *fp);
extern int fread(char *buf, int size, int n, char *fp);
extern int fwrite(const char *buf, int size, int n, char *fp);
extern int puts(const char *s);

int main()
{
	char *fp;
	char buf[64];
	int n;
	int i;

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
	return 0;
}
