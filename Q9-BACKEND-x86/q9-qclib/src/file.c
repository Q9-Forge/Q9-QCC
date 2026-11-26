/* Portable unbuffered stream layer derived from the 68k qclib path table.
 * The path adapter is deliberately external: its OS-9000/x86 parameter
 * layouts must be validated before connecting it to the raw trap primitive.
 * Adapter read/write return transferred bytes, or -1 on an error. */
extern int q9_x86_path_open(const char *, const char *);
extern int q9_x86_path_close(int);
extern int q9_x86_path_read(int, char *, int);
extern int q9_x86_path_write(int, const char *, int);
extern int q9_x86_path_seek(int, int, int);
extern int strlen(const char *);

int q9_stream_path[16];
int q9_stream_used[16];
int q9_stream_error[16];
int q9_stream_eof[16];
int q9_stream_pushback[16];
int q9_stream_has_pushback[16];

int q9_stream_initialized;
int *q9_standard_stream(int path)
{
    int i;
    if (!q9_stream_initialized) {
        for (i = 0; i < 3; ++i) {
            q9_stream_path[i] = i;
            q9_stream_used[i] = 1;
        }
        q9_stream_initialized = 1;
    }
    if (path < 0 || path > 2) return (int *)0;
    return &q9_stream_path[path];
}

static int stream_index(int *stream)
{
    int i;
    for (i = 0; i < 16; ++i)
        if (stream == &q9_stream_path[i] && q9_stream_used[i]) return i;
    return -1;
}

int *fopen(const char *name, const char *mode)
{
    int i, path;
    q9_standard_stream(0);
    if (!name || !mode) return (int *)0;
    for (i = 0; i < 16; ++i) if (!q9_stream_used[i]) break;
    if (i == 16) return (int *)0;
    path = q9_x86_path_open(name, mode);
    if (path < 0) return (int *)0;
    q9_stream_path[i] = path;
    q9_stream_used[i] = 1;
    q9_stream_error[i] = 0;
    q9_stream_eof[i] = 0;
    q9_stream_has_pushback[i] = 0;
    return &q9_stream_path[i];
}

int fclose(int *stream)
{
    int i = stream_index(stream), rc;
    if (i < 0) return -1;
    rc = q9_x86_path_close(q9_stream_path[i]);
    q9_stream_used[i] = 0;
    return rc < 0 ? -1 : 0;
}

int fread(char *buffer, unsigned int size, unsigned int count, int *stream)
{
    int i = stream_index(stream), got;
    unsigned int total, done = 0;
    if (i < 0 || !buffer || !size || !count) return 0;
    total = size * count;
    if (total / size != count || total > 2147483647U) {
        q9_stream_error[i] = 1;
        return 0;
    }
    if (q9_stream_has_pushback[i]) {
        buffer[done++] = q9_stream_pushback[i];
        q9_stream_has_pushback[i] = 0;
    }
    while (done < total) {
        got = q9_x86_path_read(q9_stream_path[i], buffer + done, total - done);
        if (got < 0) { q9_stream_error[i] = 1; break; }
        if (!got) { q9_stream_eof[i] = 1; break; }
        if ((unsigned int)got > total - done) { q9_stream_error[i] = 1; break; }
        done += got;
    }
    return done / size;
}

int fwrite(const char *buffer, unsigned int size, unsigned int count, int *stream)
{
    int i = stream_index(stream), put;
    unsigned int total, done = 0;
    if (i < 0 || !buffer || !size || !count) return 0;
    total = size * count;
    if (total / size != count || total > 2147483647U) {
        q9_stream_error[i] = 1;
        return 0;
    }
    while (done < total) {
        put = q9_x86_path_write(q9_stream_path[i], buffer + done, total - done);
        if (put <= 0 || (unsigned int)put > total - done) {
            q9_stream_error[i] = 1;
            break;
        }
        done += put;
    }
    return done / size;
}

int fgetc(int *stream)
{
    unsigned char c;
    return fread((char *)&c, 1, 1, stream) == 1 ? c : -1;
}
int getc(int *stream) { return fgetc(stream); }
int fputc(int c, int *stream)
{
    char byte = c;
    return fwrite(&byte, 1, 1, stream) == 1 ? (c & 255) : -1;
}
int putc(int c, int *stream) { return fputc(c, stream); }
int fputs(const char *text, int *stream)
{
    int len;
    if (!text) return -1;
    len = strlen(text);
    return fwrite(text, 1, len, stream) == len ? 0 : -1;
}
char *fgets(char *buffer, int capacity, int *stream)
{
    int n = 0, c;
    if (!buffer || capacity <= 1) return (char *)0;
    while (n < capacity - 1) {
        c = fgetc(stream);
        if (c < 0) break;
        buffer[n++] = c;
        if (c == 13 || c == 10) break;
    }
    buffer[n] = 0;
    return n ? buffer : (char *)0;
}
int ferror(int *stream)
{
    int i = stream_index(stream);
    return i < 0 ? 1 : q9_stream_error[i];
}
int feof(int *stream)
{
    int i = stream_index(stream);
    return i < 0 ? 1 : q9_stream_eof[i];
}
void clearerr(int *stream)
{
    int i = stream_index(stream);
    if (i >= 0) { q9_stream_error[i] = 0; q9_stream_eof[i] = 0; }
}
int ungetc(int c, int *stream)
{
    int i = stream_index(stream);
    if (i < 0 || c < 0 || q9_stream_has_pushback[i]) return -1;
    q9_stream_pushback[i] = c & 255;
    q9_stream_has_pushback[i] = 1;
    q9_stream_eof[i] = 0;
    return c & 255;
}
int fflush(int *stream) { return !stream || stream_index(stream) >= 0 ? 0 : -1; }

int getchar(void) { return fgetc(q9_standard_stream(0)); }
int putchar(int c) { return fputc(c, q9_standard_stream(1)); }
int puts(const char *text)
{
    int *output = q9_standard_stream(1);
    if (fputs(text, output) < 0) return -1;
    return fputc(13, output) < 0 ? -1 : 0;
}

int fseek(int *stream, long offset, int origin)
{
    int i = stream_index(stream), result;
    if (i < 0 || origin < 0 || origin > 2) return -1;
    if (origin == 1 && q9_stream_has_pushback[i]) {
        if (offset == (long)0x80000000U) return -1;
        --offset;
    }
    result = q9_x86_path_seek(q9_stream_path[i], offset, origin);
    if (result < 0) { q9_stream_error[i] = 1; return -1; }
    q9_stream_has_pushback[i] = 0; q9_stream_eof[i] = 0;
    return 0;
}
long ftell(int *stream)
{
    int i = stream_index(stream), result;
    if (i < 0) return -1;
    result = q9_x86_path_seek(q9_stream_path[i], 0, 1);
    if (result < 0) { q9_stream_error[i] = 1; return -1; }
    return q9_stream_has_pushback[i] ? result - 1 : result;
}
void rewind(int *stream) { if (fseek(stream, 0, 0) == 0) clearerr(stream); }
int *freopen(const char *name, const char *mode, int *stream)
{
    int i = stream_index(stream), path;
    if (i < 0 || !name || !mode) return (int *)0;
    q9_x86_path_close(q9_stream_path[i]);
    q9_stream_used[i] = 0;
    path = q9_x86_path_open(name, mode);
    if (path < 0) return (int *)0;
    q9_stream_path[i] = path; q9_stream_used[i] = 1;
    q9_stream_error[i] = 0; q9_stream_eof[i] = 0; q9_stream_has_pushback[i] = 0;
    return stream;
}

void q9_x86_close_all_streams(void)
{
    int i;
    q9_standard_stream(0);
    for (i=0;i<16;++i) if (q9_stream_used[i]) fclose(&q9_stream_path[i]);
}
