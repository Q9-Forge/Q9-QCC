
/* ---------------------------------------------------------------------
 * Testtreiber. Uebersetzt mit clang/gcc genauso wie mit QCC selbst und
 * laeuft daher im VM-Orakel wie auf echter 68030-Hardware. Die Sollwerte
 * sind die Bitmuster, die IEEE-754 vorschreibt -- nachgerechnet, nicht
 * vom Konverter selbst erzeugt.
 * ------------------------------------------------------------------ */

/* Im VM-Orakel gibt es kein printf -- dort wird nur die Zahl der Fehler
 * ueber putint gemeldet (qcpp -DD2I_QUIET). Auf dem Zielrechner und auf
 * dem Host soll dagegen sichtbar sein, WELCHER Fall scheitert. */
#ifdef D2I_QUIET
extern void putint(int);
#define D2I_SAY1(f,a)           /* nichts */

#else
extern int printf(char*, ...);
#define D2I_SAY1(f,a)           printf(f,a)

#endif

char* d2iCase[] = { "0.0", "1.0", "0.5", "1.5", "3.14", "0.1", "2.5", "100.0", "7.0", "1e10", "1e-10", "1e20", "1e-20", "1e100", "1e-100", "1e308", "1e309", "5e-324", "1e-320", "9007199254740993", "0.49999999999999994", "1.0000000000000002", "3.141592653589793", "123456789012345678901234567890", "2.2250738585072014e-308" };
unsigned long d2iWantHi[] = { 0x00000000UL, 0x3FF00000UL, 0x3FE00000UL, 0x3FF80000UL, 0x40091EB8UL, 0x3FB99999UL, 0x40040000UL, 0x40590000UL, 0x401C0000UL, 0x4202A05FUL, 0x3DDB7CDFUL, 0x4415AF1DUL, 0x3BC79CA1UL, 0x54B249ADUL, 0x2B2BFF2EUL, 0x7FE1CCF3UL, 0x7FF00000UL, 0x00000000UL, 0x00000000UL, 0x43400000UL, 0x3FDFFFFFUL, 0x3FF00000UL, 0x400921FBUL, 0x45F8EE90UL, 0x00100000UL };
unsigned long d2iWantLo[] = { 0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL, 0x51EB851FUL, 0x9999999AUL, 0x00000000UL, 0x00000000UL, 0x00000000UL, 0x20000000UL, 0xD9D7BDBBUL, 0x78B58C40UL, 0x0C924223UL, 0x2594C37DUL, 0xE48E0530UL, 0x85EBC8A0UL, 0x00000000UL, 0x00000001UL, 0x000007E8UL, 0x00000000UL, 0xFFFFFFFFUL, 0x00000001UL, 0x54442D18UL, 0xFF6C373EUL, 0x00000000UL };

static long d2iLen(char* s) {
    long n = 0L;
    while (s[n] != 0) n++;
    return n;
}

int main()
{
    int i, bad;
    unsigned long hi, lo;
    bad = 0;
    i = 0;
    while (i < 25) {
        hi = 0UL; lo = 0UL;
        if (!qccDecToDouble(d2iCase[i], d2iCase[i] + d2iLen(d2iCase[i]), 0, &hi, &lo)) {
            D2I_SAY1("FEHLER unlesbar: %s\n", d2iCase[i]);
            bad = bad + 1;
        } else if (hi != d2iWantHi[i] || lo != d2iWantLo[i]) {
            D2I_SAY1("FALSCH %s\n", d2iCase[i]);
            bad = bad + 1;
        } else {
            D2I_SAY1("ok %s\n", d2iCase[i]);
        }
        i = i + 1;
    }
#ifdef D2I_QUIET
    putint(bad);
#else
    printf("dec2ieee fertig: %d von %d falsch\n", bad, 25);
#endif
    return 0;
}
