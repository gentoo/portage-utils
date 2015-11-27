/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qfile

#define QFILE_MAX_MAX_ARGS 5000000
#define QFILE_DEFAULT_MAX_ARGS 5000
#define QFILE_DEFAULT_MAX_ARGS_STR "5000"

#define QFILE_FLAGS "bef:m:oRx:S" COMMON_FLAGS
static struct option const qfile_long_opts[] = {
	{"slots",       no_argument, NULL, 'S'},
	{"root-prefix", no_argument, NULL, 'R'},
	{"from",         a_argument, NULL, 'f'},
	{"max-args",     a_argument, NULL, 'm'},
	{"basename",    no_argument, NULL, 'b'},
	{"orphans",     no_argument, NULL, 'o'},
	{"exclude",      a_argument, NULL, 'x'},
	{"exact",       no_argument, NULL, 'e'},
	COMMON_LONG_OPTS
};
static const char * const qfile_opts_help[] = {
	"Display installed packages with slots",
	"Assume arguments are already prefixed by $ROOT",
	"Read arguments from file <arg> (\"-\" for stdin)",
	"Treat from file arguments by groups of <arg> (defaults to " QFILE_DEFAULT_MAX_ARGS_STR ")",
	"Match any component of the path",
	"List orphan files",
	"Don't look in package <arg> (used with --orphans)",
	"Exact match (used with --exclude)",
	COMMON_OPTS_HELP
};
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
	short *non_orphans;
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
	bool slotted;
	bool basename;
	bool exact;
	bool orphans;
	bool assume_root_prefix;
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
	qfile_args_t *args = &state->args;
	FILE *fp;
	const char *base;
	char pkg[_Q_PATH_MAX];
	depend_atom *atom = NULL;
	int i;
	bool path_ok;
	char *real_root = state->real_root;
	char **base_names = args->basenames;
	char **dir_names = args->dirnames;
	char **real_dir_names = args->realdirnames;
	short *non_orphans = args->non_orphans;
	int found = 0;

	snprintf(pkg, sizeof(pkg), "%s/%s", catname, pkgname);

	/* If exclude_pkg is not NULL, check it.  We are looking for files
	 * collisions, and must exclude one package.
	 */
	if (state->exclude_pkg) {
		/* see if CATEGORY matches */
		if (state->exclude_atom->CATEGORY &&
		    strcmp(state->exclude_atom->CATEGORY, catname))
			goto dont_skip_pkg;
		atom = atom_explode(pkg);
		if (state->exclude_atom->PVR) {
			/* see if PVR is exact match */
			if (strcmp(state->exclude_atom->PVR, atom->PVR))
				goto dont_skip_pkg;
		} else {
			/* see if PN is exact match */
			if (strcmp(state->exclude_atom->PN, atom->PN))
				goto dont_skip_pkg;
		}
		if (state->exclude_slot == NULL)
			goto qlist_done; /* "(CAT/)?(PN|PF)" matches, and no SLOT specified */
		q_vdb_pkg_eat(pkg_ctx, "SLOT", &state->buf, &state->buflen);
		rmspace(state->buf);
		if (strcmp(state->exclude_slot, state->buf) == 0)
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

		for (i = 0; i < args->length; i++) {
			if (base_names[i] == NULL)
				continue;
			if (non_orphans && non_orphans[i])
				continue;

			/* For optimization of qfile(), we also give it an array of the first char
			 * of each basename.  This way we avoid numerous strcmp() calls.
			*/
			if (base[0] != base_names[i][0] || strcmp(base, base_names[i]))
				continue;

			path_ok = false;

			/* check the full filepath ... */
			size_t dirname_len = (base - e->name - 1);
			/* basename(/usr)     = usr, dirname(/usr)     = /
			 * basename(/usr/bin) = bin, dirname(/usr/bin) = /usr
			 */
			if (dirname_len == 0)
				dirname_len = 1;

			if (dir_names[i] &&
			    strncmp(e->name, dir_names[i], dirname_len) == 0 &&
			    dir_names[i][dirname_len] == '\0') {
				/* dir_name == dirname(CONTENTS) */
				path_ok = true;

			} else if (real_dir_names[i] &&
			         strncmp(e->name, real_dir_names[i], dirname_len) == 0 &&
			         real_dir_names[i][dirname_len] == '\0') {
				/* real_dir_name == dirname(CONTENTS) */
				path_ok = true;

			} else if (real_root[0]) {
				char rpath[_Q_PATH_MAX + 1], *_rpath;
				char *fullpath;
				size_t real_root_len = state->real_root_len;

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
					path_ok = true;
				} else if (real_dir_names[i] &&
				           strcmp(_rpath, real_dir_names[i]) == 0) {
					/* real_dir_name == realpath(dirname(CONTENTS)) */
					path_ok = true;
				}
				free(fullpath);
			} else if (state->basename) {
				path_ok = true;
			} else if (state->pwd) {
				if (!strncmp(e->name, state->pwd, dirname_len))
					path_ok = true;
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
					/* XXX: This assumes the buf is big enough. */
					char *slot_hack = slot + 1;
					size_t slot_len = sizeof(slot) - 1;
					q_vdb_pkg_eat(pkg_ctx, "SLOT", &slot_hack, &slot_len);
					rmspace(slot_hack);
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
	free(qfile_args->non_orphans);

	memset(qfile_args, 0, sizeof(qfile_args_t));
}

_q_static int
prepare_qfile_args(const int argc, const char **argv, struct qfile_opt_state *state)
{
	qfile_args_t *args = &state->args;
	int i;
	int nb_of_queries = argc;
	char *pwd = state->pwd;
	size_t real_root_len = state->real_root_len;
	char *real_root = state->real_root;
	char **basenames = NULL;
	char **dirnames = NULL;
	char **realdirnames = NULL;
	char tmppath[_Q_PATH_MAX+1];
	char abspath[_Q_PATH_MAX+1];

	/* For each argument, we store its basename, its absolute dirname,
	 * and the realpath of its dirname.  Dirnames and their realpaths
	 * are stored without their $ROOT prefix, but $ROOT is used when
	 * checking realpaths.
	 */
	basenames = xcalloc(argc, sizeof(char*));
	dirnames = xcalloc(argc, sizeof(char*));
	realdirnames = xcalloc(argc, sizeof(char*));

	for (i = 0; i < argc; ++i) {
		/* Record basename, but if it is ".", ".." or "/" */
		strncpy(abspath, argv[i], _Q_PATH_MAX); /* strncopy so that "argv" can be "const" */
		strncpy(tmppath, basename(abspath), _Q_PATH_MAX);
		if ((strlen(tmppath) > 2) ||
		    (strncmp(tmppath, "..", strlen(tmppath))
		     && strncmp(tmppath, "/", strlen(tmppath))))
		{
			basenames[i] = xstrdup(tmppath);
			/* If there is no "/" in the argument, then it's over.
			 * (we are searching a simple file name)
			 */
			if (strchr(argv[i], '/') == NULL)
				continue;
		}

		/* Make sure we have an absolute path available (with "realpath(ROOT)" prefix) */
		if (argv[i][0] == '/') {
			if (state->assume_root_prefix)
				strncpy(abspath, argv[i], _Q_PATH_MAX);
			else
				snprintf(abspath, _Q_PATH_MAX, "%s%s", real_root, argv[i]);
		} else if (pwd) {
			if (state->assume_root_prefix)
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
			if (abspath[real_root_len] == '\0')
				strncat(abspath, "/", 1);
			dirnames[i] = xstrdup(abspath + real_root_len);
			if (realpath(abspath, tmppath) == NULL) {
				if (verbose) {
					warnp("Could not read real path of \"%s\"", abspath);
					warn("Results for query item \"%s\" may be inaccurate.", argv[i]);
				}
				continue;
			}
			if (!qfile_is_prefix(tmppath, real_root, real_root_len)) {
				warn("Real path of \"%s\" is not under ROOT: %s", abspath, tmppath);
				goto skip_query_item;
			}
			if (tmppath[real_root_len] == '\0')
				strncat(tmppath, "/", 1);
			if (strcmp(dirnames[i], tmppath + real_root_len))
				realdirnames[i] = xstrdup(tmppath + real_root_len);
		} else {
			/* No basename means we are looking for something like "/foo/bar/.."
			 * Dirname is meaningless here, we can only get realpath of the full
			 * path and then split it.
			 */
			if (realpath(abspath, tmppath) == NULL) {
				warnp("Could not read real path of \"%s\"", abspath);
				goto skip_query_item;
			}
			if (!qfile_is_prefix(tmppath, real_root, real_root_len)) {
				warn("Real path of \"%s\" is not under ROOT: %s", abspath, tmppath);
				goto skip_query_item;
			}
			strncpy(abspath, tmppath, _Q_PATH_MAX);
			basenames[i] = xstrdup(basename(abspath));
			strncpy(abspath, dirname(tmppath), _Q_PATH_MAX);
			if (tmppath[real_root_len] == '\0')
				strncat(tmppath, "/", 1);
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

	if (state->orphans)
		args->non_orphans = xcalloc(argc, sizeof(short));

	return nb_of_queries;
}

int qfile_main(int argc, char **argv)
{
	struct qfile_opt_state state = {
		.buflen = _Q_PATH_MAX,
		.slotted = false,
		.basename = false,
		.exact = false,
		.orphans = false,
		.assume_root_prefix = false,
	};
	int i, nb_of_queries, found = 0;
	char *p;
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
			case 'b': state.basename = true; break;
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
			case 'o': state.orphans = true; break;
			case 'R': state.assume_root_prefix = true; break;
			case 'x':
				if (state.exclude_pkg)
					err("--exclude can only be used once.");
				state.exclude_pkg = xstrdup(optarg);
				if ((state.exclude_slot = strchr(state.exclude_pkg, ':')) != NULL)
					*state.exclude_slot++ = '\0';
				state.exclude_atom = atom_explode(optarg);
				if (!state.exclude_atom)
					err("invalid atom %s", optarg);
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
	if (state.assume_root_prefix) {
		/* Get a copy of $ROOT, with no trailing slash
		 * (this one is just for qfile(...) output)
		 */
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

	do { /* This block may be repeated if using --from with a big files list */
		if (args_file) {
			/* Read up to max_args files from the input file */
			for (i = 0; i < qargc; ++i)
				free(qargv[i]);
			qargc = 0;
			size_t linelen;
			while ((linelen = getline(&state.buf, &state.buflen, args_file)) != -1) {
				rmspace_len(state.buf, linelen);
				if (state.buf[0] == '\0')
					continue;
				qargv[qargc] = xstrdup(state.buf);
				if (++qargc >= max_args)
					break;
			}
		}
		if (qargc == 0)
			break;

		/* Prepare the qfile(...) arguments structure */
		nb_of_queries = prepare_qfile_args(qargc, (const char **) qargv, &state);
		if (nb_of_queries < 0)
			break;

		/* Now do the actual `qfile` checking */
		if (nb_of_queries)
			found += q_vdb_foreach_pkg(qfile_cb, &state, NULL);

		if (state.args.non_orphans) {
			/* display orphan files */
			for (i = 0; i < state.args.length; i++) {
				if (state.args.non_orphans[i])
					continue;
				if (state.args.basenames[i]) {
					found = 0; /* ~inverse return code (as soon as an orphan is found, return non-zero) */
					if (!quiet)
						puts(qargv[i]);
					else
						break;
				}
			}
		}

		destroy_qfile_args(&state.args);
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

#else
DEFINE_APPLET_STUB(qfile)
#endif
