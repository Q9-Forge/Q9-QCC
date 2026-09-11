/* qcpp test source for a real 68030. Covers macros, #, ##, precedence in
   #if, // comments, #asm, and the Q9 predefined identifiers. */
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
/* Unsigned #if arithmetic: on the target this passes through QCC's
   CMPUGT/UDIV/USHR. The emulator therefore checks unsigned code generation,
   not only qcpp's evaluator. */
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
