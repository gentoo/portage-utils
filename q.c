/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/q.c,v 1.53 2014/02/25 21:30:50 vapier Exp $
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2010 Mike Frysinger  - <vapier@gentoo.org>
 */

#define Q_FLAGS "irmM:" COMMON_FLAGS
static struct option const q_long_opts[] = {
	{"install",      no_argument, NULL, 'i'},
	{"reinitialize", no_argument, NULL, 'r'},
	{"metacache",    no_argument, NULL, 'm'},
	{"modpath",       a_argument, NULL, 'M'},
	COMMON_LONG_OPTS
};
static const char * const q_opts_help[] = {
	"Install symlinks for applets",
	"Reinitialize ebuild cache",
	"Reinitialize metadata cache",
	"Module path",
	COMMON_OPTS_HELP
};
static const char q_rcsid[] = "$Id: q.c,v 1.53 2014/02/25 21:30:50 vapier Exp $";
#define q_usage(ret) usage(ret, Q_FLAGS, q_long_opts, q_opts_help, lookup_applet_idx("q"))

static APPLET lookup_applet(const char *applet)
{
	unsigned int i;

	if (strlen(applet) < 1)
		return NULL;

	for (i = 0; applets[i].name; ++i) {
		if (strcmp(applets[i].name, applet) == 0) {
			DBG("found applet %s at %p", applets[i].name, applets[i].func);
			argv0 = applets[i].name;
			if (i && applets[i].desc != NULL) ++argv0; /* chop the leading 'q' */
			return applets[i].func;
		}
	}

	/* No applet found? Search by shortname then... */
	DBG("Looking up applet (%s) by short name", applet);
	for (i = 1; applets[i].desc != NULL; ++i) {
		if (strcmp(applets[i].name + 1, applet) == 0) {
			DBG("found applet by short name %s", applets[i].name);
			argv0 = applets[i].name + 1;
			return applets[i].func;
		}
	}
	/* still nothing ?  those bastards ... */
	warn("Unknown applet '%s'", applet);
	return NULL;
}

int lookup_applet_idx(const char *applet)
{
	unsigned int i;
	for (i = 0; applets[i].name; i++)
		if (strcmp(applets[i].name, applet) == 0)
			return i;
	return 0;
}

int q_main(int argc, char **argv)
{
	int i, install;
	const char *p;
	APPLET func;

	if (argc == 0)
		return 1;

	argv0 = p = basename(argv[0]);

	if ((func = lookup_applet(p)) == NULL)
		return 1;
	if (strcmp("q", p) != 0)
		return (func)(argc, argv);

	if (argc == 1)
		q_usage(EXIT_FAILURE);

	install = 0;

	while ((i = GETOPT_LONG(Q, q, "+")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(q)
		case 'M': modpath = optarg; break;
		case 'm': reinitialize_metacache = 1; break;
		case 'r': reinitialize = 1; break;
		case 'i': install = 1; break;
		}
	}

	if (install) {
		char buf[_Q_PATH_MAX];
		const char *prog;
		ssize_t rret;
		int fd, ret;

		if (!quiet)
			printf("Installing symlinks:\n");

		rret = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
		if (rret == -1) {
			char *ptr = which("q");
			if (ptr == NULL) {
				warnfp("haha no symlink love for you");
				return 1;
			}
			strncpy(buf, ptr, sizeof(buf));
			buf[sizeof(buf) - 1] = '\0';
		} else
			buf[rret] = '\0';

		prog = basename(buf);
		fd = open(dirname(buf), O_RDONLY|O_CLOEXEC);
		if (fd < 0) {
			warnfp("chdir(%s) failed", buf);
			return 1;
		}

		ret = 0;
		for (i = 1; applets[i].desc; ++i) {
			int r = symlinkat(prog, fd, applets[i].name);
			if (!quiet)
				printf(" %s ...\t[%s]\n", applets[i].name, r ? strerror(errno) : "OK");
			if (r && errno != EEXIST)
				ret = 1;
		}

		close(fd);

		return ret;
	}

	if (reinitialize || reinitialize_metacache)
		return 0;
	if (argc == optind)
		q_usage(EXIT_FAILURE);
	if ((func = lookup_applet(argv[optind])) == NULL)
		return 1;

	/* In case of "q --option ... appletname ...", remove appletname from the
	 * applet's args. */
	if (optind > 1) {
		argv[0] = argv[optind];
		for (i = optind; i < argc; ++i)
			argv[i] = argv[i + 1];
	} else
		++argv;

	optind = 0; /* reset so the applets can call getopt */

	return (func)(argc - 1, argv);
}

static int run_applet_l(const char *arg, ...)
{
	int (*applet)(int, char **);
	va_list ap;
	int ret, optind_saved, argc;
	char **argv;
	const char *argv0_saved;

	optind_saved = optind;
	argv0_saved = argv0;

	applet = lookup_applet(arg);
	if (!applet)
		return -1;

	/* This doesn't NULL terminate argv, but you should be using argc */
	va_start(ap, arg);
	argc = 0;
	argv = NULL;
	while (arg) {
		argv = xrealloc(argv, sizeof(*argv) * ++argc);
		argv[argc - 1] = xstrdup(arg);
		arg = va_arg(ap, const char *);
	}
	va_end(ap);

	optind = 0;
	argv0 = argv[0];
	ret = applet(argc, argv);

	while (argc--)
		free(argv[argc]);
	free(argv);

	optind = optind_saved;
	argv0 = argv0_saved;

	return ret;
}
