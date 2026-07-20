//------------------------------------------------------------------------------------------------
// msvc_compat.h -- Portabilitaets-Schicht fuer die MSVC "sicheren" CRT-Funktionen
//------------------------------------------------------------------------------------------------
// Der Quelltext ist urspruenglich ein Visual-Studio-Projekt und verwendet durchgehend die
// MSVC-eigenen *_s-Funktionen (strcpy_s, strncpy_s, strcat_s, sprintf_s, fopen_s, _itoa_s).
// Diese existieren auf macOS/Linux nicht (Annex K wird von glibc/BSD-libc nicht angeboten).
// Dieser Header bildet sie fuer Nicht-Windows-Plattformen nach -- inklusive der MSVC-
// C++-Template-Varianten, die die Puffergroesse aus dem Array-Typ ableiten, damit die
// bestehenden Aufrufstellen UNVERAENDERT bleiben koennen.
//
// Unter Windows (_WIN32) ist der Header ein No-Op, dort liefert die MSVC-CRT alles selbst.
//------------------------------------------------------------------------------------------------
#ifndef MSVC_COMPAT_H
#define MSVC_COMPAT_H

#ifndef _WIN32

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>

// memmove statt memcpy/strcpy: einzelne Aufrufstellen kopieren innerhalb desselben
// Puffers (Kommentar-Filter in comment()), das muss ueberlappungssicher sein.
inline int strcpy_s(char* dst, size_t dstSize, const char* src) {
	size_t len = strlen(src);
	if (dstSize == 0) return 1;
	if (len >= dstSize) len = dstSize - 1;
	memmove(dst, src, len);
	dst[len] = '\0';
	return 0;
}

template <size_t N>
inline int strcpy_s(char (&dst)[N], const char* src) {
	return strcpy_s(dst, N, src);
}

inline int strncpy_s(char* dst, size_t dstSize, const char* src, size_t count) {
	size_t len = strnlen(src, count);
	if (dstSize == 0) return 1;
	if (len >= dstSize) len = dstSize - 1;
	memmove(dst, src, len);
	dst[len] = '\0';
	return 0;
}

template <size_t N>
inline int strncpy_s(char (&dst)[N], const char* src, size_t count) {
	return strncpy_s(dst, N, src, count);
}

inline int strcat_s(char* dst, size_t dstSize, const char* src) {
	size_t used = strnlen(dst, dstSize);
	if (used >= dstSize) return 1;
	return strcpy_s(dst + used, dstSize - used, src);
}

template <size_t N>
inline int strcat_s(char (&dst)[N], const char* src) {
	return strcat_s(dst, N, src);
}

inline int strncat_s(char* dst, size_t dstSize, const char* src, size_t count) {
	size_t used = strnlen(dst, dstSize);
	if (used >= dstSize) return 1;
	return strncpy_s(dst + used, dstSize - used, src, count);
}

template <size_t N>
inline int strncat_s(char (&dst)[N], const char* src, size_t count) {
	return strncat_s(dst, N, src, count);
}

template <size_t N>
inline int sprintf_s(char (&dst)[N], const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int ret = vsnprintf(dst, N, fmt, args);
	va_end(args);
	return ret;
}

inline int fopen_s(FILE** fp, const char* name, const char* mode) {
	*fp = fopen(name, mode);
	return (*fp == NULL) ? 1 : 0;
}

// nur Basis 10 wird im Projekt verwendet; andere Basen der Vollstaendigkeit halber
inline int _itoa_s(int value, char* dst, size_t dstSize, int radix) {
	if (radix == 10) {
		snprintf(dst, dstSize, "%d", value);
	}
	else if (radix == 16) {
		snprintf(dst, dstSize, "%x", value);
	}
	else if (radix == 8) {
		snprintf(dst, dstSize, "%o", value);
	}
	else {
		return 1;
	}
	return 0;
}

template <size_t N>
inline int _itoa_s(int value, char (&dst)[N], int radix) {
	return _itoa_s(value, dst, N, radix);
}

#endif // !_WIN32

#endif // MSVC_COMPAT_H
