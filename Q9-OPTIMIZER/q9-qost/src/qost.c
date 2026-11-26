/*--------------------------------------------------------------------------------------
 * File:        qost.c
 * Description: Conservative optimizer for Q9 Stack-IR.
 * Owner:       cla / afoe
 * Version:     0.02
 *
 * This file is part of the Q9-Forge repository. See https://github.com/Q9-Forge.
 *
 * Edition History
 * Date    | Ver. | Description                                               | By
 * 26-11-26| 0.01 | Document functions, name line endings and translate status.      | cla
 * 26-11-26| 0.02 | Preserve negation that would overflow.                    | cla
 *--------------------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define READ_CHUNK 8192
#define U32_MASK 0xffffffffUL
#define U32_SIGN 0x80000000UL
#define Q9_EOL_NONE 0 /* No line ending. */
#define Q9_EOL_LF 1   /* Line feed. */
#define Q9_EOL_CRLF 2 /* Carriage return followed by line feed. */
#define Q9_EOL_CR 3   /* Carriage return. */

typedef struct {
	char *text;
	char *replacement;
	int eol;
	int removed;
} IrLine;

static char *source;
static int source_size;
static int source_capacity;
static IrLine *lines;
static int line_count;
static int optimizations;

/*--------------------------------------------------------------------------------------
 * Function:    fail
 * Description: Report an error and terminate the process.
 * Parameter:   const char *message: Error message.
 * Return:      No return value.
 *--------------------------------------------------------------------------------------*/
static void fail(const char *message)
{
	fprintf(stderr, "qost: %s\n", message);
	exit(1);
}

/*--------------------------------------------------------------------------------------
 * Function:    grow
 * Description: Allocate or resize a buffer.
 * Parameter:   void *old: Existing allocation, or null.
 *              size_t size: Requested allocation size.
 * Return:      Pointer to the allocated buffer.
 *--------------------------------------------------------------------------------------*/
static void *grow(void *old, size_t size)
{
	void *next = realloc(old, size);
	if (next == 0) fail("not enough memory");
	return next;
}

/*--------------------------------------------------------------------------------------
 * Function:    load_file
 * Description: Read the input file into the source buffer.
 * Parameter:   const char *path: Input or output path.
 * Return:      No return value.
 *--------------------------------------------------------------------------------------*/
static void load_file(const char *path)
{
	FILE *fp = fopen(path, "rb");
	char chunk[READ_CHUNK];
	int got;
	if (fp == 0) fail("cannot open input file");
	source_capacity = READ_CHUNK;
	source = (char *)grow(0, (size_t)source_capacity + 1);
	for (;;) {
		got = (int)fread(chunk, 1, sizeof chunk, fp);
		if (got <= 0) break;
		if (source_size > INT_MAX - got - 1) fail("input is too large");
		if (source_size + got + 1 > source_capacity) {
			int cap = source_capacity;
			while (cap < source_size + got + 1) {
				if (cap > INT_MAX / 2) { cap = source_size + got + 1; break; }
				cap *= 2;
			}
			source = (char *)grow(source, (size_t)cap + 1);
			source_capacity = cap;
		}
		memcpy(source + source_size, chunk, (size_t)got);
		source_size += got;
	}
	if (ferror(fp)) fail("error reading input file");
	fclose(fp);
	source[source_size] = 0;
}

/*--------------------------------------------------------------------------------------
 * Function:    split_lines
 * Description: Split the source buffer into IR lines and record line endings.
 * Parameter:   void: No parameters.
 * Return:      No return value.
 *--------------------------------------------------------------------------------------*/
static void split_lines(void)
{
	int i, count = 1, paired;
	for (i = 0; i < source_size; ++i) {
		if (source[i] == '\r' || source[i] == '\n') {
			paired = source[i] == '\r' && i + 1 < source_size && source[i + 1] == '\n';
			if (paired) ++i;
			if (i + 1 < source_size) ++count;
		}
	}
	if ((size_t)count > ((size_t)-1) / sizeof(IrLine)) fail("too many IR lines");
	lines = (IrLine *)grow(0, (size_t)count * sizeof(IrLine));
	line_count = 0;
	lines[line_count].text = source;
	lines[line_count].replacement = 0;
	lines[line_count].eol = Q9_EOL_NONE;
	lines[line_count++].removed = 0;
	for (i = 0; i < source_size; ++i) {
		if (source[i] != '\r' && source[i] != '\n') continue;
		paired = source[i] == '\r' && i + 1 < source_size && source[i + 1] == '\n';
		if (paired) lines[line_count - 1].eol = Q9_EOL_CRLF;
		else lines[line_count - 1].eol = source[i] == '\r' ? Q9_EOL_CR : Q9_EOL_LF;
		source[i] = 0;
		if (paired) {
			source[i + 1] = 0;
			++i;
		}
		if (i + 1 < source_size) {
			lines[line_count].text = source + i + 1;
			lines[line_count].replacement = 0;
			lines[line_count].eol = Q9_EOL_NONE;
			lines[line_count++].removed = 0;
		}
	}
}

/*--------------------------------------------------------------------------------------
 * Function:    next_live
 * Description: Find the next line that has not been removed.
 * Parameter:   int index: IR line index.
 * Return:      Line index, or -1 if no line remains.
 *--------------------------------------------------------------------------------------*/
static int next_live(int index)
{
	++index;
	while (index < line_count && lines[index].removed) ++index;
	return index < line_count ? index : -1;
}

/*--------------------------------------------------------------------------------------
 * Function:    line_text
 * Description: Get the replacement or source text for a line.
 * Parameter:   int index: IR line index.
 * Return:      Pointer to the line text.
 *--------------------------------------------------------------------------------------*/
static char *line_text(int index)
{
	return lines[index].replacement != 0 ? lines[index].replacement : lines[index].text;
}

/*--------------------------------------------------------------------------------------
 * Function:    trim
 * Description: Trim leading and trailing spaces and tabs in place.
 * Parameter:   char *text: Mutable IR text.
 * Return:      Pointer to the trimmed text.
 *--------------------------------------------------------------------------------------*/
static char *trim(char *text)
{
	char *end;
	while (*text == ' ' || *text == '\t') ++text;
	end = text + strlen(text);
	while (end > text && (end[-1] == ' ' || end[-1] == '\t')) --end;
	*end = 0;
	return text;
}

/*--------------------------------------------------------------------------------------
 * Function:    parse_push
 * Description: Parse a signed 32-bit PUSH operand.
 * Parameter:   char *text: Mutable IR text.
 *              unsigned long *value: Operand value or destination for the parsed value.
 * Return:      1 on success, otherwise 0.
 *--------------------------------------------------------------------------------------*/
static int parse_push(char *text, unsigned long *value)
{
	char token[64], *p, *end;
	unsigned long v;
	int negative;
	text = trim(text);
	if (strncmp(text, "PUSH", 4) != 0 || (text[4] != ' ' && text[4] != '\t')) return 0;
	p = text + 4;
	while (*p == ' ' || *p == '\t') ++p;
	end = token;
	while (*p != 0 && *p != ' ' && *p != '\t' && end < token + sizeof token - 1) *end++ = *p++;
	*end = 0;
	while (*p == ' ' || *p == '\t') ++p;
	if (*token == 0 || *p != 0) return 0;
	negative = token[0] == '-';
	if (negative) {
		long signed_value = strtol(token, &end, 0);
		if (*end != 0 || signed_value < -2147483647L - 1L || signed_value > 0) return 0;
		v = (unsigned long)signed_value & U32_MASK;
	} else {
		v = strtoul(token, &end, 0);
		/* Without a signedness tag in PUSH, values above INT32_MAX may be
		   unsigned constants. Leave them alone rather than guessing. */
		if (*end != 0 || v > 2147483647UL) return 0;
	}
	*value = v;
	return 1;
}

/*--------------------------------------------------------------------------------------
 * Function:    signed32
 * Description: Interpret the low 32 bits as a signed value.
 * Parameter:   unsigned long value: Operand value or destination for the parsed value.
 * Return:      Signed value.
 *--------------------------------------------------------------------------------------*/
static long signed32(unsigned long value)
{
	value &= U32_MASK;
	if (value & U32_SIGN) return -(long)(U32_MASK - value) - 1L;
	return (long)value;
}

/*--------------------------------------------------------------------------------------
 * Function:    binary_result
 * Description: Evaluate a supported binary operation without signed overflow.
 * Parameter:   const char *op: IR operation name.
 *              unsigned long a: First operand.
 *              unsigned long b: Second operand.
 *              unsigned long *result: Receives the evaluated value.
 * Return:      1 if evaluated, otherwise 0.
 *--------------------------------------------------------------------------------------*/
static int binary_result(const char *op, unsigned long a, unsigned long b, unsigned long *result)
{
	long sa = signed32(a), sb = signed32(b);
	long sr;
	if (strcmp(op, "ADD") == 0) {
		if ((sb > 0 && sa > 2147483647L - sb) ||
		    (sb < 0 && sa < (-2147483647L - 1L) - sb)) return 0;
		sr = sa + sb;
		*result = (unsigned long)sr & U32_MASK;
	}
	else if (strcmp(op, "SUB") == 0) {
		if ((sb < 0 && sa > 2147483647L + sb) ||
		    (sb > 0 && sa < (-2147483647L - 1L) + sb)) return 0;
		sr = sa - sb;
		*result = (unsigned long)sr & U32_MASK;
	}
	else if (strcmp(op, "MUL") == 0) {
		unsigned long ma, mb, limit;
		ma = sa < 0 ? (unsigned long)(-(sa + 1L)) + 1UL : (unsigned long)sa;
		mb = sb < 0 ? (unsigned long)(-(sb + 1L)) + 1UL : (unsigned long)sb;
		limit = ((sa < 0) != (sb < 0)) ? U32_SIGN : U32_SIGN - 1UL;
		if (mb != 0 && ma > limit / mb) return 0;
		ma *= mb;
		*result = ((sa < 0) != (sb < 0)) ? (0UL - ma) & U32_MASK : ma;
	}
	else if (strcmp(op, "BAND") == 0) *result = a & b;
	else if (strcmp(op, "BXOR") == 0) *result = a ^ b;
	else if (strcmp(op, "BOR") == 0) *result = a | b;
	else if (strcmp(op, "CMPEQ") == 0) *result = a == b;
	else if (strcmp(op, "CMPNE") == 0) *result = a != b;
	else if (strcmp(op, "CMPLT") == 0) *result = sa < sb;
	else if (strcmp(op, "CMPGT") == 0) *result = sa > sb;
	else if (strcmp(op, "CMPLE") == 0) *result = sa <= sb;
	else if (strcmp(op, "CMPGE") == 0) *result = sa >= sb;
	else if (strcmp(op, "CMPULT") == 0) *result = a < b;
	else if (strcmp(op, "CMPUGT") == 0) *result = a > b;
	else if (strcmp(op, "CMPULE") == 0) *result = a <= b;
	else if (strcmp(op, "CMPUGE") == 0) *result = a >= b;
	else return 0;
	*result &= U32_MASK;
	return 1;
}

/*--------------------------------------------------------------------------------------
 * Function:    unary_result
 * Description: Evaluate a supported unary operation.
 * Parameter:   const char *op: IR operation name.
 *              unsigned long a: First operand.
 *              unsigned long *result: Receives the evaluated value.
 * Return:      1 if evaluated, otherwise 0.
 *--------------------------------------------------------------------------------------*/
static int unary_result(const char *op, unsigned long a, unsigned long *result)
{
	if (strcmp(op, "NEG") == 0) {
		if (a == U32_SIGN) return 0;
		*result = (0UL - a) & U32_MASK;
	}
	else if (strcmp(op, "NOT") == 0) *result = a == 0;
	else if (strcmp(op, "NOTBIT") == 0) *result = (~a) & U32_MASK;
	else if (strcmp(op, "NARROWC") == 0) *result = a & 0xffUL;
	else return 0;
	return 1;
}

/*--------------------------------------------------------------------------------------
 * Function:    set_push
 * Description: Replace a line with a PUSH of the given value.
 * Parameter:   int index: IR line index.
 *              unsigned long value: Operand value or destination for the parsed value.
 * Return:      No return value.
 *--------------------------------------------------------------------------------------*/
static void set_push(int index, unsigned long value)
{
	char text[48], digits[16];
	char *out = text;
	int ndigits = 0;
	long number = signed32(value);
	unsigned long magnitude;
	memcpy(out, "PUSH ", 5);
	out += 5;
	if (number < 0) {
		*out++ = '-';
		magnitude = (unsigned long)(-(number + 1L)) + 1UL;
	} else magnitude = (unsigned long)number;
	do {
		digits[ndigits++] = (char)('0' + magnitude % 10UL);
		magnitude /= 10UL;
	} while (magnitude != 0);
	while (ndigits > 0) *out++ = digits[--ndigits];
	*out = 0;
	lines[index].replacement = (char *)grow(0, strlen(text) + 1);
	strcpy(lines[index].replacement, text);
}

/*--------------------------------------------------------------------------------------
 * Function:    optimize
 * Description: Fold supported constant expressions in one pass.
 * Parameter:   void: No parameters.
 * Return:      Nonzero if the pass changed any lines.
 *--------------------------------------------------------------------------------------*/
static int optimize(void)
{
	int i, j, k, changed = 0;
	unsigned long a, b, result;
	for (i = 0; i < line_count; ++i) {
		if (lines[i].removed) continue;
		j = next_live(i);
		if (j < 0) continue;
		if (parse_push(line_text(i), &a)) {
			if (parse_push(line_text(j), &b)) {
				k = next_live(j);
				if (k >= 0 && binary_result(trim(line_text(k)), a, b, &result)) {
					set_push(i, result);
					lines[j].removed = 1;
					lines[k].removed = 1;
					++optimizations; changed = 1;
					--i;
				}
			} else if (unary_result(trim(line_text(j)), a, &result)) {
				set_push(i, result);
				lines[j].removed = 1;
				++optimizations; changed = 1;
				--i;
			}
		}
	}
	return changed;
}

/*--------------------------------------------------------------------------------------
 * Function:    write_file
 * Description: Write remaining IR lines to an output file.
 * Parameter:   const char *path: Input or output path.
 * Return:      No return value.
 *--------------------------------------------------------------------------------------*/
static void write_file(const char *path)
{
	FILE *fp = fopen(path, "wb");
	int i;
	if (fp == 0) fail("cannot open output file");
	for (i = 0; i < line_count; ++i) {
		char *text;
		if (lines[i].removed) continue;
		text = line_text(i);
		if (fwrite(text, 1, strlen(text), fp) != strlen(text)) fail("error writing output file");
		if (lines[i].eol == Q9_EOL_LF && fputc('\n', fp) == EOF) fail("error writing output file");
		if (lines[i].eol == Q9_EOL_CRLF && (fputc('\r', fp) == EOF || fputc('\n', fp) == EOF))
			fail("error writing output file");
		if (lines[i].eol == Q9_EOL_CR && fputc('\r', fp) == EOF) fail("error writing output file");
	}
	if (fclose(fp) != 0) fail("error closing output file");
}

/*--------------------------------------------------------------------------------------
 * Function:    main
 * Description: Read, optimize and write the IR file.
 * Parameter:   int argc: Number of command-line arguments.
 *              char **argv: Command-line argument vector.
 * Return:      Process exit status.
 *--------------------------------------------------------------------------------------*/
int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: qost <input.ir> <output.opt.ir>\n");
		return 2;
	}
	load_file(argv[1]);
	split_lines();
	while (optimize()) { /* Fold newly exposed constant expressions. */ }
	write_file(argv[2]);
	fprintf(stderr, "qost: %d constant folds\n", optimizations);
	return 0;
}
