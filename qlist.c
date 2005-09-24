/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qlist.c,v 1.15 2005/09/24 01:56:36 vapier Exp $
 *
 * Copyright 2005 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2005 Martin Schlemmer - <azarah@gentoo.org>
 */

#define QLIST_FLAGS "Iedos" COMMON_FLAGS
static struct option const qlist_long_opts[] = {
	{"installed", no_argument, NULL, 'I'},
	{"exact",     no_argument, NULL, 'e'},
	{"dir",       no_argument, NULL, 'd'},
	{"obj",       no_argument, NULL, 'o'},
	{"sym",       no_argument, NULL, 's'},
	/* {"file",       a_argument, NULL, 'f'}, */
	COMMON_LONG_OPTS
};
static const char *qlist_opts_help[] = {
	"Just show installed packages",
	"Exact match (only CAT/PN or PN without PV)",
	"Only show directories",
	"Only show objects",
	"Only show symlinks",
	/* "query filename for pkgname", */
	COMMON_OPTS_HELP
};
#define qlist_usage(ret) usage(ret, QLIST_FLAGS, qlist_long_opts, qlist_opts_help, APPLET_QLIST)


int qlist_main(int argc, char **argv)
{
	DIR *dir, *dirp;
	int i;
	char just_pkgname = 0;
	char show_dir, show_obj, show_sym;
	struct dirent *dentry, *de;
	char buf[_POSIX_PATH_MAX];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	show_dir = show_obj = show_sym = 0;

	while ((i = GETOPT_LONG(QLIST, qlist, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qlist)
		case 'I': just_pkgname = 1; break;
		case 'e': exact = 1; break;
		case 'd': show_dir = 1; break;
		case 'o': show_obj = 1; break;
		case 's': show_sym = 1; break;
		case 'f': break;
		}
	}
	/* default to showing syms and objs */
	if (!show_dir && !show_obj && !show_sym)
		show_obj = show_sym = 1;
	if ((argc == optind) && (!just_pkgname))
		qlist_usage(EXIT_FAILURE);

	if (chdir(portroot))
		errp("could not chdir(%s) for ROOT", portroot);

	if (chdir(portvdb) != 0 || (dir = opendir(".")) == NULL)
		return EXIT_FAILURE;

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
			for (i = optind; i < argc; ++i) {
				snprintf(buf, sizeof(buf), "%s/%s", dentry->d_name, 
					 de->d_name);

				if (exact) {
					depend_atom *atom;
					if ((atom = atom_explode(buf)) == NULL) {
						warn("invalid atom %s", buf);
						continue;
					}
					snprintf(buf, sizeof(buf), "%s/%s",
						 atom->CATEGORY, atom->PN);
					atom_implode(atom);
					if (strcmp(argv[i], buf) == 0)
						break;
					if (strcmp(argv[i], strstr(buf, "/") + 1) == 0)
						break;
				} else {
					if (rematch(argv[i], buf, REG_EXTENDED) == 0)
						break;
					if (rematch(argv[i], de->d_name, REG_EXTENDED) == 0)
						break;
				}
			}
			if ((i == argc) && (argc != optind))
				continue;

			if (just_pkgname) {
				printf("%s%s/%s%s%s\n", BOLD, dentry->d_name, BLUE, 
				       de->d_name, NORM);
				continue;
			}

			snprintf(buf, sizeof(buf), "%s%s/%s/%s/CONTENTS", portroot, portvdb,
			         dentry->d_name, de->d_name);
			if ((fp = fopen(buf, "r")) == NULL)
				continue;

			while ((fgets(buf, sizeof(buf), fp)) != NULL) {
				contents_entry *e;

				e = contents_parse_line(buf);
				if (!e)
					continue;

				switch (e->type) {
					case CONTENTS_DIR:
						if (show_dir)
							printf("%s/\n", e->name);
						break;
					case CONTENTS_OBJ:
						if (show_obj)
							printf("%s\n", e->name);
						break;
					case CONTENTS_SYM:
						if (show_sym) {
							if (verbose)
								printf("%s -> %s\n", e->name, e->sym_target);
							else
								printf("%s\n", e->name);
						}
						break;
				}
			}
			fclose(fp);
		}
		closedir(dirp);
		chdir("..");
	}

	return EXIT_SUCCESS;
}
