/* String literals. Demonstrates GARRAY/GINIT for the implicit char buffer
   including the NUL terminator, ADDRG as a literal address, and direct
   indexing without an intermediate variable. */
int first_char(void) {
    return "Hallo"[0];
}

char *greeting(void) {
    return "Hallo, Q9!";
}

int main(void) {
    char *s;
    putint(first_char()); /* erwartet: 72 ('H') */
    s = greeting();
    while (*s) {
        putchar(*s);
        s = s + 1;
    }
    putchar(10); /* line break */
    return 0;
}
