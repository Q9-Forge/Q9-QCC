/* Portable integer conversion subset derived from the 68k qclib semantics. */

long strtol(const char *s, char **end, int base)
{
    unsigned long value = 0;
    int sign = 1;
    int digit;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') ++s;
    if (*s == '-') { sign = -1; ++s; } else if (*s == '+') ++s;
    if (base == 0) {
        base = 10;
        if (s[0] == '0') { base = 8; if (s[1] == 'x' || s[1] == 'X') { base = 16; s += 2; } }
    }
    while (*s != 0) {
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        value = value * (unsigned long)base + (unsigned long)digit;
        ++s;
    }
    if (end != (char **)0) *end = (char *)s;
    return sign < 0 ? -(long)value : (long)value;
}

unsigned long strtoul(const char *s, char **end, int base) { return (unsigned long)strtol(s, end, base); }
int atoi(const char *s) { return (int)strtol(s, (char **)0, 10); }
long atol(const char *s) { return strtol(s, (char **)0, 10); }
int abs(int value) { return value < 0 ? -value : value; }
long labs(long value) { return value < 0 ? -value : value; }
