
/* removed leading/trailing extraneous white space */
static char *rmspace(char *s)
{
	register char *p;
	/* find the start of trailing space and set it to \0 */
	for (p = s + strlen(s) - 1; (p >= s && isspace(*p)); --p);
	if (p != s + strlen(s) - 1)
		*(p + 1) = 0;
	/* find the end of leading space and set p to it */
	for (p = s; (isspace(*p) && *p); ++p);
	/* move the memory backward to overwrite leading space */
	if (p != s)
		memmove(s, p, strlen(p)+1);
	return s;
}

