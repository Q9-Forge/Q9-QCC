/* Integer/string printf core. x86 adapters supply a contiguous cdecl frame.
 * The supported C89 subset includes h/l, flags, field widths and precision.
 * Floating conversion and %n are rejected; no argument is silently skipped. */
extern int fputc(int, int *);
extern int *q9_standard_stream(int);

static int fmt_emit(int *stream, char *dest, int offset, int c)
{
    if (dest) { dest[offset] = c; return 0; }
    return fputc(c, stream) < 0 ? -1 : 0;
}

int q9_format(int *args, int kind)
{
    int *stream;
    char *dest;
    const char *format, *text;
    int ai, count = 0, width, precision, left, zero, plus, alternate, space;
    int c, len, pad, negative, base, upper, i, numeric, zeros, sign, prefix, short_arg;
    unsigned int value;
    char digits[34];
    if (kind == 0) {
        stream = q9_standard_stream(1); dest = (char *)0; ai = 1;
        format = (const char *)args[0];
    } else {
        stream = (int *)args[0]; dest = kind == 2 ? (char *)args[0] : (char *)0;
        ai = 2; format = (const char *)args[1];
    }
    if (!format || (kind == 2 && !dest)) return -1;
    while (*format) {
        if (*format != '%') {
            if (fmt_emit(stream, dest, count, *format++) < 0) return -1;
            ++count; continue;
        }
        ++format;
        left = 0; zero = 0; plus = 0; alternate = 0; space = 0;
        while (*format == '-' || *format == '0' || *format == '+' || *format == '#' || *format == ' ') {
            if (*format == '-') left = 1;
            if (*format == '0') zero = 1;
            if (*format == '+') plus = 1;
            if (*format == '#') alternate = 1;
            if (*format == ' ') space = 1;
            ++format;
        }
        width = 0;
        if (*format == '*') {
            width = args[ai++]; ++format;
            if (width < -1000000 || width > 1000000) return -1;
            if (width < 0) { left = 1; width = -width; }
        } else while (*format >= '0' && *format <= '9') {
            if (width > 100000) return -1;
            width = width * 10 + *format++ - '0';
        }
        precision = -1;
        if (*format == '.') {
            ++format; precision = 0;
            if (*format == '*') { precision = args[ai++]; ++format; if (precision > 1000000) return -1; }
            else while (*format >= '0' && *format <= '9') {
                if (precision > 100000) return -1;
                precision = precision * 10 + *format++ - '0';
            }
        }
        short_arg = 0;
        if (*format == 'h') { short_arg = 1; ++format; }
        else if (*format == 'l') ++format;
        c = *format;
        if (!c) return -1;
        ++format;
        negative = 0; len = 0; zeros = 0; sign = 0; prefix = 0; numeric = 0;
        if (c == 's') {
            text = (const char *)args[ai++];
            if (!text) text = "(null)";
            while (text[len] && (precision < 0 || len < precision)) ++len;
        } else if (c == 'c' || c == '%') {
            digits[0] = c == '%' ? '%' : args[ai++]; text = digits; len = 1;
        } else {
            if (c != 'd' && c != 'i' && c != 'u' && c != 'o' && c != 'x' && c != 'X' && c != 'p') return -1;
            numeric = 1;
            base = c == 'o' ? 8 : (c == 'x' || c == 'X' || c == 'p' ? 16 : 10);
            value = (unsigned int)args[ai++];
            if (short_arg) {
                value &= 65535U;
                if ((c == 'd' || c == 'i') && (value & 32768U)) value |= 0xffff0000U;
            }
            if (c == 'd' || c == 'i') {
                if ((int)value < 0) { negative = 1; value = 0U - value; }
                sign = negative ? '-' : (plus ? '+' : (space ? ' ' : 0));
            }
            if (c == 'p' || (alternate && (c == 'x' || c == 'X') && value)) prefix = 2;
            upper = c == 'X';
            if (value || precision != 0) {
                do {
                    i = value % base;
                    digits[len++] = i < 10 ? '0' + i : (upper ? 'A' : 'a') + i - 10;
                    value = value / base;
                } while (value);
            }
            for (i = 0; i < len / 2; ++i) {
                pad = digits[i]; digits[i] = digits[len - 1 - i]; digits[len - 1 - i] = pad;
            }
            text = digits;
            if (precision > len) zeros = precision - len;
            if (alternate && c == 'o' && !zeros && (!len || text[0] != '0')) zeros = 1;
        }
        if (!numeric || precision >= 0 || left) zero = 0;
        pad = width - len - zeros - prefix;
        if (sign) --pad;
        if (!left && !zero) while (pad-- > 0) if (fmt_emit(stream,dest,count++,' ') < 0) return -1;
        if (sign) if (fmt_emit(stream,dest,count++,sign) < 0) return -1;
        if (prefix) {
            if (fmt_emit(stream,dest,count++,'0') < 0 || fmt_emit(stream,dest,count++,c == 'X' ? 'X' : 'x') < 0) return -1;
        }
        if (!left && zero) while (pad-- > 0) if (fmt_emit(stream,dest,count++,'0') < 0) return -1;
        while (zeros-- > 0) if (fmt_emit(stream,dest,count++,'0') < 0) return -1;
        for (i = 0; i < len; ++i) if (fmt_emit(stream,dest,count++,text[i]) < 0) return -1;
        if (left) while (pad-- > 0) if (fmt_emit(stream,dest,count++,' ') < 0) return -1;
    }
    if (dest) dest[count] = 0;
    return count;
}
