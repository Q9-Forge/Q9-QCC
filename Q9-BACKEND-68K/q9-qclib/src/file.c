/*
 * q9-qclib file I/O
 *
 * Purpose:
 *   Provides the Q9 FILE abstraction and unbuffered OS-9 file operations.
 *   A FILE handle identifies an entry in the Q9 path table rather than a
 *   Microware FILE layout.
 *
 * Edition history:
 *   2026-09-11  Introduced the English source-header format.
 */
/*
 * The public FILE handle is the address of a Q9 path-table entry. The entry
 * stores the OS-9 path number; Microware's private FILE layout is not copied.
 * The implementation is intentionally unbuffered because the toolchain reads
 * and writes large blocks, making an additional buffer unnecessary.
 *
 * The source stays within the QCC subset: no string concatenation, no indexed
 * access through pointer fields and no static local variables.
 */

extern int _os_open(char *name, int mode, int *path);
extern int _os_create(char *name, int mode, int *path, int perms);
extern int _os_delete(char *name);
extern int _os_close(int path);
extern int _os_read(int path, char *buf, int *count);
extern int _os_write(int path, char *buf, int *count);

#define QF_MAX   16     /* Maximum number of simultaneously open files. */

/* Access modes: FAM_READ 0x01 and FAM_WRITE 0x02. */
#define QF_READ  1
#define QF_WRITE 2

/* One path-table entry per open file; zero means unused. */
int qf_path[QF_MAX];

/* Per-file state fields, indexed like qf_path.
 *
 * fgets reads line by line. Without a buffer this would require one system
 * call per byte, so fgets reads a block and serves lines from it.
 *
 * The buffer is allocated lazily on the first fgets call and only for files
 * read line by line. fread remains unbuffered for large toolchain inputs.
 *
 * Do not mix fgets and fread on the same stream: fread would skip bytes that
 * are already buffered. The toolchain does not do this. */
char *qf_rbuf[QF_MAX];          /* Read buffer, zero before allocation. */
int qf_rlen[QF_MAX];            /* Number of valid bytes. */
int qf_rpos[QF_MAX];            /* Current read position. */
int qf_err[QF_MAX];             /* Error flag for ferror. */
int qf_eof[QF_MAX];             /* End-of-file flag for feof. */

#define QF_RBUF 1024

extern char *realloc(char *p, int n);

/* A FILE handle is the address of qf_path[i]. Compare addresses directly
 * instead of assuming a particular array element size. */
/* Function: qf_index
 * Finds the table entry represented by a FILE handle.
 * Parameters: fp FILE handle.
 * Returns: Table index, or -1 when invalid. */
int qf_index(char *fp)
{
	int i;

	if (fp == 0)
		return -1;
	i = 0;
	while (i < QF_MAX) {
		if ((char *) &qf_path[i] == fp)
			return i;
		i++;
	}
	return -1;
}

/* Refills one per-file read buffer. */
/* Function: qf_fill
 * Fills the read buffer for an open file.
 * Parameters: i File table index.
 * Returns: Number of bytes read, or a negative error status. */
int qf_fill(int i)
{
	char *buf;
	int n;
	int rc;

	buf = qf_rbuf[i];
	if (buf == 0) {
		buf = realloc(0, QF_RBUF);
		if (buf == 0) {
			qf_err[i] = 1;
			return 0;
		}
		qf_rbuf[i] = buf;
	}
	n = QF_RBUF;
	rc = _os_read(qf_path[i], buf, &n);
	if (rc != 0) {
		/* End of file is not an error; OS-9 reports E$EOF (211). */
		if (rc != 211)
			qf_err[i] = 1;
		qf_rlen[i] = 0;
		qf_rpos[i] = 0;
		qf_eof[i] = (rc == 211);
		return 0;
	}
	if (n <= 0) {
		qf_rlen[i] = 0;
		qf_rpos[i] = 0;
		qf_eof[i] = 1;
		return 0;
	}
	qf_rlen[i] = n;
	qf_rpos[i] = 0;
	qf_eof[i] = 0;
	return 1;
}

/* Function: qf_open
 * Opens or creates a file using the requested mode.
 * Parameters: a IR argument frame containing path and mode.
 * Returns: FILE handle, or null on failure. */
char *qf_open(int *a)
{
	char *name;
	char *mode;
	int i;
	int frei;
	int p;
	int rc;
	int m;
	char fullName[256];
	int j;

	name = (char *) a[0];
	mode = (char *) a[1];
	frei = -1;
	for (i = 0; i < QF_MAX; i++) {
		if (qf_path[i] == 0)
			frei = i;
	}
	if (frei < 0 || name == 0 || mode == 0)
		return 0;

	m = mode[0];
	p = 0;
	if (m == 'r') {
		rc = _os_open(name, QF_READ, &p);
	} else if (m == 'w') {
		/* OS-9 I$Create rejects an existing file. C fopen("w") must truncate,
		 * so remove the old directory entry before creating the new file. */
		_os_delete(name);
		rc = _os_create(name, QF_WRITE, &p, 0x03);
		/* Some OS-9 file managers do not resolve relative I$Create paths even
		 * when the process has a current data directory. Retry in /dd, the
		 * Q9 system data device, while preserving the caller's relative name. */
		if (rc != 0 && name[0] != '/') {
			fullName[0] = '/';
			fullName[1] = 'd';
			fullName[2] = 'd';
			fullName[3] = '/';
			j = 0;
			while (name[j] != 0 && j < 251) {
				fullName[j + 4] = name[j];
				j++;
			}
			fullName[j + 4] = 0;
			_os_delete(fullName);
			rc = _os_create(fullName, QF_WRITE, &p, 0x03);
		}
	} else {
		/* Unsupported modes fail explicitly instead of opening incorrectly. */
		return 0;
	}
	if (rc != 0 || p == 0)
		return 0;
	qf_path[frei] = p;
	qf_rlen[frei] = 0;
	qf_rpos[frei] = 0;
	qf_err[frei] = 0;
	qf_eof[frei] = 0;
	/* Keep the buffer for reuse. qclib currently has no free-list allocator. */
	return (char *) &qf_path[frei];
}

/* Function: qf_close
 * Closes a Q9 file handle and releases its table entry.
 * Parameters: a IR argument frame containing the handle.
 * Returns: Zero on success, or an OS-9 error code. */
int qf_close(int *a)
{
	char *fp;
	int *slot;
	int rc;

	fp = (char *) a[0];
	if (fp == 0)
		return -1;
	slot = (int *) fp;
	if (*slot == 0)
		return -1;
	rc = _os_close(*slot);
	*slot = 0;
	if (rc != 0)
		return -1;
	return 0;
}

/* Return the number of complete items read, as required by C89. */
/* Function: qf_read
 * Reads complete items from a Q9 file.
 * Parameters: a IR argument frame containing buffer, size, count and handle.
 * Returns: Number of items read. */
int qf_read(int *a)
{
	char *buf;
	int size;
	int n;
	char *fp;
	int *slot;
	int want;
	int got;
	int rc;
	int i;

	buf = (char *) a[0];
	size = a[1];
	n = a[2];
	fp = (char *) a[3];
	if (fp == 0 || buf == 0 || size <= 0 || n <= 0)
		return 0;
	slot = (int *) fp;
	if (*slot == 0)
		return 0;
	want = size * n;
	got = want;
	rc = _os_read(*slot, buf, &got);
	if (rc != 0) {
		/* 211 is E$EOF and is not an I/O error. */
		if (rc != 211) {
			i = qf_index(fp);
			if (i >= 0)
				qf_err[i] = 1;
		}
		return 0;
	}
	if (got <= 0)
		return 0;
	return got / size;
}

/* Function: qf_write
 * Writes complete items to a Q9 file.
 * Parameters: a IR argument frame containing buffer, size, count and handle.
 * Returns: Number of items written. */
int qf_write(int *a)
{
	char *buf;
	int size;
	int n;
	char *fp;
	int *slot;
	int want;
	int done;
	int rc;
	int i;

	buf = (char *) a[0];
	size = a[1];
	n = a[2];
	fp = (char *) a[3];
	if (fp == 0 || buf == 0 || size <= 0 || n <= 0)
		return 0;
	slot = (int *) fp;
	if (*slot == 0)
		return 0;
	want = size * n;
	done = want;
	rc = _os_write(*slot, buf, &done);
	if (rc != 0) {
		/* Remember the error so ferror can report it; callers may only inspect
		   the number of successfully written items. */
		i = qf_index(fp);
		if (i >= 0)
			qf_err[i] = 1;
		return 0;
	}
	if (done <= 0)
		return 0;
	return done / size;
}

/* fgets includes the line ending, reads at most n-1 bytes and always
   terminates the result with NUL. Q9 uses LF ($0a), while Microware clib
   uses CR ($0d); the difference follows from the compiler's newline
   representation. */
/* Function: qf_gets
 * Reads one line into the caller's buffer.
 * Parameters: a IR argument frame containing buffer, limit and handle.
 * Returns: Buffer on success, or null at EOF/error. */
char *qf_gets(int *a)
{
	char *dst;
	int n;
	char *fp;
	int i;
	int k;
	char *buf;
	int c;

	dst = (char *) a[0];
	n = a[1];
	fp = (char *) a[2];
	i = qf_index(fp);
	if (dst == 0 || n <= 1 || i < 0 || qf_path[i] == 0)
		return 0;
	k = 0;
	while (k < n - 1) {
		if (qf_rpos[i] >= qf_rlen[i]) {
			if (qf_fill(i) == 0)
				break;
		}
		buf = qf_rbuf[i];
		c = buf[qf_rpos[i]] & 255;
		qf_rpos[i] = qf_rpos[i] + 1;
		/* ToolShed text copies use OS-9 CR line endings. Normalize them to
		   the Q9 LF convention exposed by fgets(). */
		if (c == 13) {
			dst[k] = 10;
			k++;
			break;
		}
		dst[k] = c;
		k++;
		if (c == 10)
			break;
	}
	if (k == 0)
		return 0;               /* No bytes read: end of file. */
	dst[k] = 0;
	return dst;
}

/* Return the stored stream error state. */
/* Function: qf_error
 * Returns the pending error state of a Q9 file handle.
 * Parameters: a IR argument frame containing the handle.
 * Returns: Non-zero when an I/O error occurred. */
int qf_error(int *a)
{
	int i;

	i = qf_index((char *) a[0]);
	if (i < 0)
		return 1;               /* An invalid stream is itself an error. */
	return qf_err[i];
}

/* Function: qf_eof_state
 * Returns the pending end-of-file state of a Q9 file handle.
 * Parameters: a IR argument frame containing the handle.
 * Returns: Non-zero after a read reached end of file. */
int qf_eof_state(int *a)
{
	int i;

	i = qf_index((char *) a[0]);
	if (i < 0)
		return 1;
	return qf_eof[i];
}

/* Derive the OS-9 path number from a FILE handle. A null handle maps to
   path 2 (diagnostic output), while a closed handle maps to -1. This logic
   is duplicated in printf.c because the QCC ABI exports tc_* symbols while
   callers use the plain names. */
/* Function: qf_pathof
 * Resolves a Q9 FILE handle to its OS-9 path number.
 * Parameters: fp Handle value.
 * Returns: OS-9 path number, or the diagnostic path for null. */
int qf_pathof(int fp)
{
	int *slot;

	if (fp == 0)
		return 2;
	slot = (int *) fp;
	if (*slot == 0)
		return -1;
	return *slot;
}

/* C89 requires fputc to return the written character, not zero. */
/* Function: qf_putc
 * Writes one character to a Q9 stream.
 * Parameters: a IR argument frame containing character and handle.
 * Returns: Character on success, or EOF-style failure. */
int qf_putc(int *a)
{
	char eins[4];
	int p;
	int n;
	int rc;

	p = qf_pathof(a[1]);
	if (p < 0)
		return -1;
	eins[0] = a[0];
	n = 1;
	rc = _os_write(p, eins, &n);
	if (rc != 0)
		return -1;
	return a[0] & 255;
}

/* putchar always targets path 1 (standard output), unlike putc/fputc which
   go through a caller-supplied handle -- qf_pathof(0) would resolve to path
   2 (diagnostic output) instead. */
/* Function: putchar
 * Writes one character to standard output.
 * Parameters: a IR argument frame containing the character.
 * Returns: Character on success, or EOF-style failure. */
int putchar(int *a)
{
	char eins[4];
	int n;
	int rc;

	eins[0] = a[0];
	n = 1;
	rc = _os_write(1, eins, &n);
	if (rc != 0)
		return -1;
	return a[0] & 255;
}

/* Write directly from the caller's string; no copy is needed. */
/* Function: qf_puts_f
 * Writes a string without appending a newline.
 * Parameters: a IR argument frame containing string and handle.
 * Returns: Non-negative success status, or failure. */
int qf_puts_f(int *a)
{
	char *s;
	int p;
	int n;
	int rc;

	s = (char *) a[0];
	if (s == 0)
		return -1;
	p = qf_pathof(a[1]);
	if (p < 0)
		return -1;
	n = 0;
	while (s[n] != 0)
		n++;
	if (n == 0)
		return 0;
	rc = _os_write(p, s, &n);
	if (rc != 0)
		return -1;
	return 0;
}

/* puts appends LF ($0a), matching qclib printf output and Q9 text files. */
/* Function: qf_puts
 * Writes a string followed by a newline.
 * Parameters: a IR argument frame containing the string.
 * Returns: Non-negative success status, or failure. */
int qf_puts(int *a)
{
	char *s;
	char zeile[256];
	int n;
	int rc;

	s = (char *) a[0];
	if (s == 0)
		return -1;
	n = 0;
	while (s[n] != 0 && n < 254) {
		zeile[n] = s[n];
		n++;
	}
	zeile[n] = 0x0a;
	n++;
	rc = _os_write(1, zeile, &n);
	if (rc != 0)
		return -1;
	return 0;
}
