extern int printf(const char *fmt, ...);

int main()
{
	printf("Hallo %s\n", "Welt");
	printf("%d %d %d\n", 42, -7, 0);
	printf("hex %x, Zeichen %c, Prozent %%\n", 255, 65);
	return 0;
}
