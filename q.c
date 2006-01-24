/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/q.c,v 1.27 2006/01/24 23:35:08 vapier Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#define Q_FLAGS "ir" COMMON_FLAGS
static struct option const q_long_opts[] = {
	{"install",      no_argument, NULL, 'i'},
	{"reinitialize", no_argument, NULL, 'r'},
	COMMON_LONG_OPTS
};
static const char *q_opts_help[] = {
	"Install symlinks for applets",
	"Reinitialize ebuild cache",
	COMMON_OPTS_HELP
};
static const char q_rcsid[] = "$Id: q.c,v 1.27 2006/01/24 23:35:08 vapier Exp $";
#define q_usage(ret) usage(ret, Q_FLAGS, q_long_opts, q_opts_help, lookup_applet_idx("q"))


APPLET lookup_applet(char *applet);
APPLET lookup_applet(char *applet)
{
	unsigned int i;
	for (i = 0; applets[i].name; ++i) {
		if (strcmp(applets[i].name, applet) == 0) {
			DBG("found applet %s at %p", applets[i].name, applets[i].func);
			argv0 = applets[i].name;
			if (i && applets[i].desc != NULL) ++argv0; /* chop the leading 'q' */
			return applets[i].func;
		}
	}
	/* No applet found? Search by shortname then... */
	if (strlen(applet) > 1) {
		DBG("Looking up applet (%s) by short name", applet);
		for (i = 1; applets[i].desc != NULL; ++i) {
			if (strcmp(applets[i].name + 1, applet) == 0) {
				DBG("found applet by short name %s", applets[i].name);
				argv0 = applets[i].name + 1;
				return applets[i].func;
			}
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
	int i;
	char *p;
	APPLET func;

	if (argc == 0)
		return 1;

	argv0 = p = basename(argv[0]);

	if ((func = lookup_applet(p)) == 0)
		return 1;
	if (strcmp("q", p) != 0)
		return (func)(argc, argv);

	if (argc == 1)
		q_usage(EXIT_FAILURE);

	while ((i = GETOPT_LONG(Q, q, "+")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(q)
		case 'r': reinitialize = 1; return 0;
		case 'i': {
			char buf[_Q_PATH_MAX];
			/* always bzero a buffer before using readlink() */
			memset(buf, 0x00, sizeof(buf));
			printf("Installing symlinks:\n");
			if ((readlink("/proc/self/exe", buf, sizeof(buf))) == (-1)) {
				warnf("could not readlink '/proc/self/exe': %s", strerror(errno));
				return 1;
			}
			if (chdir(dirname(buf)) != 0) {
				warnf("could not chdir to '%s': %s", buf, strerror(errno));
				return 1;
			}
			for (i = 1; applets[i].desc != NULL; ++i) {
				printf(" %s ...", applets[i].name);
				errno = 0;
				symlink("q", applets[i].name);
				printf("\t[%s]\n", strerror(errno));
			}
			return 0;
		}
		}
	}
	if (argc == optind)
		q_usage(EXIT_FAILURE);
	if ((func = lookup_applet(argv[optind])) == 0)
		return 1;

	optind = 0; /* reset so the applets can call getopt */

	return (func)(argc - 1, ++argv);
}
