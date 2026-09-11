//------------------------------------------------------------------------------------------------
// msvc_compat.h -- portability layer for MSVC "secure" CRT functions
//------------------------------------------------------------------------------------------------
// The source originated as a Visual Studio project and consistently uses
// MSVC-eigenen *_s-Funktionen (strcpy_s, strncpy_s, strcat_s, sprintf_s, fopen_s, _itoa_s).
// These are unavailable on macOS/Linux (glibc/BSD libc do not provide Annex K).
// This header supplies compatible implementations on non-Windows platforms.
//
// Deliberately WITHOUT C++ templates (as of 2026-07-23): the original version had
// Funktion zusaetzlich eine Template-Ueberladung, die die Zielgroesse aus dem Array-Typ
// ableitet (kein "sizeof(dst)" an der Aufrufstelle noetig). Microwares "xcc"-Compiler
// (Ultra C/C++ 2.5, Baujahr 2001) stuerzt bei dieser Deduktion mit einem internen Fehler
// ab ("get_integer_size_and_alignment: bad integer kind"). Da Templates im ganzen Projekt
// sonst nirgends vorkommen, wurden alle rund 75 Aufrufstellen auf die explizite Form
// (Zielgroesse per sizeof() als eigenes Argument) umgestellt -- funktioniert unveraendert
// auf allen Plattformen, keine Bedingungs-Kompilierung noetig.
//
// On Windows (_WIN32) this header is a no-op; the MSVC CRT supplies everything.
//------------------------------------------------------------------------------------------------
#ifndef MSVC_COMPAT_H
#define MSVC_COMPAT_H

#ifndef _WIN32

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>

inline size_t strnlen(const char* s, size_t maxlen) {
	size_t n = 0;
	while (n < maxlen && s[n] != '\0') n++;
	return n;
}

// Use memmove rather than memcpy/strcpy: some call sites copy within the same
// buffer (the comment filter), so overlap safety is required.
inline int strcpy_s(char* dst, size_t dstSize, const char* src) {
	size_t len = strlen(src);
	if (dstSize == 0) return 1;
	if (len >= dstSize) len = dstSize - 1;
	memmove(dst, src, len);
	dst[len] = '\0';
	return 0;
}

inline int strncpy_s(char* dst, size_t dstSize, const char* src, size_t count) {
	size_t len = strnlen(src, count);
	if (dstSize == 0) return 1;
	if (len >= dstSize) len = dstSize - 1;
	memmove(dst, src, len);
	dst[len] = '\0';
	return 0;
}

inline int strcat_s(char* dst, size_t dstSize, const char* src) {
	size_t used = strnlen(dst, dstSize);
	if (used >= dstSize) return 1;
	return strcpy_s(dst + used, dstSize - used, src);
}

inline int strncat_s(char* dst, size_t dstSize, const char* src, size_t count) {
	size_t used = strnlen(dst, dstSize);
	if (used >= dstSize) return 1;
	return strncpy_s(dst + used, dstSize - used, src, count);
}

inline int sprintf_s(char* dst, size_t dstSize, const char* fmt, ...) {
	va_list args;
	int ret;
	va_start(args, fmt);
	ret = vsnprintf(dst, dstSize, fmt, args);
	va_end(args);
	return ret;
}

inline int fopen_s(FILE** fp, const char* name, const char* mode) {
	*fp = fopen(name, mode);
	return (*fp == NULL) ? 1 : 0;
}

// Only base 10 is used by the project; support other bases for completeness.
inline int _itoa_s(int value, char* dst, size_t dstSize, int radix) {
	if (radix == 10) {
		sprintf(dst, "%d", value);
	}
	else if (radix == 16) {
		sprintf(dst, "%x", value);
	}
	else if (radix == 8) {
		sprintf(dst, "%o", value);
	}
	else {
		return 1;
	}
	return 0;
}

#endif // !_WIN32

#endif // MSVC_COMPAT_H
