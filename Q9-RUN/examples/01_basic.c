/* Einfachste Form: lokale Variablen, Arithmetik, Aufruf, Rueckgabewert.
   Zeigt: FUNC/ENDFUNC/RET, PUSH, LOADL/STOREL, ADD/MUL, einen echten CALL. */
int square(int x) {
    return x * x;
}

int add_and_square(int a, int b) {
    int sum;
    sum = a + b;
    return square(sum);
}

int main(void) {
    putint(square(5));           /* erwartet: 25 */
    putint(add_and_square(2, 3)); /* erwartet: 25 */
    return 0;
}
