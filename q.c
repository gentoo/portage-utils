/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2017-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if defined(__MACH__) && defined(__APPLE__)
#include <libproc.h>
#endif

#include "atom.h"
#include "basename.h"
#include "eat_file.h"
#include "rmspace.h"

#define Q_FLAGS "ioem" COMMON_FLAGS
static struct option const q_long_opts[] = {
	{"install",       no_argument, NULL, 'i'},
	{"overlays",      no_argument, NULL, 'o'},
	{"envvar",        no_argument, NULL, 'e'},
	{"masks",         no_argument, NULL, 'm'},
	COMMON_LONG_OPTS
};
static const char * const q_opts_help[] = {
	"Install symlinks for applets",
	"Print available overlays (read from repos.conf)",
	"Print used variables and their found values",
	"Print (package.)masks for the current profile",
	COMMON_OPTS_HELP
};
#define q_usage(ret) usage(ret, Q_FLAGS, q_long_opts, q_opts_help, NULL, lookup_applet_idx("q"))

APPLET lookup_applet(const char *applet)
{
	unsigned int i;

	if (strlen(applet) < 1)
		return NULL;

	if (applet[0] == 'q')
		applet++;

	/* simple case, e.g. something like "qlop" or "q lop" both being "lop"  */
	for (i = 0; applets[i].desc != NULL; i++) {
		if (strcmp(applets[i].name + 1, applet) == 0) {
			argv0 = applets[i].name;
			return applets[i].func;
		}
	}

	/* this is possibly an alias like "belongs"
	 * NOTE: we continue where the previous loop left, e.g. on the first
	 * alias (desc == NULL) */
	for ( ; applets[i].name != NULL; i++) {
		if (strcmp(applets[i].name, applet) == 0) {
			unsigned int j;

			/* lookup the real name for this alias */
			for (j = 1; applets[j].desc != NULL; j++) {
				if (applets[j].func == applets[i].func) {
					argv0 = applets[j].name;
					return applets[j].func;
				}
			}

			/* this shouldn't happen and means our array from applets.h
			 * is inconsistent */
			warn("invalid applet alias '%s': target applet unknown", applet);
			return NULL;
		}
	}

	/* still nothing ?  those bastards ... */
	warn("unknown applet: %s", applet);
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
	bool install;
	bool print_overlays;
	bool print_vars;
	bool print_masks;
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

	install = false;
	print_overlays = false;
	print_vars = false;
	print_masks = false;
	while ((i = GETOPT_LONG(Q, q, "+")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(q)
		case 'i': install = true;        break;
		case 'o': print_overlays = true; break;
		case 'e': print_vars = true;     break;
		case 'm': print_masks = true;    break;
		}
	}

	if (install) {
		char buf[_Q_PATH_MAX];
		const char *prog, *dir;
		ssize_t rret;
		int fd, ret;

		if (!quiet)
			printf("Installing symlinks:\n");

#if defined(__MACH__) && defined(__APPLE__)
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
			warnfp("haha no symlink love for you ... :(");
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

	if (print_overlays) {
		char *overlay;
		char *repo_name = NULL;
		size_t repo_name_len = 0;
		char buf[_Q_PATH_MAX];
		size_t n;

		array_for_each(overlays, n, overlay) {
			repo_name = xarrayget(overlay_names, n);
			if (strcmp(repo_name, "<PORTDIR>") == 0) {
				repo_name = NULL;
				snprintf(buf, sizeof(buf), "%s/profiles/repo_name", overlay);
				if (!eat_file(buf, &repo_name, &repo_name_len)) {
					free(repo_name);
					repo_name = NULL;
				}
				if (repo_name != NULL)
					rmspace(repo_name);
			}
			printf("%s%s%s: %s%s%s%s",
					GREEN, repo_name == NULL ? "?unknown?" : repo_name,
					NORM, overlay,
					YELLOW, main_overlay == overlay ? " (main)" : "", NORM);
			if (verbose)
				printf(" [%s]\n", (char *)xarrayget(overlay_src, n));
			else
				printf("\n");
			if (repo_name_len != 0) {
				free(repo_name);
				repo_name_len = 0;
			}
		}

		return 0;
	}

	if (print_vars) {
		env_vars *var;
		int j;

		if (argc == optind || argc - optind > 1) {
			for (i = 0; vars_to_read[i].name; i++) {
				var = &vars_to_read[i];

				/* check if we want this variable */
				for (j = optind; j < argc; j++)
					if (strcmp(var->name, argv[j]) == 0)
						break;
				if (j == argc && optind != argc)
					continue;

				printf("%s%s%s=", BLUE, var->name, NORM);
				switch (var->type) {
					case _Q_BOOL:
						printf("%s%s%s",
								YELLOW, *var->value.b ? "1" : "0", NORM);
						break;
				case _Q_STR:
				case _Q_ISTR:
						printf("%s\"%s\"%s", RED, *var->value.s, NORM);
						break;
				}
				if (verbose)
					printf(" [%s]\n", var->src);
				else
					printf("\n");
			}
		} else {
			/* single envvar printing, just output the value, like
			 * portageq envvar does */
			for (i = 0; vars_to_read[i].name; i++) {
				var = &vars_to_read[i];

				if (strcmp(var->name, argv[optind]) != 0)
					continue;

				switch (var->type) {
					case _Q_BOOL:
						printf("%s%s%s",
								YELLOW, *var->value.b ? "1" : "0", NORM);
						break;
				case _Q_STR:
				case _Q_ISTR:
						printf("%s%s%s", RED, *var->value.s, NORM);
						break;
				}
				if (verbose)
					printf(" [%s]\n", var->src);
				else
					printf("\n");
			}
		}

		return 0;
	}

	if (print_masks) {
		DECLARE_ARRAY(masks);
		DECLARE_ARRAY(files);
		char *mask;
		size_t n;
		int j;
		bool match;
		depend_atom *atom;
		depend_atom *qatom;

		array_set(package_masks, masks);
		values_set(package_masks, files);

		array_for_each(masks, n, mask) {
			if ((atom = atom_explode(mask)) == NULL)
				continue;

			match = true;
			if (argc > optind) {
				match = false;
				for (j = optind; j < argc; j++) {
					qatom = atom_explode(argv[j]);
					if (qatom != NULL && atom_compare(atom, qatom) == EQUAL)
						match = true;
					atom_implode(qatom);
					if (match)
						break;
				}
			}
			if (!match)
				continue;

			printf("%s", atom_format(
						"%[pfx]%[CAT]%[PF]%[SLOT]%[SUBSLOT]%[sfx]%[USE]%[REPO]",
						atom));
			if (verbose)
				printf(" [%s]\n", (char *)array_get_elem(files, n));
			else
				printf("\n");
			atom_implode(atom);
		}

		xarrayfree_int(masks);
		xarrayfree_int(files);

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
