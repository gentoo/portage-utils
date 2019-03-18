/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005 Martin Schlemmer     - <azarah@gentoo.org>
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifdef APPLET_qlist

#define QLIST_FLAGS "ISRUcDeados" COMMON_FLAGS
static struct option const qlist_long_opts[] = {
	{"installed", no_argument, NULL, 'I'},
	{"slots",     no_argument, NULL, 'S'},
	{"repo",      no_argument, NULL, 'R'},
	{"umap",      no_argument, NULL, 'U'},
	{"columns",   no_argument, NULL, 'c'},
	{"showdebug", no_argument, NULL, 128},
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
	"Display installed packages with slots (use twice for subslots)",
	"Display installed packages with repository",
	"Display installed packages with flags used",
	"Display column view",
	"Show /usr/lib/debug and /usr/src/debug files",
	"Exact match (only CAT/PN or PN without PV)",
	"Show every installed package",
	"Only show directories",
	"Only show objects",
	"Only show symlinks",
	/* "query filename for pkgname", */
	COMMON_OPTS_HELP
};
#define qlist_usage(ret) usage(ret, QLIST_FLAGS, qlist_long_opts, qlist_opts_help, NULL, lookup_applet_idx("qlist"))

static int
cmpstringp(const void *p1, const void *p2)
{
	/* case insensitive comparator */
	return strcasecmp(*((char * const *)p1), *((char * const *)p2));
}

/*
 * ==> /var/db/pkg/mail-mta/exim-4.92/IUSE <==
 * arc dane dcc +dkim dlfunc dmarc +dnsdb doc dovecot-sasl dsn
 * elibc_glibc exiscan-acl gnutls idn ipv6 ldap libressl lmtp maildir
 * mbx mysql nis pam perl pkcs11 postgres +prdr proxy radius redis sasl
 * selinux spf sqlite srs ssl syslog tcpd +tpda X
 *
 * ==> /var/db/pkg/mail-mta/exim-4.92/PKGUSE <==
 * -X dkim dmarc exiscan-acl ipv6 -ldap lmtp maildir -mbox pam -perl spf
 * ssl tcpd
 *
 * ==> /var/db/pkg/mail-mta/exim-4.92/USE <==
 * abi_x86_64 amd64 dkim dmarc dnsdb elibc_glibc exiscan-acl ipv6
 * kernel_linux lmtp maildir pam prdr spf ssl tcpd tpda userland_GNU
 *
 * % emerge -pv exim
 *
 * These are the packages that would be merged, in order:
 *
 * Calculating dependencies... done!
 * [ebuild   R   ~] mail-mta/exim-4.92::gentoo  USE="dkim dmarc dnsdb
 * exiscan-acl ipv6 lmtp maildir pam prdr spf ssl tcpd tpda -X -arc
 * -dane -dcc -dlfunc -doc -dovecot-sasl -dsn -gnutls -idn -ldap
 * -libressl -mbx -mysql -nis -perl -pkcs11 -postgres -proxy -radius
 * -redis -sasl (-selinux) -sqlite -srs -syslog" 0 KiB
 *
 * % qlist -IUv exim
 * mail-mta/exim-4.92 (-arc -dane -dcc dkim -dlfunc dmarc dnsdb -doc
 * -dovecot-sasl -dsn exiscan-acl -gnutls -idn ipv6 -ldap -libressl lmtp
 * maildir -mbx -mysql -nis pam -perl -pkcs11 -postgres prdr -proxy
 * -radius -redis -sasl -selinux spf -sqlite -srs ssl -syslog tcpd tpda
 * -X)
 */
static char _umapstr_buf[BUFSIZ];
static const char *
umapstr(char display, q_vdb_pkg_ctx *pkg_ctx)
{
	char *bufp = _umapstr_buf;
	char *use = NULL;
	char *iuse = NULL;
	size_t use_len;
	size_t iuse_len;
	int use_argc = 0;
	int iuse_argc = 0;
	char **use_argv = NULL;
	char **iuse_argv = NULL;
	int i;
	int u;
	int d;

	*bufp = '\0';
	if (!display)
		return bufp;

	q_vdb_pkg_eat(pkg_ctx, "USE", &use, &use_len);
	if (!use[0])
		return bufp;
	q_vdb_pkg_eat(pkg_ctx, "IUSE", &iuse, &iuse_len);
	if (!iuse[0])
		return bufp;

	/* strip out possible leading +/- flags in IUSE */
	u = (int)strlen(iuse);
	for (i = 0; i < u; i++)
		if (iuse[i] == '+' || iuse[i] == '-')
			if (i == 0 || iuse[i - 1] == ' ')
				iuse[i] = ' ';

	makeargv(use, &use_argc, &use_argv);
	makeargv(iuse, &iuse_argc, &iuse_argv);

#define add_to_buf(fmt, Cb, use, Ce) \
	bufp += snprintf(bufp, sizeof(_umapstr_buf) - (bufp - _umapstr_buf), \
			" %s%s" fmt "%s", \
			bufp == _umapstr_buf && !quiet ? "(" : "", Cb, use, Ce);

	/* merge join, ensure inputs are sorted (Portage does this, but just
	 * to be sure) */
	qsort(&use_argv[1], use_argc - 1, sizeof(char *), cmpstringp);
	qsort(&iuse_argv[1], iuse_argc - 1, sizeof(char *), cmpstringp);
	for (i = 1, u = 1; i < iuse_argc; i++) {
		/* filter out implicits */
		if (strncmp(iuse_argv[i], "elibc_", 6) == 0 ||
				strncmp(iuse_argv[i], "kernel_", 7) == 0 ||
				strncmp(iuse_argv[i], "userland_", 9) == 0)
			continue;

		/* ensure USE is in IUSE */
		for (d = 1; u < use_argc; u++) {
			d = strcmp(use_argv[u], iuse_argv[i]);
			if (d >= 0)
				break;
		}

		if (d == 0) {
			add_to_buf("%s", RED, iuse_argv[i], NORM);
			u++;
		} else if (verbose) {
			add_to_buf("-%s", DKBLUE, iuse_argv[i], NORM);
		}
	}

	bufp += snprintf(bufp, sizeof(_umapstr_buf) - (bufp - _umapstr_buf),
			"%s", bufp == _umapstr_buf || quiet ? "" : ")");

	freeargv(iuse_argc, iuse_argv);
	freeargv(use_argc, use_argv);
	free(iuse);
	free(use);

	return _umapstr_buf;
}

static bool
qlist_match(q_vdb_pkg_ctx *pkg_ctx, const char *name, depend_atom **name_atom, bool exact)
{
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	char buf[_Q_PATH_MAX];
	char swap[_Q_PATH_MAX];
	const char *uslot;
	size_t uslot_len = 0;
	const char *urepo;
	size_t urepo_len = 0;
	depend_atom *atom;

	uslot = strchr(name, ':');
	if (uslot) {
		if (*++uslot == ':')
			uslot = NULL;
		else {
			if (!pkg_ctx->slot)
				q_vdb_pkg_eat(pkg_ctx, "SLOT", &pkg_ctx->slot, &pkg_ctx->slot_len);
			uslot_len = strlen(uslot);
		}
	}

	urepo = strstr(name, "::");
	if (urepo) {
		if (!pkg_ctx->repo)
			q_vdb_pkg_eat(pkg_ctx, "repository", &pkg_ctx->repo, &pkg_ctx->repo_len);
		urepo += 2;
		urepo_len = strlen(urepo);

		if (uslot_len)
			uslot_len -= (urepo_len + 2);
	}

	/* maybe they're using a version range */
	switch (name[0]) {
	case '=':
	case '>':
	case '<':
	case '~':
		snprintf(buf, sizeof(buf), "%s/%s%c%s%s%s", catname, pkgname,
			pkg_ctx->slot ? ':' : '\0', pkg_ctx->slot ? : "",
			pkg_ctx->repo ? "::" : "", pkg_ctx->repo ? : "");
		if ((atom = atom_explode(buf)) == NULL) {
			warn("invalid atom %s", buf);
			return false;
		}

		depend_atom *_atom = NULL;
		if (!name_atom)
			name_atom = &_atom;
		if (!*name_atom) {
			if ((*name_atom = atom_explode(name)) == NULL) {
				atom_implode(atom);
				warn("invalid atom %s", name);
				return false;
			}
		}

		bool ret = atom_compare(atom, *name_atom) == EQUAL;
		atom_implode(atom);
		return ret;
	}

	if (uslot) {
		/* Require exact match on SLOTs.  If the user didn't include a subslot,
		 * then ignore it when checking the package's value. */
		if (strncmp(pkg_ctx->slot, uslot, uslot_len) != 0 ||
		    (pkg_ctx->slot[uslot_len] != '\0' &&
		     pkg_ctx->slot[uslot_len] != '/'))
			return false;
	}

	if (urepo) {
		/* require exact match on repositories */
		if (strcmp(pkg_ctx->repo, urepo) != 0)
			return false;
	}

	if (exact) {
		int i;

		snprintf(buf, sizeof(buf), "%s/%s:%s::%s",
			catname, pkgname, pkg_ctx->slot, pkg_ctx->repo);

		/* exact match: CAT/PN-PVR[:SLOT][::REPO] */
		if (strcmp(name, buf) == 0)
			return true;
		/* exact match: PN-PVR[:SLOT][::REPO] */
		if (strcmp(name, strstr(buf, "/") + 1) == 0)
			return true;

		/* let's try exact matching w/out the PV */
		if ((atom = atom_explode(buf)) == NULL) {
			warn("invalid atom %s", buf);
			return false;
		}

		i = snprintf(swap, sizeof(swap), "%s/%s", atom->CATEGORY, atom->PN);
		if (uslot && i <= (int)sizeof(swap))
			i += snprintf(swap + i, sizeof(swap) - i, ":%s", atom->SLOT);
		if (urepo && i <= (int)sizeof(swap))
			i += snprintf(swap + i, sizeof(swap) - i, "::%s", atom->REPO);

		atom_implode(atom);
		/* exact match: CAT/PN[:SLOT][::REPO] */
		if (strcmp(name, swap) == 0)
			return true;
		/* exact match: PN[:SLOT][::REPO] */
		if (strcmp(name, strstr(swap, "/") + 1) == 0)
			return true;
	} else {
		size_t ulen = strlen(name);
		if (urepo)
			ulen -= (urepo_len + 2);
		if (uslot)
			ulen -= (uslot_len + 1);
		snprintf(buf, sizeof(buf), "%s/%s", catname, pkgname);
		/* partial leading match: CAT/PN-PVR */
		if (strncmp(name, buf, ulen) == 0)
			return true;
		/* partial leading match: PN-PVR */
		if (strncmp(name, pkgname, ulen) == 0)
			return true;
		/* try again but with regexps */
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
	depend_atom **atoms;
	bool exact;
	bool all;
	bool just_pkgname;
	bool show_dir;
	bool show_obj;
	bool show_repo;
	bool show_sym;
	int show_slots;
	bool show_umap;
	bool show_dbg;
	bool columns;
	char *buf;
	size_t buflen;
};

static int
qlist_cb(q_vdb_pkg_ctx *pkg_ctx, void *priv)
{
	struct qlist_opt_state *state = priv;
	int i;
	FILE *fp;
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;

	/* see if this cat/pkg is requested */
	for (i = optind; i < state->argc; ++i)
		if (qlist_match(pkg_ctx, state->argv[i], &state->atoms[i - optind], state->exact))
			break;
	if ((i == state->argc) && (state->argc != optind))
		return 0;

	if (state->just_pkgname) {
		depend_atom *atom;
		atom = (verbose ? NULL : atom_explode(pkgname));
		if ((state->all + state->just_pkgname) < 2) {
			if (state->show_slots && !pkg_ctx->slot) {
				q_vdb_pkg_eat(pkg_ctx, "SLOT", &pkg_ctx->slot, &pkg_ctx->slot_len);
				/* chop off the subslot if desired */
				if (state->show_slots == 1) {
					char *s = strchr(pkg_ctx->slot, '/');
					if (s)
						*s = '\0';
				}
			}
			if (state->show_repo && !pkg_ctx->repo)
				q_vdb_pkg_eat(pkg_ctx, "repository",
						&pkg_ctx->repo, &pkg_ctx->repo_len);
			/* display it */
			printf("%s%s/%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
					BOLD, catname, BLUE,
					(!state->columns ? (atom ? atom->PN : pkgname) : atom->PN),
					(state->columns ? " " : ""),
					(state->columns ? atom->PV : ""),
					NORM, YELLOW,
					state->show_slots ? ":" : "",
					state->show_slots ? pkg_ctx->slot : "",
					NORM,
					NORM, GREEN,
					state->show_repo ? "::" : "",
					state->show_repo ? pkg_ctx->repo : "",
					NORM,
					umapstr(state->show_umap, pkg_ctx));
		}
		if (atom)
			atom_implode(atom);

		if (!state->all)
			return 1;
	}

	if (verbose > 1)
		printf("%s%s/%s%s%s\n%sCONTENTS%s:\n", BOLD, catname, BLUE, pkgname, NORM, DKBLUE, NORM);

	fp = q_vdb_pkg_fopenat_ro(pkg_ctx, "CONTENTS");
	if (fp == NULL)
		return 1;

	while (getline(&state->buf, &state->buflen, fp) != -1) {
		contents_entry *e;

		e = contents_parse_line(state->buf);
		if (!e)
			continue;

		if (!state->show_dbg) {
			if ((strncmp(e->name, "/usr/lib/debug", 14) == 0
						|| strncmp(e->name, "/usr/src/debug", 14) == 0)
					&& (e->name[14] == '/' || e->name[14] == '\0'))
				continue;
		}

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

	return 1;
}

int qlist_main(int argc, char **argv)
{
	struct qlist_opt_state state = {
		.argc = argc,
		.argv = argv,
		.exact = false,
		.all = false,
		.just_pkgname = false,
		.show_dir = false,
		.show_obj = false,
		.show_repo = false,
		.show_sym = false,
		.show_slots = 0,
		.show_umap = false,
		.show_dbg = false,
		.columns = false,
		.buflen = _Q_PATH_MAX,
	};
	int i, ret;

	while ((i = GETOPT_LONG(QLIST, qlist, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qlist)
		case 'a': state.all = true; /* fall through */
		case 'I': state.just_pkgname = true; break;
		case 'S': state.just_pkgname = true; ++state.show_slots; break;
		case 'R': state.just_pkgname = state.show_repo = true; break;
		case 'U': state.just_pkgname = state.show_umap = true; break;
		case 'e': state.exact = true; break;
		case 'd': state.show_dir = true; break;
		case 128: state.show_dbg = true; break;
		case 'o': state.show_obj = true; break;
		case 's': state.show_sym = true; break;
		case 'c': state.columns = true; break;
		case 'f': break;
		}
	}
	if (state.columns)
		verbose = 0; /* if not set to zero; atom wont be exploded; segv */
	/* default to showing syms and objs */
	if (!state.show_dir && !state.show_obj && !state.show_sym)
		state.show_obj = state.show_sym = true;
	if (argc == optind && !state.just_pkgname)
		qlist_usage(EXIT_FAILURE);

	state.buf = xmalloc(state.buflen);
	state.atoms = xcalloc(argc - optind, sizeof(*state.atoms));
	ret = q_vdb_foreach_pkg_sorted(portroot, portvdb, qlist_cb, &state);
	free(state.buf);
	for (i = optind; i < state.argc; ++i)
		if (state.atoms[i - optind])
			atom_implode(state.atoms[i - optind]);
	free(state.atoms);

	/* The return value is whether we matched anything. */
	return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}

#else
DEFINE_APPLET_STUB(qlist)
#endif
