static int wregcomp(regex_t *preg, const char *regex, int cflags)
{
	int ret = regcomp(preg, regex, cflags);
	if (unlikely(ret)) {
		char errbuf[256];
		regerror(ret, preg, errbuf, sizeof(errbuf));
		warn("invalid regexp: %s -- %s\n", regex, errbuf);
	}
	return ret;
}

static void xregcomp(regex_t *preg, const char *regex, int cflags)
{
	if (unlikely(wregcomp(preg, regex, cflags)))
		exit(EXIT_FAILURE);
}
