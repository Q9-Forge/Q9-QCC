/* q9-qclib ASCII character classification.
 *
 * This implementation returns Microware-like bitmask values for the
 * classification tests (not plain booleans). The masks were inferred from
 * clib behaviour used in tests: the bits are
 *   1   ISCNTRL
 *   2   ISUPPER
 *   4   ISLOWER
 *   8   ISDIGIT
 *  16   ISSPACE
 *  32   ISPUNCT
 *  64   ISXDIGIT
 *
 * Each API returns the corresponding bitmask when the class applies or 0
 * otherwise, matching the behaviour expected by the test-suite. */

#define C_ISCNTRL  1
#define C_ISUPPER  2
#define C_ISLOWER  4
#define C_ISDIGIT  8
#define C_ISSPACE  16
#define C_ISPUNCT  32
#define C_ISXDIGIT 64

static int ctype_mask_of(int ch);

/* Provide single-character wrappers for C89 macros used by tests. */
int iscntrl_c(int ch) { return ctype_mask_of(ch) & C_ISCNTRL; }
int isupper_c(int ch) { return ctype_mask_of(ch) & C_ISUPPER; }
int islower_c(int ch) { return ctype_mask_of(ch) & C_ISLOWER; }
int isdigit_c(int ch) { return ctype_mask_of(ch) & C_ISDIGIT; }
int isgraph_c(int ch) { return ctype_mask_of(ch) & (C_ISPUNCT|C_ISUPPER|C_ISLOWER|C_ISDIGIT); }
int ispunct_c(int ch) { return ctype_mask_of(ch) & C_ISPUNCT; }

static int ctype_mask_of(int ch)
{
    int c = ch & 255;
    int m = 0;

    if (c < 32 || c == 127)
        m |= C_ISCNTRL;
    if (c >= 'A' && c <= 'Z')
        m |= C_ISUPPER;
    if (c >= 'a' && c <= 'z')
        m |= C_ISLOWER;
    if (c >= '0' && c <= '9')
        m |= C_ISDIGIT;
    if (c == ' ' || (c >= 9 && c <= 13))
        m |= C_ISSPACE;
    if ((c >= 33 && c <= 126) && !( (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ))
        m |= C_ISPUNCT;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
        m |= C_ISXDIGIT;
    return m;
}

int isalpha(int *a)
{
    return ctype_mask_of(a[0]) & (C_ISUPPER | C_ISLOWER);
}

int isalnum(int *a)
{
    return ctype_mask_of(a[0]) & (C_ISUPPER | C_ISLOWER | C_ISDIGIT);
}

int isspace(int *a)
{
    return ctype_mask_of(a[0]) & C_ISSPACE;
}

int isprint(int *a)
{
    int c = a[0] & 255;
    if (c >= 32 && c <= 126)
        return ctype_mask_of(c);
    return 0;
}

int isdigit(int *a)
{
    return ctype_mask_of(a[0]) & C_ISDIGIT;
}

int isupper(int *a)
{
    return ctype_mask_of(a[0]) & C_ISUPPER;
}

int islower(int *a)
{
    return ctype_mask_of(a[0]) & C_ISLOWER;
}

int isxdigit(int *a)
{
    return ctype_mask_of(a[0]) & C_ISXDIGIT;
}

int iscntrl(int *a)
{
    return ctype_mask_of(a[0]) & C_ISCNTRL;
}

int isgraph(int *a)
{
    int c = a[0] & 255;
    if (c > 32 && c <= 126)
        return ctype_mask_of(c);
    return 0;
}

int ispunct(int *a)
{
    return ctype_mask_of(a[0]) & C_ISPUNCT;
}
