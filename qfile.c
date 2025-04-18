/*
 * Copyright 2005-2025 Gentoo Foundation
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
#include <sys/stat.h>

#include "atom.h"
#include "basename.h"
#include "contents.h"
#include "rmspace.h"
#include "tree.h"

#define QFILE_FLAGS "F:LdoRx:SP" COMMON_FLAGS
static struct option const qfile_long_opts[] = {
	{"format",       a_argument, NULL, 'F'},
	{"follow",      no_argument, NULL, 'L'},
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
	"Follow symlinks in CONTENTS entries (slower)",
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
	char   *str;
	size_t  len;
} qfile_str_len_t;

typedef struct {
	size_t           length;
	qfile_str_len_t *basenames;
	qfile_str_len_t *dirnames;
	qfile_str_len_t *realdirnames;
	short           *non_orphans;
	int             *results;
} qfile_args_t;

struct qfile_opt_state {
	char *buf;
	size_t buflen;
	qfile_args_t args;
	char *root;
	char *pwd;
	size_t pwd_len;
	char *real_root;
	size_t real_root_len;
	char *exclude_pkg;
	char *exclude_slot;
	depend_atom *exclude_atom;
	bool basename;
	bool followlinks;
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
	struct qfile_opt_state *state       = priv;
	qfile_args_t           *args        = &state->args;
	qfile_str_len_t        *base_names  = args->basenames;
	qfile_str_len_t        *dir_names   = args->dirnames;
	short                  *non_orphans = args->non_orphans;
	int                    *results     = args->results;

	char fn_plibreg[_Q_PATH_MAX];
	int fd_plibreg;
	FILE *fp_plibreg;
	struct stat cst;

	char file[_Q_PATH_MAX];
	char *line = NULL;
	size_t len = 0;
	int found = 0;
	size_t i;

	/* Open plibreg */
	snprintf(fn_plibreg, _Q_PATH_MAX, "%s%s",
			CONFIG_EPREFIX, "var/lib/portage/preserved_libs_registry");
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

	for (i = 0; i < args->length; i++) {
		if (base_names[i].len == 0)
			continue;
		if (non_orphans && non_orphans[i])
			continue;
		if (results[i] == 1)
			continue;

		if (dir_names[i].len > 0)
			snprintf(file, sizeof(file), "%s/%s",
					 dir_names[i].str, base_names[i].str);
		else
			snprintf(file, sizeof(file), "%s", base_names[i].str);

		while (getline(&line, &len, fp_plibreg) != -1)
			if (strstr(line, file) != NULL) {
				found++;
				if (!quiet)
					printf("%splib_registry%s\n", BLUE, NORM);
				else
					printf("%splib_registry%s: %s\n", BLUE, NORM, file);
			}
	}

	if (line)
		free(line);
	fclose(fp_plibreg);

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
	struct qfile_opt_state *state          = priv;
	const char             *catname        = pkg_ctx->cat_ctx->name;
	qfile_args_t           *args           = &state->args;
	qfile_str_len_t        *base_names     = args->basenames;
	qfile_str_len_t        *dir_names      = args->dirnames;
	qfile_str_len_t        *real_dir_names = args->realdirnames;
	short                  *non_orphans    = args->non_orphans;
	int                    *results        = args->results;
	const char             *_rpath;
	char                    rpath[_Q_PATH_MAX + 1];
	char                    fullpath[_Q_PATH_MAX + 1];
	char *line;
	char *savep;
	const char *base;
	depend_atom *atom = NULL;
	size_t i;
	bool path_ok;
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

	line = tree_pkg_meta_get(pkg_ctx, CONTENTS);
	if (line == NULL)
		goto qlist_done;

	/* Run through CONTENTS file */
	for (; (line = strtok_r(line, "\n", &savep)) != NULL; line = NULL) {
		size_t dirname_len;
		contents_entry *e;

		e = contents_parse_line(line);
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
			if (base_names[i].len == 0)
				continue;
			if (non_orphans != NULL && non_orphans[i])
				continue;

			/* Try to avoid numerous strcmp() calls. */
			if (base[0] != base_names[i].str[0] ||
				strcmp(base, base_names[i].str) != 0)
				continue;

			path_ok = false;

			if (state->followlinks) {
				snprintf(fullpath, sizeof(fullpath), "%s%s",
						 state->real_root, e->name);
				if (realpath(fullpath, rpath) == NULL) {
					if (verbose) {
						atom = tree_get_atom(pkg_ctx, false);
						warnp("Could not read real path of \"%s\" (from %s)",
								fullpath,
								atom_format("%[CATEGORY]%[PF]", atom));
						warn("We'll never know whether \"%s\" was a result "
								"for your query...", e->name);
					}
					break;
				}
				if (state->real_root_len > 0 &&
					!qfile_is_prefix(rpath,
									 state->real_root, state->real_root_len))
				{
					if (verbose)
						warn("Real path of \"%s\" is not under ROOT: %s",
								fullpath, rpath);
					break;
				}
				_rpath = rpath + state->real_root_len;
				dirname_len = strlen(_rpath) - 1 - base_names[i].len;
			} else {
				_rpath = e->name;
			}

			if (real_dir_names[i].len == dirname_len &&
				memcmp(_rpath, real_dir_names[i].str,
					   real_dir_names[i].len) == 0)
			{
				/* real_dir_name == dirname(CONTENTS) */
				path_ok = true;
			}
			else if ((!state->followlinks ||
					  real_dir_names[i].len == 0) &&
					 dir_names[i].len == dirname_len &&
					 memcmp(_rpath, dir_names[i].str, dir_names[i].len) == 0)
			{
				/* dir_name == dirname(CONTENTS) */
				path_ok = true;
			}

			if (!path_ok && state->basename)
				path_ok = true;

			if (!path_ok && state->pwd && dir_names[i].len == 0) {
				/* try to match file in current directory */
				if (state->pwd_len > 0 &&
					state->pwd_len == dirname_len &&
					memcmp(e->name, state->pwd, state->pwd_len) == 0)
					path_ok = true;
			}

			if (!path_ok && dir_names[i].len == 0 &&
				real_dir_names[i].len == 0)
			{
				/* basename match */
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
				else if (verbose && e->type == CONTENTS_SYM)
					printf(": %s%s -> %s\n",
							state->root ? state->root : "",
							e->name, e->sym_target);
				else
					printf(": %s%s\n", state->root ? state->root : "", e->name);
			} else {
				non_orphans[i] = 1;
			}

			/* Success */
			results[i] = 1;
			found++;
		}
	}

 qlist_done:
	return found;
}

static void destroy_qfile_args(qfile_args_t *qfile_args)
{
	size_t i;

	for (i = 0; i < qfile_args->length; ++i) {
		if (qfile_args->basenames[i].len > 0)
			free(qfile_args->basenames[i].str);
		if (qfile_args->dirnames[i].len > 0)
			free(qfile_args->dirnames[i].str);
		if (qfile_args->realdirnames[i].len > 0)
			free(qfile_args->realdirnames[i].str);
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
	qfile_str_len_t *basenames = NULL;
	qfile_str_len_t *dirnames = NULL;
	qfile_str_len_t *realdirnames = NULL;
	int *results = NULL;
	char tmppath[_Q_PATH_MAX];
	char abspath[_Q_PATH_MAX * 2];

	/* For each argument, we store its basename, its absolute dirname,
	 * and the realpath of its dirname.  Dirnames and their realpaths
	 * are stored without their $ROOT prefix, but $ROOT is used when
	 * checking realpaths.
	 */
	basenames = xcalloc(argc, sizeof(basenames[0]));
	dirnames = xcalloc(argc, sizeof(dirnames[0]));
	realdirnames = xcalloc(argc, sizeof(realdirnames[0]));
	results = xcalloc(argc, sizeof(int));

	for (i = 0; i < argc; ++i) {
		/* copy so that "argv" can be "const", but skip trailing /
		 * because our basename doesn't modify its input */
		len = strlen(argv[i]);
		if (len > 1 && argv[i][len - 1] == '/')
			len--;
		snprintf(tmppath, sizeof(tmppath), "%.*s", (int)len, argv[i]);
		p = basename(tmppath);

		/* record basename, but if it is ".", ".." or "/" */
		if ((len > 2) ||
		    (strncmp(tmppath, "..", len) != 0 &&
		     strncmp(tmppath, "/", len) != 0))
		{
			basenames[i].str = xstrdup(p);
			basenames[i].len = strlen(p);
			/* If there is no "/" in the argument, then it's over.
			 * (we are searching a simple file name) */
			if (strchr(argv[i], '/') == NULL)
				continue;
		}

		/* Make sure we have an absolute path available (with
		 * "realpath(ROOT)" prefix) */
		if (tmppath[0] == '/') {
			snprintf(abspath, sizeof(abspath), "%s%s",
					state->assume_root_prefix ? "" : real_root, tmppath);
		} else if (pwd) {
			if (state->assume_root_prefix)
				snprintf(abspath, sizeof(abspath), "%s/%s", pwd, tmppath);
			else
				snprintf(abspath, sizeof(abspath), "%s%s/%s",
						real_root, pwd, tmppath);
		} else {
			warn("$PWD was not found in environment, "
					"or is not an absolute path");
			goto skip_query_item;
		}

		if (basenames[i].len > 0) {
			/* Get both the dirname and its realpath.  These paths will
			 * have no trailing slash, except if it is the only char (ie.,
			 * when searching for "/foobar"). */
			snprintf(tmppath, sizeof(tmppath), "%s%s",
					dirname(abspath),
					abspath[real_root_len] == '\0' ? "/" : "");
			dirnames[i].str = xstrdup(tmppath + real_root_len);
			dirnames[i].len = strlen(dirnames[i].str);
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
			if (strcmp(dirnames[i].str, abspath + real_root_len) != 0)
			{
				realdirnames[i].str = xstrdup(abspath + real_root_len);
				realdirnames[i].len = strlen(realdirnames[i].str);
			}
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
			basenames[i].str = xstrdup(basename(tmppath));
			basenames[i].len = strlen(basenames[i].str);
			snprintf(abspath, sizeof(abspath), "%s%s",
					dirname(tmppath),
					tmppath[real_root_len] == '\0' ? "/" : "");
			realdirnames[i].str = xstrdup(abspath + real_root_len);
			realdirnames[i].len = strlen(realdirnames[i].str);
		}
		continue;

 skip_query_item:
		--nb_of_queries;
		warn("Skipping query item \"%s\".", argv[i]);
		free(basenames[i].str);
		free(dirnames[i].str);
		free(realdirnames[i].str);
		basenames[i].len = dirnames[i].len = realdirnames[i].len = 0;
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
	int i;
	int nb_of_queries;
	int found = 0;
	char *p;

	while ((i = GETOPT_LONG(QFILE, qfile, "")) != -1) {
		switch (i) {
			COMMON_GETOPTS_CASES(qfile)
			case 'F': state.format = optarg;            /* fall through */
			case 'S': state.need_full_atom = true;      break;
			case 'L': state.followlinks = true;         break;
			case 'd': state.basename = true;            break;
			case 'o': state.orphans = true;             break;
			case 'R': state.assume_root_prefix = true;  break;
			case 'P': state.skip_plibreg = true;        break;
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
		if (lastc > 0 && state.root[lastc] == '/')
			state.root[lastc] = '\0';
	}

	/* Try to get $PWD. Must be absolute, with no trailing slash. */
	state.pwd = getcwd(state.buf, state.buflen);
	if (state.pwd) {
		size_t lastc = strlen(state.pwd) - 1;
		state.pwd = xstrdup(state.pwd);
		if (state.pwd[lastc] == '/')
			state.pwd[lastc] = '\0';
		state.pwd_len = strlen(state.pwd);
	}

	/* Get realpath of $ROOT, with no trailing slash */
	if (portroot[0] == '/')
		p = realpath(portroot, NULL);
	else if (state.pwd_len > 0) {
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
			found += tree_foreach_pkg_sorted(vdb, qfile_cb, &state, NULL);
			tree_close(vdb);
		}
	}

	/* Also check the prune lib registry.
	 * But only for files we otherwise couldn't account for. If we'd
	 * check plib_reg for all files, we would get duplicate messages for
	 * files that were re-added to CONTENTS files after a version
	 * upgrade (which are also recorded in plib_reg). */
	if (nb_of_queries > 0 && !state.skip_plibreg)
		found += qfile_check_plibreg(&state);

	if (state.args.non_orphans) {
		size_t j;
		/* display orphan files */
		for (j = 0; j < state.args.length; j++) {
			if (state.args.non_orphans[j])
				continue;
			if (state.args.basenames[j].len > 0) {
				/* inverse return code (as soon as an orphan is found,
				 * return non-zero) */
				found = 0;
				if (!quiet)
					puts(argv[j]);
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
