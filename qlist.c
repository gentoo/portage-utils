/*
 * Copyright 2005-2020 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005 Martin Schlemmer     - <azarah@gentoo.org>
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <xalloc.h>

#include "atom.h"
#include "contents.h"
#include "tree.h"
#include "xpak.h"
#include "xregex.h"

#define QLIST_FLAGS "IkSRUcDedosF:" COMMON_FLAGS
static struct option const qlist_long_opts[] = {
	{"installed", no_argument, NULL, 'I'},
	{"binpkgs",   no_argument, NULL, 'k'},
	{"slots",     no_argument, NULL, 'S'},
	{"repo",      no_argument, NULL, 'R'},
	{"umap",      no_argument, NULL, 'U'},
	{"columns",   no_argument, NULL, 'c'},
	{"showdebug", no_argument, NULL, 128},
	{"exact",     no_argument, NULL, 'e'},
	{"dir",       no_argument, NULL, 'd'},
	{"obj",       no_argument, NULL, 'o'},
	{"sym",       no_argument, NULL, 's'},
	/* {"file",       a_argument, NULL, 'f'}, */
	{"format",     a_argument, NULL, 'F'},
	COMMON_LONG_OPTS
};
static const char * const qlist_opts_help[] = {
	"Just show installed package names",
	"Use binpkgs instead of installed packages",
	"Display installed packages with slots (use twice for subslots)",
	"Display installed packages with repository",
	"Display installed packages with flags used",
	"Display column view",
	"Show /usr/lib/debug and /usr/src/debug files",
	"Exact match (only CAT/PN or PN without PV)",
	"Only show directories",
	"Only show objects",
	"Only show symlinks",
	/* "query filename for pkgname", */
	"Print matched atom using given format string",
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
 * mail-mta/exim-4.92 -arc -dane -dcc dkim -dlfunc dmarc dnsdb -doc
 * -dovecot-sasl -dsn exiscan-acl -gnutls -idn ipv6 -ldap -libressl lmtp
 * maildir -mbx -mysql -nis pam -perl -pkcs11 -postgres prdr -proxy
 * -radius -redis -sasl -selinux spf -sqlite -srs ssl -syslog tcpd tpda
 * -X
 */
static char _umapstr_buf[BUFSIZ];
static const char *
umapstr(char display, tree_pkg_ctx *pkg_ctx)
{
	char *bufp = _umapstr_buf;
	char *use = NULL;
	char *iuse = NULL;
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

	use = tree_pkg_meta_get(pkg_ctx, USE);
	if (use == NULL || *use == '\0')
		return bufp;
	iuse = tree_pkg_meta_get(pkg_ctx, IUSE);
	if (iuse == NULL || *iuse == '\0')
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
			" %s" fmt "%s", Cb, use, Ce);

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

	freeargv(iuse_argc, iuse_argv);
	freeargv(use_argc, use_argv);

	return _umapstr_buf;
}

/* forward declaration necessary for misuse from qmerge.c, see HACK there */
bool
qlist_match(
		tree_pkg_ctx *pkg_ctx,
		const char *name,
		depend_atom **name_atom,
		bool exact);
bool
qlist_match(
		tree_pkg_ctx *pkg_ctx,
		const char *name,
		depend_atom **name_atom,
		bool exact)
{
	char buf[_Q_PATH_MAX];
	char uslot[32];
	char *usslot = NULL;
	const char *urepo;
	size_t pf_len;
	depend_atom *atom;
	depend_atom *_atom = NULL;

	uslot[0] = '\0';
	urepo = strchr(name, ':');
	if (urepo != NULL) {
		pf_len = urepo - name;
		if (*++urepo == ':') {
			uslot[0] = '\0';
			urepo++;
		} else {
			usslot = (char *)urepo;
			urepo = strstr(urepo, "::");
			if (urepo != NULL) {
				snprintf(uslot, sizeof(uslot), "%.*s",
						(int)(urepo - usslot), usslot);
				urepo += 2;
			} else {
				snprintf(uslot, sizeof(uslot), "%s", usslot);
			}
			if ((usslot = strchr(uslot, '/')) != NULL)
				*usslot++ = '\0';
		}
	} else {
		pf_len = strlen(name);
	}

	atom = tree_get_atom(pkg_ctx, uslot[0] != '\0' || urepo != NULL);

	/* maybe they're using a version range */
	switch (name[0]) {
	case '=':
	case '>':
	case '<':
	case '~':
		if (!name_atom)
			name_atom = &_atom;
		if (!*name_atom) {
			if ((*name_atom = atom_explode(name)) == NULL) {
				warn("invalid atom %s", name);
				return false;
			}
		}

		return atom_compare(atom, *name_atom) == EQUAL;
	}

	if (uslot[0] != '\0') {
		if (pkg_ctx->slot == NULL)
			return false;

		/* Require exact match on SLOTs.  If the user didn't include a
		 * subslot, then ignore it when checking the package's value. */
		if (strcmp(atom->SLOT, uslot) != 0)
			return false;

		if (usslot != NULL &&
				(atom->SUBSLOT == NULL || strcmp(atom->SUBSLOT, usslot) != 0))
			return false;
	}

	if (urepo) {
		/* require exact match on repositories */
		if (strcmp(atom->REPO, urepo) != 0)
			return false;
	}

	if (exact) {
		int i;

		snprintf(buf, sizeof(buf), "%s/%s%s%s%s%s%s%s",
			atom->CATEGORY,
			atom->PF,
			atom->SLOT != NULL ? ":" : "",
			atom->SLOT != NULL ? atom->SLOT : "",
			atom->SUBSLOT != NULL ? "/" : "",
			atom->SUBSLOT != NULL ? atom->SUBSLOT : "",
			atom->REPO != NULL ? "::" : "",
			atom->REPO != NULL ? atom->REPO : "");

		/* exact match: CAT/PF[:SLOT][::REPO] */
		if (strcmp(name, buf) == 0)
			return true;
		/* exact match: PF[:SLOT][::REPO] */
		if (strcmp(name, strstr(buf, "/") + 1) == 0)
			return true;

		/* let's try exact matching w/out the PV */
		i = snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, atom->PN);
		if (uslot[0] != '\0' && i <= (int)sizeof(buf))
			i += snprintf(buf + i, sizeof(buf) - i, ":%s", atom->SLOT);
		if (urepo && i <= (int)sizeof(buf))
			i += snprintf(buf + i, sizeof(buf) - i, "::%s", atom->REPO);

		/* exact match: CAT/PN[:SLOT][::REPO] */
		if (strcmp(name, buf) == 0)
			return true;
		/* exact match: PN[:SLOT][::REPO] */
		if (strcmp(name, strstr(buf, "/") + 1) == 0)
			return true;
	} else {
		/* partial leading match: CAT/PF */
		snprintf(buf, sizeof(buf), "%s/%s",
				atom->CATEGORY, atom->PF);
		if (strncmp(name, buf, pf_len) == 0 ||
				rematch(name, buf, REG_EXTENDED) == 0)
			return true;
		/* partial leading match: PF */
		if (strncmp(name, atom->PF, pf_len) == 0 ||
				rematch(name, buf, REG_EXTENDED) == 0)
			return true;
	}

	return false;
}

struct qlist_opt_state {
	int argc;
	char **argv;
	depend_atom **atoms;
	bool exact:1;
	bool all:1;
	bool do_binpkgs:1;
	bool just_pkgname:1;
	bool show_dir:1;
	bool show_obj:1;
	bool show_sym:1;
	bool need_full_atom:1;
	bool show_umap:1;
	bool show_dbg:1;
	char *buf;
	size_t buflen;
	const char *fmt;
};

static int
qlist_cb(tree_pkg_ctx *pkg_ctx, void *priv)
{
	struct qlist_opt_state *state = priv;
	int i;
	char *contents;
	char *line;
	char *savep;
	depend_atom *atom;

	/* see if this cat/pkg is requested */
	if (!state->all) {
		for (i = optind; i < state->argc; ++i)
			if (qlist_match(pkg_ctx, state->argv[i],
						&state->atoms[i - optind], state->exact))
				break;
		if (i == state->argc)
			return 0;
	}

	atom = tree_get_atom(pkg_ctx, state->need_full_atom);
	if (state->just_pkgname) {
		printf("%s%s\n",
				atom_format(state->fmt, atom),
				umapstr(state->show_umap, pkg_ctx));

		return 1;
	}

	if (verbose)
		printf("%s %sCONTENTS%s:\n",
				atom_format(state->fmt, atom), DKBLUE, NORM);

	if ((contents = tree_pkg_meta_get(pkg_ctx, CONTENTS)) == NULL)
		return 1;

	while ((line = strtok_r(contents, "\n", &savep)) != NULL) {
		contents_entry *e;

		contents = NULL;  /* for strtok_r */

		e = contents_parse_line(line);
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
					printf("%s%s%s/\n", YELLOW, e->name, NORM);
				break;
			case CONTENTS_OBJ:
				if (state->show_obj)
					printf("%s%s%s\n", DKGREEN, e->name, NORM);
				break;
			case CONTENTS_SYM:
				if (state->show_sym) {
					if (verbose)
						printf("%s%s%s -> %s%s%s\n",
								CYAN, e->name, NORM,
								CYAN, e->sym_target, NORM);
					else
						printf("%s%s%s\n", CYAN, e->name, NORM);
				}
				break;
		}
	}

	return 1;
}

int qlist_main(int argc, char **argv)
{
	int i;
	int ret;
	tree_ctx *vdb;
	int show_slots = 0;
	bool show_repo = false;
	bool do_columns = false;
	char qfmt[128];
	struct qlist_opt_state state = {
		.argc = argc,
		.argv = argv,
		.exact = false,
		.all = false,
		.do_binpkgs = false,
		.just_pkgname = false,
		.show_dir = false,
		.show_obj = false,
		.show_sym = false,
		.need_full_atom = false,
		.show_umap = false,
		.show_dbg = false,
		.buflen = _Q_PATH_MAX,
		.fmt = NULL,
	};

	while ((i = GETOPT_LONG(QLIST, qlist, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qlist)
		case 'I': state.just_pkgname = true;                    break;
		case 'k': state.do_binpkgs = true;                      break;
		case 'S': state.just_pkgname = true; show_slots++;      break;
		case 'R': state.just_pkgname = show_repo = true;        break;
		case 'U': state.just_pkgname = state.show_umap = true;  break;
		case 'e': state.exact = true;                           break;
		case 'd': state.show_dir = true;                        break;
		case 128: state.show_dbg = true;                        break;
		case 'o': state.show_obj = true;                        break;
		case 's': state.show_sym = true;                        break;
		case 'c': do_columns = true;                            break;
		case 'F': state.fmt = optarg;                           break;
		}
	}

	/* default to showing syms and objs */
	if (!state.show_dir && !state.show_obj && !state.show_sym)
		state.show_obj = state.show_sym = true;
	if (argc == optind) {
		if (state.just_pkgname)
			state.all = true;
		else
			qlist_usage(EXIT_FAILURE);
	}

	if (state.fmt == NULL) {
		const char *l = "%[";
		const char *r = "]";
		if (do_columns) {
			l = "%{";
			r = "} ";
		}
		snprintf(qfmt, sizeof(qfmt), "%sCATEGORY%s"
				"%s%s%s"   /* PN/PF */
				"%s%s%s"   /* SLOT */
				"%s%s%s"   /* SUBSLOT */
				"%s%s%s",  /* REPO */
				l, r,
				l, verbose ? "PF" : "PN", r,
				show_slots >= 1 ? l : "",
					show_slots >= 1 ? "SLOT" : "",
					show_slots >= 1 ? r : "",
				show_slots >= 2 ? l : "",
					show_slots >= 2 ? "SUBSLOT" : "",
					show_slots >= 2 ? r : "",
				show_repo ? l : "",
					show_repo ? "REPO" : "",
					show_repo ? r : "");
		state.fmt = qfmt;
		state.need_full_atom = show_slots > 0 || show_repo;
	} else {
		state.need_full_atom = true;
	}

	state.buf = xmalloc(state.buflen);
	state.atoms = xcalloc(argc - optind, sizeof(*state.atoms));
	ret = 1;
	if (state.do_binpkgs)
		vdb = tree_open_binpkg(portroot, pkgdir);
	else
		vdb = tree_open_vdb(portroot, portvdb);
	if (vdb != NULL) {
		ret = tree_foreach_pkg_sorted(vdb, qlist_cb, &state, NULL);
		tree_close(vdb);
	}
	free(state.buf);
	for (i = optind; i < state.argc; ++i)
		if (state.atoms[i - optind])
			atom_implode(state.atoms[i - optind]);
	free(state.atoms);

	/* The return value is whether we matched anything. */
	return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
