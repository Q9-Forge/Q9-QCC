/* dec2ieee.c -- Dezimalliteral nach IEEE-754 (binary64), OHNE Gleitkomma.
 *
 * Warum ohne: QCC uebersetzt sich selbst. Waehrend dieser Uebersetzung steht
 * kein Gleitkomma zur Verfuegung -- der Compiler muss das Bitmuster eines
 * Literals also mit Ganzzahlen ausrechnen. Das ist die Henne-Ei-Frage des
 * ganzen Vorhabens.
 *
 * C89. Keine long long, keine unsigned long long: auf dem 68k ist
 * unsigned long 32 Bit, auf dem Mac 64 -- also wird durchweg mit Gliedern
 * zu 16 Bit gerechnet, deren Produkte immer in 32 Bit passen.
 */



typedef struct {
    int n;                      /* Anzahl belegter Glieder, 0 = Wert null */
    unsigned long d[260]; /* Basis 2^16, kleinstes Glied zuerst.
                             260 Glieder = 4160 Bit, genug fuer 10^325
                             samt Schiebeplatz. */
} Big;

/* Zwischenwerte auf Dateiebene statt auf dem Stack: jede dieser Strukturen
 * ist gut ein Kilobyte gross, und der Stack eines OS-9-Moduls ist knapp.
 * Der Konverter ist dadurch nicht wiedereintrittsfaehig -- ein Compiler
 * braucht das nicht. (QCC kennt ausserdem kein 'static' an lokalen
 * Variablen, s. docs/KNOWN_BUGS_C89_de.md.) */
static Big d2iCur, d2iRound, d2iMant, d2iDen, d2iNum, d2iQuot, d2iRem;

static void bigZero(Big* a) { a->n = 0; }

static void bigTrim(Big* a) {
    while (a->n > 0 && a->d[a->n - 1] == 0UL) a->n--;
}

static int bigIsZero(const Big* a) { return a->n == 0; }

static void bigSetSmall(Big* a, unsigned long v) {
    bigZero(a);
    while (v != 0UL && a->n < 260) {
        a->d[a->n++] = v & 0xFFFFUL;
        v >>= 16;
    }
}

/* a = a * d2iRound + add,  d2iRound und add unter 2^16 */
static int bigMulAddSmall(Big* a, unsigned long d2iRound, unsigned long add) {
    unsigned long carry = add;
    int i;
    for (i = 0; i < a->n; i++) {
        unsigned long t = a->d[i] * d2iRound + carry;
        a->d[i] = t & 0xFFFFUL;
        carry = t >> 16;
    }
    while (carry != 0UL) {
        if (a->n >= 260) return 0;      /* Ueberlauf */
        a->d[a->n++] = carry & 0xFFFFUL;
        carry >>= 16;
    }
    return 1;
}

/* Anzahl signifikanter Bits */
static long bigBitLen(const Big* a) {
    unsigned long top;
    long bits;
    if (a->n == 0) return 0L;
    top = a->d[a->n - 1];
    bits = 0L;
    while (top != 0UL) { bits++; top >>= 1; }
    return (long)(a->n - 1) * 16L + bits;
}

static int bigGetBit(const Big* a, long bit) {
    long limb = bit / 16L;
    if (bit < 0L || limb >= (long)a->n) return 0;
    return (int)((a->d[limb] >> (bit % 16L)) & 1UL);
}

/* Sind unter Bit `bit` noch gesetzte Bits? (Sticky) */
static int bigAnyBitBelow(const Big* a, long bit) {
    long i;
    long limb = bit / 16L;
    int off = (int)(bit % 16L);
    for (i = 0L; i < limb && i < (long)a->n; i++)
        if (a->d[i] != 0UL) return 1;
    if (off > 0 && limb < (long)a->n) {
        unsigned long mask = (1UL << off) - 1UL;
        if ((a->d[limb] & mask) != 0UL) return 1;
    }
    return 0;
}

static int bigShiftLeft(Big* a, long bits) {
    long limbShift = bits / 16L;
    int bitShift = (int)(bits % 16L);
    long i;
    if (bigIsZero(a) || bits <= 0L) return 1;
    if (a->n + limbShift + 1L > (long)260) return 0;
    if (limbShift > 0L) {
        for (i = (long)a->n - 1L; i >= 0L; i--) a->d[i + limbShift] = a->d[i];
        for (i = 0L; i < limbShift; i++) a->d[i] = 0UL;
        a->n += (int)limbShift;
    }
    if (bitShift > 0) {
        unsigned long carry = 0UL;
        for (i = limbShift; i < (long)a->n; i++) {
            unsigned long t = (a->d[i] << bitShift) | carry;
            a->d[i] = t & 0xFFFFUL;
            carry = t >> 16;
        }
        if (carry != 0UL) {
            if (a->n >= 260) return 0;
            a->d[a->n++] = carry;
        }
    }
    return 1;
}

static void bigShiftRight(Big* a, long bits) {
    long limbShift = bits / 16L;
    int bitShift = (int)(bits % 16L);
    long i;
    if (bigIsZero(a) || bits <= 0L) return;
    if (limbShift >= (long)a->n) { bigZero(a); return; }
    if (limbShift > 0L) {
        for (i = 0L; i + limbShift < (long)a->n; i++) a->d[i] = a->d[i + limbShift];
        a->n -= (int)limbShift;
    }
    if (bitShift > 0) {
        for (i = 0L; i < (long)a->n; i++) {
            unsigned long lo = a->d[i] >> bitShift;
            unsigned long hi = (i + 1L < (long)a->n) ? a->d[i + 1L] : 0UL;
            a->d[i] = (lo | (hi << (16 - bitShift))) & 0xFFFFUL;
        }
    }
    bigTrim(a);
}

/* -1, 0, +1 */
static int bigCmp(const Big* a, const Big* b) {
    int i;
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}

/* a -= b, setzt voraus a >= b */
static void bigSub(Big* a, const Big* b) {
    long borrow = 0L;
    int i;
    for (i = 0; i < a->n; i++) {
        long t = (long)a->d[i] - borrow - (i < b->n ? (long)b->d[i] : 0L);
        if (t < 0L) { t += 65536L; borrow = 1L; } else borrow = 0L;
        a->d[i] = (unsigned long)t;
    }
    bigTrim(a);
}

static void bigCopy(Big* dst, const Big* src) {
    int i;
    dst->n = src->n;
    for (i = 0; i < src->n; i++) dst->d[i] = src->d[i];
}

static int bigSetPow10(Big* a, long e) {
    long i;
    bigSetSmall(a, 1UL);
    for (i = 0L; i + 4L <= e; i += 4L)
        if (!bigMulAddSmall(a, 10000UL, 0UL)) return 0;
    for (; i < e; i++)
        if (!bigMulAddSmall(a, 10UL, 0UL)) return 0;
    return 1;
}

/* q = d2iNum / d2iDen, rest bleibt in d2iNum. Schulmethode, bitweise. */
static int bigDivMod(const Big* d2iNum, const Big* d2iDen, Big* q, Big* d2iRem) {
    long shift, i;
    if (bigIsZero(d2iDen)) return 0;
    bigZero(q);
    bigZero(&d2iCur);
    shift = bigBitLen(d2iNum) - 1L;
    if (shift < 0L) { bigZero(d2iRem); return 1; }
    q->n = (int)(shift / 16L) + 1;
    for (i = 0L; i < (long)q->n; i++) q->d[i] = 0UL;
    for (i = shift; i >= 0L; i--) {
        if (!bigShiftLeft(&d2iCur, 1L)) return 0;
        if (bigGetBit(d2iNum, i)) {
            if (d2iCur.n == 0) { d2iCur.n = 1; d2iCur.d[0] = 1UL; }
            else d2iCur.d[0] |= 1UL;
        }
        if (bigCmp(&d2iCur, d2iDen) >= 0) {
            bigSub(&d2iCur, d2iDen);
            q->d[i / 16L] |= (1UL << (i % 16L));
        }
    }
    bigTrim(q);
    bigCopy(d2iRem, &d2iCur);
    return 1;
}

static unsigned long bigLimb(const Big* a, int i) {
    return (i < a->n) ? a->d[i] : 0UL;
}

/* Rundet d2iDen Wert q * 2^e0 (zuzueglich eines Restes, d2iDen stickyIn anzeigt)
 * auf binary64 und liefert die beiden 32-Bit-Haelften.
 *
 * Alles laeuft ueber die Stelle `drop`: so viele Bits fallen unten weg.
 * Fuer normale Zahlen ergibt sie sich aus der Bitlaenge, fuer denormale aus
 * der festen kleinsten Stufe 2^-1074 -- daher das Maximum der beiden.
 */
static void roundToDouble(const Big* q, long e0, int stickyIn, int neg,
                          unsigned long* hi, unsigned long* lo) {
    long len, drop, exp2, biased;   /* 53 = Mantissenbits von binary64 */
    int roundBit, sticky;
    unsigned long field_lo, field_hi;

    if (bigIsZero(q) && !stickyIn) {
        *hi = neg ? 0x80000000UL : 0UL;
        *lo = 0UL;
        return;
    }

    len = bigBitLen(q);
    drop = len - (long)53;
    if (drop < -1074L - e0) drop = -1074L - e0;   /* nicht unter die kleinste Stufe */

    bigCopy(&d2iRound, q);
    if (drop <= 0L) {
        bigShiftLeft(&d2iRound, -drop);
        roundBit = 0;
        sticky = stickyIn;
    } else {
        roundBit = bigGetBit(q, drop - 1L);
        sticky = bigAnyBitBelow(q, drop - 1L) || stickyIn;
        bigShiftRight(&d2iRound, drop);
    }

    /* Zur naechsten Zahl, bei genau der Haelfte zur geraden (Ties-to-even) */
    if (roundBit && (sticky || (bigLimb(&d2iRound, 0) & 1UL))) {
        unsigned long carry = 1UL;
        int i = 0;
        while (carry != 0UL) {
            if (i >= d2iRound.n) d2iRound.d[d2iRound.n++] = 0UL;
            d2iRound.d[i] += carry;
            carry = d2iRound.d[i] >> 16;
            d2iRound.d[i] &= 0xFFFFUL;
            i++;
        }
    }

    /* Traegt das Aufrunden ein Bit ueber, verschiebt sich der Exponent. */
    if (bigBitLen(&d2iRound) > (long)53) {
        bigShiftRight(&d2iRound, 1L);
        drop += 1L;
    }

    exp2 = e0 + drop;

    if (bigBitLen(&d2iRound) == (long)53) {
        biased = exp2 + 52L + 1023L;
    } else {
        biased = 0L;                 /* denormal: kein implizites Bit */
    }

    if (biased >= 2047L) {           /* Ueberlauf -> unendlich */
        *hi = (neg ? 0x80000000UL : 0UL) | 0x7FF00000UL;
        *lo = 0UL;
        return;
    }

    field_lo = bigLimb(&d2iRound, 0) | (bigLimb(&d2iRound, 1) << 16);
    field_hi = (bigLimb(&d2iRound, 2) | ((bigLimb(&d2iRound, 3) & 0xFUL) << 16)) & 0xFFFFFUL;

    *lo = field_lo;
    *hi = (neg ? 0x80000000UL : 0UL)
        | ((unsigned long)(biased & 0x7FFL) << 20)
        | field_hi;
}

/* Wandelt das Literal zwischen start und end um.
 * Rueckgabe: 1 = in Ordnung, 0 = kein gueltiges Gleitkommaliteral.
 *
 * Die Big-Zwischenwerte stehen bewusst als static im Datenbereich und nicht
 * auf dem Stack: unter OS-9 ist der Stack eines Moduls knapp bemessen, und
 * fuenf dieser Strukturen waeren gut sechs Kilobyte.
 */

int qccDecToDouble(const char* start, const char* end, int neg,
                   unsigned long* hi, unsigned long* lo) {
    const char* p = start;
    long decExp = 0L;
    long ndig = 0L;
    int seenDigit = 0, seenDot = 0, cut = 0;
    long e;

    bigSetSmall(&d2iMant, 0UL);

    while (p < end) {
        if (*p >= '0' && *p <= '9') {
            seenDigit = 1;
            if (ndig < 800) {
                if (!(bigIsZero(&d2iMant) && *p == '0')) {
                    if (!bigMulAddSmall(&d2iMant, 10UL, (unsigned long)(*p - '0'))) return 0;
                    ndig++;
                }
            } else {
                if (*p != '0') cut = 1;      /* abgeschnittene Ziffern nur als Rest */
                decExp++;
            }
            if (seenDot) decExp--;
            p++;
        } else if (*p == '.' && !seenDot) {
            seenDot = 1;
            p++;
        } else break;
    }
    if (!seenDigit) return 0;

    if (p < end && (*p == 'e' || *p == 'E')) {
        int esign = 0;
        long ev = 0L;
        const char* q = p + 1;
        if (q < end && (*q == '+' || *q == '-')) { esign = (*q == '-'); q++; }
        if (q >= end || *q < '0' || *q > '9') return 0;
        while (q < end && *q >= '0' && *q <= '9') {
            if (ev < 100000L) ev = ev * 10L + (long)(*q - '0');
            q++;
        }
        decExp += esign ? -ev : ev;
        p = q;
    }
    while (p < end && (*p == 'f' || *p == 'F' || *p == 'l' || *p == 'L')) p++;
    if (p != end) return 0;

    if (bigIsZero(&d2iMant)) {
        *hi = neg ? 0x80000000UL : 0UL;
        *lo = 0UL;
        return 1;
    }

    /* Grob abschaetzen, bevor gerechnet wird: sonst waechst 10^E ins Uferlose */
    if (decExp + ndig > 330L) {
        *hi = (neg ? 0x80000000UL : 0UL) | 0x7FF00000UL;
        *lo = 0UL;
        return 1;
    }
    if (decExp + ndig < -360L) {
        *hi = neg ? 0x80000000UL : 0UL;
        *lo = 0UL;
        return 1;
    }

    if (decExp >= 0L) {
        /* Ganzzahl: Mantisse mal 10^E, dann runden */
        bigCopy(&d2iNum, &d2iMant);
        for (e = 0L; e < decExp; e++)
            if (!bigMulAddSmall(&d2iNum, 10UL, 0UL)) return 0;
        roundToDouble(&d2iNum, 0L, cut, neg, hi, lo);
    } else {
        /* Bruch: Zaehler so weit hochschieben, dass der Quotient reichlich
         * ueber 53 Bit hat -- der Divisionsrest wird zum Sticky-Bit. */
        long s;
        if (!bigSetPow10(&d2iDen, -decExp)) return 0;
        bigCopy(&d2iNum, &d2iMant);
        s = 64L + bigBitLen(&d2iDen) - bigBitLen(&d2iNum);
        if (s < 0L) s = 0L;
        if (!bigShiftLeft(&d2iNum, s)) return 0;
        if (!bigDivMod(&d2iNum, &d2iDen, &d2iQuot, &d2iRem)) return 0;
        roundToDouble(&d2iQuot, -s, cut || !bigIsZero(&d2iRem), neg, hi, lo);
    }
    return 1;
}

