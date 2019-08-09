/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <xalloc.h>
#include <sys/types.h>
#include <fcntl.h>

#include "atom.h"
#include "basename.h"
#include "contents.h"
#include "rmspace.h"
#include "tree.h"

#define QFILE_FLAGS "F:doRx:SP" COMMON_FLAGS
static struct option const qfile_long_opts[] = {
	{"format",       a_argument, NULL, 'F'},
	{"slots",       no_argument, NULL, 'S'},
	{"root-prefix", no_argument, NULL, 'R'},
	{"dir",         no_argument, NULL, 'd'},
	{"orphans",     no_argument, NULL, 'o'},
	{"exclude",      a_argument, NULL, 'x'},
	{"skip-plibreg",no_argument, NULL, 'P'},
	COMMON_LONG_OPTS
};
static const char * const qfile_opts_help[] = {
	"Print matched atom using given format string",
	"Display installed packages with slots",
	"Assume arguments are already prefixed by $ROOT",
	"Also match directories for single component arguments",
	"List orphan files",
	"Don't look in package <arg> (used with --orphans)",
	"Don't look in the prunelib registry",
	COMMON_OPTS_HELP
};
#define qfile_usage(ret) usage(ret, QFILE_FLAGS, qfile_long_opts, qfile_opts_help, NULL, lookup_applet_idx("qfile"))

#define qfile_is_prefix(path, prefix, prefix_length) \
	(!prefix_length \
		|| (strlen(path) >= (size_t)prefix_length \
			&& (path[prefix_length] == '/' || path[prefix_length] == '\0') \
			&& !strncmp(path, prefix, prefix_length)))

typedef struct {
	int length;
	char **basenames;
	char **dirnames;
	char **realdirnames;
	short *non_orphans;
	int *results;
} qfile_args_t;

struct qfile_opt_state {
	char *buf;
	size_t buflen;
	qfile_args_t args;
	char *root;
	char *pwd;
	char *real_root;
	size_t real_root_len;
	char *exclude_pkg;
	char *exclude_slot;
	depend_atom *exclude_atom;
	bool basename;
	bool orphans;
	bool assume_root_prefix;
	bool skip_plibreg;
	const char *format;
	bool need_full_atom;
};

/*
 * As a final step, check if file is in the plib_reg
 */
static int qfile_check_plibreg(void *priv)
{
	struct qfile_opt_state *state = priv;

	char fn_plibreg[_Q_PATH_MAX];
	int fd_plibreg;
        FILE *fp_plibreg;
	struct stat cst;

	char file[_Q_PATH_MAX];
	char *line = NULL;
	size_t len = 0;
	int found = 0;

	snprintf(fn_plibreg, _Q_PATH_MAX, "%s%s", CONFIG_EPREFIX, "var/lib/portage/preserved_libs_registry");

	/* Open plibreg */
        fp_plibreg = NULL;
	fd_plibreg = open(fn_plibreg, O_RDONLY|O_CLOEXEC, 0);
	if (fd_plibreg == -1)
		return 0;
	if (fstat(fd_plibreg, &cst)) {
		close(fd_plibreg);
		return 0;
	}
	if ((fp_plibreg = fdopen(fd_plibreg, "r")) == NULL) {
		close(fd_plibreg);
		return 0;
	}

        qfile_args_t *args = &state->args;
        char **base_names = args->basenames;
        char **dir_names = args->dirnames;
        short *non_orphans = args->non_orphans;
	int *results = args->results;

	for (int i = 0; i < args->length; i++) {
		if (base_names[i] == NULL)
			continue;
		if (non_orphans && non_orphans[i])
			continue;
		if (results[i] == 1)
			continue;

		if (dir_names[i] != NULL)
			snprintf(file, sizeof(file), "%s/%s", dir_names[i], base_names[i]);
		else
			snprintf(file, sizeof(file), "%s", base_names[i]);

		while (getline(&line, &len, fp_plibreg) != -1)
			if (strstr(line, file) != NULL) {
				found++;
				if (quiet)
					puts("");
				else
					printf("%s%splib_registry%s (%s)\n", BOLD, BLUE, NORM, file);
			}
	}

	if (line)
		free(line);

	return found;
}

/*
 * 1. Do package exclusion tests
 * 2. Run through CONTENTS file, perform tests and fail-by-continue
 * 3. On success bump and return retvalue 'found'
 *
 * We assume the people calling us have chdir(/var/db/pkg) and so
 * we use relative paths throughout here.
 */
static int qfile_cb(tree_pkg_ctx *pkg_ctx, void *priv)
{
	struct qfile_opt_state *state = priv;
	const char *catname = pkg_ctx->cat_ctx->name;
	qfile_args_t *args = &state->args;
	FILE *fp;
	const char *base;
	depend_atom *atom = NULL;
	int i;
	bool path_ok;
	char *real_root = state->real_root;
	char **base_names = args->basenames;
	char **dir_names = args->dirnames;
	char **real_dir_names = args->realdirnames;
	short *non_orphans = args->non_orphans;
	int *results = args->results;
	int found = 0;

	/* If exclude_pkg is not NULL, check it.  We are looking for files
	 * collisions, and must exclude one package. */
	if (state->exclude_pkg) {
		/* see if CATEGORY matches */
		if (state->exclude_atom->CATEGORY &&
		    strcmp(state->exclude_atom->CATEGORY, catname))
			goto dont_skip_pkg;
		atom = tree_get_atom(pkg_ctx,
				state->exclude_atom->SLOT != NULL ||
				state->exclude_atom->REPO != NULL);
		if (atom_compare(atom, state->exclude_atom) != EQUAL)
			goto dont_skip_pkg;
		/* "(CAT/)?(PN|PF)" matches, and no SLOT specified */
		if (state->exclude_slot == NULL)
			goto qlist_done;
		/* retrieve atom, this time with SLOT */
		atom = tree_get_atom(pkg_ctx, true);
		if (strcmp(state->exclude_slot, atom->SLOT) == 0)
			goto qlist_done; /* "(CAT/)?(PN|PF):SLOT" matches */
	}
 dont_skip_pkg: /* End of the package exclusion tests. */

	fp = tree_pkg_vdb_fopenat_ro(pkg_ctx, "CONTENTS");
	if (fp == NULL)
		goto qlist_done;

	/* Run through CONTENTS file */
	while (getline(&state->buf, &state->buflen, fp) != -1) {
		size_t dirname_len;
		contents_entry *e;

		e = contents_parse_line(state->buf);
		if (!e)
			continue;

		/* note: this is our own basename which doesn't modify its input */
		base = basename(e->name);
		if (base < e->name || base > (e->name + strlen(e->name)))
			continue;

		/* basename(/usr)     = usr, dirname(/usr)     = /
		 * basename(/usr/bin) = bin, dirname(/usr/bin) = /usr */
		if ((dirname_len = (base - e->name - 1)) == 0)
			dirname_len = 1;

		for (i = 0; i < args->length; i++) {
			if (base_names[i] == NULL)
				continue;
			if (non_orphans != NULL && non_orphans[i])
				continue;

			/* For optimization of qfile(), we also give it an array of
			 * the first char of each basename.  This way we avoid
			 * numerous strcmp() calls. */
			if (base[0] != base_names[i][0] || strcmp(base, base_names[i]) != 0)
				continue;

			path_ok = false;

			if (dir_names[i] &&
					strncmp(e->name, dir_names[i], dirname_len) == 0 &&
					dir_names[i][dirname_len] == '\0')
			{
				/* dir_name == dirname(CONTENTS) */
				path_ok = true;
			} else if (real_dir_names[i] &&
					strncmp(e->name, real_dir_names[i], dirname_len) == 0 &&
					real_dir_names[i][dirname_len] == '\0')
			{
				/* real_dir_name == dirname(CONTENTS) */
				path_ok = true;
			} else if (real_root[0]) {
				char rpath[_Q_PATH_MAX + 1];
				char *_rpath;
				char fullpath[_Q_PATH_MAX + 1];
				size_t real_root_len = state->real_root_len;

				snprintf(fullpath, sizeof(fullpath), "%s%s",
						real_root, e->name);
				_rpath = rpath + real_root_len;
				if (realpath(fullpath, rpath) == NULL) {
					if (verbose) {
						atom = tree_get_atom(pkg_ctx, false);
						warnp("Could not read real path of \"%s\" (from %s)",
								fullpath,
								atom_format("%[CATEGORY]%[PF]", atom));
						warn("We'll never know whether \"%s\" was a result "
								"for your query...", e->name);
					}
				} else if (!qfile_is_prefix(rpath, real_root, real_root_len)) {
					if (verbose)
						warn("Real path of \"%s\" is not under ROOT: %s",
								fullpath, rpath);
				} else if (dir_names[i] &&
				           strcmp(_rpath, dir_names[i]) == 0) {
					/* dir_name == realpath(dirname(CONTENTS)) */
					path_ok = true;
				} else if (real_dir_names[i] &&
				           strcmp(_rpath, real_dir_names[i]) == 0) {
					/* real_dir_name == realpath(dirname(CONTENTS)) */
					path_ok = true;
				}
			}

			if (!path_ok && state->basename) {
				path_ok = true;
			}

			if (!path_ok && state->pwd && dir_names[i] == NULL) {
				/* try to match file in current directory */
				if (strncmp(e->name, state->pwd, dirname_len) == 0 &&
						state->pwd[dirname_len] == '\0')
					path_ok = true;
			}

			if (!path_ok && dir_names[i] == NULL && real_dir_names[i] == NULL) {
				/* try basename match */
				if (e->type != CONTENTS_DIR)
					path_ok = true;
			}

			if (!path_ok)
				continue;

			if (non_orphans == NULL) {
				atom = tree_get_atom(pkg_ctx, state->need_full_atom);

				printf("%s", atom_format(state->format, atom));
				if (quiet)
					puts("");
				else
					printf(": %s%s\n", state->root ? : "", e->name);
			} else {
				non_orphans[i] = 1;
			}

			/* Success */
			results[i] = 1;
			found++;
		}
	}
	fclose(fp);

 qlist_done:
	return found;
}

static void destroy_qfile_args(qfile_args_t *qfile_args)
{
	int i;

	for (i = 0; i < qfile_args->length; ++i) {
		if (qfile_args->basenames)
			free(qfile_args->basenames[i]);
		if (qfile_args->dirnames)
			free(qfile_args->dirnames[i]);
		if (qfile_args->realdirnames)
			free(qfile_args->realdirnames[i]);
	}

	free(qfile_args->basenames);
	free(qfile_args->dirnames);
	free(qfile_args->realdirnames);
	free(qfile_args->non_orphans);
	free(qfile_args->results);

	memset(qfile_args, 0, sizeof(qfile_args_t));
}

static int
prepare_qfile_args(const int argc, const char **argv, struct qfile_opt_state *state)
{
	qfile_args_t *args = &state->args;
	int i;
	int nb_of_queries = argc;
	char *pwd = state->pwd;
	const char *p;
	size_t real_root_len = state->real_root_len;
	size_t len;
	char *real_root = state->real_root;
	char **basenames = NULL;
	char **dirnames = NULL;
	char **realdirnames = NULL;
	int *results = NULL;
	char tmppath[_Q_PATH_MAX];
	char abspath[_Q_PATH_MAX];

	/* For each argument, we store its basename, its absolute dirname,
	 * and the realpath of its dirname.  Dirnames and their realpaths
	 * are stored without their $ROOT prefix, but $ROOT is used when
	 * checking realpaths.
	 */
	basenames = xcalloc(argc, sizeof(char*));
	dirnames = xcalloc(argc, sizeof(char*));
	realdirnames = xcalloc(argc, sizeof(char*));
	results = xcalloc(argc, sizeof(int));

	for (i = 0; i < argc; ++i) {
		/* Record basename, but if it is ".", ".." or "/" */
		/* copy so that "argv" can be "const" */
		snprintf(tmppath, sizeof(tmppath), "%s", argv[i]);
		p = basename(tmppath);
		len = strlen(p);
		if ((len > 2) ||
		    (strncmp(tmppath, "..", len) != 0 &&
		     strncmp(tmppath, "/", len) != 0))
		{
			basenames[i] = xstrdup(p);
			/* If there is no "/" in the argument, then it's over.
			 * (we are searching a simple file name) */
			if (strchr(argv[i], '/') == NULL)
				continue;
		}

		/* Make sure we have an absolute path available (with
		 * "realpath(ROOT)" prefix) */
		if (argv[i][0] == '/') {
			snprintf(abspath, sizeof(abspath), "%s%s",
					state->assume_root_prefix ? "" : real_root, argv[i]);
		} else if (pwd) {
			if (state->assume_root_prefix)
				snprintf(abspath, sizeof(abspath), "%s/%s", pwd, argv[i]);
			else
				snprintf(abspath, sizeof(abspath), "%s%s/%s",
						real_root, pwd, argv[i]);
		} else {
			warn("$PWD was not found in environment, "
					"or is not an absolute path");
			goto skip_query_item;
		}

		if (basenames[i] != NULL) {
			/* Get both the dirname and its realpath.  These paths will
			 * have no trailing slash, except if it is the only char (ie.,
			 * when searching for "/foobar"). */
			snprintf(tmppath, sizeof(tmppath), "%s%s",
					dirname(abspath),
					abspath[real_root_len] == '\0' ? "/" : "");
			dirnames[i] = xstrdup(tmppath + real_root_len);
			if (realpath(tmppath, abspath) == NULL) {
				if (verbose) {
					warnp("Could not read real path of \"%s\"", tmppath);
					warn("Results for query item \"%s\" may be inaccurate.",
							argv[i]);
				}
				continue;
			}
			if (!qfile_is_prefix(abspath, real_root, real_root_len)) {
				warn("Real path of \"%s\" is not under ROOT: %s",
						tmppath, abspath);
				goto skip_query_item;
			}
			snprintf(tmppath, sizeof(tmppath), "%s%s",
					dirname(abspath),
					abspath[real_root_len] == '\0' ? "/" : "");
			if (strcmp(dirnames[i], tmppath + real_root_len))
				realdirnames[i] = xstrdup(tmppath + real_root_len);
		} else {
			/* No basename means we are looking for something like "/foo/bar/.."
			 * Dirname is meaningless here, we can only get realpath of the full
			 * path and then split it. */
			if (realpath(abspath, tmppath) == NULL) {
				warnp("Could not read real path of \"%s\"", abspath);
				goto skip_query_item;
			}
			if (!qfile_is_prefix(tmppath, real_root, real_root_len)) {
				warn("Real path of \"%s\" is not under ROOT: %s",
						abspath, tmppath);
				goto skip_query_item;
			}
			snprintf(abspath, sizeof(abspath), "%s", tmppath);
			basenames[i] = xstrdup(basename(abspath));
			snprintf(abspath, sizeof(abspath), "%s%s",
					dirname(tmppath),
					tmppath[real_root_len] == '\0' ? "/" : "");
			realdirnames[i] = xstrdup(abspath + real_root_len);
		}
		continue;

		skip_query_item:
			--nb_of_queries;
			warn("Skipping query item \"%s\".", argv[i]);
			free(basenames[i]);
			free(dirnames[i]);
			free(realdirnames[i]);
			basenames[i] = dirnames[i] = realdirnames[i] = NULL;
	}

	args->basenames = basenames;
	args->dirnames = dirnames;
	args->realdirnames = realdirnames;
	args->length = argc;
	args->results = results;

	if (state->orphans)
		args->non_orphans = xcalloc(argc, sizeof(short));

	return nb_of_queries;
}

int qfile_main(int argc, char **argv)
{
	struct qfile_opt_state state = {
		.buflen = _Q_PATH_MAX,
		.need_full_atom = false,
		.basename = false,
		.orphans = false,
		.assume_root_prefix = false,
		.skip_plibreg = false,
		.format = NULL,
	};
	int i, nb_of_queries, found = 0;
	char *p;

	while ((i = GETOPT_LONG(QFILE, qfile, "")) != -1) {
		switch (i) {
			COMMON_GETOPTS_CASES(qfile)
			case 'F': state.format = optarg;            /* fall through */
			case 'S': state.need_full_atom = true;      break;
			case 'd': state.basename = true;            break;
			case 'o': state.orphans = true;             break;
			case 'R': state.assume_root_prefix = true;  break;
			case 'P': state.skip_plibreg = true; break;
			case 'x':
				if (state.exclude_pkg)
					err("--exclude can only be used once.");
				state.exclude_pkg = xstrdup(optarg);
				state.exclude_slot = strchr(state.exclude_pkg, ':');
				if (state.exclude_slot != NULL)
					*state.exclude_slot++ = '\0';
				state.exclude_atom = atom_explode(optarg);
				if (!state.exclude_atom)
					err("invalid atom %s", optarg);
				break;
		}
	}
	if (argc == optind)
		qfile_usage(EXIT_FAILURE);

	argc -= optind;
	argv += optind;

	if (state.format == NULL) {
		if (state.need_full_atom)
			if (verbose)
				state.format = "%[CATEGORY]%[PF]%[SLOT]";
			else
				state.format = "%[CATEGORY]%[PN]%[SLOT]";
		else
			if (verbose)
				state.format = "%[CATEGORY]%[PF]";
			else
				state.format = "%[CATEGORY]%[PN]";
	}

	state.buf = xmalloc(state.buflen);
	if (state.assume_root_prefix) {
		/* Get a copy of $ROOT, with no trailing slash
		 * (this one is just for qfile(...) output) */
		size_t lastc = strlen(portroot) - 1;
		state.root = xstrdup(portroot);
		if (state.root[lastc] == '/')
			state.root[lastc] = '\0';
	}

	/* Try to get $PWD. Must be absolute, with no trailing slash. */
	state.pwd = getcwd(state.buf, state.buflen);
	if (state.pwd) {
		size_t lastc = strlen(state.pwd) - 1;
		state.pwd = xstrdup(state.pwd);
		if (state.pwd[lastc] == '/')
			state.pwd[lastc] = '\0';
	}

	/* Get realpath of $ROOT, with no trailing slash */
	if (portroot[0] == '/')
		p = realpath(portroot, NULL);
	else if (state.pwd) {
		snprintf(state.buf, state.buflen, "%s/%s", state.pwd, portroot);
		p = realpath(state.buf, NULL);
	} else
		p = NULL;
	if (p == NULL)
		errp("Could not read real path of ROOT (\"%s\") + $PWD", portroot);
	if (!strcmp(p, "/"))
		*p = '\0';
	state.real_root = p;
	state.real_root_len = strlen(p);

	/* Prepare the qfile(...) arguments structure */
	nb_of_queries = prepare_qfile_args(argc, (const char **) argv, &state);

	/* Now do the actual `qfile` checking by looking at CONTENTS of all pkgs */
	if (nb_of_queries > 0) {
		tree_ctx *vdb = tree_open_vdb(portroot, portvdb);
		if (vdb != NULL) {
			found += tree_foreach_pkg_sorted(vdb, qfile_cb, &state);
			tree_close(vdb);
		}
	}

	/* Also check the prune lib registry.
	 * But only for files we otherwise couldn't account for. If we'd check
	 * plib_reg for all files, we would get duplicate messages for files that were
	 * re-added to CONTENTS files after a version upgrade (which are also recorded
	 * in plib_reg). */
	if (nb_of_queries > 0 && !state.skip_plibreg)
		found += qfile_check_plibreg(&state);

	if (state.args.non_orphans) {
		/* display orphan files */
		for (i = 0; i < state.args.length; i++) {
			if (state.args.non_orphans[i])
				continue;
			if (state.args.basenames[i]) {
				/* inverse return code (as soon as an orphan is found,
				 * return non-zero) */
				found = 0;
				if (!quiet)
					puts(argv[i]);
				else
					break;
			}
		}
	}

	destroy_qfile_args(&state.args);
	free(state.buf);
	free(state.root);
	free(state.real_root);
	free(state.pwd);
	if (state.exclude_pkg) {
		free(state.exclude_pkg);
		/* don't free state.exclude_slot as it's a pointer into exclude_pkg */
		atom_implode(state.exclude_atom);
	}

	return (found ? EXIT_SUCCESS : EXIT_FAILURE);
}
