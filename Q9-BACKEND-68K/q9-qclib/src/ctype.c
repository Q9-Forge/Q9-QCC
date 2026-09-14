/* q9-qclib ASCII character classification. */

/* Function: isalpha
 * Tests for an ASCII letter.
 * Parameters: a IR argument frame containing the character.
 * Returns: Non-zero for A-Z or a-z. */
int isalpha(int *a)
{
	int c;
	c = a[0] & 255;
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/* Function: isalnum
 * Tests for an ASCII letter or digit. */
int isalnum(int *a)
{
	int c;
	c = a[0] & 255;
	return isalpha(a) || (c >= '0' && c <= '9');
}

/* Function: isspace
 * Tests for the C89 ASCII whitespace characters. */
int isspace(int *a)
{
	int c;
	c = a[0] & 255;
	return c == 9 || c == 10 || c == 11 || c == 12 || c == 13 || c == 32;
}

/* Function: isprint
 * Tests for an ASCII printable character. */
int isprint(int *a)
{
	int c;
	c = a[0] & 255;
	return c >= 32 && c <= 126;
}
