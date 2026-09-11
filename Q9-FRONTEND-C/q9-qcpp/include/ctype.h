/* ctype.h -- minimal Q9 character-classification interface. Edition: 2026-09-11.
 *
 * Eigene Fassung, KEINE Uebernahme von Microwares DEFS/ctype.h (das steht
 * unter Microware-Copyright und ist nicht frei weitergebbar) -- nur die
 * ISO-C-Funktionssignatur ist Standard, nicht deren Tabellenimplementierung.
 * Nur tolower(): die einzige ctype-Funktion, die bisher in Q9-Tools
 * gebraucht wird (System/grep -i, Gross-/Kleinschreibung ignorieren).
 * Liste bei Bedarf erweitern, nicht vorsorglich.
 */
#ifndef Q9_CTYPE_H
#define Q9_CTYPE_H

extern int tolower(int c);

#endif
