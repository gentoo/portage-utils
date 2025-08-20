/*
 * Copyright 2005-2025 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "colors.h"
#include "rmspace.h"

#define COLOR_MAP CONFIG_EPREFIX "etc/portage/color.map"

#define CPAIR_VALUE_LEN 16
typedef struct {
	const char  *name;
	const char  *origval;
	const char **var;
	char         tmpbuf[CPAIR_VALUE_LEN];
} colourpair;

typedef struct {
	const char *name;
	const char *value;
} colourmap;

/* color constants */
#ifdef OPTIMIZE_FOR_SIZE
# define _MAKE_COLOUR1(c)   ""
# define _MAKE_COLOUR2(c,b) ""
#else
# define _MAKE_COLOUR1(c)   "\e[" #c "m"
# define _MAKE_COLOUR2(c,b) "\e[" #c ";" #b "m"
#endif
#define LQC_BLACK      _MAKE_COLOUR1(30)
#define LQC_DARKGREY   _MAKE_COLOUR2(30,01)
#define LQC_RED        _MAKE_COLOUR1(31)
#define LQC_DARKRED    _MAKE_COLOUR2(31,01)
#define LQC_GREEN      _MAKE_COLOUR1(32)
#define LQC_DARKGREEN  _MAKE_COLOUR2(32,01)
#define LQC_YELLOW     _MAKE_COLOUR1(33)
#define LQC_BROWN      _MAKE_COLOUR2(33,01)
#define LQC_BLUE       _MAKE_COLOUR1(34)
#define LQC_DARKBLUE   _MAKE_COLOUR2(34,01)
#define LQC_FUCHSIA    _MAKE_COLOUR1(35)
#define LQC_PURPLE     _MAKE_COLOUR2(35,01)
#define LQC_TURQUOISE  _MAKE_COLOUR1(36)
#define LQC_TEAL       _MAKE_COLOUR2(36,01)
#define LQC_WHITE      _MAKE_COLOUR1(37)
#define LQC_LIGHTGREY  _MAKE_COLOUR2(37,01)

/* symbols that are used by the code and hold the active colour escapes */
const char *NORM;
const char *BLUE;
const char *BOLD;
const char *BRYELLOW;
const char *CYAN;
const char *DKBLUE;
const char *DKGREEN;
const char *GREEN;
const char *MAGENTA;
const char *RED;
const char *WHITE;
const char *YELLOW;

/* all of the above need to be referenced in the list below */
static colourpair colour_pairs[] = {
	{"norm",       _MAKE_COLOUR2(0,0),   &NORM,      ""},
	{"teal",       LQC_TEAL,             &BLUE,      ""},
	{"bold",       _MAKE_COLOUR2(0,1),   &BOLD,      ""},
	{"brown",      LQC_BROWN,            &BRYELLOW,  ""},
	{"turquoise",  LQC_TURQUOISE,        &CYAN,      ""},
	{"darkblue",   LQC_DARKBLUE,         &DKBLUE,    ""},
	{"green",      LQC_GREEN,            &DKGREEN,   ""},
	{"darkgreen",  LQC_DARKGREEN,        &GREEN,     ""},
	{"fuchsia",    LQC_FUCHSIA,          &MAGENTA,   ""},
	{"red",        LQC_DARKRED,          &RED,       ""},
	{"white",      _MAKE_COLOUR2(1,38),  &WHITE,     ""},
	{"yellow",     LQC_BROWN,            &YELLOW,    ""}
};

static colourmap colour_map[] = {
	/* Portage's list of names */
	{"black",      LQC_BLACK    },
	{"darkgrey",   LQC_DARKGREY },
	{"darkgray",   LQC_DARKGREY },
	{"red",        LQC_RED      },
	{"darkred",    LQC_DARKRED  },
	{"green",      LQC_GREEN    },
	{"darkgreen",  LQC_DARKGREEN},
	{"yellow",     LQC_YELLOW   },
	{"brown",      LQC_BROWN    },
	{"darkyellow", LQC_BROWN    },
	{"blue",       LQC_BLUE     },
	{"darkblue",   LQC_DARKBLUE },
	{"fuchsia",    LQC_FUCHSIA  },
	{"purple",     LQC_PURPLE   },
	{"turquoise",  LQC_TURQUOISE},
	{"darkteal",   LQC_TURQUOISE},
	{"teal",       LQC_TEAL     },
	/* portage-utils historical colour names */
	{"bryellow",   LQC_BROWN    },
	{"cyan",       LQC_TURQUOISE},
	{"dkblue",     LQC_DARKBLUE },
	{"dkgreen",    LQC_GREEN    },
	{"magenta",    LQC_FUCHSIA  }
};
static colourmap rgb_map[] = {
	/* RGB ANSI codes */
	{"0x000000",   LQC_BLACK    },
	{"0x555555",   LQC_DARKGREY },
	{"0xAA0000",   LQC_RED      },
	{"0xFF5555",   LQC_DARKRED  },
	{"0x00AA00",   LQC_GREEN    },
	{"0x55FF55",   LQC_DARKGREEN},
	{"0xAA5500",   LQC_YELLOW   },
	{"0xFFFF55",   LQC_BROWN    },
	{"0x0000AA",   LQC_BLUE     },
	{"0x5555FF",   LQC_DARKBLUE },
	{"0xAA00AA",   LQC_FUCHSIA  },
	{"0xFF55FF",   LQC_PURPLE   },
	{"0x00AAAA",   LQC_TURQUOISE},
	{"0x55FFFF",   LQC_TEAL     },
	{"0xAAAAAA",   LQC_WHITE    },
	{"0xFFFFFF",   LQC_LIGHTGREY},
	/* some terminals have darkyellow instead of brown */
	{"0xAAAA00",   LQC_BROWN    }
};

void
color_remap(void)
{
	FILE *fp;
	unsigned int i;
	int linelen;
	size_t buflen;
	char *buf;
	char *p;
	unsigned int lineno = 0;

	/* (re)set to defaults */
	for (i = 0; i < ARRAY_SIZE(colour_pairs); i++)
		*(colour_pairs[i].var) = colour_pairs[i].origval;

	if ((fp = fopen(COLOR_MAP, "r")) == NULL)
		return;

	buf = NULL;
	while ((linelen = getline(&buf, &buflen, fp)) >= 0) {
		lineno++;
		/* eat comments */
		if ((p = strchr(buf, '#')) != NULL)
			*p = '\0';

		p = strchr(buf, '=');
		if (p == NULL)
			continue;

		*p++ = '\0'; /* split the pair */
		rmspace(buf);
		rmspace(p);

		/* strip off quotes from values */
		linelen = (int)strlen(p);
		if (linelen > 1 &&
			p[0] == p[linelen - 1] &&
			(p[0] == '\'' ||
			 p[0] == '"'))
		{
			p[linelen - 1] = '\0';
			p++;
		}

		for (i = 0; i < ARRAY_SIZE(colour_pairs); i++) {
			int found = 0;
			if (strcmp(buf, colour_pairs[i].name) == 0) {
				if (strncmp(p, "0x", 2) == 0) {
					size_t n;
					for (n = 0; n < ARRAY_SIZE(rgb_map); n++) {
						if (strcasecmp(rgb_map[n].name, p) == 0) {
							found = 1;
							*(colour_pairs[i].var) = rgb_map[n].value;
							break;
						}
					}
					if (found == 0)
						warn("[%s=%s] arbitrary RGB values in color map "
							 "are not supported on line %d of %s",
							 buf, p, lineno, COLOR_MAP);
				} else {
					/* color=color format support */
					size_t n;
					for (n = 0; n < ARRAY_SIZE(colour_map); n++) {
						if (strcasecmp(colour_map[n].name, p) == 0) {
							found = 1;
							*(colour_pairs[i].var) = colour_map[n].value;
							break;
						}
					}

					if (found == 0) {
						snprintf(colour_pairs[i].tmpbuf,
								 sizeof(colour_pairs[i].tmpbuf),
								 "\e[%s", p);
						*(colour_pairs[i].var) = colour_pairs[i].tmpbuf;
					}
				}

				break;
			}
		}
	}

	free(buf);
	fclose(fp);
}

void
color_clear(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(colour_pairs); i++)
		*(colour_pairs[i].var) = "";
}
