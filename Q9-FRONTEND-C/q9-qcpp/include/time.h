/* time.h -- Q9-Werkzeugkette, bewusst minimal (s. stddef.h/string.h).
 *
 * Eigene Fassung, KEINE Uebernahme von Microwares DEFS/time.h (Microware-
 * Copyright). Nur die Typen, nicht die Funktionen: time_t/struct tm werden
 * schon zum PARSEN von Q9-Tools/System/touch,date,dir gebraucht, aber
 * time()/localtime() sind hier ABSICHTLICH noch nicht deklariert -- qclib
 * hat den dafuer noetigen OS-9-Uhrzeit-Syscall noch nicht (eigenes,
 * spaeteres Vorhaben). Diese Header bleiben sonst immer deckungsgleich mit
 * dem, was qclib tatsaechlich bereitstellt (s. string.h); eine Deklaration
 * ohne Gegenstueck waere nur ein spaeterer Binder-Fehler statt eines
 * fruehen, klaren "unknown function".
 *
 * struct tm nur mit den sechs Feldern, die diese drei Werkzeuge lesen
 * (tm_wday/tm_yday/tm_isdst fehlen bewusst -- bei Bedarf ergaenzen).
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
