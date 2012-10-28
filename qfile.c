/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qfile.c,v 1.62 2012/10/28 07:56:51 vapier Exp $
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2010 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qfile

#define QFILE_MAX_MAX_ARGS 5000000
#define QFILE_DEFAULT_MAX_ARGS 5000
#define QFILE_DEFAULT_MAX_ARGS_STR "5000"

#define QFILE_FLAGS "ef:m:oRx:S" COMMON_FLAGS
static struct option const qfile_long_opts[] = {
	{"exact",       no_argument, NULL, 'e'},
	{"from",        a_argument,  NULL, 'f'},
	{"max-args",    a_argument,  NULL, 'm'},
	{"orphans",     no_argument, NULL, 'o'},
	{"root-prefix", no_argument, NULL, 'R'},
	{"exclude",     a_argument,  NULL, 'x'},
	{"slots",       no_argument, NULL, 'S'},
	COMMON_LONG_OPTS
};
static const char * const qfile_opts_help[] = {
	"Exact match",
	"Read arguments from file <arg> (\"-\" for stdin)",
	"Treat from file arguments by groups of <arg> (defaults to " QFILE_DEFAULT_MAX_ARGS_STR ")",
	"List orphan files",
	"Assume arguments are already prefixed by $ROOT",
	"Don't look in package <arg>",
	"Display installed packages with slots",
	COMMON_OPTS_HELP
};
static const char qfile_rcsid[] = "$Id: qfile.c,v 1.62 2012/10/28 07:56:51 vapier Exp $";
#define qfile_usage(ret) usage(ret, QFILE_FLAGS, qfile_long_opts, qfile_opts_help, lookup_applet_idx("qfile"))

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
	char *bn_firstchars;
	short *non_orphans;
	char *real_root;
	char *exclude_pkg;
	char *exclude_slot;
} qfile_args_t;

struct qfile_opt_state {
	char *buf;
	size_t buflen;
	qfile_args_t *args;
	char *root;
	bool slotted;
	bool exact;
};

/*
 * We assume the people calling us have chdir(/var/db/pkg) and so
 * we use relative paths throughout here.
 */
_q_static int qfile_cb(q_vdb_pkg_ctx *pkg_ctx, void *priv)
{
	struct qfile_opt_state *state = priv;
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	qfile_args_t *args = state->args;
	FILE *fp;
	const char *base;
	char pkg[_Q_PATH_MAX];
	depend_atom *atom = NULL;
	int i, path_ok;
	char bn_firstchar;
	char *real_root = args->real_root;
	char **base_names = args->basenames;
	char **dir_names = args->dirnames;
	char **real_dir_names = args->realdirnames;
	char *bn_firstchars = args->bn_firstchars;
	short *non_orphans = args->non_orphans;
	int found = 0;

	snprintf(pkg, sizeof(pkg), "%s/%s", catname, pkgname);

	/* If exclude_pkg is not NULL, check it.  We are looking for files
	 * collisions, and must exclude one package.
	 */
	if (args->exclude_pkg) {
		if (strcmp(args->exclude_pkg, pkg) == 0)
			goto check_pkg_slot; /* CAT/PF matches */
		if (strcmp(args->exclude_pkg, pkg_ctx->name) == 0)
			goto check_pkg_slot; /* PF matches */
		if ((atom = atom_explode(pkg)) == NULL) {
			warn("invalid atom %s", pkg);
			goto dont_skip_pkg;
		}
		snprintf(state->buf, state->buflen, "%s/%s", atom->CATEGORY, atom->PN);
		if (strncmp(args->exclude_pkg, state->buf, state->buflen) != 0
				&& strcmp(args->exclude_pkg, atom->PN) != 0)
			goto dont_skip_pkg; /* "(CAT/)?PN" doesn't match */
 check_pkg_slot: /* Also compare slots, if any was specified */
		if (args->exclude_slot == NULL)
			goto qlist_done; /* "(CAT/)?(PN|PF)" matches, and no SLOT specified */
		eat_file_at(pkg_ctx->fd, "SLOT", state->buf, state->buflen);
		rmspace(state->buf);
		if (strcmp(args->exclude_slot, state->buf) == 0)
			goto qlist_done; /* "(CAT/)?(PN|PF):SLOT" matches */
	}
 dont_skip_pkg: /* End of the package exclusion tests. */

	fp = q_vdb_pkg_fopenat_ro(pkg_ctx, "CONTENTS");
	if (fp == NULL)
		goto qlist_done;

	while (getline(&state->buf, &state->buflen, fp) != -1) {
		contents_entry *e;
		e = contents_parse_line(state->buf);
		if (!e)
			continue;

		/* assume sane basename() -- doesnt modify argument */
		if ((base = basename(e->name)) == NULL)
			continue;

		/* used to cut the number of strcmp() calls */
		bn_firstchar = base[0];

		for (i = 0; i < args->length; i++) {
			if (base_names[i] == NULL)
				continue;
			if (non_orphans && non_orphans[i])
				continue;
			path_ok = (dir_names[i] == NULL && real_dir_names[i] == NULL);

			if (bn_firstchar != bn_firstchars[i]
					|| strcmp(base, base_names[i]))
				continue;

			if (!path_ok) {
				/* check the full filepath ... */
				size_t dirname_len = (base - e->name - 1);
				/* basename(/usr)     = usr, dirname(/usr)     = /
				 * basename(/usr/bin) = bin, dirname(/usr/bin) = /usr
				 */
				if (dirname_len == 0)
					dirname_len = 1;

				if (dir_names[i] &&
				    strncmp(e->name, dir_names[i], dirname_len) == 0 &&
				    dir_names[i][dirname_len] == '\0')
					/* dir_name == dirname(CONTENTS) */
					path_ok = 1;

				else if (real_dir_names[i] &&
				         strncmp(e->name, real_dir_names[i], dirname_len) == 0 &&
				         real_dir_names[i][dirname_len] == '\0')
					/* real_dir_name == dirname(CONTENTS) */
					path_ok = 1;

				else if (real_root[0]) {
					char rpath[_Q_PATH_MAX + 1], *_rpath;
					char *fullpath;
					size_t real_root_len = strlen(real_root);

					xasprintf(&fullpath, "%s%s", real_root, e->name);
					fullpath[real_root_len + dirname_len] = '\0';
					_rpath = rpath + real_root_len;
					if (realpath(fullpath, rpath) == NULL) {
						if (verbose) {
							warnp("Could not read real path of \"%s\" (from %s)", fullpath, pkg);
							warn("We'll never know whether \"%s\" was a result for your query...",
									e->name);
						}
					} else if (!qfile_is_prefix(rpath, real_root, real_root_len)) {
						if (verbose)
							warn("Real path of \"%s\" is not under ROOT: %s", fullpath, rpath);
					} else if (dir_names[i] &&
					           strcmp(_rpath, dir_names[i]) == 0) {
						/* dir_name == realpath(dirname(CONTENTS)) */
						path_ok = 1;
					} else if (real_dir_names[i] &&
					           strcmp(_rpath, real_dir_names[i]) == 0) {
						/* real_dir_name == realpath(dirname(CONTENTS)) */
						path_ok = 1;
					}
					free(fullpath);
				}
			}
			if (!path_ok)
				continue;

			if (non_orphans == NULL) {
				char slot[126];

				if (!atom) {
					if ((atom = atom_explode(pkg)) == NULL) {
						warn("invalid atom %s", pkg);
						continue;
					}
				}
				if (state->slotted) {
					eat_file_at(pkg_ctx->fd, "SLOT", slot+1, sizeof(slot)-1);
					rmspace(slot+1);
					slot[0] = ':';
				} else
					slot[0] = '\0';
				printf("%s%s/%s%s%s%s", BOLD, atom->CATEGORY, BLUE,
					(state->exact ? pkg_ctx->name : atom->PN),
					slot, NORM);
				if (quiet)
					puts("");
				else
					printf(" (%s%s)\n", state->root ? : "", e->name);

			} else {
				non_orphans[i] = 1;
			}
			found++;
		}
	}
	fclose(fp);

 qlist_done:
	if (atom)
		atom_implode(atom);

	return found;
}

_q_static void destroy_qfile_args(qfile_args_t *qfile_args)
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
	free(qfile_args->bn_firstchars);
	free(qfile_args->non_orphans);
	free(qfile_args->real_root);
	free(qfile_args->exclude_pkg);
	/* don't free qfile_args->exclude_slot, it's the same chunk */

	memset(qfile_args, 0, sizeof(qfile_args_t));
}

_q_static int
prepare_qfile_args(const int argc, const char **argv,
	bool assume_root_prefix, bool search_orphans,
	const char *exclude_pkg_arg, qfile_args_t *qfile_args)
{
	int i;
	int nb_of_queries = argc;
	char *pwd = NULL;
	int real_root_length;
	char *real_root = NULL;
	char **basenames = NULL;
	char **dirnames = NULL;
	char **realdirnames = NULL;
	char *basenames_firstchars = NULL;
	char tmppath[_Q_PATH_MAX+1];
	char abspath[_Q_PATH_MAX+1];

	/* Try to get $PWD. Must be absolute, with no trailing slash. */
	if ((pwd = getenv("PWD")) != NULL && pwd[0] == '/') {
		pwd = xstrdup(pwd);
		if (pwd[strlen(pwd) - 1] == '/')
			pwd[strlen(pwd) - 1] = '\0';
	} else
		pwd = NULL;

	/* Get realpath of $ROOT, with no trailing slash */
	if (portroot[0] == '/')
		strncpy(tmppath, portroot, _Q_PATH_MAX);
	else if (pwd)
		snprintf(tmppath, _Q_PATH_MAX, "%s/%s", pwd, portroot);
	else {
		free(pwd);
		warn("Could not get absolute path for ROOT (\"%s\"), because of missing or not absolute $PWD", tmppath);
		return -1;
	}
	if (realpath(tmppath, abspath) == NULL) {
		free(pwd);
		warnp("Could not read real path of ROOT (\"%s\")", tmppath);
		return -1;
	}
	if (strlen(abspath) == 1)
		abspath[0] = '\0';
	real_root = xstrdup(abspath);
	real_root_length = strlen(real_root);

	/* For each argument, we store its basename, its absolute dirname,
	 * and the realpath of its dirname.  Dirnames and their realpaths
	 * are stored without their $ROOT prefix, but $ROOT is used when
	 * checking realpaths.
	 */
	basenames = xcalloc(argc, sizeof(char*));
	dirnames = xcalloc(argc, sizeof(char*));
	realdirnames = xcalloc(argc, sizeof(char*));
	/* For optimization of qfile(), we also give it an array of the first char
	 * of each basename.  This way we avoid numerous strcmp() calls.
	 */
	basenames_firstchars = xcalloc(argc, sizeof(char));

	for (i = 0; i < argc; ++i) {
		/* Record basename, but if it is ".", ".." or "/" */
		strncpy(abspath, argv[i], _Q_PATH_MAX); /* strncopy so that "argv" can be "const" */
		strncpy(tmppath, basename(abspath), _Q_PATH_MAX);
		if ((strlen(tmppath) > 2) ||
		    (strncmp(tmppath, "..", strlen(tmppath))
		     && strncmp(tmppath, "/", strlen(tmppath))))
		{
			basenames[i] = xstrdup(tmppath);
			basenames_firstchars[i] = basenames[i][0];
			/* If there is no "/" in the argument, then it's over.
			 * (we are searching a simple file name)
			 */
			if (strchr(argv[i], '/') == NULL)
				continue;
		}

		/* Make sure we have an absolute path available (with "realpath(ROOT)" prefix) */
		if (argv[i][0] == '/') {
			if (assume_root_prefix)
				strncpy(abspath, argv[i], _Q_PATH_MAX);
			else
				snprintf(abspath, _Q_PATH_MAX, "%s%s", real_root, argv[i]);
		} else if (pwd) {
			if (assume_root_prefix)
				snprintf(abspath, _Q_PATH_MAX, "%s/%s", pwd, argv[i]);
			else
				snprintf(abspath, _Q_PATH_MAX, "%s%s/%s", real_root, pwd, argv[i]);
		} else {
			warn("$PWD was not found in environment, or is not an absolute path");
			goto skip_query_item;
		}

		if (basenames[i]) {
			/* Get both the dirname and its realpath.  This paths will
			 * have no trailing slash, but if it is the only char (ie.,
			 * when searching for "/foobar").
			 */
			strncpy(tmppath, abspath, _Q_PATH_MAX);
			strncpy(abspath, dirname(tmppath), _Q_PATH_MAX);
			if (abspath[real_root_length] == '\0')
				strncat(abspath, "/", 1);
			dirnames[i] = xstrdup(abspath + real_root_length);
			if (realpath(abspath, tmppath) == NULL) {
				if (verbose) {
					warnp("Could not read real path of \"%s\"", abspath);
					warn("Results for query item \"%s\" may be inaccurate.", argv[i]);
				}
				continue;
			}
			if (!qfile_is_prefix(tmppath, real_root, real_root_length)) {
				warn("Real path of \"%s\" is not under ROOT: %s", abspath, tmppath);
				goto skip_query_item;
			}
			if (tmppath[real_root_length] == '\0')
				strncat(tmppath, "/", 1);
			if (strcmp(dirnames[i], tmppath + real_root_length))
				realdirnames[i] = xstrdup(tmppath + real_root_length);
		} else {
			/* No basename means we are looking for something like "/foo/bar/.."
			 * Dirname is meaningless here, we can only get realpath of the full
			 * path and then split it.
			 */
			if (realpath(abspath, tmppath) == NULL) {
				warnp("Could not read real path of \"%s\"", abspath);
				goto skip_query_item;
			}
			if (!qfile_is_prefix(tmppath, real_root, real_root_length)) {
				warn("Real path of \"%s\" is not under ROOT: %s", abspath, tmppath);
				goto skip_query_item;
			}
			strncpy(abspath, tmppath, _Q_PATH_MAX);
			basenames[i] = xstrdup(basename(abspath));
			basenames_firstchars[i] = basenames[i][0];
			strncpy(abspath, dirname(tmppath), _Q_PATH_MAX);
			if (tmppath[real_root_length] == '\0')
				strncat(tmppath, "/", 1);
			realdirnames[i] = xstrdup(abspath + real_root_length);
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

	free(pwd);

	qfile_args->real_root = real_root;
	qfile_args->basenames = basenames;
	qfile_args->dirnames = dirnames;
	qfile_args->realdirnames = realdirnames;
	qfile_args->bn_firstchars = basenames_firstchars;
	qfile_args->length = argc;

	if (search_orphans) {
		qfile_args->non_orphans = xcalloc(argc, sizeof(short));
		memset(qfile_args->non_orphans, 0, argc);
	}

	if (exclude_pkg_arg) {
		qfile_args->exclude_pkg = xstrdup(exclude_pkg_arg);
		if ((qfile_args->exclude_slot = strchr(qfile_args->exclude_pkg, ':')) != NULL)
			*qfile_args->exclude_slot++ = '\0';
		/* Maybe this should be atom-exploded instead (to check syntax, etc.) */
	}

	return nb_of_queries;
}

int qfile_main(int argc, char **argv)
{
	struct qfile_opt_state state = {
		.buflen = _Q_PATH_MAX,
		.slotted = false,
		.exact = false,
	};
	int i, nb_of_queries, found = 0;
	char *p;
	bool search_orphans = false;
	bool assume_root_prefix = false;
	char *exclude_pkg_arg = NULL;
	int qargc = 0;
	char **qargv = NULL;
	FILE *args_file = NULL;
	int max_args = QFILE_DEFAULT_MAX_ARGS;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QFILE, qfile, "")) != -1) {
		switch (i) {
			COMMON_GETOPTS_CASES(qfile)
			case 'S': state.slotted = true; break;
			case 'e': state.exact = true; break;
			case 'f':
				if (args_file)
					err("Don't use -f twice!");
				if (strcmp(optarg, "-") == 0)
					args_file = stdin;
				else if ((args_file = fopen(optarg, "r")) == NULL) {
					warnp("%s", optarg);
					goto exit;
				}
				break;
			case 'm':
				errno = 0;
				max_args = strtol(optarg, &p, 10);
				if (errno != 0) {
					warnp("%s: not a valid integer", optarg);
					goto exit;
				} else if (p == optarg || *p != '\0') {
					warn("%s: not a valid integer", optarg);
					goto exit;
				}
				if (max_args <= 0 || max_args > QFILE_MAX_MAX_ARGS) {
					warn("%s: silly value!", optarg);
					goto exit;
				}
				break;
			case 'o': search_orphans = true; break;
			case 'R': assume_root_prefix = true; break;
			case 'x':
				if (exclude_pkg_arg)
					err("--exclude can only be used once.");
				exclude_pkg_arg = optarg;
				break;
		}
	}
	if (!state.exact && verbose)
		state.exact = true;
	if ((argc == optind) && (args_file == NULL))
		qfile_usage(EXIT_FAILURE);

	if ((args_file == NULL) && (max_args != QFILE_DEFAULT_MAX_ARGS))
		warn("--max-args is only used when reading arguments from a file (with -f)");

	/* Are we using --from ? */
	if (args_file == NULL) {
		qargc = argc - optind;
		qargv = argv + optind;
	} else {
		qargc = 0;
		qargv = xcalloc(max_args, sizeof(char*));
	}

	state.buf = xmalloc(state.buflen);
	state.args = xzalloc(sizeof(qfile_args_t));
	if (assume_root_prefix) {
		/* Get a copy of $ROOT, with no trailing slash
		 * (this one is just for qfile(...) output)
		 */
		size_t lastc = strlen(portroot) - 1;
		state.root = xstrdup(portroot);
		if (state.root[lastc] == '/')
			state.root[lastc] = '\0';
	}

	do { /* This block may be repeated if using --from with a big files list */
		if (args_file) {
			/* Read up to max_args files from the input file */
			for (i = 0; i < qargc; ++i)
				free(qargv[i]);
			qargc = 0;
			while (getline(&state.buf, &state.buflen, args_file) != -1) {
				if ((p = strchr(state.buf, '\n')) != NULL)
					*p = '\0';
				if (state.buf == p)
					continue;
				qargv[qargc] = xstrdup(state.buf);
				if (++qargc >= max_args)
					break;
			}
		}
		if (qargc == 0)
			break;

		/* Prepare the qfile(...) arguments structure */
		nb_of_queries = prepare_qfile_args(qargc, (const char **) qargv,
				assume_root_prefix, search_orphans, exclude_pkg_arg, state.args);
		if (nb_of_queries < 0)
			break;

		if (nb_of_queries)
			found += q_vdb_foreach_pkg(qfile_cb, &state, NULL);

		if (state.args->non_orphans) {
			/* display orphan files */
			for (i = 0; i < state.args->length; i++) {
				if (state.args->non_orphans[i])
					continue;
				if (state.args->basenames[i]) {
					found = 0; /* ~inverse return code (as soon as an orphan is found, return non-zero) */
					if (!quiet)
						puts(qargv[i]);
					else
						break;
				}
			}
		}

		destroy_qfile_args(state.args);
	} while (args_file && qargc == max_args);

 exit:
	if (args_file) {
		if (qargv) {
			for (i = 0; i < qargc; ++i)
				free(qargv[i]);
			free(qargv);
		}

		if (args_file != stdin)
			fclose(args_file);
	}

	destroy_qfile_args(state.args);
	free(state.buf);
	free(state.args);
	free(state.root);

	return (found ? EXIT_SUCCESS : EXIT_FAILURE);
}

#else
DEFINE_APPLET_STUB(qfile)
#endif
