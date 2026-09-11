/* Pruefquelle fuer qcpp auf echtem 68030. Deckt Makros, #, ##, #if mit
   Vorrang, //-Kommentar, #asm und die Q9-Kennungen ab. */
#define N 40
#define ADD(a,b) ((a)+(b))
#define STR(x) #x
#define XSTR(x) STR(x)
#define CAT(a,b) a##b
int CAT(wert,1) = ADD(N,2);   // 42
char *s1 = STR(N);
char *s2 = XSTR(N);
#if N * 2 + 2 == 82 && !defined(NICHTDA)
int vorrang = 1;
#endif
#if defined(_Q9) && defined(_Q9OS) && defined(_OSK) && defined(_UCC)
int kennungen = 4;
#endif
#ifdef _OS9000
int falsch = 1;
#endif
/* Vorzeichenlose #if-Arithmetik: auf dem Ziel laeuft das durch QCCs
   CMPUGT/UDIV/USHR -- der Lauf im Emulator prueft also mit, ob QCCs
   vorzeichenlose Codeerzeugung stimmt, nicht nur qcpps Auswerter. */
#if 0xFFFFFFFF > 0 && 0x80000000 / 2 == 0x40000000 && 0x80000000 >> 4 == 0x08000000
int unsigned_ok = 7;
#endif
#if -1 < 0 && -7 / 2 == -3 && (-8 >> 1) == -4
int signed_ok = 8;
#endif
#asm
 move.l  #N,d0
 nop                    * asm-Kommentar
#endasm
int ende = 99;
