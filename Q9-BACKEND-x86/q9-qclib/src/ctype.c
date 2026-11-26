/*
 * x86 implementation of the portable ASCII ctype subset.
 *
 * This is derived from the classification rules in the 68k qclib, but uses
 * the normal C function ABI.  The 68k implementation receives an argument
 * frame from assembler adapters, which must not be carried over to i386.
 */

#define Q9_ISCNTRL  1
#define Q9_ISUPPER  2
#define Q9_ISLOWER  4
#define Q9_ISDIGIT  8
#define Q9_ISSPACE 16
#define Q9_ISPUNCT 32
#define Q9_ISXDIGIT 64

static int ctype_mask(int ch)
{
    int c = ch & 255;
    int mask = 0;

    if (c < 32 || c == 127) mask |= Q9_ISCNTRL;
    if (c >= 'A' && c <= 'Z') mask |= Q9_ISUPPER;
    if (c >= 'a' && c <= 'z') mask |= Q9_ISLOWER;
    if (c >= '0' && c <= '9') mask |= Q9_ISDIGIT;
    if (c == ' ' || (c >= '\t' && c <= '\r')) mask |= Q9_ISSPACE;
    if (c >= 33 && c <= 126 &&
        !(mask & (Q9_ISUPPER | Q9_ISLOWER | Q9_ISDIGIT))) mask |= Q9_ISPUNCT;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
        (c >= 'a' && c <= 'f')) mask |= Q9_ISXDIGIT;
    return mask;
}

int isalpha(int ch)  { return ctype_mask(ch) & (Q9_ISUPPER | Q9_ISLOWER); }
int isalnum(int ch)  { return ctype_mask(ch) & (Q9_ISUPPER | Q9_ISLOWER | Q9_ISDIGIT); }
int isspace(int ch)  { return ctype_mask(ch) & Q9_ISSPACE; }
int isprint(int ch)  { return (ch & 255) >= 32 && (ch & 255) <= 126 ? ctype_mask(ch) : 0; }
int isdigit(int ch)  { return ctype_mask(ch) & Q9_ISDIGIT; }
int isupper(int ch)  { return ctype_mask(ch) & Q9_ISUPPER; }
int islower(int ch)  { return ctype_mask(ch) & Q9_ISLOWER; }
int isxdigit(int ch) { return ctype_mask(ch) & Q9_ISXDIGIT; }
int iscntrl(int ch)  { return ctype_mask(ch) & Q9_ISCNTRL; }
int isgraph(int ch)  { return ctype_mask(ch) & (Q9_ISPUNCT | Q9_ISUPPER | Q9_ISLOWER | Q9_ISDIGIT); }
int ispunct(int ch)  { return ctype_mask(ch) & Q9_ISPUNCT; }

int tolower(int ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

int toupper(int ch)
{
    return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
}
