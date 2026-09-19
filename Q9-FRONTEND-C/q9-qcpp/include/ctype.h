/* ctype.h -- minimal Q9 character-classification interface. Edition: 2026-09-11.
 *
 * Local implementation; it does not copy Microware's copyrighted DEFS/ctype.h.
 * Only the ISO C function signature is standardized, not the table layout.
 * Only tolower() is currently needed by Q9 tools (System/grep -i). Extend the
 * interface when required, rather than adding unused declarations in advance.
 */
#ifndef Q9_CTYPE_H
#define Q9_CTYPE_H

extern int tolower(int c);

/* Added 2026-09-18 for the qclib STATUS.md gap-closing test (tests/hello.c
   compares these against Microware clib). */
extern int isdigit(int c);
extern int isupper(int c);
extern int islower(int c);
extern int isxdigit(int c);
extern int iscntrl(int c);
extern int isgraph(int c);
extern int ispunct(int c);
extern int toupper(int c);

#endif
