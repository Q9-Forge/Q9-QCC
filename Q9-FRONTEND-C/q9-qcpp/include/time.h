/* time.h -- Q9-Werkzeugkette, bewusst minimal (s. stddef.h/string.h).
 *
 * Local interface; it does not copy Microware's copyrighted DEFS/time.h.
 * It provides types, not functions: time_t/struct tm are needed to parse
 * Q9-Tools/System/touch,date,dir, but
 * time()/localtime() sind hier ABSICHTLICH noch nicht deklariert -- qclib
 * hat den dafuer noetigen OS-9-Uhrzeit-Syscall noch nicht (eigenes,
 * spaeteres Vorhaben). Diese Header bleiben sonst immer deckungsgleich mit
 * dem, was qclib tatsaechlich bereitstellt (s. string.h); eine Deklaration
 * ohne Gegenstueck waere nur ein spaeterer Binder-Fehler statt eines
 * fruehen, klaren "unknown function".
 *
 * struct tm contains only the six fields read by these tools; add the others
 * when required.
 */
#ifndef Q9_TIME_H
#define Q9_TIME_H

typedef long time_t;

struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
};

#endif
