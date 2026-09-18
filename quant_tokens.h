#ifndef QUANT_TOKENS_H
#define QUANT_TOKENS_H

/* Token-Definitionen für den Quant9-Compiler */
/* Diese IDs müssen mit der Logik in der .lextab übereinstimmen */

/* Keywords */
#define TOK_IMPORT      1
#define TOK_NAMESPACE   2
#define TOK_INTERFACE   3
#define TOK_CLASS       4
#define TOK_IMPLEMENTS  5
#define TOK_CONSTRUCTOR 6
#define TOK_FUNCTION    7
#define TOK_LET         8
#define TOK_RETURN      9
#define TOK_IF          10
#define TOK_ELSE        11
#define TOK_WHILE       12
#define TOK_PUBLIC      13
#define TOK_PRIVATE     14

/* Operatoren & Symbole */
#define TOK_LBRACE      20
#define TOK_RBRACE      21
#define TOK_LPAREN      22
#define TOK_RPAREN      23
#define TOK_COLON       24
#define TOK_SEMICOLON   25
#define TOK_DOT         26
#define TOK_ASSIGN      27
#define TOK_PLUS        28
#define TOK_MINUS       29
#define TOK_STAR        30
#define TOK_SLASH       31
#define TOK_LBRACK      32
#define TOK_RBRACK      33
#define TOK_AMPERSAND   34

/* Literale */
#define TOK_IDENTIFIER  40
#define TOK_NUMBER      41
#define TOK_HEXNUMBER   42

#endif
