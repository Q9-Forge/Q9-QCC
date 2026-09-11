/* String literals. Demonstrates GARRAY/GINIT for the implicit char buffer
   samt Nullterminator, ADDRG als Adresse eines Literals, direkte
   Indizierung ohne Zwischenvariable. */
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
    putchar(10); /* Zeilenumbruch */
    return 0;
}
