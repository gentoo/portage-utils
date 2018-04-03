/*
 * Copyright 2005-2018 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_quse

/*
 quse -CKe -- '-*' {'~',-,}{alpha,amd64,hppa,ia64,ppc,ppc64,sparc,x86}
 quse -Ke --  nls
*/

#define QUSE_FLAGS "eavKLDF:N" COMMON_FLAGS
static struct option const quse_long_opts[] = {
	{"exact",     no_argument, NULL, 'e'},
	{"all",       no_argument, NULL, 'a'},
	{"keywords",  no_argument, NULL, 'K'},
	{"license",   no_argument, NULL, 'L'},
	{"describe",  no_argument, NULL, 'D'},
	{"format",     a_argument, NULL, 'F'},
	{"name-only", no_argument, NULL, 'N'},
	COMMON_LONG_OPTS
};
static const char * const quse_opts_help[] = {
	"Show exact non regexp matching using strcmp",
	"List all ebuilds, don't match anything",
	"Use the KEYWORDS vs IUSE",
	"Use the LICENSE vs IUSE",
	"Describe the USE flag",
	"Use your own variable formats. -F NAME=",
	"Only show package name",
	COMMON_OPTS_HELP
};
#define quse_usage(ret) usage(ret, QUSE_FLAGS, quse_long_opts, quse_opts_help, NULL, lookup_applet_idx("quse"))

char quse_name_only = 0;

static void
print_highlighted_use_flags(char *string, int ind, int argc, char **argv)
{
	char *str, *p;
	char buf[BUFSIZ];
	size_t pos, len;
	short highlight = 0;
	int i;

	if (quse_name_only)
		return;

	strncpy(buf, string, sizeof(buf));
	str = buf;
	remove_extra_space(str);
	rmspace(str);

	if (*WHITE != '\e') {
		printf("%s", str);
		return;
	}

	len = strlen(str);
	for (pos = 0; pos < len; pos++) {
		highlight = 0;
		if ((p = strchr(str, ' ')) != NULL)
			*p = 0;
		pos += strlen(str);
		for (i = ind; i < argc; ++i)
			if (strcmp(str, argv[i]) == 0)
				highlight = 1;
		if (highlight)
			printf("%s%s%s ", BOLD, str, NORM);
		else
			printf("%s%s%s%s ", NORM, MAGENTA, str, NORM);
		if (p != NULL)
			str = p + 1;
	}
}

static void
quse_describe_flag(const char *overlay, unsigned int ind, unsigned int argc, char **argv)
{
#define NUM_SEARCH_FILES ARRAY_SIZE(search_files)
	int linelen;
	size_t buflen;
	char *buf, *p;
	unsigned int i, f;
	size_t s;
	const char * const search_files[] = { "use.desc", "use.local.desc", "arch.list", };
	FILE *fp[NUM_SEARCH_FILES];
	int dfd, fd;
	DIR *d;
	struct dirent *de;

	/* pick 1000 arbitrarily long enough for all files under desc/ */
	buflen = strlen(overlay) + 1000;
	buf = xmalloc(buflen);

	for (i = 0; i < NUM_SEARCH_FILES; ++i) {
		snprintf(buf, buflen, "%s/profiles/%s", overlay, search_files[i]);
		fp[i] = fopen(buf, "r");
		if (verbose && fp[i] == NULL)
			warnp("skipping %s", buf);
	}

	for (i = ind; i < argc; i++) {
		s = strlen(argv[i]);

		for (f = 0; f < NUM_SEARCH_FILES; ++f) {
			if (fp[f] == NULL)
				continue;

			while ((linelen = getline(&buf, &buflen, fp[f])) >= 0) {
				rmspace_len(buf, (size_t)linelen);
				if (buf[0] == '#' || buf[0] == '\0')
					continue;

				switch (f) {
					case 0: /* Global use.desc */
						if (!strncmp(buf, argv[i], s))
							if (buf[s] == ' ' && buf[s + 1] == '-') {
								printf(" %sglobal%s:%s%s%s: %s\n",
										BOLD, NORM, BLUE, argv[i], NORM,
										buf + s + 3);
								goto skip_file;
							}
						break;

					case 1: /* Local use.local.desc */
						if ((p = strchr(buf, ':')) == NULL)
							break;
						++p;
						if (!strncmp(p, argv[i], s)) {
							if (p[s] == ' ' && p[s + 1] == '-') {
								*p = '\0';
								printf(" %slocal%s:%s%s%s:%s%s%s %s\n",
										BOLD, NORM, BLUE, argv[i], NORM,
										BOLD, buf, NORM, p + s + 3);
							}
						}
						break;

					case 2: /* Architectures arch.list */
						if (!strcmp(buf, argv[i])) {
							printf(" %sarch%s:%s%s%s: %s architecture\n",
									BOLD, NORM, BLUE, argv[i], NORM, argv[i]);
							goto skip_file;
						}
						break;
				}
			}

 skip_file:
			rewind(fp[f]);
		}
	}

	for (f = 0; f < NUM_SEARCH_FILES; ++f)
		if (fp[f] != NULL)
			fclose(fp[f]);

	/* now scan the desc dir */
	snprintf(buf, buflen, "%s/profiles/desc/", overlay);
	dfd = open(buf, O_RDONLY|O_CLOEXEC);
	if (dfd == -1) {
		if (verbose)
			warnp("skipping %s", buf);
		goto done;
	}
	d = fdopendir(dfd);
	if (!d) {
		close(dfd);
		goto done;
	}

	while ((de = readdir(d)) != NULL) {
		s = strlen(de->d_name);
		if (s < 6)
			continue;
		p = de->d_name + s - 5;
		if (strcmp(p, ".desc"))
			continue;

		fd = openat(dfd, de->d_name, O_RDONLY|O_CLOEXEC);
		if (fd == -1) {
			if (verbose)
				warnp("skipping %s/profiles/desc/%s", overlay, de->d_name);
			continue;
		}
		fp[0] = fdopen(fd, "r");
		if (!fp[0]) {
			close(fd);
			continue;
		}

		/* Chop the trailing .desc for better display */
		*p = '\0';

		while ((linelen = getline(&buf, &buflen, fp[0])) >= 0) {
			rmspace_len(buf, (size_t)linelen);
			if (buf[0] == '#' || buf[0] == '\0')
				continue;

			if ((p = strchr(buf, '-')) == NULL) {
 invalid_line:
				warn("Invalid line in '%s': %s", de->d_name, buf);
				continue;
			}
			while (p[-1] != ' ' && p[1] != ' ') {
				/* maybe the flag has a '-' in it ... */
				if ((p = strchr(p + 1, '-')) == NULL)
					goto invalid_line;
			}
			p[-1] = '\0';
			p += 2;

			for (i = ind; i < argc; i++)
				if (!strcmp(argv[i], buf))
					printf(" %s%s%s:%s%s%s: %s\n", BOLD, de->d_name, NORM, BLUE, argv[i], NORM, p);
		}
		fclose(fp[0]);
	}
	closedir(d);

 done:
	free(buf);
}

int quse_main(int argc, char **argv)
{
	FILE *fp;
	const char *cache_file;
	char *p;

	char buf0[_Q_PATH_MAX];
	char buf1[_Q_PATH_MAX];
	char buf2[_Q_PATH_MAX];

	int linelen;
	size_t ebuildlen;
	char *ebuild;

	const char *search_var = NULL;
	const char *search_vars[] = { "IUSE=", "KEYWORDS=", "LICENSE=", search_var };
	short quse_all = 0;
	int regexp_matching = 1, i, idx = 0;
	size_t search_len;
	size_t n;
	const char *overlay;

	while ((i = GETOPT_LONG(QUSE, quse, "")) != -1) {
		switch (i) {
		case 'e': regexp_matching = 0; break;
		case 'a': quse_all = 1; break;
		case 'K': idx = 1; break;
		case 'L': idx = 2; break;
		case 'D': idx = -1; break;
		case 'F': idx = 3, search_vars[idx] = xstrdup(optarg); break;
		case 'N': quse_name_only = 1; break;
		COMMON_GETOPTS_CASES(quse)
		}
	}
	if (argc == optind && !quse_all && idx >= 0)
		quse_usage(EXIT_FAILURE);

	if (idx == -1) {
		array_for_each(overlays, n, overlay)
			quse_describe_flag(overlay, optind, argc, argv);
		return 0;
	}

	if (quse_all) optind = argc;

	search_len = strlen(search_vars[idx]);
	assert(search_len < sizeof(buf0));

	array_for_each(overlays, n, overlay) {
		cache_file = initialize_flat(overlay, CACHE_EBUILD, false);

		if ((fp = fopen(cache_file, "re")) == NULL) {
			warnp("could not read cache: %s", cache_file);
			continue;
		}

		int overlay_fd = open(overlay, O_RDONLY|O_CLOEXEC|O_PATH);

		ebuild = NULL;
		while ((linelen = getline(&ebuild, &ebuildlen, fp)) >= 0) {
			FILE *newfp;
			int fd;

			rmspace_len(ebuild, (size_t)linelen);

			fd = openat(overlay_fd, ebuild, O_RDONLY|O_CLOEXEC);
			if (fd < 0)
				continue;
			newfp = fdopen(fd, "r");
			if (newfp != NULL) {
				unsigned int lineno = 0;
				char revision[sizeof(buf0)];
				char date[sizeof(buf0)];
				char user[sizeof(buf0)];

				revision[0] = 0;
				user[0] = 0;
				date[0] = 0;
				while (fgets(buf0, sizeof(buf0), newfp) != NULL) {
					int ok = 0;
					char warned = 0;
					lineno++;

					if (*buf0 == '#') {
						if (strncmp(buf0, "# $Header: /", 12) == 0)
							sscanf(buf0, "%*s %*s %*s %s %s %*s %s %*s %*s",
								revision, date, user);
						continue;
					}
					if (strncmp(buf0, search_vars[idx], search_len) != 0)
						continue;

					if ((p = strchr(buf0, '\n')) != NULL)
						*p = 0;
					if ((p = strchr(buf0, '#')) != NULL) {
						if (buf0 != p && p[-1] == ' ')
							p[-1] = 0;
						else
							*p = 0;
					}
					if (verbose > 1) {
						if ((strchr(buf0, '\t') != NULL)
						    || (strchr(buf0, '$') != NULL)
						    || (strchr(buf0, '\\') != NULL)
						    || (strchr(buf0, '\'') != NULL)
						    || (strstr(buf0, "  ") != NULL)) {
							warned = 1;
							warn("# Line %d of %s has an annoying %s",
								lineno, ebuild, buf0);
						}
					}
#ifdef THIS_SUCKS
					if ((p = strrchr(&buf0[search_len + 1], '\\')) != NULL) {

					multiline:
						*p = ' ';

						if (fgets(buf1, sizeof(buf1), newfp) == NULL)
							continue;
						lineno++;

						if ((p = strchr(buf1, '\n')) != NULL)
							*p = 0;
						snprintf(buf2, sizeof(buf2), "%s %s", buf0, buf1);
						remove_extra_space(buf2);
						strcpy(buf0, buf2);
						if ((p = strrchr(buf1, '\\')) != NULL)
							goto multiline;
					}
#else
					remove_extra_space(buf0);
#endif
					while ((p = strrchr(&buf0[search_len + 1], '"')) != NULL)  *p = 0;
					while ((p = strrchr(&buf0[search_len + 1], '\'')) != NULL) *p = 0;
					while ((p = strrchr(&buf0[search_len + 1], '\\')) != NULL) *p = ' ';

					if (verbose && warned == 0) {
						if ((strchr(buf0, '$') != NULL) || (strchr(buf0, '\\') != NULL)) {
							warned = 1;
							warn("# Line %d of %s has an annoying %s",
								lineno, ebuild, buf0);
						}
					}

					if (strlen(buf0) < search_len + 1) {
						/* warnf("err '%s'/%zu <= %zu; line %u\n", buf0, strlen(buf0), search_len + 1, lineno); */
						continue;
					}

					if ((argc == optind) || (quse_all)) {
						ok = 1;
					} else {
						ok = 0;
						if (regexp_matching) {
							for (i = optind; i < argc; ++i) {
								if (rematch(argv[i], &buf0[search_len + 1], REG_NOSUB) == 0) {
									ok = 1;
									break;
								}
							}
						} else {
							remove_extra_space(buf0);
							strcpy(buf1, &buf0[search_len + 1]);

							for (i = (size_t) optind; i < argc && argv[i] != NULL; i++) {
								if (strcmp(buf1, argv[i]) == 0) {
									ok = 1;
									break;
								}
							}
							if (ok == 0) while ((p = strchr(buf1, ' ')) != NULL) {
								*p = 0;
								for (i = (size_t) optind; i < argc && argv[i] != NULL; i++) {
									if (strcmp(buf1, argv[i]) == 0) {
										ok = 1;
										break;
									}
								}
								strcpy(buf2, p + 1);
								strcpy(buf1, buf2);
								if (strchr(buf1, ' ') == NULL)
									for (i = (size_t) optind; i < argc && argv[i] != NULL; i++) {
										if (strcmp(buf1, argv[i]) == 0)
											ok = 1;
									}
							}
						}
					}
					if (ok) {
						if (verbose > 3)
							printf("%s %s %s ",
								*user ? user : "MISSING",
								*revision ? revision : "MISSING",
								*date ? date : "MISSING");

						printf("%s%s%s ", CYAN, ebuild, NORM);
						print_highlighted_use_flags(&buf0[search_len + 1], optind, argc, argv);
						puts(NORM);
						if (verbose > 1) {
							char **ARGV;
							int ARGC;
							makeargv(&buf0[search_len + 1], &ARGC, &ARGV);
							quse_describe_flag(overlay, 1, ARGC, ARGV);
							freeargv(ARGC, ARGV);
						}
					}
					break;
				}
				fclose(newfp);
			} else {
				if (!reinitialize)
					warnfp("(cache update pending) %s", ebuild);
				reinitialize = 1;
			}
		}
		fclose(fp);
		close(overlay_fd);
	}

	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(quse)
#endif
