
/* color constants */
#ifdef OPTIMIZE_FOR_SIZE
# define _MAKE_COLOR(c,b) ""
#else
# define _MAKE_COLOR(c,b) "\e[" c ";" b "m"
#endif
static const char *BOLD = _MAKE_COLOR("00", "01");
static const char *NORM = _MAKE_COLOR("00", "00");
static const char *BLUE = _MAKE_COLOR("36", "01");
static const char *DKBLUE = _MAKE_COLOR("34", "01");
static const char *CYAN = _MAKE_COLOR("00", "36");
static const char *GREEN = _MAKE_COLOR("32", "01");
static const char *DKGREEN = _MAKE_COLOR("00", "32");
static const char *MAGENTA = _MAKE_COLOR("00", "35");
static const char *RED = _MAKE_COLOR("31", "01");
static const char *YELLOW = _MAKE_COLOR("33", "01");
static const char *BRYELLOW = _MAKE_COLOR("01", "33");
static const char *WHITE = _MAKE_COLOR("01", "38");

static const char *COLOR_MAP = CONFIG_EPREFIX "etc/portage/color.map";

#define COLOR _MAKE_COLOR

typedef struct {
	const char *name;
	char value[16];
} cpairtype;

static cpairtype color_pairs[] = {
	{"blue",      COLOR("34", "01") },
	{"brown",     COLOR("00", "33") },
	{"darkblue",  COLOR("00", "34") },
	{"darkgreen", COLOR("00", "32") },
	{"darkred",   COLOR("00", "31") },
	{"faint",     COLOR("00", "02") },
	{"fuchsia",   COLOR("35", "01") },
	{"green",     COLOR("32", "01") },
	{"purple",    COLOR("00", "35") },
	{"red",       COLOR("31", "01") },
	{"teal",      COLOR("00", "36") },
	{"turquoise", COLOR("36", "01") },
	{"yellow",    COLOR("01", "33") },
	{"white",     COLOR("01", "38") },
	{"eol",       COLOR("00", "00") },
};

void color_remap(void);
void color_remap(void)
{
	FILE *fp;
	unsigned int i;
	size_t buflen, linelen;
	char *buf;
	char *p;
	unsigned int lineno = 0;

	if ((fp = fopen(COLOR_MAP, "r")) == NULL)
		return;

	buf = NULL;
	while ((linelen = getline(&buf, &buflen, fp)) != -1) {
		lineno++;
		/* eat comments */
		if ((p = strchr(buf, '#')) != NULL)
			*p = '\0';

		rmspace_len(buf, linelen);

		p = strchr(buf, '=');
		if (p == NULL)
			continue;

		*p++ = 0; /* split the pair */
		for (i = 0; i < ARRAY_SIZE(color_pairs); ++i)
			if (strcmp(buf, color_pairs[i].name) == 0) {
				if (strncmp(p, "0x", 2) == 0)
					warn("[%s=%s] RGB values in color map are not supported on line %d of %s", buf, p, lineno, COLOR_MAP);
				else
					snprintf(color_pairs[i].value, sizeof(color_pairs[i].value), "\e[%s", p);
			}
	}

	free(buf);
	fclose(fp);

	for (i = 0; i < ARRAY_SIZE(color_pairs); ++i) {
		/* unmapped: MAGENTA YELLOW */
		if (strcmp(color_pairs[i].name, "white") == 0)
			WHITE = color_pairs[i].value;
		else if (strcmp(color_pairs[i].name, "green") == 0)
			GREEN = color_pairs[i].value;
		else if (strcmp(color_pairs[i].name, "darkgreen") == 0)
			DKGREEN = color_pairs[i].value;
		else if (strcmp(color_pairs[i].name, "red") == 0)
			RED = color_pairs[i].value;
		else if (strcmp(color_pairs[i].name, "blue") == 0)
			DKBLUE = color_pairs[i].value;
		else if (strcmp(color_pairs[i].name, "turquoise") == 0)
			BLUE = color_pairs[i].value;
		else if (strcmp(color_pairs[i].name, "yellow") == 0)
			BRYELLOW = color_pairs[i].value;
		else if (strcmp(color_pairs[i].name, "teal") == 0)
			CYAN = color_pairs[i].value;
	}
}
