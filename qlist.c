/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qlist.c,v 1.64 2011/12/18 06:31:29 vapier Exp $
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2010 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2005 Martin Schlemmer - <azarah@gentoo.org>
 */

#ifdef APPLET_qlist

#define QLIST_FLAGS "ISULcDeados" COMMON_FLAGS
static struct option const qlist_long_opts[] = {
	{"installed", no_argument, NULL, 'I'},
	{"slots",     no_argument, NULL, 'S'},
	{"separator", no_argument, NULL, 'L'},
	{"columns",   no_argument, NULL, 'c'},
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
static const char * const qlist_opts_help[] = {
	"Just show installed packages",
	"Display installed packages with slots",
	"Display : as the slot separator",
	"Display column view",
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
static const char qlist_rcsid[] = "$Id: qlist.c,v 1.64 2011/12/18 06:31:29 vapier Exp $";
#define qlist_usage(ret) usage(ret, QLIST_FLAGS, qlist_long_opts, qlist_opts_help, lookup_applet_idx("qlist"))

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

_q_static char *q_vdb_pkg_eat(q_vdb_pkg_ctx *pkg_ctx, const char *item)
{
	static char buf[_Q_PATH_MAX];
	int fd;

	fd = q_vdb_pkg_openat(pkg_ctx, item, O_RDONLY);
	if (fd == -1)
		return NULL;

	eat_file_at(fd, item, buf, sizeof(buf));
	rmspace(buf);

	return buf;
}

static char *grab_pkg_umap(char *CAT, char *PV)
{
	static char umap[BUFSIZ];
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

	return umap;
}

static const char *umapstr(char display, char *cat, char *name)
{
	static char buf[BUFSIZ];
	char *umap = NULL;

	buf[0] = '\0';
	if (!display)
		return buf;
	if ((umap = grab_pkg_umap(cat, name)) == NULL)
		return buf;
	rmspace(umap);
	if (!strlen(umap))
		return buf;
	snprintf(buf, sizeof(buf), " %s%s%s%s%s", quiet ? "": "(", RED, umap, NORM, quiet ? "": ")");
	return buf;
}

int qlist_main(int argc, char **argv)
{
	q_vdb_ctx *ctx;
	q_vdb_cat_ctx *cat_ctx;
	q_vdb_pkg_ctx *pkg_ctx;
	int i, j, dfd, ret;
	char qlist_all = 0, just_pkgname = 0, dups_only = 0;
	char show_dir, show_obj, show_sym, show_slots, show_umap;
	struct dirent **de, **cat;
	size_t buflen;
	char *buf;
	char swap[_Q_PATH_MAX];
	queue *sets = NULL;
	depend_atom *pkgname, *atom;
	const char *slot_separator;
	int columns = 0;

	slot_separator = " ";

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	show_dir = show_obj = show_sym = show_slots = show_umap = 0;

	while ((i = GETOPT_LONG(QLIST, qlist, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qlist)
		case 'a': qlist_all = 1;
		case 'I': just_pkgname = 1; break;
		case 'L': slot_separator = ":"; break;
		case 'S': just_pkgname = 1; show_slots = 1; break;
		case 'U': just_pkgname = 1; show_umap = 1; break;
		case 'e': exact = 1; break;
		case 'd': show_dir = 1; break;
		case 'o': show_obj = 1; break;
		case 's': show_sym = 1; break;
		case 'D': dups_only = 1; exact = 1; just_pkgname = 1; break;
		case 'c': columns = 1; break;
		case 'f': break;
		}
	}
	if (columns) verbose = 0; /* if not set to zero; atom wont be exploded; segv */
	/* default to showing syms and objs */
	if (!show_dir && !show_obj && !show_sym)
		show_obj = show_sym = 1;
	if ((argc == optind) && (!just_pkgname))
		qlist_usage(EXIT_FAILURE);

	buflen = _Q_PATH_MAX;
	buf = xmalloc(buflen);
	ret = EXIT_FAILURE;

	ctx = q_vdb_open();
	if (!ctx)
		return EXIT_FAILURE;

	if ((dfd = scandirat(ctx->vdb_fd, ".", &cat, q_vdb_filter_cat, alphasort)) < 0)
		goto done;

	/* open /var/db/pkg */
	for (j = 0; j < dfd; j++) {
		int a, x;

		/* open the cateogry */
		cat_ctx = q_vdb_open_cat(ctx, cat[j]->d_name);
		if (!cat_ctx)
			continue;

		a = scandirat(ctx->vdb_fd, cat[j]->d_name, &de, q_vdb_filter_pkg, alphasort);
		if (a < 0) {
			q_vdb_close_cat(cat_ctx);
			continue;
		}

		for (x = 0; x < a; x++) {
			FILE *fp;
			static char *slotted_item, *slot;

			if (de[x]->d_name[0] == '-')
				continue;

			pkg_ctx = q_vdb_open_pkg(cat_ctx, de[x]->d_name);
			if (!pkg_ctx)
				continue;

			slot = slotted_item = NULL;

			/* see if this cat/pkg is requested */
			for (i = optind; i < argc; ++i) {
				char *name = pkg_name(argv[i]);
				snprintf(buf, buflen, "%s/%s", cat[j]->d_name, de[x]->d_name);
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
				goto next_cat;

			if (i < argc) {
				if ((slotted_item = slot_name(argv[i])) != NULL) {
					slot = q_vdb_pkg_eat(pkg_ctx, "SLOT");
					if (strcmp(slotted_item, slot) != 0)
						goto next_cat;
				}
			}
			if (just_pkgname) {
				if (dups_only) {
					pkgname = atom_explode(de[x]->d_name);
					snprintf(buf, buflen, "%s/%s", cat[j]->d_name, pkgname->P);
					snprintf(swap, sizeof(swap), "%s/%s", cat[j]->d_name, pkgname->PN);
					sets = add_set(swap, buf, sets);
					atom_implode(pkgname);
					goto next_cat;
				}
				pkgname = (verbose ? NULL : atom_explode(de[x]->d_name));
				if ((qlist_all + just_pkgname) < 2) {

					slot = NULL;

					if (show_slots)
						slot = q_vdb_pkg_eat(pkg_ctx, "SLOT");

					/* display it */
					printf("%s%s/%s%s%s%s%s%s%s%s%s%s\n", BOLD, cat[j]->d_name, BLUE,
						(!columns ? (pkgname ? pkgname->PN : de[x]->d_name) : pkgname->PN),
						(columns ? " " : ""), (columns ? pkgname->PV : ""),
						NORM, YELLOW, slot ? slot_separator : "", slot ? slot : "", NORM,
						umapstr(show_umap, cat[j]->d_name, de[x]->d_name));
				}
				if (pkgname)
					atom_implode(pkgname);

				if (qlist_all == 0)
					goto next_cat;
			}

			if (verbose > 1)
				printf("%s%s/%s%s%s\n%sCONTENTS%s:\n", BOLD, cat[j]->d_name, BLUE, de[x]->d_name, NORM, DKBLUE, NORM);

			fp = q_vdb_pkg_fopenat_ro(pkg_ctx, "CONTENTS");
			if (fp == NULL)
				goto next_cat;

			while (getline(&buf, &buflen, fp) != -1) {
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

 next_cat:
			q_vdb_close_pkg(pkg_ctx);
		}

		q_vdb_close_cat(cat_ctx);
		scandir_free(de, a);
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
					slot = grab_vdb_item("SLOT", atom->CATEGORY, atom->P);
				printf("%s%s/%s%s%s%s%s%s%s%s%s", BOLD, atom->CATEGORY, BLUE,
					(!columns ? (verbose ? atom->P : atom->PN) : atom->PN), (columns ? " " : ""), (columns ? atom->PV : ""),
					NORM, YELLOW, slot ? slot_separator : "", slot ? slot : "", NORM);
				puts(umapstr(show_umap, atom->CATEGORY, atom->P));
			}
			atom_implode(atom);
		}
		free_sets(dups);
		free_sets(sets);
	}

	scandir_free(cat, dfd);

	ret = EXIT_SUCCESS;
 done:
	q_vdb_close(ctx);
	return ret;
}

#else
DEFINE_APPLET_STUB(qlist)
#endif
