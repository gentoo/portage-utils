/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qlist.c,v 1.67 2011/12/20 22:47:45 vapier Exp $
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
static const char qlist_rcsid[] = "$Id: qlist.c,v 1.67 2011/12/20 22:47:45 vapier Exp $";
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

	eat_file_at(pkg_ctx->fd, item, buf, sizeof(buf));
	rmspace(buf);

	return buf;
}

static char *grab_pkg_umap(const char *CAT, const char *PV)
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

static const char *umapstr(char display, const char *cat, const char *name)
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

_q_static bool qlist_match(q_vdb_pkg_ctx *pkg_ctx, const char *name, bool exact)
{
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	char buf[_Q_PATH_MAX];
	char swap[_Q_PATH_MAX];
	depend_atom *atom;

	snprintf(buf, sizeof(buf), "%s/%s", catname, pkgname);
	/* printf("buf=%s:%s\n", buf,grab_vdb_item("SLOT", catname, pkgname)); */
	if (exact) {
		if (strcmp(name, buf) == 0)
			return true;
		if (strcmp(name, strstr(buf, "/") + 1) == 0)
			return true;

		if ((atom = atom_explode(buf)) == NULL) {
			warn("invalid atom %s", buf);
			return false;
		}
		snprintf(swap, sizeof(swap), "%s/%s",
			 atom->CATEGORY, atom->PN);
		atom_implode(atom);
		if (strcmp(name, swap) == 0)
			return true;
		if (strcmp(name, strstr(swap, "/") + 1) == 0)
			return true;
	} else {
		if (charmatch(name, buf) == 0)
			return true;
		if (charmatch(name, pkgname) == 0)
			return true;
		if (rematch(name, buf, REG_EXTENDED) == 0)
			return true;
		if (rematch(name, pkgname, REG_EXTENDED) == 0)
			return true;
	}

	return false;
}

struct qlist_opt_state {
	int argc;
	char **argv;
	bool exact;
	bool all;
	bool just_pkgname;
	bool dups_only;
	bool show_dir;
	bool show_obj;
	bool show_sym;
	bool show_slots;
	bool show_umap;
	bool columns;
	const char *slot_separator;
	queue *sets;
	char *buf;
	size_t buflen;
};

_q_static int qlist_cb(q_vdb_pkg_ctx *pkg_ctx, void *priv)
{
	struct qlist_opt_state *state = priv;
	int i;
	FILE *fp;
	static char *slotted_item, *slot;
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	depend_atom *atom;
	char swap[_Q_PATH_MAX];

	slot = slotted_item = NULL;

	/* see if this cat/pkg is requested */
	for (i = optind; i < state->argc; ++i) {
		char *name = pkg_name(state->argv[i]);
		if (qlist_match(pkg_ctx, name, state->exact))
			break;
	}
	if ((i == state->argc) && (state->argc != optind))
		return 0;

	if (i < state->argc) {
		if ((slotted_item = slot_name(state->argv[i])) != NULL) {
			slot = q_vdb_pkg_eat(pkg_ctx, "SLOT");
			if (strcmp(slotted_item, slot) != 0)
				return 0;
		}
	}
	if (state->just_pkgname) {
		if (state->dups_only) {
			atom = atom_explode(pkgname);
			snprintf(state->buf, state->buflen, "%s/%s", catname, atom->P);
			snprintf(swap, sizeof(swap), "%s/%s", catname, atom->PN);
			state->sets = add_set(swap, state->buf, state->sets);
			atom_implode(atom);
			return 0;
		}
		atom = (verbose ? NULL : atom_explode(pkgname));
		if ((state->all + state->just_pkgname) < 2) {

			slot = NULL;

			if (state->show_slots)
				slot = q_vdb_pkg_eat(pkg_ctx, "SLOT");

			/* display it */
			printf("%s%s/%s%s%s%s%s%s%s%s%s%s\n", BOLD, catname, BLUE,
				(!state->columns ? (atom ? atom->PN : pkgname) : atom->PN),
				(state->columns ? " " : ""), (state->columns ? atom->PV : ""),
				NORM, YELLOW, slot ? state->slot_separator : "", slot ? slot : "", NORM,
				umapstr(state->show_umap, catname, pkgname));
		}
		if (atom)
			atom_implode(atom);

		if (!state->all)
			return 0;
	}

	if (verbose > 1)
		printf("%s%s/%s%s%s\n%sCONTENTS%s:\n", BOLD, catname, BLUE, pkgname, NORM, DKBLUE, NORM);

	fp = q_vdb_pkg_fopenat_ro(pkg_ctx, "CONTENTS");
	if (fp == NULL)
		return 0;

	while (getline(&state->buf, &state->buflen, fp) != -1) {
		contents_entry *e;

		e = contents_parse_line(state->buf);
		if (!e)
			continue;

		switch (e->type) {
			case CONTENTS_DIR:
				if (state->show_dir)
					printf("%s%s%s/\n", verbose > 1 ? YELLOW : "" , e->name, verbose > 1 ? NORM : "");
				break;
			case CONTENTS_OBJ:
				if (state->show_obj)
					printf("%s%s%s\n", verbose > 1 ? WHITE : "" , e->name, verbose > 1 ? NORM : "");
				break;
			case CONTENTS_SYM:
				if (state->show_sym) {
					if (verbose)
						printf("%s%s -> %s%s\n", verbose > 1 ? CYAN : "", e->name, e->sym_target, NORM);
					else
						printf("%s\n", e->name);
				}
				break;
		}
	}
	fclose(fp);

	return 0;
}

int qlist_main(int argc, char **argv)
{
	struct qlist_opt_state state = {
		.argc = argc,
		.argv = argv,
		.exact = false,
		.all = false,
		.just_pkgname = false,
		.dups_only = false,
		.show_dir = false,
		.show_obj = false,
		.show_sym = false,
		.show_slots = false,
		.show_umap = false,
		.columns = false,
		.slot_separator = " ",
		.sets = NULL,
		.buflen = _Q_PATH_MAX,
	};
	int i, ret;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QLIST, qlist, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qlist)
		case 'a': state.all = true;
		case 'I': state.just_pkgname = true; break;
		case 'L': state.slot_separator = ":"; break;
		case 'S': state.just_pkgname = state.show_slots = true; break;
		case 'U': state.just_pkgname = state.show_umap = true; break;
		case 'e': state.exact = true; break;
		case 'd': state.show_dir = true; break;
		case 'o': state.show_obj = true; break;
		case 's': state.show_sym = true; break;
		case 'D': state.dups_only = state.exact = state.just_pkgname = true; break;
		case 'c': state.columns = true; break;
		case 'f': break;
		}
	}
	if (state.columns) verbose = 0; /* if not set to zero; atom wont be exploded; segv */
	/* default to showing syms and objs */
	if (!state.show_dir && !state.show_obj && !state.show_sym)
		state.show_obj = state.show_sym = true;
	if (argc == optind && !state.just_pkgname)
		qlist_usage(EXIT_FAILURE);

	state.buf = xmalloc(state.buflen);
	ret = q_vdb_foreach_pkg_sorted(qlist_cb, &state);

	if (state.dups_only) {
		char last[126];
		depend_atom *atom;
		queue *ll, *dups = filter_dups(state.sets);
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
				if (state.show_slots)
					slot = grab_vdb_item("SLOT", atom->CATEGORY, atom->P);
				printf("%s%s/%s%s%s%s%s%s%s%s%s", BOLD, atom->CATEGORY, BLUE,
					(!state.columns ? (verbose ? atom->P : atom->PN) : atom->PN),
					(state.columns ? " " : ""), (state.columns ? atom->PV : ""),
					NORM, YELLOW, slot ? state.slot_separator : "", slot ? slot : "", NORM);
				puts(umapstr(state.show_umap, atom->CATEGORY, atom->P));
			}
			atom_implode(atom);
		}
		free_sets(dups);
		free_sets(state.sets);
	}

	return ret;
}

#else
DEFINE_APPLET_STUB(qlist)
#endif
