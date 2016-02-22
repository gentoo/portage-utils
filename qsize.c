/*
 * Copyright 2005-2014 Gentoo Foundation
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
#define qsize_usage(ret) usage(ret, QSIZE_FLAGS, qsize_long_opts, qsize_opts_help, lookup_applet_idx("qsize"))

int qsize_main(int argc, char **argv)
{
	q_vdb_ctx *ctx;
	q_vdb_cat_ctx *cat_ctx;
	q_vdb_pkg_ctx *pkg_ctx;
	size_t i;
	struct stat st;
	char fs_size = 0, summary = 0, summary_only = 0;
	size_t num_all_files, num_all_nonfiles, num_all_ignored;
	size_t num_files, num_nonfiles, num_ignored;
	uint64_t num_all_bytes, num_bytes;
	size_t disp_units = 0;
	const char *str_disp_units = NULL;
	size_t buflen;
	char *buf;
	char filename[_Q_PATH_MAX], *filename_root;
	depend_atom *atom;
	DECLARE_ARRAY(atoms);
	DECLARE_ARRAY(ignore_regexp);

	while ((i = GETOPT_LONG(QSIZE, qsize, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qsize)
		case 'f': fs_size = 1; break;
		case 's': summary = 1; break;
		case 'S': summary = summary_only = 1; break;
		case 'm': disp_units = MEGABYTE; str_disp_units = "MiB"; break;
		case 'k': disp_units = KILOBYTE; str_disp_units = "KiB"; break;
		case 'b': disp_units = 1; str_disp_units = "bytes"; break;
		case 'i': {
			regex_t regex;
			xregcomp(&regex, optarg, REG_EXTENDED|REG_NOSUB);
			xarraypush(ignore_regexp, &regex, sizeof(regex));
			break;
		}
		}
	}

	argc -= optind;
	argv += optind;
	for (i = 0; i < argc; ++i) {
		atom = atom_explode(argv[i]);
		if (!atom)
			warn("invalid atom: %s", argv[i]);
		else
			xarraypush_ptr(atoms, atom);
	}

	num_all_bytes = num_all_files = num_all_nonfiles = num_all_ignored = 0;

	strcpy(filename, portroot);
	filename_root = filename + strlen(filename);
	buflen = _Q_PATH_MAX;
	buf = xmalloc(buflen);

	ctx = q_vdb_open();
	if (!ctx)
		return EXIT_FAILURE;

	/* open /var/db/pkg */
	while ((cat_ctx = q_vdb_next_cat(ctx))) {
		/* open the cateogry */
		const char *catname = cat_ctx->name;
		while ((pkg_ctx = q_vdb_next_pkg(cat_ctx))) {
			const char *pkgname = pkg_ctx->name;
			FILE *fp;
			bool showit = false;

			/* see if this cat/pkg is requested */
			if (array_cnt(atoms)) {
				depend_atom *qatom;

				snprintf(buf, buflen, "%s/%s", catname, pkgname);
				qatom = atom_explode(buf);
				array_for_each(atoms, i, atom)
					if (atom_compare(atom, qatom) == EQUAL) {
						showit = true;
						break;
					}
				atom_implode(qatom);
			} else
				showit = true;
			if (!showit)
				goto next_pkg;

			if ((fp = q_vdb_pkg_fopenat_ro(pkg_ctx, "CONTENTS")) == NULL)
				goto next_pkg;

			num_ignored = num_files = num_nonfiles = num_bytes = 0;
			while (getline(&buf, &buflen, fp) != -1) {
				contents_entry *e;
				regex_t *regex;
				int ok = 0;

				e = contents_parse_line(buf);
				if (!e)
					continue;

				array_for_each(ignore_regexp, i, regex)
					if (!regexec(regex, buf, 0, NULL, 0)) {
						num_ignored += 1;
						ok = 1;
					}
				if (ok)
					continue;

				if (e->type == CONTENTS_OBJ || e->type == CONTENTS_SYM) {
					strcpy(filename_root, e->name);
					++num_files;
					if (!lstat(filename, &st))
						num_bytes += (fs_size ? st.st_blocks * S_BLKSIZE : st.st_size);
				} else
					++num_nonfiles;
			}
			fclose(fp);
			num_all_bytes += num_bytes;
			num_all_files += num_files;
			num_all_nonfiles += num_nonfiles;
			num_all_ignored += num_ignored;

			if (!summary_only) {
				printf("%s%s/%s%s%s: %'zu files, %'zu non-files, ", BOLD,
				       catname, BLUE, pkgname, NORM,
				       num_files, num_nonfiles);
				if (num_ignored)
					printf("%'zu names-ignored, ", num_ignored);
				if (disp_units)
					printf("%s %s\n",
					       make_human_readable_str(num_bytes, 1, disp_units),
					       str_disp_units);
				else
					printf("%'"PRIu64"%s%"PRIu64" KiB\n",
					       num_bytes / KILOBYTE,
					       decimal_point,
					       ((num_bytes % KILOBYTE) * 1000) / KILOBYTE);
			}

 next_pkg:
			q_vdb_close_pkg(pkg_ctx);
		}
	}

	if (summary) {
		printf(" %sTotals%s: %'zu files, %'zu non-files, ", BOLD, NORM,
		       num_all_files, num_all_nonfiles);
		if (num_all_ignored)
			printf("%'zu names-ignored, ", num_all_ignored);
		if (disp_units)
			printf("%s %s\n",
			       make_human_readable_str(num_all_bytes, 1, disp_units),
			       str_disp_units);
		else
			printf("%'"PRIu64"%s%"PRIu64" MiB\n",
			       num_all_bytes / MEGABYTE,
			       decimal_point,
			       ((num_all_bytes % MEGABYTE) * 1000) / MEGABYTE);
	}
	array_for_each(atoms, i, atom)
		atom_implode(atom);
	xarrayfree_int(atoms);
	xarrayfree(ignore_regexp);
	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qsize)
#endif
