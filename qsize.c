/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qsize.c,v 1.34 2010/04/07 05:58:16 solar Exp $
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2010 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qsize

#define QSIZE_FLAGS "fasSmkbi:" COMMON_FLAGS
static struct option const qsize_long_opts[] = {
	{"filesystem", no_argument, NULL, 'f'},
	{"all",        no_argument, NULL, 'a'},
	{"sum",        no_argument, NULL, 's'},
	{"sum-only",   no_argument, NULL, 'S'},
	{"megabytes",  no_argument, NULL, 'm'},
	{"kilobytes",  no_argument, NULL, 'k'},
	{"bytes",      no_argument, NULL, 'b'},
	{"ignore",      a_argument, NULL, 'i'},
	COMMON_LONG_OPTS
};
static const char *qsize_opts_help[] = {
	"Show size used on disk",
	"Size all installed packages",
	"Include a summary",
	"Show just the summary",
	"Display size in megabytes",
	"Display size in kilobytes",
	"Display size in bytes",
	"Ignore regexp string",
	COMMON_OPTS_HELP
};
static const char qsize_rcsid[] = "$Id: qsize.c,v 1.34 2010/04/07 05:58:16 solar Exp $";
#define qsize_usage(ret) usage(ret, QSIZE_FLAGS, qsize_long_opts, qsize_opts_help, lookup_applet_idx("qsize"))

int qsize_main(int argc, char **argv)
{
	DIR *dir, *dirp;
	int i;
	struct dirent *dentry, *de;
	char search_all = 0;
	struct stat st;
	char fs_size = 0, summary = 0, summary_only = 0;
	size_t num_all_files, num_all_nonfiles, num_all_ignored;
	size_t num_files, num_nonfiles, num_ignored;
	uint64_t num_all_bytes, num_bytes;
	size_t disp_units = 0;
	const char *str_disp_units = NULL;
	char buf[_Q_PATH_MAX];
	queue *ignore_regexp = NULL;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QSIZE, qsize, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qsize)
		case 'f': fs_size = 1; break;
		case 'a': search_all = 1; break;
		case 's': summary = 1; break;
		case 'S': summary = summary_only = 1; break;
		case 'm': disp_units = MEGABYTE; str_disp_units = "MB"; break;
		case 'k': disp_units = KILOBYTE; str_disp_units = "KB"; break;
		case 'b': disp_units = 1; str_disp_units = "bytes"; break;
		case 'i': ignore_regexp = add_set(optarg, optarg, ignore_regexp); break;
		}
	}
	if ((argc == optind) && !search_all)
		qsize_usage(EXIT_FAILURE);

	xchdir(portroot);
	xchdir(portvdb);
	if ((dir = opendir(".")) == NULL)
		return EXIT_FAILURE;

	num_all_bytes = num_all_files = num_all_nonfiles = num_all_ignored = 0;

	/* open /var/db/pkg */
	while ((dentry = q_vdb_get_next_dir(dir))) {

		if (chdir(dentry->d_name) != 0)
			continue;
		if ((dirp = opendir(".")) == NULL)
			continue;

		/* open the cateogry */
		while ((de = readdir(dirp)) != NULL) {
			FILE *fp;
			if (*de->d_name == '.')
				continue;

			/* see if this cat/pkg is requested */
			if (!search_all) {
				for (i = optind; i < argc; ++i) {
					snprintf(buf, sizeof(buf), "%s/%s", dentry->d_name,
					         de->d_name);
					if (rematch(argv[i], buf, REG_EXTENDED) == 0)
						break;
					if (rematch(argv[i], de->d_name, REG_EXTENDED) == 0)
						break;
				}
				if (i == argc)
					continue;
			}

			snprintf(buf, sizeof(buf), "%s%s/%s/%s/CONTENTS", portroot, portvdb,
			         dentry->d_name, de->d_name);
			if ((fp = fopen(buf, "r")) == NULL)
				continue;

			num_ignored = num_files = num_nonfiles = num_bytes = 0;
			while ((fgets(buf, sizeof(buf), fp)) != NULL) {
				contents_entry *e;
				queue *ll;
				int ok = 0;

				e = contents_parse_line(buf);
				if (!e)
					continue;

				for (ll = ignore_regexp; ll != NULL; ll = ll->next) {
					if (rematch(ll->name, e->name, REG_EXTENDED) == 0) {
						num_ignored += 1;
						ok = 1;
					}
				}
				if (ok)
					continue;

				if (e->type == CONTENTS_OBJ || e->type == CONTENTS_SYM) {
					++num_files;
					if (!lstat(e->name, &st))
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
				printf("%s%s/%s%s%s: %lu files, %lu non-files, ", BOLD,
				       basename(dentry->d_name), BLUE, de->d_name, NORM,
				       (unsigned long)num_files,
				       (unsigned long)num_nonfiles);
				if (num_ignored)
					printf("%lu names-ignored, ", (unsigned long)num_ignored);
				if (disp_units)
					printf("%s %s\n",
					       make_human_readable_str(num_bytes, 1, disp_units),
					       str_disp_units);
				else
					printf("%lu.%lu KB\n",
					       (unsigned long)(num_bytes / KILOBYTE),
					       (unsigned long)(((num_bytes%KILOBYTE)*1000)/KILOBYTE));
			}
		}
		closedir(dirp);
		xchdir("..");
	}

	if (summary) {
		printf(" %sTotals%s: %lu files, %lu non-files, ", BOLD, NORM,
		       (unsigned long)num_all_files,
		       (unsigned long)num_all_nonfiles);
		if (num_all_ignored)
			printf("%lu names-ignored, ", (unsigned long)num_all_ignored);
		if (disp_units)
			printf("%s %s\n",
			       make_human_readable_str(num_all_bytes, 1, disp_units),
			       str_disp_units);
		else
			printf("%lu.%lu MB\n",
			       (unsigned long)(num_all_bytes / MEGABYTE),
			       (unsigned long)(((num_all_bytes%MEGABYTE)*1000)/MEGABYTE));
	}
	free_sets(ignore_regexp);
	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qsize)
#endif
