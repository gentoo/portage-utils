/*
 * Copyright 2005-2007 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qlist.c,v 1.53 2010/01/13 18:07:14 vapier Exp $
 *
 * Copyright 2005-2007 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2007 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2005 Martin Schlemmer - <azarah@gentoo.org>
 */

#ifdef APPLET_qlist

#define QLIST_FLAGS "ISULDeados" COMMON_FLAGS
static struct option const qlist_long_opts[] = {
	{"installed", no_argument, NULL, 'I'},
	{"slots",     no_argument, NULL, 'S'},
	{"separator", no_argument, NULL, 'L'},
	{"umap",      no_argument, NULL, 'U'},
	{"dups",      no_argument, NULL, 'D'},
	{"exact",     no_argument, NULL, 'e'},
	{"all",       no_argument, NULL, 'a'},
	{"dir",       no_argument, NULL, 'd'},
	{"obj",       no_argument, NULL, 'o'},
	{"sym",       no_argument, NULL, 's'},
	/* {"file",       a_argument, NULL, 'f'}, */
	COMMON_LONG_OPTS
};
static const char *qlist_opts_help[] = {
	"Just show installed packages",
	"Display installed packages with slots",
	"Display : as the slot separator",
	"Display installed packages with flags used",
	"Only show package dups",
	"Exact match (only CAT/PN or PN without PV)",
	"Show every installed package",
	"Only show directories",
	"Only show objects",
	"Only show symlinks",
	/* "query filename for pkgname", */
	COMMON_OPTS_HELP
};
static const char qlist_rcsid[] = "$Id: qlist.c,v 1.53 2010/01/13 18:07:14 vapier Exp $";
#define qlist_usage(ret) usage(ret, QLIST_FLAGS, qlist_long_opts, qlist_opts_help, lookup_applet_idx("qlist"))

extern char *grab_vdb_item(const char *, const char *, const char *);

queue *filter_dups(queue *sets);
queue *filter_dups(queue *sets)
{
	queue *ll = NULL;
	queue *dups = NULL;
	queue *list = NULL;

	for (list = sets; list != NULL;  list = list->next) {
		for (ll = sets; ll != NULL; ll = ll->next) {
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

static char *grab_pkg_umap(char *CAT, char *PV)
{
	static char umap[BUFSIZ] = "";
	char *use = NULL;
	char *iuse = NULL;
	int use_argc = 0, iuse_argc = 0;
	char **use_argv = NULL, **iuse_argv = NULL;
	queue *ll = NULL;
	queue *sets = NULL;
	int i, u;

	if ((use = grab_vdb_item("USE", CAT, PV)) == NULL)
		return NULL;

	memset(umap, 0, sizeof(umap)); /* reset the buffer */

	/* grab_vdb is a static function so save it to memory right away */
	makeargv(use, &use_argc, &use_argv);
	if ((iuse = grab_vdb_item("IUSE", CAT, PV)) != NULL) {
		for (i = 0; i < strlen(iuse); i++)
			if (iuse[i] == '+') iuse[i] = ' ';
		makeargv(iuse, &iuse_argc, &iuse_argv);
		for (u = 1; u < use_argc; u++) {
			for (i = 1; i < iuse_argc; i++) {
				if ((strcmp(use_argv[u], iuse_argv[i])) == 0) {
					strncat(umap, use_argv[u], sizeof(umap)-strlen(umap)-1);
					strncat(umap, " ", sizeof(umap)-strlen(umap)-1);
				}
			}
		}
		freeargv(iuse_argc, iuse_argv);
	}
	freeargv(use_argc, use_argv);

	/* filter out the dup use flags */
	use_argc = 0; use_argv = NULL;
	makeargv(umap, &use_argc, &use_argv);
	for (i = 1; i < use_argc; i++) {
		int ok = 0;
		sets = del_set(use_argv[i], sets, &ok);
		sets = add_set(use_argv[i], use_argv[i], sets);
	}
	memset(umap, 0, sizeof(umap)); /* reset the buffer */
	strcpy(umap, "");
	for (ll = sets; ll != NULL; ll = ll->next) {
		strncat(umap, ll->name, sizeof(umap)-strlen(umap)-1);
		strncat(umap, " ", sizeof(umap)-strlen(umap)-1);
	}
	freeargv(use_argc, use_argv);
	free_sets(sets);
	/* end filter */

	return (char *) umap;
}

static char *umapstr(char display, char *cat, char *name);
static char *umapstr(char display, char *cat, char *name)
{
	static char buf[BUFSIZ] = "";
	char *umap = NULL;

	if (!display) return (char *) "";
	if ((umap = grab_pkg_umap(cat, name)) == NULL)
		return (char *) "";
	rmspace(umap);
	if (!strlen(umap))
		return (char *) "";
	snprintf(buf, sizeof(buf), " %s%s%s%s%s", quiet ? "": "(", RED, umap, NORM, quiet ? "": ")");
	return buf;
}

int qlist_main(int argc, char **argv)
{
	int i, j, dfd;
	char qlist_all = 0, just_pkgname = 0, dups_only = 0;
	char show_dir, show_obj, show_sym, show_slots, show_umap;
	struct dirent **de, **cat;
	char buf[_Q_PATH_MAX];
	char swap[_Q_PATH_MAX];
	queue *sets = NULL;
	depend_atom *pkgname, *atom;
	char *slot_separator;

	slot_separator = (char *) " ";

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	show_dir = show_obj = show_sym = show_slots = show_umap = 0;

	while ((i = GETOPT_LONG(QLIST, qlist, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qlist)
		case 'a': qlist_all = 1;
		case 'I': just_pkgname = 1; break;
		case 'L': slot_separator = (char *) ":"; break;
		case 'S': just_pkgname = 1; show_slots = 1; break;
		case 'U': just_pkgname = 1; show_umap = 1; break;
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
	for (j = 0; j < dfd; j++) {
		int a, x;
		if (cat[j]->d_name[0] == '-')
			continue;
		if (chdir(cat[j]->d_name) != 0)
			continue;

		/* open the cateogry */
		if ((a = scandir(".", &de, filter_hidden, alphasort)) < 0)
			continue;

		for (x = 0; x < a; x++) {
			FILE *fp;
			static char *slotted_item, *slot;

			if (de[x]->d_name[0] == '.' || de[x]->d_name[0] == '-')
				continue;

			slot = slotted_item = NULL;

			/* see if this cat/pkg is requested */
			for (i = optind; i < argc; ++i) {
				char *name = pkg_name(argv[i]);
				snprintf(buf, sizeof(buf), "%s/%s", cat[j]->d_name,
					 de[x]->d_name);
				/* printf("buf=%s:%s\n", buf,grab_vdb_item("SLOT", cat[j]->d_name, de[x]->d_name)); */
				if (exact) {
					if ((atom = atom_explode(buf)) == NULL) {
						warn("invalid atom %s", buf);
						continue;
					}
					snprintf(swap, sizeof(swap), "%s/%s",
						 atom->CATEGORY, atom->PN);
					atom_implode(atom);
					if ((strcmp(name, swap) == 0) || (strcmp(name, buf) == 0))
						break;
					if ((strcmp(name, strstr(swap, "/") + 1) == 0) || (strcmp(name, strstr(buf, "/") + 1) == 0))
						break;
				} else {
					if (charmatch(name, buf) == 0)
						break;
					if (charmatch(name, de[x]->d_name) == 0)
						break;
					if (rematch(name, buf, REG_EXTENDED) == 0)
						break;
					if (rematch(name, de[x]->d_name, REG_EXTENDED) == 0)
						break;
				}
			}
			if ((i == argc) && (argc != optind))
				continue;

			if (i < argc) {
				if ((slotted_item = slot_name(argv[i])) != NULL) {
					slot = grab_vdb_item("SLOT", cat[j]->d_name, de[x]->d_name);
					if (strcmp(slotted_item, slot) != 0)
						continue;
				}
			}
			if (just_pkgname) {
				if (dups_only) {
					pkgname = atom_explode(de[x]->d_name);
					snprintf(buf, sizeof(buf), "%s/%s", cat[j]->d_name, pkgname->P);
					snprintf(swap, sizeof(swap), "%s/%s", cat[j]->d_name, pkgname->PN);
					sets = add_set(swap, buf, sets);
					atom_implode(pkgname);
					continue;
				}
				pkgname = (verbose ? NULL : atom_explode(de[x]->d_name));
				if ((qlist_all + just_pkgname) < 2) {

					slot = NULL;

					if (show_slots)
						slot = grab_vdb_item("SLOT", cat[j]->d_name, de[x]->d_name);

					/* display it */
					printf("%s%s/%s%s%s%s%s%s%s", BOLD, cat[j]->d_name, BLUE,
					       (pkgname ? pkgname->PN : de[x]->d_name), NORM,
						YELLOW, slot ? slot_separator : "", slot ? slot : "", NORM);
					puts(umapstr(show_umap, cat[j]->d_name, de[x]->d_name));
				}
				if (pkgname)
					atom_implode(pkgname);

				if (qlist_all == 0)
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
		while (a--) free(de[a]);
		free(de);
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
			if (ok)	{
				char *slot = NULL;
				if (show_slots)
					slot = (char *) grab_vdb_item("SLOT", (const char *) atom->CATEGORY, (const char *) atom->P);
				printf("%s%s/%s%s%s%s%s%s%s", BOLD, atom->CATEGORY, BLUE,
					(verbose ? atom->P : atom->PN), NORM,
					YELLOW, slot ? slot_separator : "", slot ? slot : "", NORM);
				puts(umapstr(show_umap, atom->CATEGORY, atom->P));
			}
			atom_implode(atom);
		}
		free_sets(dups);
		free_sets(sets);
	}
	while (dfd--) free(cat[dfd]);
	free(cat);
	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qlist)
#endif
