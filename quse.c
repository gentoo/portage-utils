/*
 * Copyright 2005-2020 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <xalloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <assert.h>

#include "set.h"
#include "rmspace.h"
#include "tree.h"
#include "xarray.h"
#include "xregex.h"

#define QUSE_FLAGS "eaLDIp:RF:" COMMON_FLAGS
static struct option const quse_long_opts[] = {
	{"exact",     no_argument, NULL, 'e'},
	{"all",       no_argument, NULL, 'a'},
	{"license",   no_argument, NULL, 'L'},
	{"describe",  no_argument, NULL, 'D'},
	{"installed", no_argument, NULL, 'I'},
	{"package",    a_argument, NULL, 'p'},
	{"repo",      no_argument, NULL, 'R'},
	{"format",     a_argument, NULL, 'F'},
	COMMON_LONG_OPTS
};
static const char * const quse_opts_help[] = {
	"Show exact non regexp matching using strcmp",
	"List all ebuilds, don't match anything",
	"Use the LICENSE vs IUSE",
	"Describe the USE flag",
	"Only search installed packages",
	"Restrict matching to package or category",
	"Show repository the ebuild originates from",
	"Print matched atom using given format string",
	COMMON_OPTS_HELP
};
#define quse_usage(ret) usage(ret, QUSE_FLAGS, quse_long_opts, quse_opts_help, NULL, lookup_applet_idx("quse"))

struct quse_state {
	int argc;
	char **argv;
	char **retv;
	const char *overlay;
	bool do_all:1;
	bool do_regex:1;
	bool do_describe:1;
	bool do_licence:1;
	bool do_installed:1;
	bool do_list:1;
	bool need_full_atom:1;
	depend_atom *match;
	regex_t *pregv;
	const char *fmt;
};

static char *_quse_getline_buf = NULL;
static size_t _quse_getline_buflen = 0;
#define GETLINE(FD, BUF, LEN) \
	LEN = getline(&_quse_getline_buf, &_quse_getline_buflen, FD); \
	BUF = _quse_getline_buf

static bool
quse_search_use_local_desc(int portdirfd, struct quse_state *state)
{
	int fd;
	FILE *f;
	ssize_t linelen;
	char *buf;
	char *p;
	char *q;
	int i;
	bool match = false;
	bool ret = false;
	depend_atom *atom;

	fd = openat(portdirfd, "profiles/use.local.desc", O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		return false;

	f = fdopen(fd, "r");
	if (f == NULL) {
		close(fd);
		return false;
	}

	/* use.local.desc: <pkg>:<use> - <desc> */
	do {
		GETLINE(f, buf, linelen);
		if (linelen < 0)
			break;

		rmspace_len(buf, (size_t)linelen);
		if (buf[0] == '#' || buf[0] == '\0')
			continue;

		if ((p = strchr(buf, ':')) == NULL)
			continue;
		*p++ = '\0';

		q = strchr(p, ' ');
		if (q == NULL || q[1] != '-')
			continue;
		*q = '\0';
		q += 3; /* " - " */

		match = false;
		for (i = 0; i < state->argc; i++) {
			if (state->do_list && state->retv[i] != NULL)
				continue;

			if (state->do_regex) {
				if (regexec(&state->pregv[i], p, 0, NULL, 0) != 0)
					continue;
			} else {
				if (strcmp(p, state->argv[i]) != 0)
					continue;
			}
			match = true;
			break;
		}

		if (match) {
			if ((atom = atom_explode(buf)) == NULL)
				continue;

			if (state->match == NULL ||
					atom_compare(atom, state->match) == EQUAL)
			{
				if (state->do_list) {
					state->retv[i] = xstrdup(q);
				} else {
					printf("%s[%s%s%s] %s\n",
							atom_format(state->fmt, atom),
							MAGENTA, p, NORM, q);
				}
			}

			atom_implode(atom);
			ret = true;
		}
	} while (1);

	if (state->do_list && ret) {
		/* check if all requested flags are retrieved */
		ret = true;
		for (i = 0; i < state->argc; i++)
			if (state->retv[i] == NULL)
				break;
		if (i < state->argc)
			ret = false;
	}

	fclose(f);
	return ret;
}

static bool
quse_search_use_desc(int portdirfd, struct quse_state *state)
{
	int fd;
	FILE *f;
	ssize_t linelen;
	char *buf;
	char *p;
	int i;
	bool match = false;
	bool ret = false;

	fd = openat(portdirfd, "profiles/use.desc", O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		return false;

	f = fdopen(fd, "r");
	if (f == NULL) {
		close(fd);
		return false;
	}

	/* use.desc: <use> - <desc> */
	do {
		GETLINE(f, buf, linelen);
		if (linelen < 0)
			break;

		rmspace_len(buf, (size_t)linelen);
		if (buf[0] == '#' || buf[0] == '\0')
			continue;

		p = strchr(buf, ' ');
		if (p == NULL || p[1] != '-')
			continue;
		*p = '\0';
		p += 3; /* " - " */

		match = false;
		for (i = 0; i < state->argc; i++) {
			if (state->do_list && state->retv[i] != NULL)
				continue;

			if (state->do_regex) {
				if (regexec(&state->pregv[i], buf, 0, NULL, 0) != 0)
					continue;
			} else {
				if (strcmp(buf, state->argv[i]) != 0)
					continue;
			}
			match = true;
			break;
		}

		if (match) {
			if (state->do_list) {
				state->retv[i] = xstrdup(p);
			} else {
				printf("%sglobal%s[%s%s%s] %s\n",
						BOLD, NORM, MAGENTA, buf, NORM, p);
			}

			ret = true;
		}
	} while (1);

	if (state->do_list && ret) {
		/* check if all requested flags are retrieved */
		ret = true;
		for (i = 0; i < state->argc; i++)
			if (state->retv[i] == NULL)
				break;
		if (i < state->argc)
			ret = false;
	}

	fclose(f);
	return ret;
}

static bool
quse_search_profiles_desc(
		int portdirfd,
		struct quse_state *state)
{
	int fd;
	FILE *f;
	ssize_t linelen;
	char *buf;
	char *p;
	int i;
	bool match = false;
	bool ret = false;
	size_t namelen;
	size_t arglen;
	char ubuf[_Q_PATH_MAX];
	struct dirent *de;
	DIR *d;
	int dfd;

	fd = openat(portdirfd, "profiles/desc", O_RDONLY|O_CLOEXEC);
	if (fd == -1)
		return false;
	d = fdopendir(fd);
	if (!d) {
		close(fd);
		return false;
	}

	if (_quse_getline_buf == NULL) {
		_quse_getline_buflen = _Q_PATH_MAX;
		_quse_getline_buf = xmalloc(sizeof(char) * _quse_getline_buflen);
	}

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;

		namelen = strlen(de->d_name);
		if (namelen <= 5 || strcmp(de->d_name + namelen - 5, ".desc") != 0)
			return false;

		snprintf(_quse_getline_buf, _quse_getline_buflen,
				"profiles/desc/%s", de->d_name);
		dfd = openat(portdirfd, _quse_getline_buf, O_RDONLY | O_CLOEXEC);
		if (dfd == -1)
			return false;

		f = fdopen(dfd, "r");
		if (f == NULL) {
			close(fd);
			return false;
		}

		/* remove trailing .desc */
		namelen -= 5;

		/* use.desc: <use> - <desc> */
		do {
			GETLINE(f, buf, linelen);
			if (linelen < 0)
				break;

			rmspace_len(buf, (size_t)linelen);
			if (buf[0] == '#' || buf[0] == '\0')
				continue;

			p = strchr(buf, ' ');
			if (p == NULL || p[1] != '-')
				continue;
			*p = '\0';
			p += 3; /* " - " */

			match = false;
			for (i = 0; i < state->argc; i++) {
				if (state->do_list && state->retv[i] != NULL)
					continue;

				arglen = strlen(state->argv[i]);
				if (arglen > namelen) {
					/* nginx_modules_http_lua = NGINX_MODULES_HTTP[lua] */
					match = strncmp(state->argv[i], de->d_name, namelen) == 0;
					if (match && state->argv[i][namelen] == '_') {
						match = strcmp(&state->argv[i][namelen + 1], buf) == 0;
					} else {
						match = false;
					}
					if (match)
						break;
				}

				if (state->do_regex) {
					if (regexec(&state->pregv[i], buf, 0, NULL, 0) != 0)
						continue;
				} else {
					if (strcmp(buf, state->argv[i]) != 0)
						continue;
				}
				match = true;
				break;
			}

			if (match) {
				if (state->do_list) {
					state->retv[i] = xstrdup(p);
				} else {
					const char *r = de->d_name;
					char *s = ubuf;
					do {
						*s++ = (char)toupper((int)*r);
					} while (++r < (de->d_name + namelen));
					*s = '\0';
					printf("%s%s%s[%s%s%s] %s\n",
							BOLD, ubuf, NORM, MAGENTA, buf, NORM, p);
				}

				ret = true;
			}
		} while (1);

		fclose(f);
	}
	closedir(d);

	if (state->do_list && ret) {
		/* check if all requested flags are retrieved */
		ret = true;
		for (i = 0; i < state->argc; i++)
			if (state->retv[i] == NULL)
				break;
		if (i < state->argc)
			ret = false;
	}

	return ret;
}

static void
quse_describe_flag(const char *root, const char *overlay,
		struct quse_state *state)
{
	char buf[_Q_PATH_MAX];
	int portdirfd;

	snprintf(buf, sizeof(buf), "%s/%s", root, overlay);
	portdirfd = open(buf, O_RDONLY|O_CLOEXEC|O_PATH);
	if (portdirfd == -1)
		return;

	quse_search_use_desc(portdirfd, state);
	quse_search_use_local_desc(portdirfd, state);
	quse_search_profiles_desc(portdirfd, state);

	close(portdirfd);
}

static int
quse_results_cb(tree_pkg_ctx *pkg_ctx, void *priv)
{
	struct quse_state *state = (struct quse_state *)priv;
	depend_atom *atom = NULL;  /* pacify compiler */
	char buf[8192];
	set *use = NULL;
	bool match;
	char *p;
	char *q;
	char *s;
	char *v;
	char *w;
	int i;
	int len;
	int maxlen;
	int cnt;
	int portdirfd = -1;  /* pacify compiler */

	if (state->match || state->do_describe) {
		atom = tree_get_atom(pkg_ctx, 0);
		if (atom == NULL)
			return 0;

		if (state->match) {
			match = atom_compare(atom, state->match) == EQUAL;

			if (!match)
				return 0;
		}
	}

	if (!state->do_licence) {
		if (tree_pkg_meta_get(pkg_ctx, IUSE) == NULL)
			return 0;

		if (state->do_describe) {
			portdirfd = openat(pkg_ctx->cat_ctx->ctx->portroot_fd,
					state->overlay == NULL ? main_overlay : state->overlay,
					O_RDONLY | O_CLOEXEC | O_PATH);
			if (portdirfd == -1)
				return 0;
		}

		/* available when dealing with VDB or binpkgs */
		if ((p = tree_pkg_meta_get(pkg_ctx, USE)) != NULL) {
			while ((q = strchr(p, (int)' ')) != NULL) {
				*q++ = '\0';
				use = add_set(p, use);
				p = q;
			}
			if (*p != '\0')
				use = add_set(p, use);
		}
	} else {
		if (tree_pkg_meta_get(pkg_ctx, LICENSE) == NULL)
			return 0;
	}

	maxlen = 0;
	cnt = 0;
	match = false;
	q = p = state->do_licence ?
		tree_pkg_meta_get(pkg_ctx, LICENSE) : tree_pkg_meta_get(pkg_ctx, IUSE);
	buf[0] = '\0';
	v = buf;
	w = buf + sizeof(buf);

	if (state->do_all && !state->do_describe) {
		match = true;
		v = q;
	} else {
		do {
			if (*p == ' ' || *p == '\0') {
				/* skip over consequtive whitespace */
				if (p == q) {
					q++;
					continue;
				}

				s = q;
				if (*q == '-' || *q == '+' || *q == '@')
					q++;
				if (state->do_all) {
					i = 0;
					match = true;
				} else if (state->do_regex) {
					char r;
					for (i = 0; i < state->argc; i++) {
						r = *p;
						*p = '\0';
						if (regexec(&state->pregv[i], q, 0, NULL, 0) == 0) {
							*p = r;
							v += snprintf(v, w - v, "%s%.*s%s%c",
									RED, (int)(p - s), s, NORM, *p);
							match = true;
							break;
						}
						*p = r;
					}
				} else {
					for (i = 0; i < state->argc; i++) {
						len = strlen(state->argv[i]);
						if (len == (int)(p - q) &&
								strncmp(q, state->argv[i], len) == 0)
						{
							v += snprintf(v, w - v, "%s%.*s%s%c",
									RED, (int)(p - s), s, NORM, *p);
							match = true;
							break;
						}
					}
				}
				if (i == state->argc)
					v += snprintf(v, w - v, "%.*s%c", (int)(p - s), s, *p);

				if (maxlen < p - q)
					maxlen = p - q;
				cnt++;

				q = p + 1;
			}
		} while (*p++ != '\0' && v < w);
		v = buf;
	}

	if (match) {
		atom = tree_get_atom(pkg_ctx, state->need_full_atom);
		if (quiet) {
			printf("%s\n", atom_format(state->fmt, atom));
		} else if (state->do_describe && !state->do_licence) {
			/* multi-line result, printing USE-flags with their descs */
			size_t desclen;
			struct quse_state us = {
				.do_regex = false,
				.do_describe = false,
				.do_list = true,
				.match = atom,
				.argc = cnt,
				.argv = xmalloc(sizeof(char *) * cnt),
				.retv = xzalloc(sizeof(char *) * cnt),
				.overlay = NULL,
			};

			printf("%s\n", atom_format(state->fmt, atom));

			q = p = tree_pkg_meta_get(pkg_ctx, IUSE);
			buf[0] = '\0';
			v = buf;
			w = buf + sizeof(buf);
			i = 0;
			do {
				if (*p == ' ' || *p == '\0') {
					s = q;
					if (*q == '-' || *q == '+' || *q == '@')
						q++;

					/* pre-padd everything such that we always refer to
					 * the char before the USE-flag */
					us.argv[i++] = v + 1;
					v += snprintf(v, w - v, "%c%.*s",
							s == q ? ' ' : *s, (int)(p - q), q) + 1;

					q = p + 1;
				}
			} while (*p++ != '\0' && i < cnt && v < w);

			/* harvest descriptions for USE-flags */
			if (!quse_search_use_local_desc(portdirfd, &us))
				if (!quse_search_use_desc(portdirfd, &us))
					quse_search_profiles_desc(portdirfd, &us);

			/* calculate available space in the terminal to print
			 * descriptions, assume this makes sense from 10 chars */
			if (twidth > maxlen + 2 + 1 + 2 + 10) {
				len = twidth - maxlen - 2 - 1 - 2;
			} else {
				len = 0;
			}

			for (i = 0; i < cnt; i++) {
				match = use != NULL && contains_set(us.argv[i], use);
				desclen = us.retv[i] != NULL ? strlen(us.retv[i]) : 0;
				p = NULL;
				if (desclen > (size_t)len) {  /* need to wrap */
					for (p = &us.retv[i][len]; p > us.retv[i]; p--)
						if (isspace((int)*p))
							break;
					if (p > us.retv[i]) {
						*p++ = '\0';
						desclen -= p - us.retv[i];
					} else {
						p = NULL;
					}
				}
				printf(" %c%s%s%s%c%*s  %s\n",
						us.argv[i][-1],
						match ? GREEN : MAGENTA,
						us.argv[i],
						NORM,
						match ? '*' : ' ',
						(int)(maxlen - strlen(us.argv[i])), "",
						us.retv[i] == NULL ? "<no description found>" :
							us.retv[i]);
				while (p != NULL) {  /* continue wrapped description */
					q = p;
					p = NULL;
					if ((size_t)len < desclen) {
						for (p = q + len; p > q; p--)
							if (isspace((int)*p))
								break;
						if (p > q) {
							*p++ = '\0';
							desclen -= p - q;
						} else {
							p = NULL;
						}
					}
					printf("  %*s   %s\n", maxlen, "", q);
				}
				if (us.retv[i] != NULL)
					free(us.retv[i]);
			}

			free(us.retv);
			free(us.argv);
		} else {
			printf("%s: %s\n", atom_format(state->fmt, atom), v);
		}
	}

	if (use != NULL)
		free_set(use);
	if (state->do_describe)
		close(portdirfd);

	return EXIT_SUCCESS;
}

int quse_main(int argc, char **argv)
{
	int i;
	size_t n;
	const char *overlay;
	char *match = NULL;
	struct quse_state state = {
		.do_all         = false,
		.do_regex       = true,
		.do_describe    = false,
		.do_licence     = false,
		.do_installed   = false,
		.need_full_atom = false,
		.match          = NULL,
		.overlay        = NULL,
		.fmt            = NULL,
	};

	while ((i = GETOPT_LONG(QUSE, quse, "")) != -1) {
		switch (i) {
		case 'e': state.do_regex = false;      break;
		case 'a': state.do_all = true;         break;
		case 'L': state.do_licence = true;     break;
		case 'D': state.do_describe = true;    break;
		case 'I': state.do_installed = true;   break;
		case 'p': match = optarg;              break;
		case 'F': state.fmt = optarg;          /* fall through */
		case 'R': state.need_full_atom = true; break;
		COMMON_GETOPTS_CASES(quse)
		}
	}
	if (argc == optind && !state.do_all) {
		if (match != NULL) {
			/* default to printing everything if just package is given */
			state.do_all = true;
		} else {
			quse_usage(EXIT_FAILURE);
		}
	}

	state.argc = argc - optind;
	state.argv = &argv[optind];

	if (match != NULL) {
		state.match = atom_explode(match);
		if (state.match == NULL)
			errf("invalid atom: %s", match);
	}

	if (state.do_regex) {
		state.pregv = xmalloc(sizeof(state.pregv[0]) * state.argc);
		for (i = 0; i < state.argc; i++)
			xregcomp(&state.pregv[i], state.argv[i], REG_EXTENDED | REG_NOSUB);
	}

	if (state.fmt == NULL) {
		if (state.need_full_atom)
			if (verbose)
				state.fmt = "%[CATEGORY]%[PF]%[REPO]";
			else
				state.fmt = "%[CATEGORY]%[PN]%[REPO]";
		else
			if (verbose)
				state.fmt = "%[CATEGORY]%[PF]";
			else
				state.fmt = "%[CATEGORY]%[PN]";
	}

	if (state.do_describe && state.match == NULL) {
		array_for_each(overlays, n, overlay)
			quse_describe_flag(portroot, overlay, &state);
	} else if (state.do_installed) {
		tree_ctx *t = tree_open_vdb(portroot, portvdb);
		state.overlay = NULL;
		tree_foreach_pkg_sorted(t, quse_results_cb, &state, NULL);
		tree_close(t);
	} else {
		array_for_each(overlays, n, overlay) {
			tree_ctx *t = tree_open(portroot, overlay);
			state.overlay = overlay;
			if (t != NULL) {
				tree_foreach_pkg_sorted(t, quse_results_cb, &state, NULL);
				tree_close(t);
			}
		}
	}

	if (state.do_regex) {
		for (i = 0; i < state.argc; i++)
			regfree(&state.pregv[i]);
		free(state.pregv);
	}

	if (state.match != NULL)
		atom_implode(state.match);

	return EXIT_SUCCESS;
}
