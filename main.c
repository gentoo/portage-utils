/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <iniparser.h>
#include <xalloc.h>
#include <assert.h>
#include <ctype.h>
#include <sys/time.h>
#include <limits.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "eat_file.h"
#include "rmspace.h"
#include "scandirat.h"
#include "xasprintf.h"

/* variables to control runtime behavior */
char *main_overlay;
char *module_name = NULL;
int verbose = 0;
int quiet = 0;
int twidth;
char pretend = 0;
char *portroot;
char *config_protect;
char *config_protect_mask;
char *portvdb;
char *portlogdir;
char *pkg_install_mask;
char *binhost;
char *pkgdir;
char *port_tmpdir;
char *features;
char *install_mask;
DECLARE_ARRAY(overlays);
DECLARE_ARRAY(overlay_names);
DECLARE_ARRAY(overlay_src);

static char *portarch;
static char *portedb;
static char *eprefix;
static char *accept_license;

/* helper functions for showing errors */
const char *argv0;

FILE *warnout;

#ifdef EBUG
# include <sys/resource.h>
static void
init_coredumps(void)
{
	struct rlimit rl;
	rl.rlim_cur = RLIM_INFINITY;
	rl.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_CORE, &rl);
}
#endif

void
no_colors(void)
{
	BOLD = NORM = BLUE = DKBLUE = CYAN = GREEN = DKGREEN = \
		   MAGENTA = RED = YELLOW = BRYELLOW = WHITE = "";
	setenv("NOCOLOR", "true", 1);
}

void
setup_quiet(void)
{
	/* "e" for FD_CLOEXEC */
	if (quiet == 0)
		warnout = fopen("/dev/null", "we");
	++quiet;
}

/* display usage and exit */
void
usage(int status, const char *flags, struct option const opts[],
      const char * const help[], const char *desc, int blabber)
{
	const char opt_arg[] = "[arg]";
	const char a_arg[] = "<arg>";
	size_t a_arg_len = strlen(a_arg) + 1;
	size_t i;
	size_t optlen;
	size_t l;
	size_t prefixlen;
	const char *hstr;
	FILE *fp = status == EXIT_SUCCESS ? stdout : warnout;

	if (blabber == 0) {
		fprintf(fp, "%susage:%s %sq%s %s<applet> <args>%s  : %s"
			"invoke a portage utility applet\n\n", GREEN,
			NORM, YELLOW, NORM, DKBLUE, RED, NORM);
		fprintf(fp, "%scurrently defined applets:%s\n", GREEN, NORM);
		for (i = 0; applets[i].desc; ++i)
			if (applets[i].func)
				fprintf(fp, " %s%8s%s %s%-16s%s%s:%s %s\n",
					YELLOW, applets[i].name, NORM,
					DKBLUE, applets[i].opts, NORM,
					RED, NORM, _(applets[i].desc));
	} else if (blabber > 0) {
		fprintf(fp, "%susage:%s %s%s%s [opts] %s%s%s %s:%s %s\n",
			GREEN, NORM,
			YELLOW, applets[blabber].name, NORM,
			DKBLUE, applets[blabber].opts, NORM,
			RED, NORM, _(applets[blabber].desc));
		if (desc)
			fprintf(fp, "\n%s\n", desc);
	}
	if (module_name != NULL)
		fprintf(fp, "%sloaded module:%s\n%s%8s%s %s<args>%s\n",
			GREEN, NORM, YELLOW, module_name, NORM, DKBLUE, NORM);

	/* Prescan the --long opt length to auto-align. */
	optlen = 0;
	for (i = 0; opts[i].name; ++i) {
		l = strlen(opts[i].name);
		if (opts[i].has_arg != no_argument)
			l += a_arg_len;
		optlen = MAX(l, optlen);
	}

	fprintf(fp, "\n%soptions:%s -[%s]\n", GREEN, NORM, flags);
	for (i = 0; opts[i].name; ++i) {
		/* this assert is a life saver when adding new applets. */
		assert(help[i] != NULL);

		/* first output the short flag if it has one */
		if (opts[i].val > '~' || opts[i].val < ' ')
			fprintf(fp, "      ");
		else
			fprintf(fp, "  -%c, ", opts[i].val);

		/* then the long flag */
		if (opts[i].has_arg == no_argument)
			fprintf(fp, "--%-*s %s*%s ", (int)optlen, opts[i].name,
				RED, NORM);
		else
			fprintf(fp, "--%s %s%s%s%*s %s*%s ",
				opts[i].name,
				DKBLUE, (opts[i].has_arg == a_argument ? a_arg : opt_arg), NORM,
				(int)(optlen - strlen(opts[i].name) - a_arg_len), "",
				RED, NORM);

		/* then wrap the help text, if necessary */
		prefixlen = 6 + 2 + optlen + 1 + 1 + 1;
		if ((size_t)twidth < prefixlen + 10) {
			fprintf(fp, "%s\n", _(help[i]));
		} else {
			const char *t;
			hstr = _(help[i]);
			l = strlen(hstr);
			while (twidth - prefixlen < l) {
				/* search backwards for a space */
				t = &hstr[twidth - prefixlen];
				while (t > hstr && !isspace((int)*t))
					t--;
				if (t == hstr)
					break;
				fprintf(fp, "%.*s\n%*s",
						(int)(t - hstr), hstr, (int)prefixlen, "");
				l -= t + 1 - hstr;
				hstr = t + 1;  /* skip space */
			}
			fprintf(fp, "%s\n", hstr);
		}
	}
	exit(status);
}

void
version_barf(void)
{
	const char *vcsid = "";
	const char *eprefixid = "";

#ifndef VERSION
# define VERSION "git"
#endif

#ifdef VCSID
	vcsid = " (" VCSID ")";
#endif

	if (strlen(CONFIG_EPREFIX) > 1)
		eprefixid = "configured for " CONFIG_EPREFIX "\n";

	printf("portage-utils-%s%s\n"
	       "%s"
	       "written for Gentoo by solar, vapier and grobian\n",
	       VERSION, vcsid, eprefixid);
	exit(EXIT_SUCCESS);
}

void
freeargv(int argc, char **argv)
{
	while (argc--)
		free(argv[argc]);
	free(argv);
}

void
makeargv(const char *string, int *argc, char ***argv)
{
	int curc = 2;
	char *q, *p, *str;
	(*argv) = xmalloc(sizeof(char **) * curc);

	*argc = 1;
	(*argv)[0] = xstrdup(argv0);
	q = xstrdup(string);
	str = q;

	/* shortcut empty strings */
	while (isspace((int)*string))
		string++;
	if (*string == '\0')
		return;

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

static void
strincr_var(const char *name, const char *s, char **value, size_t *value_len)
{
	size_t len;
	char *p, *nv;
	char brace;

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
	p = nv;
	while ((p = strchr(p, '$')) != NULL) {
		nv = p;
		p++;  /* skip $ */
		brace = *p == '{';
		if (brace)
			p++;
		if (strncmp(p, name, len) == 0) {
			p += len;
			if (brace && *p == '}') {
				p++;
				memset(nv, ' ', p - nv);
			} else if (!brace && (*p == '\0' || isspace((int)*p))) {
				memset(nv, ' ', p - nv);
			}
		}
	}

	remove_extra_space(*value);
	*value_len = strlen(*value);
	/* we should sort here */
}

static env_vars *
get_portage_env_var(env_vars *vars, const char *name)
{
	size_t i;

	for (i = 0; vars[i].name; ++i)
		if (!strcmp(vars[i].name, name))
			return &vars[i];

	return NULL;
}

static void
set_portage_env_var(env_vars *var, const char *value, const char *src)
{
	switch (var->type) {
	case _Q_BOOL:
		*var->value.b = 1;
		free(var->src);
		var->src = xstrdup(src);
		break;
	case _Q_STR:
		free(*var->value.s);
		*var->value.s = xstrdup(value);
		var->value_len = strlen(value);
		free(var->src);
		var->src = xstrdup(src);
		break;
	case _Q_ISTR:
		if (*var->value.s == NULL) {
			free(var->src);
			var->src = xstrdup(src);
		} else {
			size_t l = strlen(var->src) + 2 + strlen(src) + 1;
			char *p = xmalloc(sizeof(char) * l);
			snprintf(p, l, "%s, %s", var->src, src);
			free(var->src);
			var->src = p;
		}
		strincr_var(var->name, value, var->value.s, &var->value_len);
		break;
	}
}

/* Helper to read a portage env file (e.g. make.conf), or recursively if
 * it points to a directory */
static void
read_portage_env_file(const char *file, env_vars vars[])
{
	FILE *fp;
	struct dirent **dents;
	int dentslen;
	char *s;
	char *p;
	char *buf = NULL;
	size_t buflen = 0;
	size_t line;
	int i;

	if (getenv("DEBUG"))
		fprintf(stderr, "profile %s\n", file);

	if ((dentslen = scandir(file, &dents, NULL, alphasort)) > 0) {
		int di;
		struct dirent *d;
		char npath[_Q_PATH_MAX * 2];

		/* recurse through all files */
		for (di = 0; di < dentslen; di++) {
			d = dents[di];
			if (d->d_name[0] == '.' || d->d_name[0] == '\0' ||
					d->d_name[strlen(d->d_name) - 1] == '~')
				continue;
			snprintf(npath, sizeof(npath), "%s/%s", file, d->d_name);
			read_portage_env_file(npath, vars);
		}
		scandir_free(dents, dentslen);
		goto done;
	}

	fp = fopen(file, "r");
	if (fp == NULL)
		goto done;

	line = 0;
	while (getline(&buf, &buflen, fp) != -1) {
		line++;
		rmspace(buf);
		if (*buf == '#' || *buf == '\0')
			continue;

		/* Handle "source" keyword */
		if (strncmp(buf, "source ", 7) == 0) {
			const char *sfile = buf + 7;
			char npath[_Q_PATH_MAX * 2];

			if (sfile[0] != '/') {
				/* handle relative paths */
				size_t file_path_len;

				s = strrchr(file, '/');
				file_path_len = s - file + 1;

				snprintf(npath, sizeof(npath), "%.*s/%s",
						(int)file_path_len, file, sfile);
				sfile = npath;
			}

			read_portage_env_file(sfile, vars);
			continue;
		}

		/* look for our desired variables and grab their value */
		for (i = 0; vars[i].name; ++i) {
			if (buf[vars[i].name_len] != '=' && buf[vars[i].name_len] != ' ')
				continue;
			if (strncmp(buf, vars[i].name, vars[i].name_len))
				continue;

			/* make sure we handle spaces between the varname, the =,
			 * and the value:
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
						warn("%s:%zu: %s: quote mismatch",
								file, line, vars[i].name);

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

			set_portage_env_var(&vars[i], s, file);
		}
	}

	fclose(fp);
 done:
	free(buf);
}

/* Helper to recursively read stacked make.defaults in profiles */
static void
read_portage_profile(const char *profile, env_vars vars[])
{
	char profile_file[_Q_PATH_MAX * 3];
	size_t profile_len;
	char *s;
	char *p;
	char *buf = NULL;
	size_t buf_len = 0;
	char *saveptr;

	if (getenv("DEBUG"))
		fprintf(stderr, "profile %s\n", profile);

	/* create mutable/appendable copy */
	profile_len = snprintf(profile_file, sizeof(profile_file), "%s/", profile);

	/* check if we have enough space (should always be the case) */
	if (sizeof(profile_file) - profile_len < sizeof("make.defaults"))
		return;

	/* first consume the profile's make.defaults */
	strcpy(profile_file + profile_len, "make.defaults");
	read_portage_env_file(profile_file, vars);

	/* now walk all the parents */
	strcpy(profile_file + profile_len, "parent");
	if (eat_file(profile_file, &buf, &buf_len) == 0)
		return;

	s = strtok_r(buf, "\n", &saveptr);
	while (s) {
		/* handle repo: notation (not in PMS, referenced in Wiki only?) */
		if ((p = strchr(s, ':')) != NULL) {
			char *overlay;
			char *repo_name;
			size_t n;

			/* split repo from target */
			*p++ = '\0';

			/* match the repo */
			repo_name = NULL;
			array_for_each(overlays, n, overlay) {
				repo_name = xarrayget(overlay_names, n);
				if (strcmp(repo_name, s) == 0) {
					snprintf(profile_file, sizeof(profile_file),
							"%s/profiles/%s/", overlay, p);
					break;
				}
				repo_name = NULL;
			}
			if (repo_name == NULL) {
				warn("ignoring parent with unknown repo in profile: %s", s);
				s = strtok_r(NULL, "\n", &saveptr);
				continue;
			}
		} else {
			snprintf(profile_file + profile_len,
					sizeof(profile_file) - profile_len, "%s", s);
		}
		read_portage_profile(profile_file, vars);
		/* restore original path in case we were repointed by profile */
		if (p != NULL)
			snprintf(profile_file, sizeof(profile_file), "%s/", profile);
		s = strtok_r(NULL, "\n", &saveptr);
	}

	free(buf);
}

static bool nocolor = 0;
env_vars vars_to_read[] = {
#define _Q_EV(t, V, set, lset, d) \
{ \
	.name = #V, \
	.name_len = strlen(#V), \
	.type = _Q_##t, \
	set, \
	lset, \
	.default_value = d, \
	.src = NULL, \
},
#define _Q_EVS(t, V, v, d) _Q_EV(t, V, .value.s = &v, .value_len = strlen(d), d)
#define _Q_EVB(t, V, v, d) _Q_EV(t, V, .value.b = &v, .value_len = 0, d)

	_Q_EVS(STR,  ROOT,                portroot,            "/")
	_Q_EVS(STR,  ACCEPT_LICENSE,      accept_license,      "")
	_Q_EVS(ISTR, INSTALL_MASK,        install_mask,        "")
	_Q_EVS(ISTR, PKG_INSTALL_MASK,    pkg_install_mask,    "")
	_Q_EVS(STR,  ARCH,                portarch,            "")
	_Q_EVS(ISTR, CONFIG_PROTECT,      config_protect,      "/etc")
	_Q_EVS(ISTR, CONFIG_PROTECT_MASK, config_protect_mask, "")
	_Q_EVB(BOOL, NOCOLOR,             nocolor,             0)
	_Q_EVS(ISTR, FEATURES,            features,            "")
	_Q_EVS(STR,  EPREFIX,             eprefix,             CONFIG_EPREFIX)
	_Q_EVS(STR,  EMERGE_LOG_DIR,      portlogdir,          CONFIG_EPREFIX "var/log")
	_Q_EVS(STR,  PORTDIR,             main_overlay,        CONFIG_EPREFIX "var/db/repos/gentoo")
	_Q_EVS(STR,  PORTAGE_BINHOST,     binhost,             DEFAULT_PORTAGE_BINHOST)
	_Q_EVS(STR,  PORTAGE_TMPDIR,      port_tmpdir,         CONFIG_EPREFIX "var/tmp/portage/")
	_Q_EVS(STR,  PKGDIR,              pkgdir,              CONFIG_EPREFIX "var/cache/binpkgs/")
	_Q_EVS(STR,  Q_VDB,               portvdb,             CONFIG_EPREFIX "var/db/pkg")
	_Q_EVS(STR,  Q_EDB,               portedb,             CONFIG_EPREFIX "var/cache/edb")
	{ NULL, 0, _Q_BOOL, { NULL }, 0, NULL, NULL, }

#undef _Q_EV
};

/* Handle a single file in the repos.conf format. */
static void
read_one_repos_conf(const char *repos_conf)
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
			void *ele;
			size_t n;
			char *overlay;

			array_for_each(overlay_names, n, overlay) {
				if (strcmp(overlay, repo) == 0)
					break;
				overlay = NULL;
			}
			if (overlay != NULL) {
				/* replace overlay */
				ele = array_get_elem(overlay_src, n);
				free(ele);
				array_get_elem(overlay_src, n) = xstrdup(repos_conf);
				ele = array_get_elem(overlays, n);
				free(ele);
				ele = array_get_elem(overlays, n) = xstrdup(path);
			} else {
				ele = xarraypush_str(overlays, path);
				xarraypush_str(overlay_names, repo);
				xarraypush_str(overlay_src, repos_conf);
			}
			if (main_repo && strcmp(repo, main_repo) == 0) {
				main_overlay = ele;
				free(vars_to_read[11 /* PORTDIR */].src);
				vars_to_read[11 /* PORTDIR */].src = xstrdup(repos_conf);
			}
		}
		free(conf);
	}

	iniparser_freedict(dict);
}

/* Handle a possible directory of files. */
static void
read_repos_conf(const char *configroot, const char *repos_conf)
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

			if (name[0] == '.' || name[0] == '\0')
				continue;

			/* Exclude backup files (aka files with ~ as postfix). */
			if (name[strlen(name) - 1] == '~')
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

static void
initialize_portage_env(void)
{
	size_t i;
	const char *s;
	env_vars *var;
	char pathbuf[_Q_PATH_MAX];
	const char *configroot = getenv("PORTAGE_CONFIGROOT");
	char *orig_main_overlay = main_overlay;

	/* initialize all the strings with their default value */
	for (i = 0; vars_to_read[i].name; ++i) {
		var = &vars_to_read[i];
		if (var->type != _Q_BOOL)
			*var->value.s = xstrdup(var->default_value);
		var->src = xstrdup("built-in default");
	}

	/* figure out where to find our config files */
	if (!configroot)
		configroot = CONFIG_EPREFIX;

	/* rstrip /-es */
	i = strlen(configroot);
	while (i > 0 && configroot[i - 1] == '/')
		i--;

	/* read overlays first so we can resolve repo references in profile
	 * parent files */
	snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)i, configroot);
	read_repos_conf(pathbuf, "/usr/share/portage/config/repos.conf");
	read_repos_conf(pathbuf, "/etc/portage/repos.conf");
	if (orig_main_overlay != main_overlay)
		free(orig_main_overlay);
	if (array_cnt(overlays) == 0) {
		xarraypush_ptr(overlays, main_overlay);
		xarraypush_str(overlay_names, "<PORTDIR>");
		xarraypush_str(overlay_src, "built-in default");
	} else if (orig_main_overlay == main_overlay) {
		/* if no explicit overlay was flagged as main, take the first one */
		main_overlay = array_get_elem(overlays, 0);
		set_portage_env_var(&vars_to_read[11] /* PORTDIR */, main_overlay,
				(char *)array_get_elem(overlay_src, 0));
	}

	/* walk all the stacked profiles */
	snprintf(pathbuf, sizeof(pathbuf), "%.*s/etc/make.profile",
			(int)i, configroot);
	read_portage_profile(pathbuf, vars_to_read);
	snprintf(pathbuf, sizeof(pathbuf), "%.*s/etc/portage/make.profile",
			(int)i, configroot);
	read_portage_profile(pathbuf, vars_to_read);

	/* now read all the config files */
	snprintf(pathbuf, sizeof(pathbuf),
			"%.*s/usr/share/portage/config/make.globals",
			(int)i, configroot);
	read_portage_env_file(pathbuf, vars_to_read);
	snprintf(pathbuf, sizeof(pathbuf), "%.*s/etc/make.conf",
			(int)i, configroot);
	read_portage_env_file(pathbuf, vars_to_read);
	snprintf(pathbuf, sizeof(pathbuf), "%.*s/etc/portage/make.conf",
			(int)i, configroot);
	read_portage_env_file(pathbuf, vars_to_read);

	/* finally, check the env */
	for (i = 0; vars_to_read[i].name; ++i) {
		var = &vars_to_read[i];
		s = getenv(var->name);
		if (s != NULL)
			set_portage_env_var(var, s, var->name);
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
			*var->value.s = xrealloc(*var->value.s,
					pre_len + MAX(var_len, slen) + post_len + 1);

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
	if (var->value_len == 0 || (*var->value.s)[var->value_len - 1] != '/') {
		portroot = xrealloc(portroot, var->value_len + 2);
		portroot[var->value_len] = '/';
		portroot[var->value_len + 1] = '\0';
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

	if (getenv("PORTAGE_QUIET") != NULL)
		setup_quiet();

	if (nocolor)
		no_colors();
	else
		color_remap();
}

int main(int argc, char **argv)
{
	struct stat st;
	struct winsize winsz;

	warnout = stderr;
	IF_DEBUG(init_coredumps());
	argv0 = argv[0];

	setlocale(LC_ALL, "");
	bindtextdomain(argv0, CONFIG_EPREFIX "usr/share/locale");
	textdomain(argv0);

	twidth = 0;
	if (fstat(fileno(stdout), &st) != -1) {
		if (!isatty(fileno(stdout))) {
			no_colors();
		} else {
			if ((getenv("TERM") == NULL) ||
					(strcmp(getenv("TERM"), "dumb") == 0))
				no_colors();
			if (ioctl(0, TIOCGWINSZ, &winsz) == 0 && winsz.ws_col > 0)
				twidth = (int)winsz.ws_col;
		}
	}

	initialize_portage_env();
	optind = 0;
	return q_main(argc, argv);
}
