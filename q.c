/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2017-     Fabian Groffen  - <grobian@gentoo.org>
 */

#define Q_FLAGS "irmM:" COMMON_FLAGS
static struct option const q_long_opts[] = {
	{"install",       no_argument, NULL, 'i'},
	{"reinitialize", opt_argument, NULL, 'r'},
	{"metacache",    opt_argument, NULL, 'm'},
	{"modpath",        a_argument, NULL, 'M'},
	COMMON_LONG_OPTS
};
static const char * const q_opts_help[] = {
	"Install symlinks for applets",
	"Reinitialize ebuild cache",
	"Reinitialize metadata cache",
	"Module path",
	COMMON_OPTS_HELP
};
#define q_usage(ret) usage(ret, Q_FLAGS, q_long_opts, q_opts_help, NULL, lookup_applet_idx("q"))

static APPLET lookup_applet(const char *applet)
{
	unsigned int i;

	if (strlen(applet) < 1)
		return NULL;

	for (i = 0; applets[i].name; ++i) {
		if (strcmp(applets[i].name, applet) == 0) {
			argv0 = applets[i].name;
			if (i && applets[i].desc != NULL) ++argv0; /* chop the leading 'q' */
			return applets[i].func;
		}
	}

	/* No applet found? Search by shortname then... */
	for (i = 1; applets[i].desc != NULL; ++i) {
		if (strcmp(applets[i].name + 1, applet) == 0) {
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
		case 'm':
			if (optarg) {
				const char *path =
					initialize_flat(optarg, CACHE_METADATA, true);
				if (USE_CLEANUP)
					free((void *)path);
				reinitialize_metacache = -1;
			} else
				reinitialize_metacache = 1;
			break;
		case 'r':
			if (optarg) {
				const char *path = initialize_flat(optarg, CACHE_EBUILD, true);
				if (USE_CLEANUP)
					free((void *)path);
				reinitialize = -1;
			} else
				reinitialize = 1;
			break;
		case 'i': install = 1; break;
		}
	}

	if (install) {
		char buf[_Q_PATH_MAX];
		const char *prog, *dir;
		ssize_t rret;
		int fd, ret;

		if (!quiet)
			printf("Installing symlinks:\n");

#if defined(__MACH__)
		rret = proc_pidpath(getpid(), buf, sizeof(buf));
		if (rret != -1)
			rret = strlen(buf);
#elif defined(__sun) && defined(__SVR4)
		prog = getexecname();
		rret = strlen(prog);
		if ((size_t)rret > sizeof(buf) - 1) {
			rret = -1;
		} else {
			snprintf(buf, sizeof(buf), "%s", prog);
		}
#else
		rret = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
#endif
		if (rret == -1) {
			warnfp("haha no symlink love for you");
			return 1;
		}
		buf[rret] = '\0';

		prog = basename(buf);
		dir = dirname(buf);
		fd = open(dir, O_RDONLY|O_CLOEXEC|O_PATH);
		if (fd < 0) {
			warnfp("open(%s) failed", dir);
			return 1;
		}

		ret = 0;
		for (i = 1; applets[i].desc; ++i) {
			int r = symlinkat(prog, fd, applets[i].name);
			if (!quiet)
				printf(" %s ...\t[%s]\n",
						applets[i].name, r ? strerror(errno) : "OK");
			if (r && errno != EEXIST)
				ret = 1;
		}

		close(fd);

		return ret;
	}

	if (reinitialize > 0 || reinitialize_metacache > 0)
		return 0;
	if (reinitialize < 0 || reinitialize_metacache < 0) {
		reinitialize = reinitialize_metacache = 0;
		return 0;
	}
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
