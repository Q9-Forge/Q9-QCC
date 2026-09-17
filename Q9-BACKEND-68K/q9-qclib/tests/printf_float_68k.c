/* printf("%f") auf echtem 68030 -- Hardwaretest fuer die IEEE-754-zu-
 * Dezimal-Umwandlung (qp_double) UND die Argument-Zuordnung im
 * CALLEXT-Byte-Offset-Modell (erstes variadisches Argument als double vs.
 * als Nicht-double, s. docs/FLOAT_PLAN_de.md).
 */
extern int printf(char *, ...);

int main(void)
{
	double x, y, z;

	x = 3.5;
	y = -3.5;
	z = 0.0;

	printf("A %f\n", x);            /* double ZUERST -- der kritische Fall */
	printf("B %f\n", y);            /* negativ */
	printf("C %f\n", z);            /* Null */
	printf("D %d %f\n", 7, x);      /* int zuerst, double zweitens */
	printf("E %f %d\n", x, 7);      /* double zuerst, dann int -- sticky spill */
	printf("F %f %f\n", x, y);      /* zwei double */
	z = 1.0 / 3.0;
	printf("G %f\n", z);            /* braucht echte Rundung */
	z = 100.0;
	printf("H %f\n", z);
	z = 0.001;
	printf("I %f\n", z);
	z = 123456.789;
	printf("J %f\n", z);
	printf("K %f %f %f\n", 2.0, 7.0, x); /* drei double hintereinander */
	return 0;
}
