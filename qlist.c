/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qlist.c,v 1.32 2006/01/26 02:32:04 vapier Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2005 Martin Schlemmer - <azarah@gentoo.org>
 */

#ifdef APPLET_qlist

#define QLIST_FLAGS "IDedos" COMMON_FLAGS
static struct option const qlist_long_opts[] = {
	{"installed", no_argument, NULL, 'I'},
	{"dups",      no_argument, NULL, 'D'},
	{"exact",     no_argument, NULL, 'e'},
	{"dir",       no_argument, NULL, 'd'},
	{"obj",       no_argument, NULL, 'o'},
	{"sym",       no_argument, NULL, 's'},
	/* {"file",       a_argument, NULL, 'f'}, */
	COMMON_LONG_OPTS
};
static const char *qlist_opts_help[] = {
	"Just show installed packages",
	"Only show package dups",
	"Exact match (only CAT/PN or PN without PV)",
	"Only show directories",
	"Only show objects",
	"Only show symlinks",
	/* "query filename for pkgname", */
	COMMON_OPTS_HELP
};
static const char qlist_rcsid[] = "$Id: qlist.c,v 1.32 2006/01/26 02:32:04 vapier Exp $";
#define qlist_usage(ret) usage(ret, QLIST_FLAGS, qlist_long_opts, qlist_opts_help, lookup_applet_idx("qlist"))


queue *filter_dups(queue *sets);
queue *filter_dups(queue *sets) {
	queue *ll = NULL;
	queue *dups = NULL;
	queue *list = NULL;

	for (list = sets ; list != NULL;  list = list->next) {
		for ( ll = sets ; ll != NULL; ll = ll->next) {
			if ((strcmp(ll->name, list->name) == 0) && (strcmp(ll->item, list->item) != 0)) {
					int ok = 0;
					dups = del_set(ll->item, dups, &ok);
					ok = 0;
					dups = add_set(ll->item, ll->item, dups);
				}
		}
	}
	return dups;
}

int qlist_main(int argc, char **argv)
{
	int i, j, dfd;
	char just_pkgname = 0, dups_only = 0;
	char show_dir, show_obj, show_sym;
	struct dirent **de, **cat;
	char buf[_Q_PATH_MAX];
	char swap[_Q_PATH_MAX];
	queue *sets = NULL;
	depend_atom *pkgname, *atom;

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
		case 'D': dups_only = 1; exact = 1; just_pkgname = 1; break;
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

	if (chdir(portvdb) != 0)
		return EXIT_FAILURE;


	if ((dfd = scandir(".", &cat, filter_hidden, alphasort)) < 0)
		return EXIT_FAILURE;

	/* open /var/db/pkg */
	for (j = 0; j < dfd ; j++) {
		int a, x;
		if (cat[j]->d_name[0] == '-')
			continue;
		if (chdir(cat[j]->d_name) != 0)
			continue;

		/* open the cateogry */
		if ((a = scandir(".", &de, filter_hidden, alphasort)) < 0)
			continue;

		for (x = 0 ; x < a; x++) {
			FILE *fp;

			if (de[x]->d_name[0] == '.' || de[x]->d_name[0] == '-')
				continue;

			/* see if this cat/pkg is requested */
			for (i = optind; i < argc; ++i) {
				snprintf(buf, sizeof(buf), "%s/%s", cat[j]->d_name, 
					 de[x]->d_name);

				if (exact) {
					if ((atom = atom_explode(buf)) == NULL) {
						warn("invalid atom %s", buf);
						continue;
					}
					snprintf(swap, sizeof(buf), "%s/%s",
						 atom->CATEGORY, atom->PN);
					atom_implode(atom);
					if ((strcmp(argv[i], swap) == 0) || (strcmp(argv[i], buf) == 0))
						break;
					if ((strcmp(argv[i], strstr(swap, "/") + 1) == 0) || (strcmp(argv[i], strstr(buf, "/") + 1) == 0))
						break;
				} else {
					if (rematch(argv[i], buf, REG_EXTENDED) == 0)
						break;
					if (rematch(argv[i], de[x]->d_name, REG_EXTENDED) == 0)
						break;
				}
			}
			if ((i == argc) && (argc != optind))
				continue;

			if (just_pkgname) {
				if (dups_only) {
					pkgname = atom_explode(de[x]->d_name);
					snprintf(buf, sizeof(buf), "%s/%s", cat[j]->d_name, pkgname->P);
					sets = add_set(pkgname->PN, buf, sets);
					atom_implode(pkgname);
					continue;
				}
				pkgname = (verbose ? NULL : atom_explode(de[x]->d_name));
				printf("%s%s/%s%s%s\n", BOLD, cat[j]->d_name, BLUE, 
				       (pkgname ? pkgname->PN : de[x]->d_name), NORM);
				if (pkgname) atom_implode(pkgname);
				continue;
			}

			snprintf(buf, sizeof(buf), "%s%s/%s/%s/CONTENTS", portroot, portvdb,
			         cat[j]->d_name, de[x]->d_name);

			if (verbose > 1)
				printf("%s%s/%s%s%s\n%sCONTENTS%s:\n", BOLD, cat[j]->d_name, BLUE, de[x]->d_name, NORM, DKBLUE, NORM);

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
							printf("%s%s%s/\n", verbose > 1 ? YELLOW : "" , e->name, verbose > 1 ? NORM : "");
						break;
					case CONTENTS_OBJ:
						if (show_obj)
							printf("%s%s%s\n", verbose > 1 ? WHITE : "" , e->name, verbose > 1 ? NORM : "");
						break;
					case CONTENTS_SYM:
						if (show_sym) {
							if (verbose)
								printf("%s%s -> %s%s\n", verbose > 1 ? CYAN : "", e->name, e->sym_target, NORM);
							else
								printf("%s\n", e->name);
						}
						break;
				}
			}
			fclose(fp);
		}
		while(a--) free(de[a]);
		chdir("..");
	}

	if (dups_only) {
		char last[126];
		queue *ll, *dups = filter_dups(sets);
		last[0] = 0;

		for (ll = dups; ll != NULL; ll = ll->next) {
			int ok = 1;
			atom = atom_explode(ll->item);
			if (strcmp(last, atom->PN) == 0)
				if (!verbose)
					ok = 0;
			strncpy(last, atom->PN, sizeof(last));
			if (ok)	printf("%s%s/%s%s%s\n", BOLD, atom->CATEGORY, BLUE,
					(verbose ? atom->P : atom->PN), NORM);
			atom_implode(atom);
		}
		free_sets(dups);
		free_sets(sets);
	}
	while(dfd--) free(cat[dfd]);
	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qlist)
#endif
