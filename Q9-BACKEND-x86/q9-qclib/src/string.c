/* Portable byte and string routines shared in behaviour with 68k qclib.
 * Unlike the 68k sources, these functions use the i386 C ABI directly. */

typedef unsigned int size_t;

void *memcpy(void *dst, const void *src, size_t n)
{
    size_t i;
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    size_t i;
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) for (i = 0; i < n; ++i) d[i] = s[i];
    else while (n != 0) { --n; d[n] = s[n]; }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    size_t i;
    unsigned char *d = (unsigned char *)dst;
    for (i = 0; i < n; ++i) d[i] = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    size_t i;
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (i = 0; i < n; ++i) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    size_t i;
    const unsigned char *p = (const unsigned char *)s;
    for (i = 0; i < n; ++i) if (p[i] == (unsigned char)c) return (void *)(p + i);
    return (void *)0;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n] != 0) ++n; return n; }

char *strcpy(char *dst, const char *src)
{
    char *result = dst;
    while ((*dst++ = *src++) != 0) { }
    return result;
}

char *strchr(const char *s, int c)
{
    for (;;) { if (*s == (char)c) return (char *)s; if (*s++ == 0) return (char *)0; }
}

char *strrchr(const char *s, int c)
{
    const char *result = (const char *)0;
    do { if (*s == (char)c) result = s; } while (*s++ != 0);
    return (char *)result;
}

int strcmp(const char *a, const char *b)
{
    while (*a != 0 && *a == *b) { ++a; ++b; }
    return (unsigned char)*a == (unsigned char)*b ? 0 :
           ((unsigned char)*a < (unsigned char)*b ? -1 : 1);
}

int strncmp(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if ((unsigned char)a[i] != (unsigned char)b[i])
            return (unsigned char)a[i] < (unsigned char)b[i] ? -1 : 1;
        if (a[i] == 0) return 0;
    }
    return 0;
}

char *strcat(char *dst, const char *src)
{
    char *result = dst;
    while (*dst != 0) ++dst;
    while ((*dst++ = *src++) != 0) { }
    return result;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    while (i < n && src[i] != 0) { dst[i] = src[i]; ++i; }
    while (i < n) dst[i++] = 0;
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst + strlen(dst);
    while (n != 0 && *src != 0) { *d++ = *src++; --n; }
    *d = 0;
    return dst;
}

static int contains(const char *set, int c) { return strchr(set, c) != (char *)0; }

char *strtok(char *s, const char *delim)
{
    static char *next;
    char *start;
    if (s != (char *)0) next = s;
    if (next == (char *)0) return (char *)0;
    while (*next != 0 && contains(delim, (unsigned char)*next)) ++next;
    if (*next == 0) { next = (char *)0; return (char *)0; }
    start = next;
    while (*next != 0 && !contains(delim, (unsigned char)*next)) ++next;
    if (*next != 0) *next++ = 0; else next = (char *)0;
    return start;
}

char *strstr(const char *haystack, const char *needle)
{
    const char *p;
    if (*needle == 0) return (char *)haystack;
    for (p = haystack; *p != 0; ++p)
        if (*p == *needle && strncmp(p, needle, strlen(needle)) == 0) return (char *)p;
    return (char *)0;
}

size_t strspn(const char *s, const char *accept) { size_t n = 0; while (s[n] && contains(accept, (unsigned char)s[n])) ++n; return n; }
size_t strcspn(const char *s, const char *reject) { size_t n = 0; while (s[n] && !contains(reject, (unsigned char)s[n])) ++n; return n; }
char *strpbrk(const char *s, const char *accept) { while (*s) { if (contains(accept, (unsigned char)*s)) return (char *)s; ++s; } return (char *)0; }
int strcoll(const char *a, const char *b) { return strcmp(a, b); }
size_t strxfrm(char *dst, const char *src, size_t n) { size_t len = strlen(src); if (n) { size_t copy = len < n - 1 ? len : n - 1; memcpy(dst, src, copy); dst[copy] = 0; } return len; }
char *strerror(int err) { (void)err; return "OS-9 error"; }

void bzero(void *buffer,unsigned int count) { memset(buffer,0,count); }
void bcopy(const void *source,void *destination,unsigned int count)
{ memmove(destination,source,count); }
int bcmp(const void *left,const void *right,unsigned int count)
{ return memcmp(left,right,count); }
