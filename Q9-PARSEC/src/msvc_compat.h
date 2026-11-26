/*
 * File: msvc_compat.h
 * Description: Provide the small portability layer required by the Q9 parser tools.
 * Owner: cla / afoe
 * Version: 0.2
 * Repository: Q9-Forge/Q9-QCC
 *
 * Edition History
 * 2026-11-25  0.2  Replaced unbounded formatting calls with bounded calls.
 * 2026-09-11  0.1  Introduced the English source-header format.
 */
// This header maps the secure-CRT subset used by the Q9 parser tools.
// The implementation is available on non-Windows hosts; Windows uses its
// native runtime support.
//
// The interface stays explicit so the same source remains portable across
// supported build hosts.
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

#endif // !_WIN32

#endif // MSVC_COMPAT_H
