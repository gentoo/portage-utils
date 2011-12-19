/*
 * Copyright 2005-2008 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/main.c,v 1.206 2011/12/19 04:37:23 vapier Exp $
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2008 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef HAVE_CONFIG_H
# include "config.h"  /* make sure we have EPREFIX, if set */
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifdef _AIX
#define _LINUX_SOURCE_COMPAT
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <dirent.h>
#include <getopt.h>
#include <regex.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <assert.h>
#include "main.h"

/* prototypes and such */
static char eat_file(const char *file, char *buf, const size_t bufsize);
int charmatch(const char *str1, const char *str2);
int rematch(const char *, const char *, int);
static char *rmspace(char *);

void initialize_portage_env(void);
void initialize_ebuild_flat(void);
void reinitialize_ebuild_flat(void);
void reinitialize_as_needed(void);
void cleanup(void);
int lookup_applet_idx(const char *);

/* variables to control runtime behavior */
char *module_name = NULL;
char *modpath = NULL;
int found = 0;
int verbose = 0;
int quiet = 0;
char pretend = 0;
char reinitialize = 0;
char reinitialize_metacache = 0;
static char *portdir;
static char *portarch;
static char *portvdb;
const char portcachedir[] = "metadata/cache";
static char *portroot;
static char *eprefix;
static char *config_protect, *config_protect_mask;

static char *pkgdir;
static char *port_tmpdir;

static char *binhost;
static char *features;
static char *accept_license;
static char *install_mask;

const char err_noapplet[] = "Sorry this applet was disabled at compile time";

/* helper functions for showing errors */
static const char *argv0;

#ifdef EBUG
# include <sys/resource.h>
void init_coredumps(void);
void init_coredumps(void)
{
	struct rlimit rl;
	rl.rlim_cur = RLIM_INFINITY;
	rl.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_CORE, &rl);
}
#endif

/* include common library code */
#include "libq/libq.c"

void no_colors(void);
void no_colors()
{
	/* echo $(awk '{print $4,"="}' libq/colors.c  | grep ^* |cut -c 2-| grep ^[A-Z] |tr '\n' ' ') = \"\"\;  */
	BOLD = NORM = BLUE = DKBLUE = CYAN = GREEN = DKGREEN = MAGENTA = RED = YELLOW = BRYELLOW = WHITE = "";
	setenv("NOCOLOR", "true", 1);
}

/* include common applet defs */
#include "applets.h"

/* Common usage for all applets */
#define COMMON_FLAGS "vqChV"
#define COMMON_LONG_OPTS \
	{"verbose",   no_argument, NULL, 'v'}, \
	{"quiet",     no_argument, NULL, 'q'}, \
	{"nocolor",   no_argument, NULL, 'C'}, \
	{"help",      no_argument, NULL, 'h'}, \
	{"version",   no_argument, NULL, 'V'}, \
	{NULL,        no_argument, NULL, 0x0}
#define COMMON_OPTS_HELP \
	"Make a lot of noise", \
	"Tighter output; suppress warnings", \
	"Don't output color", \
	"Print this help and exit", \
	"Print version and exit", \
	NULL
#define COMMON_GETOPTS_CASES(applet) \
	case 'v': ++verbose; break; \
	case 'q': ++quiet; if (freopen("/dev/null", "w", stderr)) { /* ignore errors */ } break; \
	case 'V': version_barf( applet ## _rcsid ); break; \
	case 'h': applet ## _usage(EXIT_SUCCESS); break; \
	case 'C': no_colors(); break; \
	default: applet ## _usage(EXIT_FAILURE); break;

/* display usage and exit */
static void usage(int status, const char *flags, struct option const opts[],
                  const char * const help[], int blabber)
{
	unsigned long i;
	if (blabber == 0) {
		printf("%sUsage:%s %sq%s %s<applet> <args>%s  : %s"
			"invoke a portage utility applet\n\n", GREEN,
			NORM, YELLOW, NORM, DKBLUE, RED, NORM);
		printf("%sCurrently defined applets:%s\n", GREEN, NORM);
		for (i = 0; applets[i].desc; ++i)
			if (applets[i].func)
				printf(" %s%8s%s %s%-16s%s%s:%s %s\n",
					YELLOW, applets[i].name, NORM,
					DKBLUE, applets[i].opts, NORM,
					RED, NORM, _(applets[i].desc));
	} else if (blabber > 0) {
			printf("%sUsage:%s %s%s%s <opts> %s%s%s %s:%s %s\n", GREEN, NORM,
				YELLOW, applets[blabber].name, NORM,
				DKBLUE, applets[blabber].opts, NORM,
				RED, NORM, _(applets[blabber].desc));
	}
	if (module_name != NULL)
		printf("%sLoaded module:%s\n%s%8s%s %s<args>%s\n", GREEN, NORM, YELLOW, module_name, NORM, DKBLUE, NORM);

	printf("\n%sOptions:%s -[%s]\n", GREEN, NORM, flags);
	for (i = 0; opts[i].name; ++i) {
		assert(help[i] != NULL); /* this assert is a life saver when adding new applets. */
		if (opts[i].has_arg == no_argument)
			printf("  -%c, --%-15s%s*%s %s\n", opts[i].val,
				opts[i].name, RED, NORM, _(help[i]));
		else
			printf("  -%c, --%-8s %s<arg>%s %s*%s %s\n", opts[i].val,
				opts[i].name, DKBLUE, NORM, RED, NORM, _(help[i]));
	}
	exit(status);
}

static void version_barf(const char *Id)
{
#ifndef VERSION
# define VERSION "cvs"
#endif
	printf("portage-utils-%s: compiled on %s\n%s\n"
	       "%s written for Gentoo by <solar and vapier @ gentoo.org>\n",
	       VERSION, __DATE__, Id, argv0);
	exit(EXIT_SUCCESS);
}

static char eat_file_fd(int fd, char *buf, const size_t bufsize)
{
	struct stat s;

	buf[0] = '\0';
	if (fstat(fd, &s) != 0)
		return 0;
	if (s.st_size) {
		if (bufsize < (size_t)s.st_size)
			return 0;
		if (read(fd, buf, s.st_size) != (ssize_t)s.st_size)
			return 0;
		buf[s.st_size] = '\0';
	} else {
		if (read(fd, buf, bufsize) == 0)
			return 0;
		buf[bufsize - 1] = '\0';
	}

	return 1;
}

static char eat_file_at(int dfd, const char *file, char *buf, const size_t bufsize)
{
	int fd;
	char ret;

	if ((fd = openat(dfd, file, O_RDONLY)) == -1) {
		buf[0] = '\0';
		return 0;
	}

	ret = eat_file_fd(fd, buf, bufsize);

	close(fd);
	return ret;
}

static char eat_file(const char *file, char *buf, const size_t bufsize)
{
	return eat_file_at(AT_FDCWD, file, buf, bufsize);
}

static bool prompt(const char *p)
{
	printf("%s? [Y/n] ", p);
	fflush(stdout);
	switch (getc(stdin)) {
	case '\n':
	case 'y':
	case 'Y':
		return true;
	default:
		return false;
	}
}

/* if all chars in str1 coincide with the begining of the str2 return 0 */
int charmatch(const char *str1, const char *str2)
{
	if ((str1 == NULL) || (str2 == NULL))
		return EXIT_FAILURE;

	while (*str1 != 0) {
		/* if str2==0 but str1!=0 we'll return and so it's
		 * impossible to touch match++ address here */
		if (*str1 != *str2)
			return 1;
		str1++;
		str2++;
	}

	return 0;
}

int rematch(const char *re, const char *match, int cflags)
{
	regex_t preg;
	int ret;

	if ((match == NULL) || (re == NULL))
		return EXIT_FAILURE;

	if (wregcomp(&preg, re, cflags))
		return EXIT_FAILURE;
	ret = regexec(&preg, match, 0, NULL, 0);
	regfree(&preg);

	return ret;
}

/* removes adjacent extraneous white space */
static char *remove_extra_space(char *str);
static char *remove_extra_space(char *str)
{
	char *p, c = ' ';
	size_t len, pos = 0;
	char *buf;

	if (str == NULL)
		return NULL;
	len = strlen(str);
	buf = xzalloc(len+1);
	for (p = str; *p != 0; ++p) {
		if (!isspace(*p)) c = *p; else {
			if (c == ' ') continue;
			c = ' ';
		}
		buf[pos] = c;
		pos++;
	}
	if (pos > 0 && buf[pos-1] == ' ') buf[pos-1] = '\0';
	strcpy(str, buf);
	free(buf);
	return str;
}

static char *pkg_name(const char *const_name);
static char *pkg_name(const char *const_name) {
	static char name[_POSIX_PATH_MAX];
	char *ptr;
	if (!const_name)
		return NULL;
	strncpy(name, const_name, sizeof(name));
	if ((ptr = strrchr(name, ':')) != NULL)
		*ptr = 0;
	return name;
}

static char *slot_name(const char *name);
static char *slot_name(const char *name) {
	char *ptr;
	if (!name)
		return NULL;
	if ((ptr = strrchr(name, ':')) != NULL)
		return ptr+1;
	return NULL;
}

static void freeargv(int argc, char **argv)
{
	while (argc--)
		free(argv[argc]);
	free(argv);
}

static void makeargv(const char *string, int *argc, char ***argv)
{
	int curc = 2;
	char *q, *p, *str;
	(*argv) = xmalloc(sizeof(char **) * curc);

	*argc = 1;
	(*argv)[0] = xstrdup(argv0);
	q = xstrdup(string);
	str = q;

	remove_extra_space(str);
	rmspace(str);

	while (str) {
		if ((p = strchr(str, ' ')) != NULL)
			*(p++) = '\0';

		if (*argc == curc) {
			curc *= 2;
			(*argv) = xrealloc(*argv, sizeof(char **) * curc);
		}
		(*argv)[*argc] = xstrdup(str);
		(*argc)++;
		str = p;
	}
	free(q);
}

/*
 * Parse a line of CONTENTS file and provide access to the individual fields
 */
typedef enum {
	CONTENTS_DIR, CONTENTS_OBJ, CONTENTS_SYM
} contents_type;
typedef struct {
	contents_type type;
	char *_data;
	char *name;
	char *sym_target;
	char *digest;
	char *mtime_str;
	long mtime;
} contents_entry;

contents_entry *contents_parse_line(char *line);
contents_entry *contents_parse_line(char *line)
{
	static contents_entry e;
	char *p;

	if (!line || !*line || *line == '\n')
		return NULL;

	/* chop trailing newline */
	if ((p = strrchr(line, '\n')) != NULL)
		*p = '\0';

	/* ferringb wants to break portage/vdb by using tabs vs spaces
	 * so filenames can have lame ass spaces in them..
	 * (I smell Windows near by)
	 * Anyway we just convert that crap to a space so we can still
	 * parse quickly */
	p = line;
	while ((p = strchr(p, '\t')) != NULL)
		*p = ' ';

	memset(&e, 0x00, sizeof(e));
	e._data = line;

	if (!strncmp(e._data, "obj ", 4))
		e.type = CONTENTS_OBJ;
	else if (!strncmp(e._data, "dir ", 4))
		e.type = CONTENTS_DIR;
	else if (!strncmp(e._data, "sym ", 4))
		e.type = CONTENTS_SYM;
	else
		return NULL;

	e.name = e._data + 4;

	switch (e.type) {
		/* dir /bin */
		case CONTENTS_DIR:
			break;

		/* obj /bin/bash 62ed51c8b23866777552643ec57614b0 1120707577 */
		case CONTENTS_OBJ:
			if ((e.mtime_str = strrchr(e.name, ' ')) == NULL)
				return NULL;
			*e.mtime_str++ = '\0';
			if ((e.digest = strrchr(e.name, ' ')) == NULL)
				return NULL;
			*e.digest++ = '\0';
			break;

		/* sym /bin/sh -> bash 1120707577 */
		case CONTENTS_SYM:
			if ((e.mtime_str = strrchr(e.name, ' ')) == NULL)
				return NULL;
			*e.mtime_str++ = '\0';
			if ((e.sym_target = strstr(e.name, " -> ")) == NULL)
				return NULL;
			*e.sym_target = '\0';
			e.sym_target += 4;
			break;
	}

	if (e.mtime_str) {
		e.mtime = strtol(e.mtime_str, NULL, 10);
		if (e.mtime == LONG_MAX) {
			e.mtime = 0;
			e.mtime_str = NULL;
		}
	}

	return &e;
}

static void strincr_var(const char *name, const char *s, char **value, size_t *value_len)
{
	size_t len;
	char *buf, *p, *nv;

	len = strlen(s);
	*value = xrealloc(*value, *value_len + len + 2);
	nv = &(*value)[*value_len];
	if (*value_len)
		*nv++ = ' ';
	memcpy(nv, s, len + 1);

	while ((p = strstr(nv, "-*")) != NULL)
		memset(*value, ' ', p - *value);

	/* This function is mainly used by the startup code for parsing
		make.conf and stacking variables remove.
		variables can be in the form of ${v} or $v
		works:
			FEATURES="${FEATURES} foo"
			FEATURES="$FEATURES foo"
			FEATURES="baz bar -* foo"

		wont work:
			FEATURES="${OTHERVAR} foo"
			FEATURES="-nls nls -nls"
			FEATURES="nls nls nls"
	*/

	len = strlen(name);
	buf = alloca(len + 3 + 1);

	sprintf(buf, "${%s}", name);
	if ((p = strstr(nv, buf)) != NULL)
		memset(p, ' ', len + 3);

	sprintf(buf, "$%s", name);
	if ((p = strstr(nv, buf)) != NULL)
		memset(p, ' ', len + 1);

	remove_extra_space(*value);
	*value_len = strlen(*value);
	/* we should sort here */
}

typedef enum { _Q_BOOL, _Q_STR, _Q_ISTR } var_types;
typedef struct {
	const char *name;
	const size_t name_len;
	const var_types type;
	union {
		char **s;
		bool *b;
	} value;
	size_t value_len;
	const char *default_value;
} env_vars;

_q_static env_vars *get_portage_env_var(env_vars *vars, const char *name)
{
	size_t i;

	for (i = 0; vars[i].name; ++i)
		if (!strcmp(vars[i].name, name))
			return &vars[i];

	return NULL;
}

_q_static void set_portage_env_var(env_vars *var, const char *value)
{
	switch (var->type) {
	case _Q_BOOL:
		*var->value.b = 1;
		break;
	case _Q_STR:
		free(*var->value.s);
		*var->value.s = xstrdup_len(value, &var->value_len);
		break;
	case _Q_ISTR:
		strincr_var(var->name, value, var->value.s, &var->value_len);
		break;
	}
}

/* Helper to read a portage env file (e.g. make.conf) */
_q_static void read_portage_env_file(const char *file, env_vars vars[])
{
	size_t i, buflen, line;
	FILE *fp;
	char *buf, *s, *p;

	IF_DEBUG(fprintf(stderr, "profile %s\n", file));

	fp = fopen(file, "r");
	if (fp == NULL)
		return;

	line = 0;
	buf = NULL;
	while (getline(&buf, &buflen, fp) != -1) {
		++line;
		rmspace(buf);
		if (*buf == '#' || *buf == '\0')
			continue;

		/* Handle "source" keyword */
		if (!strncmp(buf, "source ", 7))
			read_portage_env_file(buf + 7, vars);

		/* look for our desired variables and grab their value */
		for (i = 0; vars[i].name; ++i) {
			if (buf[vars[i].name_len] != '=' && buf[vars[i].name_len] != ' ')
				continue;
			if (strncmp(buf, vars[i].name, vars[i].name_len))
				continue;

			/* make sure we handle spaces between the varname, the =, and the value:
			 * VAR=val   VAR = val   VAR="val"
			 */
			s = buf + vars[i].name_len;
			if ((p = strchr(s, '=')) != NULL)
				s = p + 1;
			while (isspace(*s))
				++s;
			if (*s == '"' || *s == '\'') {
				char q = *s;
				size_t l = strlen(s);

				if (q != s[l - 1]) {
					/* If the last char is not a quote, then we span lines */
					size_t abuflen;
					char *abuf, *qq;

					qq = abuf = NULL;
					while (getline(&abuf, &abuflen, fp) != -1) {
						buf = xrealloc(buf, buflen + abuflen);
						strcat(buf, abuf);
						buflen += abuflen;

						qq = strchr(abuf, q);
						if (qq) {
							*qq = '\0';
							break;
						}
					}
					free(abuf);

					if (!qq)
						warn("%s:%zu: %s: quote mismatch", file, line, vars[i].name);

					s = buf + vars[i].name_len + 1;
				} else {
					s[l - 1] = '\0';
					++s;
				}
			}

			set_portage_env_var(&vars[i], s);
		}
	}

	free(buf);
	fclose(fp);
}

/* Helper to recursively read stacked make.defaults in profiles */
static void read_portage_profile(const char *configroot, const char *profile, env_vars vars[])
{
	size_t configroot_len, profile_len, sub_len;
	char *profile_file, *sub_file;
	char buf[BUFSIZE], *s;

	/* initialize the base profile path */
	if (!configroot)
		configroot = "";
	configroot_len = strlen(configroot);
	profile_len = strlen(profile);
	sub_len = 1024;	/* should be big enough for longest line in "parent" */
	profile_file = xmalloc(configroot_len + profile_len + sub_len + 2);

	memcpy(profile_file, configroot, configroot_len);
	memcpy(profile_file + configroot_len, profile, profile_len);
	sub_file = profile_file + configroot_len + profile_len + 1;
	sub_file[-1] = '/';

	/* first consume the profile's make.defaults */
	strcpy(sub_file, "make.defaults");
	read_portage_env_file(profile_file, vars);

	/* now walk all the parents */
	strcpy(sub_file, "parent");
	if (eat_file(profile_file, buf, sizeof(buf)) == 0)
		return;
	rmspace(buf);

	s = strtok(buf, "\n");
	while (s) {
		strncpy(sub_file, s, sub_len);
		read_portage_profile(NULL, profile_file, vars);
		s = strtok(NULL, "\n");
	}

	free(profile_file);
}

void initialize_portage_env(void)
{
	size_t i;
	const char *s;

	static const char * const files[] = {
		EPREFIX "etc/make.globals",
		EPREFIX "usr/share/portage/config/make.globals",
		EPREFIX "etc/make.conf",
		EPREFIX "etc/portage/make.conf",
	};
	bool nocolor = 0;

	env_vars *var;
	env_vars vars_to_read[] = {
#define _Q_EV(t, V, set, lset, d) \
	{ \
		.name = #V, \
		.name_len = strlen(#V), \
		.type = _Q_##t, \
		set, \
		lset, \
		.default_value = d, \
	},
#define _Q_EVS(t, V, v, d) _Q_EV(t, V, .value.s = &v, .value_len = strlen(d), d)
#define _Q_EVB(t, V, v, d) _Q_EV(t, V, .value.b = &v, .value_len = 0, d)

		_Q_EVS(STR,  ROOT,                portroot,            "/")
		_Q_EVS(STR,  ACCEPT_LICENSE,      accept_license,      "")
		_Q_EVS(ISTR, INSTALL_MASK,        install_mask,        "")
		_Q_EVS(STR,  ARCH,                portarch,            "")
		_Q_EVS(ISTR, CONFIG_PROTECT,      config_protect,      EPREFIX "etc")
		_Q_EVS(ISTR, CONFIG_PROTECT_MASK, config_protect_mask, "")
		_Q_EVB(BOOL, NOCOLOR,             nocolor,             0)
		_Q_EVS(ISTR, FEATURES,            features,            "noman noinfo nodoc")
		_Q_EVS(STR,  EPREFIX,             eprefix,             EPREFIX)
		_Q_EVS(STR,  PORTDIR,             portdir,             EPREFIX "usr/portage")
		_Q_EVS(STR,  PORTAGE_BINHOST,     binhost,             DEFAULT_PORTAGE_BINHOST)
		_Q_EVS(STR,  PORTAGE_TMPDIR,      port_tmpdir,         EPREFIX "var/tmp/portage/")
		_Q_EVS(STR,  PKGDIR,              pkgdir,              EPREFIX "usr/portage/packages/")
		_Q_EVS(STR,  Q_VDB,               portvdb,             EPREFIX "var/db/pkg")
		{ NULL, 0, _Q_BOOL, NULL, 0, NULL, }

#undef _Q_EV
	};

	/* initialize all the strings with their default value */
	for (i = 0; vars_to_read[i].name; ++i) {
		var = &vars_to_read[i];
		if (var->type != _Q_BOOL)
			*var->value.s = xstrdup(var->default_value);
	}

	/* walk all the stacked profiles */
	s = getenv("PORTAGE_CONFIGROOT");
	if (!s)
		s = "/";
	read_portage_profile(s, EPREFIX "etc/make.profile", vars_to_read);
	read_portage_profile(s, EPREFIX "etc/portage/make.profile", vars_to_read);

	/* now read all the config files */
	for (i = 0; i < ARRAY_SIZE(files); ++i)
		read_portage_env_file(files[i], vars_to_read);

	/* finally, check the env */
	for (i = 0; vars_to_read[i].name; ++i) {
		var = &vars_to_read[i];
		s = getenv(var->name);
		if (s != NULL)
			set_portage_env_var(var, s);
		if (getenv("DEBUG") IF_DEBUG(|| 1)) {
			fprintf(stderr, "%s = ", var->name);
			switch (var->type) {
			case _Q_BOOL: fprintf(stderr, "%i\n", *var->value.b); break;
			case _Q_STR:
			case _Q_ISTR: fprintf(stderr, "%s\n", *var->value.s); break;
			}
		}
	}

	/* expand any nested variables e.g. PORTDIR=${EPREFIX}/usr/portage */
	for (i = 0; vars_to_read[i].name; ++i) {
		char *svar;

		var = &vars_to_read[i];
		if (var->type == _Q_BOOL)
			continue;

		while ((svar = strchr(*var->value.s, '$'))) {
			env_vars *evar;
			bool brace;
			const char *sval;
			size_t slen, pre_len, var_len, post_len;
			char byte;

			pre_len = svar - *var->value.s;

			/* First skip the leading "${" */
			s = ++svar;
			brace = (*svar == '{');
			if (brace)
				s = ++svar;

			/* Now skip the variable name itself */
			while (isalnum(*svar) || *svar == '_')
				++svar;

			/* Finally skip the trailing "}" */
			if (brace && *svar != '}') {
				warn("invalid variable setting: %s\n", *var->value.s);
				break;
			}

			var_len = svar - *var->value.s + 1;

			byte = *svar;
			*svar = '\0';
			evar = get_portage_env_var(vars_to_read, s);
			if (evar) {
				sval = *evar->value.s;
			} else {
				sval = getenv(s);
				if (!sval)
					sval = "";
			}
			*svar = byte;
			slen = strlen(sval);
			post_len = strlen(svar + 1);
			*var->value.s = xrealloc(*var->value.s, pre_len + MAX(var_len, slen) + post_len + 1);

			/*
			 * VAR=XxXxX	(slen = 5)
			 * FOO${VAR}BAR
			 * pre_len = 3
			 * var_len = 6
			 * post_len = 3
			 */
			memmove(*var->value.s + pre_len + slen,
				*var->value.s + pre_len + var_len,
				post_len + 1);
			memcpy(*var->value.s + pre_len, sval, slen);
		}
	}

	/* Make sure ROOT always ends in a slash */
	var = &vars_to_read[0];
	if ((*var->value.s)[var->value_len - 1] != '/') {
		portroot = xrealloc(portroot, var->value_len + 2);
		portroot[var->value_len] = '/';
		portroot[var->value_len + 1] = '\0';
	}

	if (getenv("PORTAGE_QUIET") != NULL)
		quiet = 1;

	if (nocolor)
		no_colors();
	else
		color_remap();
}

enum {
	CACHE_EBUILD = 1,
	CACHE_METADATA = 2
};

int filter_hidden(const struct dirent *dentry);
int filter_hidden(const struct dirent *dentry)
{
	if (dentry->d_name[0] == '.')
		return 0;
	return 1;
}

#define CACHE_EBUILD_FILE (getenv("CACHE_EBUILD_FILE") ? getenv("CACHE_EBUILD_FILE") : ".ebuild.x")
#define CACHE_METADATA_FILE ".metadata.x"
const char *initialize_flat(int cache_type);
const char *initialize_flat(int cache_type)
{
	struct dirent **category, **pn, **eb;
	struct stat st;
	struct timeval start, finish;
	static const char *cache_file;
	char *p;
	int a, b, c, d, e, i;
	int frac, secs, count;
	FILE *fp;

	a = b = c = d = e = i = 0;
	count = frac = secs = 0;

	cache_file = (cache_type == CACHE_EBUILD ? CACHE_EBUILD_FILE : CACHE_METADATA_FILE);

	if (chdir(portdir) != 0) {
		warnp("chdir to PORTDIR '%s' failed", portdir);
		goto ret;
	}

	if (cache_type == CACHE_METADATA && chdir(portcachedir) != 0) {
		warnp("chdir to portage cache '%s/%s' failed", portdir, portcachedir);
		goto ret;
	}

	if (stat(cache_file, &st) != -1)
		if (st.st_size == 0)
			unlink(cache_file);

	/* assuming --sync is used with --delete this will get recreated after every merge */
	if (access(cache_file, R_OK) == 0)
		goto ret;
	if (!quiet)
		warn("Updating ebuild %scache ... ", cache_type == CACHE_EBUILD ? "" : "meta");

	unlink(cache_file);
	if (errno != ENOENT) {
		warnfp("unlinking '%s/%s' failed", portdir, cache_file);
		goto ret;
	}

	if ((fp = fopen(cache_file, "w")) == NULL) {
		if (cache_type == CACHE_EBUILD)
			warnfp("opening '%s/%s' failed", portdir, cache_file);
		else
			warnfp("opening '%s/%s/%s' failed", portdir, portcachedir, cache_file);
		if (errno == EACCES)
			warnf("You should run this command as root: q -%c",
					cache_type == CACHE_EBUILD ? 'r' : 'm');
		goto ret;
	}

	gettimeofday(&start, NULL);

	if ((a = scandir(".", &category, q_vdb_filter_cat, alphasort)) < 0)
		goto ret;

	for (i = 0; i < a; i++) {
		stat(category[i]->d_name, &st);
		if (!S_ISDIR(st.st_mode))
			continue;
		if (strchr(category[i]->d_name, '-') == NULL)
			if ((strncmp(category[i]->d_name, "virtual", 7)) != 0)
				continue;

		if ((b = scandir(category[i]->d_name, &pn, q_vdb_filter_pkg, alphasort)) < 0)
			continue;
		for (c = 0; c < b; c++) {
			char de[_Q_PATH_MAX];

			snprintf(de, sizeof(de), "%s/%s", category[i]->d_name, pn[c]->d_name);

			if (stat(de, &st) < 0)
				continue;

			switch (cache_type) {
			case CACHE_EBUILD:
				if (!S_ISDIR(st.st_mode))
					continue;
				break;
			case CACHE_METADATA:
				if (S_ISREG(st.st_mode))
					fprintf(fp, "%s\n", de);
				continue;
				break;
			}
			if ((e = scandir(de, &eb, filter_hidden, alphasort)) < 0)
				continue;
			for (d = 0; d < e; d++) {
				if ((p = strrchr(eb[d]->d_name, '.')) != NULL)
					if (strcmp(p, ".ebuild") == 0) {
						count++;
						fprintf(fp, "%s/%s/%s\n", category[i]->d_name, pn[c]->d_name, eb[d]->d_name);
					}
			}
			while (d--) free(eb[d]);
			free(eb);
		}
		scandir_free(pn, b);
	}
	fclose(fp);
	scandir_free(category, a);

	if (quiet) goto ret;

	gettimeofday(&finish, NULL);
	if (start.tv_usec > finish.tv_usec) {
		finish.tv_usec += 1000000;
		finish.tv_sec--;
	}
	frac = (finish.tv_usec - start.tv_usec);
	secs = (finish.tv_sec - start.tv_sec);
	if (secs < 0) secs = 0;
	if (frac < 0) frac = 0;

	warn("Finished %u entries in %d.%06d seconds", count, secs, frac);
	if (secs > 120)
		warn("You should consider using the noatime mount option for PORTDIR='%s' if it's not already enabled", portdir);
ret:
	return cache_file;
}
#define initialize_ebuild_flat() initialize_flat(CACHE_EBUILD)
#define initialize_metadata_flat() initialize_flat(CACHE_METADATA)

void reinitialize_ebuild_flat(void)
{
	if (chdir(portdir) != 0) {
		warnp("chdir to PORTDIR '%s' failed", portdir);
		return;
	}
	unlink(CACHE_EBUILD_FILE);
	initialize_ebuild_flat();
}

void reinitialize_as_needed(void)
{
	if (reinitialize)
		reinitialize_ebuild_flat();
	if (reinitialize_metacache)
		initialize_metadata_flat();
}

typedef struct {
	char *_data;
	char *DEPEND;        /* line 1 */
	char *RDEPEND;
	char *SLOT;
	char *SRC_URI;
	char *RESTRICT;      /* line 5 */
	char *HOMEPAGE;
	char *LICENSE;
	char *DESCRIPTION;
	char *KEYWORDS;
	char *INHERITED;     /* line 10 */
	char *IUSE;
	char *CDEPEND;
	char *PDEPEND;
	char *PROVIDE;       /* line 14 */
	char *EAPI;
	char *PROPERTIES;
	depend_atom *atom;
} portage_cache;

void cache_free(portage_cache *cache);
portage_cache *cache_read_file(const char *file);
portage_cache *cache_read_file(const char *file)
{
	struct stat s;
	char *ptr;
	FILE *f;
	portage_cache *ret = NULL;
	size_t len;

	if ((f = fopen(file, "r")) == NULL)
		goto err;

	if (fstat(fileno(f), &s) != 0)
		goto err;
	len = sizeof(*ret) + s.st_size + 1;
	ret = xzalloc(len);
	ptr = (char*)ret;
	ret->_data = ptr + sizeof(*ret);
	if ((off_t)fread(ret->_data, 1, s.st_size, f) != s.st_size)
		goto err;

	ret->atom = atom_explode(file);
	ret->DEPEND = ret->_data;
#define next_line(curr, next) \
	if ((ptr = strchr(ret->curr, '\n')) == NULL) { \
		warn("Invalid cache file '%s'", file); \
		goto err; \
	} \
	ret->next = ptr+1; \
	*ptr = '\0';
	next_line(DEPEND, RDEPEND)
	next_line(RDEPEND, SLOT)
	next_line(SLOT, SRC_URI)
	next_line(SRC_URI, RESTRICT)
	next_line(RESTRICT, HOMEPAGE)
	next_line(HOMEPAGE, LICENSE)
	next_line(LICENSE, DESCRIPTION)
	next_line(DESCRIPTION, KEYWORDS)
	next_line(KEYWORDS, INHERITED)
	next_line(INHERITED, IUSE)
	next_line(IUSE, CDEPEND)
	next_line(CDEPEND, PDEPEND)
	next_line(PDEPEND, PROVIDE)
	next_line(PROVIDE, EAPI)
	next_line(EAPI, PROPERTIES)
#undef next_line
	ptr = strchr(ptr+1, '\n');
	*ptr = '\0';

	fclose(f);

	return ret;

err:
	if (ret) cache_free(ret);
	return NULL;
}

void cache_dump(portage_cache *cache);
void cache_dump(portage_cache *cache)
{
	if (!cache)
		errf("Cache is empty !");

	printf("DEPEND     : %s\n", cache->DEPEND);
	printf("RDEPEND    : %s\n", cache->RDEPEND);
	printf("SLOT       : %s\n", cache->SLOT);
	printf("SRC_URI    : %s\n", cache->SRC_URI);
	printf("RESTRICT   : %s\n", cache->RESTRICT);
	printf("HOMEPAGE   : %s\n", cache->HOMEPAGE);
	printf("LICENSE    : %s\n", cache->LICENSE);
	printf("DESCRIPTION: %s\n", cache->DESCRIPTION);
	printf("KEYWORDS   : %s\n", cache->KEYWORDS);
	printf("INHERITED  : %s\n", cache->INHERITED);
	printf("IUSE       : %s\n", cache->IUSE);
	printf("CDEPEND    : %s\n", cache->CDEPEND);
	printf("PDEPEND    : %s\n", cache->PDEPEND);
	printf("PROVIDE    : %s\n", cache->PROVIDE);
	printf("EAPI       : %s\n", cache->EAPI);
	printf("PROPERTIES : %s\n", cache->PROPERTIES);
	if (!cache->atom) return;
	printf("CATEGORY   : %s\n", cache->atom->CATEGORY);
	printf("PN         : %s\n", cache->atom->PN);
	printf("PV         : %s\n", cache->atom->PV);
	printf("PVR        : %s\n", cache->atom->PVR);
}

void cache_free(portage_cache *cache)
{
	if (!cache)
		errf("Cache is empty !");
	atom_implode(cache->atom);
	free(cache);
}

char *atom_to_pvr(depend_atom *atom);
char *atom_to_pvr(depend_atom *atom) {
	return (atom->PR_int == 0 ? atom->P : atom->PVR );
}

static char *grab_vdb_item(const char *item, const char *CATEGORY, const char *PF)
{
	static char buf[_Q_PATH_MAX];

	snprintf(buf, sizeof(buf), "%s%s/%s/%s/%s", portroot, portvdb, CATEGORY, PF, item);
	eat_file(buf, buf, sizeof(buf));
	rmspace(buf);

	return buf;
}

_q_static queue *get_vdb_atoms(int fullcpv)
{
	q_vdb_ctx *ctx;

	int cfd, j;
	int dfd, i;

	char buf[_Q_PATH_MAX];
	char slot[_Q_PATH_MAX];

	struct dirent **cat;
	struct dirent **pf;

	depend_atom *atom = NULL;
	queue *cpf = NULL;

	ctx = q_vdb_open();
	if (!ctx)
		return NULL;

	/* scan the cat first */
	if ((cfd = scandirat(ctx->vdb_fd, ".", &cat, q_vdb_filter_cat, alphasort)) < 0)
		goto fuckit;

	for (j = 0; j < cfd; j++) {
		if ((dfd = scandirat(ctx->vdb_fd, cat[j]->d_name, &pf, q_vdb_filter_pkg, alphasort)) < 0)
			continue;
		for (i = 0; i < dfd; i++) {
			snprintf(buf, sizeof(buf), "%s/%s", cat[j]->d_name, pf[i]->d_name);
			if ((atom = atom_explode(buf)) == NULL)
				continue;

			slot[0] = '0';
			slot[1] = 0;
			strncat(buf, "/SLOT", sizeof(buf));
			eat_file_at(ctx->vdb_fd, buf, buf, sizeof(buf));
			rmspace(buf);

			if (fullcpv) {
				if (atom->PR_int)
					snprintf(buf, sizeof(buf), "%s/%s-%s-r%i", atom->CATEGORY, atom->PN, atom->PV, atom->PR_int);
				else
					snprintf(buf, sizeof(buf), "%s/%s-%s", atom->CATEGORY, atom->PN, atom->PV);
			} else {
				snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, atom->PN);
			}
			atom_implode(atom);
			cpf = add_set(buf, slot, cpf);
		}
		scandir_free(pf, dfd);
	}
	scandir_free(cat, cfd);

 fuckit:
	q_vdb_close(ctx);
	return cpf;
}

void cleanup(void)
{
	reinitialize_as_needed();
	free_sets(virtuals);
	fclose(stderr);
}

int main(int argc, char **argv)
{
	struct stat st;
	IF_DEBUG(init_coredumps());
	argv0 = argv[0];

#ifdef ENABLE_NLS	/* never tested */
	setlocale(LC_ALL, "");
	bindtextdomain(argv0, EPREFIX "usr/share/locale");
	textdomain(argv0);
#endif
#if 1
	if (fstat(fileno(stdout), &st) != -1)
		if (!isatty(fileno(stdout)))
			if ((S_ISFIFO(st.st_mode)) == 0)
				no_colors();
#endif
	if ((getenv("TERM") == NULL) || (strcmp(getenv("TERM"), "dumb") == 0))
		no_colors();

	initialize_portage_env();
	atexit(cleanup);
	optind = 0;
	return q_main(argc, argv);
}

#include "include_applets.h"
