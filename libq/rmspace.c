
/* removed leading/trailing extraneous white space */
static char *rmspace(char *s)
{
	char *p;
	size_t len = strlen(s);
	/* find the start of trailing space and set it to \0 */
	for (p = s + len - 1; (p >= s && isspace(*p)); --p)
		continue;
	p[1] = '\0';
	len = (p - s) + 1;
	/* find the end of leading space and set p to it */
	for (p = s; (isspace(*p) && *p); ++p)
		continue;
	/* move the memory backward to overwrite leading space */
	if (p != s)
		memmove(s, p, len - (p - s) + 1);
	return s;
}
