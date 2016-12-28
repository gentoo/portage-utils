/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "porting.h"
#include "main.h"

/* prototypes and such */
static bool eat_file(const char *, char **, size_t *);
static bool eat_file_fd(int, char **, size_t *);
static bool eat_file_at(int, const char *, char **, size_t *);
int rematch(const char *, const char *, int);
static char *rmspace(char *);
static char *rmspace_len(char *, size_t);

void initialize_portage_env(void);
void initialize_ebuild_flat(void);
void reinitialize_ebuild_flat(void);
void reinitialize_as_needed(void);
void cleanup(void);
int lookup_applet_idx(const char *);

/* variables to control runtime behavior */
char *module_name = NULL;
char *modpath = NULL;
int verbose = 0;
int quiet = 0;
int portcachedir_type = 0;
char pretend = 0;
static int reinitialize = 0;
static int reinitialize_metacache = 0;
static char *portlogdir;
static char *main_overlay;
static char *portarch;
static char *portvdb;
static char *portedb;
const char portcachedir_pms[] = "metadata/cache";
const char portcachedir_md5[] = "metadata/md5-cache";
static char *portroot;
static char *eprefix;
static char *config_protect, *config_protect_mask;

static char *pkgdir;
static char *port_tmpdir;

static char *binhost;
static char *features;
static char *accept_license;
static char *install_mask;
static char *pkg_install_mask;

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

static DECLARE_ARRAY(overlays);

_q_static
void no_colors(void)
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
	{"root",       a_argument, NULL, 0x1}, \
	{"verbose",   no_argument, NULL, 'v'}, \
	{"quiet",     no_argument, NULL, 'q'}, \
	{"nocolor",   no_argument, NULL, 'C'}, \
	{"help",      no_argument, NULL, 'h'}, \
	{"version",   no_argument, NULL, 'V'}, \
	{NULL,        no_argument, NULL, 0x0}
#define COMMON_OPTS_HELP \
	"Set the ROOT env var", \
	"Make a lot of noise", \
	"Tighter output; suppress warnings", \
	"Don't output color", \
	"Print this help and exit", \
	"Print version and exit", \
	NULL
#define COMMON_GETOPTS_CASES(applet) \
	case 0x1: portroot = optarg; break; \
	case 'v': ++verbose; break; \
	case 'q': if (quiet == 0) { warnout = fopen("/dev/null", "we"); } ++quiet; break; \
	case 'V': version_barf(); break; \
	case 'h': applet ## _usage(EXIT_SUCCESS); break; \
	case 'C': no_colors(); break; \
	default: applet ## _usage(EXIT_FAILURE); break;

/* display usage and exit */
static void usage(int status, const char *flags, struct option const opts[],
                  const char * const help[], const char *desc, int blabber)
{
	const char opt_arg[] = "[arg]";
	const char a_arg[] = "<arg>";
	size_t a_arg_len = strlen(a_arg) + 1;
	size_t i, optlen;
	FILE *fp = status == EXIT_SUCCESS ? stdout : warnout;

	if (blabber == 0) {
		fprintf(fp, "%sUsage:%s %sq%s %s<applet> <args>%s  : %s"
			"invoke a portage utility applet\n\n", GREEN,
			NORM, YELLOW, NORM, DKBLUE, RED, NORM);
		fprintf(fp, "%sCurrently defined applets:%s\n", GREEN, NORM);
		for (i = 0; applets[i].desc; ++i)
			if (applets[i].func)
				fprintf(fp, " %s%8s%s %s%-16s%s%s:%s %s\n",
					YELLOW, applets[i].name, NORM,
					DKBLUE, applets[i].opts, NORM,
					RED, NORM, _(applets[i].desc));
	} else if (blabber > 0) {
		fprintf(fp, "%sUsage:%s %s%s%s [opts] %s%s%s %s:%s %s\n",
			GREEN, NORM,
			YELLOW, applets[blabber].name, NORM,
			DKBLUE, applets[blabber].opts, NORM,
			RED, NORM, _(applets[blabber].desc));
		if (desc)
			fprintf(fp, "\n%s\n", desc);
	}
	if (module_name != NULL)
		fprintf(fp, "%sLoaded module:%s\n%s%8s%s %s<args>%s\n",
			GREEN, NORM, YELLOW, module_name, NORM, DKBLUE, NORM);

	/* Prescan the --long opt length to auto-align. */
	optlen = 0;
	for (i = 0; opts[i].name; ++i) {
		size_t l = strlen(opts[i].name);
		if (opts[i].has_arg != no_argument)
			l += a_arg_len;
		optlen = MAX(l, optlen);
	}

	fprintf(fp, "\n%sOptions:%s -[%s]\n", GREEN, NORM, flags);
	for (i = 0; opts[i].name; ++i) {
		/* this assert is a life saver when adding new applets. */
		assert(help[i] != NULL);

		/* first output the short flag if it has one */
		if (opts[i].val > '~' || opts[i].val < ' ')
			fprintf(fp, "      ");
		else
			fprintf(fp, "  -%c, ", opts[i].val);

		/* then the long flag + help text */
		if (opts[i].has_arg == no_argument)
			fprintf(fp, "--%-*s %s*%s %s\n", (int)optlen, opts[i].name,
				RED, NORM, _(help[i]));
		else
			fprintf(fp, "--%s %s%s%s%*s %s*%s %s\n",
				opts[i].name,
				DKBLUE, (opts[i].has_arg == a_argument ? a_arg : opt_arg), NORM,
				(int)(optlen - strlen(opts[i].name) - a_arg_len), "",
				RED, NORM, _(help[i]));
	}
	exit(status);
}

static void version_barf(void)
{
#ifndef VERSION
# define VERSION "git"
#endif
#ifndef VCSID
# define VCSID "<unknown>"
#endif
	printf("portage-utils-%s: %s\n"
	       "%s written for Gentoo by <solar and vapier @ gentoo.org>\n",
	       VERSION, VCSID, argv0);
	exit(EXIT_SUCCESS);
}

static bool eat_file_fd(int fd, char **bufptr, size_t *bufsize)
{
	bool ret = true;
	struct stat s;
	char *buf;
	size_t read_size;

	/* First figure out how much data we should read from the fd. */
	if (fd == -1 || fstat(fd, &s) != 0) {
		ret = false;
		read_size = 0;
		/* Fall through so we set the first byte 0 */
	} else if (!s.st_size) {
		/* We might be trying to eat a virtual file like in /proc, so
		 * read an arbitrary size that should be "enough". */
		read_size = BUFSIZE;
	} else
		read_size = (size_t)s.st_size;

	/* Now allocate enough space (at least 1 byte). */
	if (!*bufptr || *bufsize < read_size) {
		/* We assume a min allocation size so that repeat calls don't
		 * hit ugly ramp ups -- if you read a file that is 1 byte, then
		 * 5 bytes, then 10 bytes, then 20 bytes, ... you'll allocate
		 * constantly.  So we round up a few pages as wasiting virtual
		 * memory is cheap when it is unused.  */
		*bufsize = ((read_size + 1) + BUFSIZE - 1) & -BUFSIZE;
		*bufptr = xrealloc(*bufptr, *bufsize);
	}
	buf = *bufptr;

	/* Finally do the actual read. */
	buf[0] = '\0';
	if (read_size) {
		if (s.st_size) {
			if (read(fd, buf, read_size) != (ssize_t)read_size)
				return false;
			buf[read_size] = '\0';
		} else {
			if (read(fd, buf, read_size) == 0)
				return false;
			buf[read_size - 1] = '\0';
		}
	}

	return ret;
}

static bool eat_file_at(int dfd, const char *file, char **bufptr, size_t *bufsize)
{
	bool ret;
	int fd;

	fd = openat(dfd, file, O_CLOEXEC|O_RDONLY);
	ret = eat_file_fd(fd, bufptr, bufsize);
	if (fd != -1)
		close(fd);

	return ret;
}

static bool eat_file(const char *file, char **bufptr, size_t *bufsize)
{
	return eat_file_at(AT_FDCWD, file, bufptr, bufsize);
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

/* Handle a single file in the repos.conf format. */
static void read_one_repos_conf(const char *repos_conf)
{
	int nsec;
	char *conf;
	const char *main_repo, *repo, *path;
	dictionary *dict;

	if (getenv("DEBUG"))
		fprintf(stderr, "  parse %s\n", repos_conf);

	dict = iniparser_load(repos_conf);

	main_repo = iniparser_getstring(dict, "DEFAULT:main-repo", NULL);

	nsec = iniparser_getnsec(dict);
	while (nsec-- > 0) {
		repo = iniparser_getsecname(dict, nsec);
		if (!strcmp(repo, "DEFAULT"))
			continue;

		xasprintf(&conf, "%s:location", repo);
		path = iniparser_getstring(dict, conf, NULL);
		if (path) {
			void *ele = xarraypush_str(overlays, path);
			if (main_repo && !strcmp(repo, main_repo))
				main_overlay = ele;
		}
		free(conf);
	}

	iniparser_freedict(dict);
}

/* Handle a possible directory of files. */
static void read_repos_conf(const char *configroot, const char *repos_conf)
{
	char *top_conf, *sub_conf;
	int i, count;
	struct dirent **confs;

	xasprintf(&top_conf, "%s%s", configroot, repos_conf);
	if (getenv("DEBUG"))
		fprintf(stderr, "repos.conf.d scanner %s\n", top_conf);
	count = scandir(top_conf, &confs, NULL, alphasort);
	if (count == -1) {
		if (errno == ENOTDIR)
			read_one_repos_conf(top_conf);
	} else {
		for (i = 0; i < count; ++i) {
			const char *name = confs[i]->d_name;

			if (name[0] == '.')
				continue;

#ifdef DT_UNKNOWN
			if (confs[i]->d_type != DT_UNKNOWN &&
			    confs[i]->d_type != DT_REG &&
			    confs[i]->d_type != DT_LNK)
				continue;
#endif

			xasprintf(&sub_conf, "%s/%s", top_conf, name);

#ifdef DT_UNKNOWN
			if (confs[i]->d_type != DT_REG)
#endif
			{
				struct stat st;
				if (stat(sub_conf, &st) || !S_ISREG(st.st_mode)) {
					free(sub_conf);
					continue;
				}
			}

			read_one_repos_conf(sub_conf);
			free(sub_conf);
		}
		scandir_free(confs, count);
	}
	free(top_conf);
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
		memset(*value, ' ', p - *value + 2);

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
_q_static void read_portage_env_file(const char *configroot, const char *file, env_vars vars[])
{
	size_t i, buflen, line, configroot_len, file_len;
	FILE *fp;
	char *buf, *s, *p;

	if (getenv("DEBUG"))
		fprintf(stderr, "profile %s\n", file);

	configroot_len = strlen(configroot);
	file_len = strlen(file);
	buflen = configroot_len + file_len + 1;
	buf = xmalloc(buflen);

	memcpy(buf, configroot, configroot_len);
	memcpy(buf + configroot_len, file, file_len);
	buf[buflen - 1] = '\0';

	fp = fopen(buf, "r");
	if (fp == NULL)
		goto done;

	line = 0;
	while (getline(&buf, &buflen, fp) != -1) {
		++line;
		rmspace(buf);
		if (*buf == '#' || *buf == '\0')
			continue;

		/* Handle "source" keyword */
		if (!strncmp(buf, "source ", 7)) {
			const char *sfile = buf + 7;

			if (sfile[0] != '/') {
				/* handle relative paths */
				size_t file_path_len, source_len;

				s = strrchr(file, '/');
				file_path_len = s - file + 1;
				source_len = strlen(sfile);

				if (buflen <= source_len + file_path_len)
					buf = xrealloc(buf, buflen = source_len + file_path_len + 1);
				memmove(buf + file_path_len, buf + 7, source_len + 1);
				memcpy(buf, file, file_path_len);
				sfile = buf;
			}

			read_portage_env_file(configroot, sfile, vars);
			continue;
		}

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
				char *endq;
				char q = *s;

				/* make sure we handle spacing/comments after the quote */
				endq = strchr(s + 1, q);
				if (!endq) {
					/* If the last char is not a quote, then we span lines */
					size_t abuflen;
					char *abuf;

					abuf = NULL;
					while (getline(&abuf, &abuflen, fp) != -1) {
						buf = xrealloc(buf, buflen + abuflen);
						endq = strchr(abuf, q);
						if (endq)
							*endq = '\0';

						strcat(buf, abuf);
						buflen += abuflen;

						if (endq)
							break;
					}
					free(abuf);

					if (!endq)
						warn("%s:%zu: %s: quote mismatch", file, line, vars[i].name);

					s = buf + vars[i].name_len + 2;
				} else {
					*endq = '\0';
					++s;
				}
			} else {
				/* no quotes, so chop the spacing/comments ourselves */
				size_t off = strcspn(s, "# \t\n");
				s[off] = '\0';
			}

			set_portage_env_var(&vars[i], s);
		}
	}

	fclose(fp);
 done:
	free(buf);
}

/* Helper to recursively read stacked make.defaults in profiles */
static void read_portage_profile(const char *configroot, const char *profile, env_vars vars[])
{
	size_t configroot_len, profile_len, sub_len;
	char *profile_file, *sub_file;
	char *s;

	static char *buf;
	static size_t buf_len;

	/* initialize the base profile path */
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
	read_portage_env_file("", profile_file, vars);

	/* now walk all the parents */
	strcpy(sub_file, "parent");
	if (eat_file(profile_file, &buf, &buf_len) == 0)
		goto done;
	rmspace(buf);

	s = strtok(buf, "\n");
	while (s) {
		strncpy(sub_file, s, sub_len);
		read_portage_profile("", profile_file, vars);
		s = strtok(NULL, "\n");
	}

 done:
	free(profile_file);
}

void initialize_portage_env(void)
{
	size_t i;
	const char *s;

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
		_Q_EVS(ISTR, PKG_INSTALL_MASK,    pkg_install_mask,    "")
		_Q_EVS(STR,  ARCH,                portarch,            "")
		_Q_EVS(ISTR, CONFIG_PROTECT,      config_protect,      CONFIG_EPREFIX "etc")
		_Q_EVS(ISTR, CONFIG_PROTECT_MASK, config_protect_mask, "")
		_Q_EVB(BOOL, NOCOLOR,             nocolor,             0)
		_Q_EVS(ISTR, FEATURES,            features,            "noman noinfo nodoc")
		_Q_EVS(STR,  EPREFIX,             eprefix,             CONFIG_EPREFIX)
		_Q_EVS(STR,  EMERGE_LOG_DIR,      portlogdir,          CONFIG_EPREFIX "var/log")
		_Q_EVS(STR,  PORTDIR,             main_overlay,        CONFIG_EPREFIX "usr/portage")
		_Q_EVS(STR,  PORTAGE_BINHOST,     binhost,             DEFAULT_PORTAGE_BINHOST)
		_Q_EVS(STR,  PORTAGE_TMPDIR,      port_tmpdir,         CONFIG_EPREFIX "var/tmp/portage/")
		_Q_EVS(STR,  PKGDIR,              pkgdir,              CONFIG_EPREFIX "usr/portage/packages/")
		_Q_EVS(STR,  Q_VDB,               portvdb,             CONFIG_EPREFIX "var/db/pkg")
		_Q_EVS(STR,  Q_EDB,               portedb,             CONFIG_EPREFIX "var/cache/edb")
		{ NULL, 0, _Q_BOOL, { NULL }, 0, NULL, }

#undef _Q_EV
	};

	/* initialize all the strings with their default value */
	for (i = 0; vars_to_read[i].name; ++i) {
		var = &vars_to_read[i];
		if (var->type != _Q_BOOL)
			*var->value.s = xstrdup(var->default_value);
	}

	/* figure out where to find our config files */
	const char *configroot = getenv("PORTAGE_CONFIGROOT");
	if (!configroot)
		configroot = "/";

	/* walk all the stacked profiles */
	read_portage_profile(configroot, CONFIG_EPREFIX "etc/make.profile", vars_to_read);
	read_portage_profile(configroot, CONFIG_EPREFIX "etc/portage/make.profile", vars_to_read);

	/* now read all the config files */
	read_portage_env_file("", CONFIG_EPREFIX "usr/share/portage/config/make.globals", vars_to_read);
	read_portage_env_file(configroot, CONFIG_EPREFIX "etc/make.conf", vars_to_read);
	read_portage_env_file(configroot, CONFIG_EPREFIX "etc/portage/make.conf", vars_to_read);

	/* finally, check the env */
	for (i = 0; vars_to_read[i].name; ++i) {
		var = &vars_to_read[i];
		s = getenv(var->name);
		if (s != NULL)
			set_portage_env_var(var, s);
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

			var_len = svar - s + 1 + (brace ? 2 : 0);

			byte = *svar;
			*svar = '\0';

			/* Don't try to expand ourselves */
			if (strcmp(var->name, s)) {
				evar = get_portage_env_var(vars_to_read, s);
				if (evar) {
					sval = *evar->value.s;
				} else {
					sval = getenv(s);
					if (!sval)
						sval = "";
				}
			} else {
				sval = "";
			}
			*svar = byte;
			slen = strlen(sval);
			post_len = strlen(svar + !!brace);
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

	if (getenv("DEBUG")) {
		for (i = 0; vars_to_read[i].name; ++i) {
			var = &vars_to_read[i];
			fprintf(stderr, "%s = ", var->name);
			switch (var->type) {
			case _Q_BOOL: fprintf(stderr, "%i\n", *var->value.b); break;
			case _Q_STR:
			case _Q_ISTR: fprintf(stderr, "%s\n", *var->value.s); break;
			}
		}
	}

	/* Make sure ROOT always ends in a slash */
	var = &vars_to_read[0];
	if ((*var->value.s)[var->value_len - 1] != '/') {
		portroot = xrealloc(portroot, var->value_len + 2);
		portroot[var->value_len] = '/';
		portroot[var->value_len + 1] = '\0';
	}

	char *orig_main_overlay = main_overlay;
	read_repos_conf(configroot, CONFIG_EPREFIX "etc/portage/repos.conf");
	if (orig_main_overlay != main_overlay)
		free(orig_main_overlay);
	if (array_cnt(overlays) == 0)
		xarraypush_str(overlays, main_overlay);

	if (getenv("PORTAGE_QUIET") != NULL)
		quiet = 1;

	if (nocolor)
		no_colors();
	else
		color_remap();
}

enum {
	CACHE_EBUILD = 1,
	CACHE_METADATA = 2,
	CACHE_METADATA_PMS = 10,
	CACHE_METADATA_MD5 = 11,
};

int filter_hidden(const struct dirent *dentry);
int filter_hidden(const struct dirent *dentry)
{
	if (dentry->d_name[0] == '.')
		return 0;
	return 1;
}

static const char *
initialize_flat(const char *overlay, int cache_type, bool force)
{
	struct dirent **category, **pn, **eb;
	struct stat st;
	struct timeval start, finish;
	char *cache_file;
	char *p;
	int i;
	int frac, secs, count;
	FILE *fp;

	xasprintf(&cache_file, "%s/dep/%s/%s", portedb, overlay,
		(cache_type == CACHE_EBUILD ? ".ebuild.x" : ".metadata.x"));

	/* If we aren't forcing a regen, make sure the file is somewhat sane. */
	if (!force) {
		if (stat(cache_file, &st) != -1)
			if (st.st_size)
				return cache_file;
	}

	warn("Updating ebuild %scache in %s ... ",
		cache_type == CACHE_EBUILD ? "" : "meta", overlay);

	count = frac = secs = 0;

	int overlay_fd, subdir_fd;
	overlay_fd = open(overlay, O_RDONLY|O_CLOEXEC|O_PATH);

	if (cache_type == CACHE_METADATA) {
		subdir_fd = openat(overlay_fd, portcachedir_md5, O_RDONLY|O_CLOEXEC);
		if (subdir_fd == -1) {
			subdir_fd = openat(overlay_fd, portcachedir_pms, O_RDONLY|O_CLOEXEC);
			if (subdir_fd == -1) {
				warnp("could not read md5 or pms cache dirs in %s", overlay);
				goto ret;
			}
			portcachedir_type = CACHE_METADATA_PMS;
		} else
			portcachedir_type = CACHE_METADATA_MD5;
	} else
		subdir_fd = overlay_fd;

	if ((fp = fopen(cache_file, "we")) == NULL) {
		warnfp("opening cache failed: %s", cache_file);
		if (errno == EACCES)
			warnf("You should run this command as root: q -%c",
				cache_type == CACHE_EBUILD ? 'r' : 'm');
		goto ret;
	}

	gettimeofday(&start, NULL);

	int cat_cnt;
	cat_cnt = scandirat(subdir_fd, ".", &category, q_vdb_filter_cat, alphasort);
	if (cat_cnt < 0)
		goto ret;

	for (i = 0; i < cat_cnt; i++) {
		if (fstatat(subdir_fd, category[i]->d_name, &st, 0))
			continue;
		if (!S_ISDIR(st.st_mode))
			continue;
		if (strchr(category[i]->d_name, '-') == NULL)
			if (strncmp(category[i]->d_name, "virtual", 7) != 0)
				continue;

		int c, pkg_cnt;
		pkg_cnt = scandirat(subdir_fd, category[i]->d_name, &pn, q_vdb_filter_pkg, alphasort);
		if (pkg_cnt < 0)
			continue;
		for (c = 0; c < pkg_cnt; c++) {
			char de[_Q_PATH_MAX];

			snprintf(de, sizeof(de), "%s/%s", category[i]->d_name, pn[c]->d_name);

			if (fstatat(subdir_fd, de, &st, 0) < 0)
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
			}

			int e, ebuild_cnt;
			ebuild_cnt = scandirat(subdir_fd, de, &eb, filter_hidden, alphasort);
			if (ebuild_cnt < 0)
				continue;
			for (e = 0; e < ebuild_cnt; ++e) {
				if ((p = strrchr(eb[e]->d_name, '.')) != NULL)
					if (strcmp(p, ".ebuild") == 0) {
						count++;
						fprintf(fp, "%s/%s\n", de, eb[e]->d_name);
					}
			}
			scandir_free(eb, ebuild_cnt);
		}
		scandir_free(pn, pkg_cnt);
	}
	fclose(fp);
	scandir_free(category, cat_cnt);

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
		warn("You should consider using the noatime mount option for '%s' if it's not already enabled", overlay);
ret:
	close(subdir_fd);
	if (subdir_fd != overlay_fd)
		close(overlay_fd);
	return cache_file;
}

void reinitialize_as_needed(void)
{
	size_t n;
	const char *overlay, *ret = ret;

	if (reinitialize)
		array_for_each(overlays, n, overlay) {
			ret = initialize_flat(overlay, CACHE_EBUILD, true);
			if (USE_CLEANUP)
				free((void *)ret);
		}

	if (reinitialize_metacache)
		array_for_each(overlays, n, overlay) {
			ret = initialize_flat(overlay, CACHE_METADATA, true);
			if (USE_CLEANUP)
				free((void *)ret);
		}
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
	/* These are MD5-Cache only */
	char *DEFINED_PHASES;
	char *REQUIRED_USE;
	char *_eclasses_;
	char *_md5_;
} portage_cache;

void cache_free(portage_cache *cache);
portage_cache *cache_read_file_pms(const char *file);
portage_cache *cache_read_file_md5(const char *file);
portage_cache *cache_read_file(const char *file);

portage_cache *cache_read_file(const char *file)
{
	if (portcachedir_type == CACHE_METADATA_MD5)
		return(cache_read_file_md5(file));
	else if (portcachedir_type == CACHE_METADATA_PMS)
		return(cache_read_file_pms(file));
	warn("Unknown metadata cache type!");
	return NULL;
}

portage_cache *cache_read_file_pms(const char *file)
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
	if (f) fclose(f);
	if (ret) cache_free(ret);
	return NULL;
}

portage_cache *cache_read_file_md5(const char *file)
{
	struct stat s;
	char *ptr, *endptr;
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

	/* We have a block of key=value\n data.
	 * KEY=VALUE\n
	 * Where KEY does NOT contain:
	 * \0 \n =
	 * And VALUE does NOT contain:
	 * \0 \n
	 * */
#define assign_var_cmp(keyname, cmpkey) \
	if (strncmp(keyptr, cmpkey, strlen(cmpkey)) == 0) { \
		ret->keyname = valptr; \
		continue; \
	}
#define assign_var(keyname) \
	assign_var_cmp(keyname, #keyname);

	ptr = ret->_data;
	endptr = strchr(ptr, '\0');
	if (endptr == NULL) {
			warn("Invalid cache file '%s' - could not find end of cache data", file);
			goto err;
	}

	while (ptr != NULL && ptr != endptr) {
		char *keyptr;
		char *valptr;
		keyptr = ptr;
		valptr = strchr(ptr, '=');
		if (valptr == NULL) {
			warn("Invalid cache file '%s' val", file);
			goto err;
		}
		*valptr = '\0';
		valptr++;
		ptr = strchr(valptr, '\n');
		if (ptr == NULL) {
			warn("Invalid cache file '%s' key", file);
			goto err;
		}
		*ptr = '\0';
		ptr++;

		assign_var(CDEPEND);
		assign_var(DEPEND);
		assign_var(DESCRIPTION);
		assign_var(EAPI);
		assign_var(HOMEPAGE);
		assign_var(INHERITED);
		assign_var(IUSE);
		assign_var(KEYWORDS);
		assign_var(LICENSE);
		assign_var(PDEPEND);
		assign_var(PROPERTIES);
		assign_var(PROVIDE);
		assign_var(RDEPEND);
		assign_var(RESTRICT);
		assign_var(SLOT);
		assign_var(SRC_URI);
		assign_var(DEFINED_PHASES);
		assign_var(REQUIRED_USE);
		assign_var(_eclasses_);
		assign_var(_md5_);
		warn("Cache file '%s' with unknown key %s", file, keyptr);
	}
#undef assign_var
#undef assign_var_cmp

	fclose(f);

	return ret;

err:
	if (f) fclose(f);
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

/* TODO: Merge this into libq/vdb.c somehow. */
_q_static queue *get_vdb_atoms(int fullcpv)
{
	q_vdb_ctx *ctx;

	int cfd, j;
	int dfd, i;

	char buf[_Q_PATH_MAX];
	char slot[_Q_PATH_MAX];
	char *slotp = slot;
	size_t slot_len;

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
			int blen = snprintf(buf, sizeof(buf), "%s/%s/SLOT", cat[j]->d_name, pf[i]->d_name);
			if (blen >= sizeof(buf)) {
				warnf("unable to parse long package: %s/%s", cat[j]->d_name, pf[i]->d_name);
				continue;
			}

			/* Chop the SLOT for the atom parsing. */
			buf[blen - 5] = '\0';
			if ((atom = atom_explode(buf)) == NULL)
				continue;
			/* Restore the SLOT. */
			buf[blen - 5] = '/';

			slot_len = sizeof(slot);
			eat_file_at(ctx->vdb_fd, buf, &slotp, &slot_len);
			rmspace(slot);

			if (fullcpv) {
				if (atom->PR_int)
					snprintf(buf, sizeof(buf), "%s/%s-%s-r%i", atom->CATEGORY, atom->PN, atom->PV, atom->PR_int);
				else
					snprintf(buf, sizeof(buf), "%s/%s-%s", atom->CATEGORY, atom->PN, atom->PV);
			} else {
				snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, atom->PN);
			}
			atom_implode(atom);
			cpf = add_set(buf, cpf);
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
}

int main(int argc, char **argv)
{
	struct stat st;
	warnout = stderr;
	IF_DEBUG(init_coredumps());
	argv0 = argv[0];

	setlocale(LC_ALL, "");
	bindtextdomain(argv0, CONFIG_EPREFIX "usr/share/locale");
	textdomain(argv0);

	if (fstat(fileno(stdout), &st) != -1)
		if (!isatty(fileno(stdout)))
			no_colors();
	if ((getenv("TERM") == NULL) || (strcmp(getenv("TERM"), "dumb") == 0))
		no_colors();

	initialize_portage_env();
	atexit(cleanup);
	optind = 0;
	return q_main(argc, argv);
}

#include "include_applets.h"
