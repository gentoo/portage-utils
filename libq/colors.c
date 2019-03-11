/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

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
#define CPAIR_VALUE_LEN 16

typedef struct {
	const char *name;
	char value[CPAIR_VALUE_LEN];
	char origval[CPAIR_VALUE_LEN];
} cpairtype;

#define X2(X) X, X
static cpairtype color_pairs[] = {
	{"blue",      X2(COLOR("34", "01")) },
	{"brown",     X2(COLOR("00", "33")) },
	{"darkblue",  X2(COLOR("00", "34")) },
	{"darkgreen", X2(COLOR("00", "32")) },
	{"darkred",   X2(COLOR("00", "31")) },
	{"faint",     X2(COLOR("00", "02")) },
	{"fuchsia",   X2(COLOR("35", "01")) },
	{"green",     X2(COLOR("32", "01")) },
	{"purple",    X2(COLOR("00", "35")) },
	{"red",       X2(COLOR("31", "01")) },
	{"teal",      X2(COLOR("00", "36")) },
	{"turquoise", X2(COLOR("36", "01")) },
	{"yellow",    X2(COLOR("01", "33")) },
	{"white",     X2(COLOR("01", "38")) },
	{"lightgray", X2(COLOR("00", "37")) },
	{"eol",       X2(COLOR("00", "00")) },
};
#undef X2

static void
color_remap(void)
{
	FILE *fp;
	unsigned int i;
	int linelen;
	size_t buflen;
	char *buf;
	char *p;
	unsigned int lineno = 0;

	if ((fp = fopen(COLOR_MAP, "r")) == NULL)
		return;

	buf = NULL;
	while ((linelen = getline(&buf, &buflen, fp)) >= 0) {
		lineno++;
		/* eat comments */
		if ((p = strchr(buf, '#')) != NULL)
			*p = '\0';

		rmspace_len(buf, (size_t)linelen);

		p = strchr(buf, '=');
		if (p == NULL)
			continue;

		*p++ = 0; /* split the pair */
		for (i = 0; i < ARRAY_SIZE(color_pairs); ++i) {
			if (strcmp(buf, color_pairs[i].name) == 0) {
				if (strncmp(p, "0x", 2) == 0) {
					warn("[%s=%s] RGB values in color map are not "
							"supported on line %d of %s",
							buf, p, lineno, COLOR_MAP);
				} else {
					/* color=color format support */
					size_t n;
					int found = 0;
					for (n = 0; n < ARRAY_SIZE(color_pairs); n++) {
						if (strcmp(color_pairs[n].name, p) == 0) {
							strncpy(color_pairs[i].value,
									color_pairs[n].origval, CPAIR_VALUE_LEN);
							found = 1;
							break;
						}
					}

					if (!found)
						snprintf(color_pairs[i].value,
								sizeof(color_pairs[i].origval), "\e[%s", p);
				}
			}
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
