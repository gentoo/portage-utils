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

/* Solaris */
#if defined(__sun) && defined(__SVR4)
# include <sys/dklabel.h>
# define S_BLKSIZE DK_DEVID_BLKSIZE
#elif defined(__hpux__) || defined(__MINT__)
/* must not include both dir.h and dirent.h on hpux11..11 & FreeMiNT */
#elif defined(__linux__)
/* Linux systems do not need sys/dir.h as they are generally POSIX sane */
#else
# include <sys/dir.h>
#endif

/* AIX */
#ifdef _AIX
# include <sys/stat.h>
# define S_BLKSIZE DEV_BSIZE
#endif

/* Windows Interix */
#ifdef __INTERIX
# define S_BLKSIZE S_BLOCK_SIZE
#endif

/* HP-UX */
#ifdef __hpux
# define S_BLKSIZE st.st_blksize
#endif

/* Everyone else */
#ifndef S_BLKSIZE
# define S_BLKSIZE 512
#endif

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "atom.h"
#include "contents.h"
#include "human_readable.h"
#include "tree.h"
#include "xarray.h"
#include "xregex.h"

#define QSIZE_FLAGS "fsSmkbi:F:" COMMON_FLAGS
static struct option const qsize_long_opts[] = {
	{"filesystem", no_argument, NULL, 'f'},
	{"sum",        no_argument, NULL, 's'},
	{"sum-only",   no_argument, NULL, 'S'},
	{"megabytes",  no_argument, NULL, 'm'},
	{"kilobytes",  no_argument, NULL, 'k'},
	{"bytes",      no_argument, NULL, 'b'},
	{"ignore",      a_argument, NULL, 'i'},
	{"format",      a_argument, NULL, 'F'},
	COMMON_LONG_OPTS
};
static const char * const qsize_opts_help[] = {
	"Show size used on disk",
	"Include a summary",
	"Show just the summary",
	"Display all sizes in megabytes",
	"Display all sizes in kilobytes",
	"Display all sizes in bytes",
	"Ignore regexp string",
	"Print matched atom using given format string",
	COMMON_OPTS_HELP
};
#define qsize_usage(ret) usage(ret, QSIZE_FLAGS, qsize_long_opts, qsize_opts_help, NULL, lookup_applet_idx("qsize"))

struct qsize_opt_state {
	array_t *atoms;
	char **argv;
	char search_all;
	char fs_size;
	char summary;
	char summary_only;
	size_t disp_units;
	const char *str_disp_units;
	array_t *ignore_regexp;
	const char *fmt;
	bool need_full_atom:1;

	set *uniq_files;
	size_t num_all_files;
	size_t num_all_nonfiles;
	size_t num_all_ignored;
	uint64_t num_all_bytes;
};

static int
qsize_cb(tree_pkg_ctx *pkg_ctx, void *priv)
{
	struct qsize_opt_state *state = priv;
	size_t i;
	depend_atom *atom;
	char *line;
	char *savep;
	size_t num_files, num_nonfiles, num_ignored;
	uint64_t num_bytes;
	struct stat st;
	bool ok = false;
	char ikey[2 * (sizeof(size_t) * 2) + 1];  /* hex rep */
	size_t cur_uniq = cnt_set(state->uniq_files);
	bool isuniq;

	if ((line = tree_pkg_meta_get(pkg_ctx, CONTENTS)) == NULL)
		return EXIT_SUCCESS;

	num_ignored = num_files = num_nonfiles = num_bytes = 0;
	for (; (line = strtok_r(line, "\n", &savep)) != NULL; line = NULL) {
		contents_entry *e;
		regex_t *regex;

		ok = false;
		e = contents_parse_line(line);
		if (!e)
			continue;

		array_for_each(state->ignore_regexp, i, regex) {
			if (!regexec(regex, e->name, 0, NULL, 0)) {
				num_ignored++;
				ok = true;
			}
		}
		if (ok)
			continue;

		if (e->type == CONTENTS_OBJ || e->type == CONTENTS_SYM) {
			num_files++;
			if (fstatat(pkg_ctx->cat_ctx->ctx->portroot_fd,
						e->name + 1, &st, AT_SYMLINK_NOFOLLOW) == 0)
			{
				snprintf(ikey, sizeof(ikey), "%zx%zx",
						(size_t)st.st_dev, (size_t)st.st_ino);
				add_set_unique(ikey, state->uniq_files, &isuniq);
				if (isuniq)
					num_bytes +=
						state->fs_size ? st.st_blocks * S_BLKSIZE : st.st_size;
			}
		} else {
			num_nonfiles++;
		}
	}
	state->num_all_bytes += num_bytes;
	state->num_all_files += num_files;
	state->num_all_nonfiles += num_nonfiles;
	state->num_all_ignored += num_ignored;

	if (!state->summary_only) {
		char uniqbuf[32];

		cur_uniq = cnt_set(state->uniq_files) - cur_uniq;
		atom = tree_get_atom(pkg_ctx, state->need_full_atom);

		if (cur_uniq != num_files)
			snprintf(uniqbuf, sizeof(uniqbuf), " (%zu unique)", cur_uniq);
		else
			uniqbuf[0] = '\0';

		printf("%s: %zu files%s, %zu non-files, ",
				atom_format(state->fmt, atom),
				num_files, uniqbuf, num_nonfiles);
		if (num_ignored)
			printf("%zu names-ignored, ", num_ignored);
		printf("%s %s\n",
			   make_human_readable_str(num_bytes, 1, state->disp_units),
			   state->disp_units ? state->str_disp_units : "");
	}

	return EXIT_SUCCESS;
}

int qsize_main(int argc, char **argv)
{
	size_t i;
	int ret;
	tree_ctx *vdb;
	DECLARE_ARRAY(ignore_regexp);
	depend_atom *atom;
	DECLARE_ARRAY(atoms);
	struct qsize_opt_state state = {
		.atoms = atoms,
		.search_all = 0,
		.fs_size = 0,
		.summary = 0,
		.summary_only = 0,
		.disp_units = 0,
		.str_disp_units = NULL,
		.ignore_regexp = ignore_regexp,
		.uniq_files = create_set(),
		.num_all_files = 0,
		.num_all_nonfiles = 0,
		.num_all_ignored = 0,
		.num_all_bytes = 0,
		.need_full_atom = false,
		.fmt = NULL,
	};

	while ((ret = GETOPT_LONG(QSIZE, qsize, "")) != -1) {
		switch (ret) {
		COMMON_GETOPTS_CASES(qsize)
		case 'f': state.fs_size = 1;                       break;
		case 's': state.summary = 1;                       break;
		case 'S': state.summary = state.summary_only = 1;  break;
		case 'm': state.disp_units = MEGABYTE;
				  state.str_disp_units = "MiB";            break;
		case 'k': state.disp_units = KILOBYTE;
				  state.str_disp_units = "KiB";            break;
		case 'b': state.disp_units = 1;
				  state.str_disp_units = "bytes";          break;
		case 'F': state.fmt = optarg;
				  state.need_full_atom = true;             break;
		case 'i': {
			regex_t regex;
			xregcomp(&regex, optarg, REG_EXTENDED|REG_NOSUB);
			xarraypush(state.ignore_regexp, &regex, sizeof(regex));
			break;
		}
		}
	}

	argc -= optind;
	argv += optind;
	for (i = 0; i < (size_t)argc; ++i) {
		atom = atom_explode(argv[i]);
		if (!atom)
			warn("invalid atom: %s", argv[i]);
		else
			xarraypush_ptr(state.atoms, atom);
	}

	if (state.fmt == NULL) {
		if (verbose)
			state.fmt = "%[CATEGORY]%[PF]";
		else
			state.fmt = "%[CATEGORY]%[PN]";
	}

	vdb = tree_open_vdb(portroot, portvdb);
	if (vdb != NULL) {
		if (array_cnt(atoms) > 0) {
			array_for_each(atoms, i, atom) {
				ret = tree_foreach_pkg_fast(vdb, qsize_cb, &state, atom);
			}
		} else {
			ret = tree_foreach_pkg_fast(vdb, qsize_cb, &state, NULL);
		}
		tree_close(vdb);
	}

	if (state.summary) {
		char uniqbuf[32];
		size_t uniq_files = cnt_set(state.uniq_files);

		if (uniq_files != state.num_all_files)
			snprintf(uniqbuf, sizeof(uniqbuf), " (%zu unique)", uniq_files);
		else
			uniqbuf[0] = '\0';

		printf(" %sTotals%s: %zu files%s, %zu non-files, ", BOLD, NORM,
		       state.num_all_files, uniqbuf, state.num_all_nonfiles);
		if (state.num_all_ignored)
			printf("%zu names-ignored, ", state.num_all_ignored);
		printf("%s %s\n",
			   make_human_readable_str(
				   state.num_all_bytes, 1, state.disp_units),
			   state.disp_units ? state.str_disp_units : "");
	}

	array_for_each(state.atoms, i, atom)
		atom_implode(atom);
	xarrayfree_int(state.atoms);
	xarrayfree(state.ignore_regexp);
	free_set(state.uniq_files);

	return ret;
}
