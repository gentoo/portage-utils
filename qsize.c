/*
 * Copyright 2005-2018 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qsize

#define QSIZE_FLAGS "fsSmkbi:" COMMON_FLAGS
static struct option const qsize_long_opts[] = {
	{"filesystem", no_argument, NULL, 'f'},
	{"sum",        no_argument, NULL, 's'},
	{"sum-only",   no_argument, NULL, 'S'},
	{"megabytes",  no_argument, NULL, 'm'},
	{"kilobytes",  no_argument, NULL, 'k'},
	{"bytes",      no_argument, NULL, 'b'},
	{"ignore",      a_argument, NULL, 'i'},
	COMMON_LONG_OPTS
};
static const char * const qsize_opts_help[] = {
	"Show size used on disk",
	"Include a summary",
	"Show just the summary",
	"Display size in megabytes",
	"Display size in kilobytes",
	"Display size in bytes",
	"Ignore regexp string",
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

	size_t buflen;
	char *buf;

	size_t num_all_files, num_all_nonfiles, num_all_ignored;
	uint64_t num_all_bytes;
};

static int
qsize_cb(q_vdb_pkg_ctx *pkg_ctx, void *priv)
{
	struct qsize_opt_state *state = priv;
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	size_t i;
	depend_atom *atom;
	FILE *fp;
	size_t num_files, num_nonfiles, num_ignored;
	uint64_t num_bytes;
	bool showit = false;

	/* see if this cat/pkg is requested */
	if (array_cnt(state->atoms)) {
		depend_atom *qatom;

		snprintf(state->buf, state->buflen, "%s/%s", catname, pkgname);
		qatom = atom_explode(state->buf);
		array_for_each(state->atoms, i, atom)
			if (atom_compare(atom, qatom) == EQUAL) {
				showit = true;
				break;
			}
		atom_implode(qatom);
	} else
		showit = true;
	if (!showit)
		return EXIT_SUCCESS;

	if ((fp = q_vdb_pkg_fopenat_ro(pkg_ctx, "CONTENTS")) == NULL)
		return EXIT_SUCCESS;

	num_ignored = num_files = num_nonfiles = num_bytes = 0;
	while (getline(&state->buf, &state->buflen, fp) != -1) {
		contents_entry *e;
		regex_t *regex;
		int ok = 0;

		e = contents_parse_line(state->buf);
		if (!e)
			continue;

		array_for_each(state->ignore_regexp, i, regex)
			if (!regexec(regex, state->buf, 0, NULL, 0)) {
				num_ignored += 1;
				ok = 1;
			}
		if (ok)
			continue;

		if (e->type == CONTENTS_OBJ || e->type == CONTENTS_SYM) {
			struct stat st;
			++num_files;
			if (!fstatat(pkg_ctx->cat_ctx->ctx->portroot_fd, e->name + 1, &st, AT_SYMLINK_NOFOLLOW))
				num_bytes += (state->fs_size ? st.st_blocks * S_BLKSIZE : st.st_size);
		} else
			++num_nonfiles;
	}
	fclose(fp);
	state->num_all_bytes += num_bytes;
	state->num_all_files += num_files;
	state->num_all_nonfiles += num_nonfiles;
	state->num_all_ignored += num_ignored;

	if (!state->summary_only) {
		printf("%s%s/%s%s%s: %'zu files, %'zu non-files, ", BOLD,
		       catname, BLUE, pkgname, NORM,
		       num_files, num_nonfiles);
		if (num_ignored)
			printf("%'zu names-ignored, ", num_ignored);
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
		.num_all_bytes = 0,
		.num_all_files = 0,
		.num_all_nonfiles = 0,
		.num_all_ignored = 0,
	};

	while ((ret = GETOPT_LONG(QSIZE, qsize, "")) != -1) {
		switch (ret) {
		COMMON_GETOPTS_CASES(qsize)
		case 'f': state.fs_size = 1; break;
		case 's': state.summary = 1; break;
		case 'S': state.summary = state.summary_only = 1; break;
		case 'm': state.disp_units = MEGABYTE; state.str_disp_units = "MiB"; break;
		case 'k': state.disp_units = KILOBYTE; state.str_disp_units = "KiB"; break;
		case 'b': state.disp_units = 1; state.str_disp_units = "bytes"; break;
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

	state.buflen = _Q_PATH_MAX;
	state.buf = xmalloc(state.buflen);

	ret = q_vdb_foreach_pkg(qsize_cb, &state, NULL);

	if (state.summary) {
		printf(" %sTotals%s: %'zu files, %'zu non-files, ", BOLD, NORM,
		       state.num_all_files, state.num_all_nonfiles);
		if (state.num_all_ignored)
			printf("%'zu names-ignored, ", state.num_all_ignored);
		printf("%s %s\n",
			   make_human_readable_str(
				   state.num_all_bytes, 1, state.disp_units),
			   state.disp_units ? state.str_disp_units : "");
	}

	array_for_each(state.atoms, i, atom)
		atom_implode(atom);
	xarrayfree_int(state.atoms);
	xarrayfree(state.ignore_regexp);
	free(state.buf);

	return ret;
}

#else
DEFINE_APPLET_STUB(qsize)
#endif
