/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qfile.c,v 1.37 2006/12/01 17:37:46 solar Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qfile

#define QFILE_FLAGS "eoR" COMMON_FLAGS
static struct option const qfile_long_opts[] = {
	{"exact",       no_argument, NULL, 'e'},
	{"orphans",     no_argument, NULL, 'o'},
	{"root-prefix", no_argument, NULL, 'R'},
	COMMON_LONG_OPTS
};
static const char *qfile_opts_help[] = {
	"Exact match",
	"List orphan files",
	"Assume arguments are already prefixed by $ROOT",
	COMMON_OPTS_HELP
};
static char qfile_rcsid[] = "$Id: qfile.c,v 1.37 2006/12/01 17:37:46 solar Exp $";
#define qfile_usage(ret) usage(ret, QFILE_FLAGS, qfile_long_opts, qfile_opts_help, lookup_applet_idx("qfile"))

static inline short qfile_is_prefix(const char* path, const char* prefix, int prefix_length)
{
	return !prefix_length
		|| (strlen(path) >= prefix_length
			&& (path[prefix_length] == '/' || path[prefix_length] == '\0')
			&& !strncmp(path, prefix, prefix_length));
}

void qfile(char *path, int argc, char* root, char* real_root, char* bn_firstchars,
		char **base_names, char **dir_names, char **real_dir_names, short * non_orphans);
void qfile(char *path, int argc, char* root, char* real_root, char* bn_firstchars,
		char **base_names, char **dir_names, char **real_dir_names, short * non_orphans)
{
	FILE *fp;
	DIR *dir;
	struct dirent *dentry;
	char *entry_basename;
	char *entry_dirname;
	char *p;
	char buf[1024];
	char pkg[126];
	depend_atom *atom;
	int i, path_ok;
	char bn_firstchar;

	if (chdir(path) != 0 || (dir = opendir(".")) == NULL)
		return;

	while ((dentry = readdir(dir))) {
		if (dentry->d_name[0] == '.')
			continue;
		xasprintf(&p, "%s/%s/CONTENTS", path, dentry->d_name);
		if ((fp = fopen(p, "r")) == NULL) {
			free(p);
			continue;
		}
		free(p);

		snprintf(pkg, sizeof(pkg), "%s/%s", basename(path), dentry->d_name);
		while ((fgets(buf, sizeof(buf), fp)) != NULL) {
			contents_entry *e;
			e = contents_parse_line(buf);
			if (!e)
				continue;

			// much faster than using basename(), since no need to strdup
			if ((entry_basename = strrchr(e->name, '/')) == NULL)
				continue;
			entry_basename++;

			// used to cut the number of strcmp() calls
			bn_firstchar = entry_basename[0];

			for (i = 0; i < argc; i++) {
				if (base_names[i] == NULL)
					continue;
				if (non_orphans != NULL && non_orphans[i])
					continue;
				path_ok = (dir_names[i] == NULL && real_dir_names[i] == NULL);

				if (bn_firstchar != bn_firstchars[i]
						|| strcmp(entry_basename, base_names[i]))
					continue;

				if (!path_ok) {
					// check the full filepath...
					entry_dirname = xstrdup(e->name);
					if ((p = strrchr(entry_dirname, '/')) == NULL) {
						free(entry_dirname);
						continue;
					}
					if (p == entry_dirname)
						// (e->name == "/foo") ==> dirname == "/"
						*(p + 1) = '\0';
					else
						*p = '\0';
					if (dir_names[i] != NULL &&
							strcmp(entry_dirname, dir_names[i]) == 0)
						// dir_name == dirname(CONTENTS)
						path_ok = 1;
					else if (real_dir_names[i] != NULL &&
							strcmp(entry_dirname, real_dir_names[i]) == 0)
						// real_dir_name == dirname(CONTENTS)
						path_ok = 1;
					else {
						char rpath[_Q_PATH_MAX+1];
						char *fullpath = entry_dirname;
						errno = 0;
						if (real_root != NULL && real_root[0] != '\0')
							xasprintf(&fullpath, "%s%s", real_root, entry_dirname);
						realpath(fullpath, rpath);
						if (errno != 0) {
							if (verbose) {
								warn("Could not read real path of \"%s\" (from %s): %s",
										fullpath, pkg, strerror(errno));
								warn("We'll never know whether \"%s/%s\" was a result for your query...",
										entry_dirname, entry_basename);
							}
						} else if (!qfile_is_prefix(rpath, real_root, strlen(real_root))) {
							if (verbose)
								warn("Real path of \"%s\" is not under ROOT: %s", fullpath, rpath);
						} else if (dir_names[i] != NULL &&
								strcmp(rpath + strlen(real_root), dir_names[i]) == 0)
							// dir_name == realpath(dirname(CONTENTS))
							path_ok = 1;
						else if (real_dir_names[i] != NULL &&
								strcmp(rpath + strlen(real_root), real_dir_names[i]) == 0)
							// real_dir_name == realpath(dirname(CONTENTS))
							path_ok = 1;
						if (fullpath != entry_dirname)
							free(fullpath);
					}
					free(entry_dirname);
				}
				if (!path_ok)
					continue;

				if (non_orphans == NULL) {
					if ((atom = atom_explode(pkg)) == NULL) {
						warn("invalid atom %s", pkg);
						continue;
					}

					printf("%s%s/%s%s%s", BOLD, atom->CATEGORY, BLUE,
						(exact ? dentry->d_name : atom->PN), NORM);
					if (quiet)
						puts("");
					else if (root != NULL)
						printf(" (%s%s)\n", root, e->name);
					else
						printf(" (%s)\n", e->name);

					atom_implode(atom);
				} else {
					non_orphans[i] = 1;
				}
				found++;
			}
		}
		fclose(fp);
	}
	closedir(dir);

	return;
}

int qfile_main(int argc, char **argv)
{
	DIR *dir;
	struct dirent *dentry;
	int i, nb_of_queries;
	char *p;
	char **basenames;
	char **dirnames;
	char **realdirnames;
	char *basenames_firstchars;
	char *pwd = NULL;
	short *non_orphans = NULL;
	short search_orphans = 0;
	short assume_root_prefix = 0;
	char *root_prefix;
	char *real_root;
	int real_root_length;
	char tmppath[_Q_PATH_MAX+1];
	char abspath[_Q_PATH_MAX+1];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QFILE, qfile, "")) != -1) {
		switch (i) {
			COMMON_GETOPTS_CASES(qfile)
			case 'e': exact = 1; break;
			case 'o': search_orphans = 1; break;
			case 'R': assume_root_prefix = 1; break;
		}
	}
	if (!exact && verbose) exact++;
	if (argc == optind)
		qfile_usage(EXIT_FAILURE);
	nb_of_queries = argc - optind;

	if (chdir(portroot))
		errp("could not chdir(%s) for ROOT", portroot);

	if (chdir(portvdb) != 0 || (dir = opendir(".")) == NULL)
		errp("could not chdir(ROOT/%s) for installed packages database", portvdb);

	// Try to get $PWD.  Must be absolute, with no trailing slash
	if ((pwd = getenv("PWD")) != NULL && pwd[0] == '/') {
		pwd = xstrdup(pwd);
		if ((pwd[strlen(pwd) - 1] == '/'))
			pwd[strlen(pwd) - 1] = '\0';
	} else
		pwd = NULL;

	// Get realpath of $ROOT, with no trailing slash
	if (portroot[0] == '/')
		strncpy(tmppath, portroot, _Q_PATH_MAX);
	else if (pwd != NULL)
		snprintf(tmppath, _Q_PATH_MAX, "%s/%s", pwd, portroot);
	else {
		free(pwd);
		errp("Could not get absolute path for ROOT (\"%s\"), because of missing or not absolute $PWD", tmppath);
	}
	errno = 0;
	realpath(tmppath, abspath);
	if (errno != 0) {
		free(pwd);
		errp("Could not read real path of ROOT (\"%s\"): %s", tmppath, strerror(errno));
	}
	if (strlen(abspath) == 1)
		abspath[0] = '\0';
	real_root = xstrdup(abspath);
	real_root_length = strlen(real_root);

	// Get a copy of $ROOT, with no trailing slash
	// (this one is just for qfile(...) output)
	root_prefix = xstrdup(portroot);
	if (root_prefix[strlen(root_prefix) - 1] == '/')
		root_prefix[strlen(root_prefix) - 1] = '\0';

	// For each argument, we store its basename, its absolute dirname,
	// and the realpath of its dirname.  Dirnames and their realpaths
	// are stored without their $ROOT prefix, but $ROOT is used when
	// checking realpaths.
	basenames = xmalloc((argc-optind) * sizeof(char*));
	dirnames = xmalloc((argc-optind) * sizeof(char*));
	realdirnames = xmalloc((argc-optind) * sizeof(char*));
	// For optimization of qfile(), we also give it an array of the first char
	// of each basename.  This way we avoid numerous strcmp() calls.
	basenames_firstchars = xmalloc((argc-optind) * sizeof(char));
	// Finally, if searching for orphans, we need an array to store the results
	if (search_orphans)
		non_orphans = xmalloc((argc-optind) * sizeof(short));
	for (i = 0; i < (argc-optind); ++i) {
		basenames[i] = NULL;
		dirnames[i] = NULL;
		realdirnames[i] = NULL;

		// Record basename, but if it is ".", ".." or "/"
		strncpy(tmppath, basename(argv[i+optind]), _Q_PATH_MAX);
		if ((strlen(tmppath) > 2) ||
		    (strncmp(tmppath, "..", strlen(tmppath))
		     && strncmp(tmppath, "/", strlen(tmppath))))
		{
			basenames[i] = xstrdup(tmppath);
			basenames_firstchars[i] = basenames[i][0];
			// If there is no "/" in the argument, then it's over.
			// (we are searching a simple file name)
			if (strchr(argv[i+optind], '/') == NULL)
				continue;
		}

		// Make sure we have an absolute path available (with "realpath(ROOT)" prefix)
		if (argv[i+optind][0] == '/') {
			if (assume_root_prefix)
				strncpy(abspath, argv[i+optind], _Q_PATH_MAX);
			else
				snprintf(abspath, _Q_PATH_MAX, "%s%s", real_root, argv[i+optind]);
		} else if (pwd != NULL) {
			if (assume_root_prefix)
				snprintf(abspath, _Q_PATH_MAX, "%s/%s", pwd, argv[i+optind]);
			else
				snprintf(abspath, _Q_PATH_MAX, "%s%s/%s", real_root, pwd, argv[i+optind]);
		} else {
			warn("$PWD was not found in environment, or is not an absolute path");
			goto skip_query_item;
		}

		if (basenames[i] != NULL) {
			// Get both the dirname and its realpath.  This paths will
			// have no trailing slash, but if it is the only char (ie.,
			// when searching for "/foobar").
			strncpy(tmppath, abspath, _Q_PATH_MAX);
			strncpy(abspath, dirname(tmppath), _Q_PATH_MAX);
			if (abspath[real_root_length] == '\0')
				strncat(abspath, "/", 1);
			dirnames[i] = xstrdup(abspath + real_root_length);
			errno = 0;
			realpath(abspath, tmppath);
			if (errno != 0) {
				if (verbose) {
					warn("Could not read real path of \"%s\": %s", abspath, strerror(errno));
					warn("Results for query item \"%s\" may be inaccurate.", argv[i+optind]);
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
			// No basename means we are looking for something like "/foo/bar/.."
			// Dirname is meaningless here, we can only get realpath of the full
			// path and then split it.
			errno = 0;
			realpath(abspath, tmppath);
			if (errno != 0) {
				warn("Could not read real path of \"%s\": %s", abspath, strerror(errno));
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
			warn("Skipping query item \"%s\".", argv[i+optind]);
			if (basenames[i] != NULL) free(basenames[i]);
			if (dirnames[i] != NULL) free(dirnames[i]);
			if (realdirnames[i] != NULL) free(realdirnames[i]);
			basenames[i] = dirnames[i] = realdirnames[i] = NULL;
	}

	/* open /var/db/pkg (but if all query items were skipped) */
	while (nb_of_queries && (dentry = q_vdb_get_next_dir(dir))) {
		xasprintf(&p, "%s/%s/%s", real_root, portvdb, dentry->d_name);
		qfile(p, (argc-optind), (assume_root_prefix ? root_prefix : NULL), real_root,
				basenames_firstchars, basenames, dirnames, realdirnames, non_orphans);
		free(p);
	}

	if (non_orphans != NULL) {
		// display orphan files
		for (i = 0; i < (argc-optind); i++) {
			if (non_orphans[i])
				continue;
			if (basenames[i] != NULL) {
				found = 0; // ~inverse return code (as soon as an orphan is found, return non-zero)
				if (!quiet)
					printf("%s\n", argv[i+optind]);
			}
		}
		free(non_orphans);
	}

	for (i = 0; i < (argc-optind); ++i) {
		if (basenames[i] != NULL) free(basenames[i]);
		if (dirnames[i] != NULL) free(dirnames[i]);
		if (realdirnames[i] != NULL) free(realdirnames[i]);
	}
	free(basenames); free(dirnames); free(realdirnames);

	free(basenames_firstchars);

	if (pwd != NULL)
		free(pwd);

	free(real_root); free(root_prefix);

	return (found ? EXIT_SUCCESS : EXIT_FAILURE);
}

#else
DEFINE_APPLET_STUB(qfile)
#endif
