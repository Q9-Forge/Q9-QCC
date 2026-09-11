/* Verzweigung und Schleifen. Zeigt: LABEL, bedingte/unbedingte Spruenge,
   Vergleichsoperatoren (CMPLT/CMPEQ/...), das Zusammenspiel von Bedingung
   und Sprungziel in der Stack-IR. */
int max(int a, int b) {
    if (a > b) {
        return a;
    }
    return b;
}

int sum_below(int n) {
    int i, total;
    total = 0;
    i = 0;
    while (i < n) {
        total = total + i;
        i = i + 1;
    }
    return total;
}

int main(void) {
    putint(max(3, 7));      /* erwartet: 7 */
    putint(sum_below(5));   /* erwartet: 0+1+2+3+4 = 10 */
    return 0;
}
