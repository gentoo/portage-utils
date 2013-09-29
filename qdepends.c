/*
 * Copyright 2005-2013 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qdepends.c,v 1.64 2013/09/29 10:10:51 vapier Exp $
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2013 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qdepends

#define QDEPENDS_FLAGS "drpafNk:Q:" COMMON_FLAGS
static struct option const qdepends_long_opts[] = {
	{"depend",    no_argument, NULL, 'd'},
	{"rdepend",   no_argument, NULL, 'r'},
	{"pdepend",   no_argument, NULL, 'p'},
	{"key",        a_argument, NULL, 'k'},
	{"query",      a_argument, NULL, 'Q'},
	{"name-only", no_argument, NULL, 'N'},
	{"all",       no_argument, NULL, 'a'},
	{"format",    no_argument, NULL, 'f'},
	COMMON_LONG_OPTS
};
static const char * const qdepends_opts_help[] = {
	"Show DEPEND info (default)",
	"Show RDEPEND info",
	"Show PDEPEND info",
	"User defined vdb key",
	"Query reverse deps",
	"Only show package name",
	"Show all DEPEND info",
	"Pretty format specified depend strings",
	COMMON_OPTS_HELP
};
static const char qdepends_rcsid[] = "$Id: qdepends.c,v 1.64 2013/09/29 10:10:51 vapier Exp $";
#define qdepends_usage(ret) usage(ret, QDEPENDS_FLAGS, qdepends_long_opts, qdepends_opts_help, lookup_applet_idx("qdepends"))

static char qdep_name_only = 0;

/* structures / types / etc ... */
typedef enum {
	DEP_NULL = 0,
	DEP_NORM = 1,
	DEP_USE = 2,
	DEP_OR = 3,
	DEP_GROUP = 4
} dep_type;
static const char * const _dep_names[] = { "NULL", "NORM", "USE", "OR", "GROUP" };

struct _dep_node {
	dep_type type;
	char *info;
	char info_on_heap;
	depend_atom *atom;
	struct _dep_node *parent;
	struct _dep_node *neighbor;
	struct _dep_node *children;
};
typedef struct _dep_node dep_node;

static const dep_node null_node = {
	.type = DEP_NULL,
};

/* prototypes */
#ifdef NDEBUG
# define dep_dump_tree(r)
#else
# define dep_dump_tree(r) _dep_print_tree(stdout, r, 0)
#endif
_q_static void _dep_print_tree(FILE *fp, const dep_node *root, size_t space);
void dep_burn_tree(dep_node *root);
char *dep_flatten_tree(const dep_node *root);
_q_static void _dep_attach(dep_node *root, dep_node *attach_me, int type);
_q_static void _dep_flatten_tree(const dep_node *root, char *buf, size_t *pos);
_q_static void _dep_burn_node(dep_node *node);
int qdepends_main_vdb(const char *depend_file, int argc, char **argv);
int qdepends_vdb_deep(const char *depend_file, const char *query);

#ifdef EBUG
_q_static void print_word(const char *ptr, size_t num)
{
	while (num--)
		printf("%c", *ptr++);
	printf("\n");
}
#endif

_q_static dep_node *
_dep_grow_node(dep_type type, const char *info, size_t info_len)
{
	dep_node *ret;
	size_t len;

	if (type == DEP_OR || type == DEP_GROUP)
		info = NULL;

	len = sizeof(*ret);
	if (info) {
		if (!info_len)
			info_len = strlen(info);
		len += info_len + 1;
	}
	ret = xzalloc(len);

	ret->type = type;
	if (info) {
		ret->info = ((char*)ret) + sizeof(*ret);
		memcpy(ret->info, info, info_len);
		if (type == DEP_NORM)
			ret->atom = atom_explode(info);
	}

	return ret;
}

void _dep_burn_node(dep_node *node)
{
	assert(node);
	if (node->info_on_heap) free(node->info);
	if (node->atom) atom_implode(node->atom);
	free(node);
}

enum {
	_DEP_NEIGH = 1,
	_DEP_CHILD = 2
};

void _dep_attach(dep_node *root, dep_node *attach_me, int type)
{
	if (type == _DEP_NEIGH) {
		if (!root->neighbor) {
			root->neighbor = attach_me;
			attach_me->parent = root->parent;
		} else
			_dep_attach(root->neighbor, attach_me, _DEP_NEIGH);
	} else {
		if (!root->children) {
			root->children = attach_me;
			attach_me->parent = root;
		} else
			_dep_attach(root->children, attach_me, _DEP_NEIGH);
	}
}

_q_static dep_node *dep_grow_tree(const char *depend)
{
	bool saw_whitespace;
	signed long paren_balanced;
	const char *ptr, *word;
	int curr_attach;
	dep_node *ret, *curr_node, *new_node;
	dep_type prev_type;

	ret = curr_node = new_node = NULL;
	prev_type = DEP_NULL;
	paren_balanced = 0;
	curr_attach = _DEP_NEIGH;
	word = NULL;

#define _maybe_consume_word(t) \
	do { \
	if (!word) break; \
	/*printf("Found word:%i ", curr_attach);*/ \
	/*print_word(word, ptr-word);*/ \
	new_node = _dep_grow_node(t, word, ptr-word); \
	if (!ret) \
		ret = curr_node = new_node; \
	else { \
		_dep_attach(curr_node, new_node, curr_attach); \
		curr_attach = _DEP_NEIGH; \
		curr_node = new_node; \
	} \
	prev_type = t; \
	word = NULL; \
	} while (0)

	saw_whitespace = true;
	for (ptr = depend; *ptr; ++ptr) {
		if (isspace(*ptr)) {
			saw_whitespace = true;
			_maybe_consume_word(DEP_NORM);
			continue;
		}

		switch (*ptr) {
		case '?': {
			if (word == NULL) {
				warnf("Found a ? but no USE flag");
				goto error_out;
			}
			_maybe_consume_word(DEP_USE);
			curr_attach = _DEP_CHILD;
			continue;
		}
		case '|': {
			if (!saw_whitespace)
				break;
			if (ptr[1] != '|') {
				warnf("Found a | but not ||");
				goto error_out;
			}
			word = ptr++;
			_maybe_consume_word(DEP_OR);
			curr_attach = _DEP_CHILD;
			continue;
		}
		case '(': {
			++paren_balanced;
			if (!saw_whitespace)
				break;
			if (prev_type == DEP_OR || prev_type == DEP_USE) {
				_maybe_consume_word(DEP_NORM);
				prev_type = DEP_NULL;
			} else {
				if (word) {
					warnf("New group has word in queue");
					goto error_out;
				}
				word = ptr;
				_maybe_consume_word(DEP_GROUP);
				curr_attach = _DEP_CHILD;
			}
			break;
		}
		case ')': {
			--paren_balanced;
			if (!saw_whitespace)
				break;
			_maybe_consume_word(DEP_NORM);

			if (curr_node->parent == NULL) {
				warnf("Group lacks a parent");
				goto error_out;
			}
			curr_node = curr_node->parent;
			curr_attach = _DEP_NEIGH;
			break;
		}
		default:
			if (!word)
				word = ptr;
		}
		saw_whitespace = false;

		/* fall through to the paren failure below */
		if (paren_balanced < 0)
			break;
	}

	if (paren_balanced != 0) {
		warnf("Parenthesis unbalanced");
		goto error_out;
	}

	/* if the depend buffer wasnt terminated with a space,
	 * we may have a word sitting in the buffer to consume */
	_maybe_consume_word(DEP_NORM);

#undef _maybe_consume_word

	return ret ? : xmemdup(&null_node, sizeof(null_node));

error_out:
	warnf("DEPEND: %s", depend);
	if (ret) {
		dep_dump_tree(ret);
		dep_burn_tree(ret);
	}
	return NULL;
}

_q_static void _dep_print_tree(FILE *fp, const dep_node *root, size_t space)
{
	size_t s;

	assert(root);
	if (root->type == DEP_NULL)
		goto this_node_sucks;

	for (s = space; s; --s)
		fprintf(fp, "\t");

	if (verbose > 1)
		fprintf(fp, "Node [%s]: ", _dep_names[root->type]);
	/*printf("Node %p [%s] %p %p %p: ", root, _dep_names[root->type], root->parent, root->neighbor, root->children);*/
	if (root->type == DEP_OR)
		fprintf(fp, "|| (");
	if (root->info) {
		fprintf(fp, "%s", root->info);
		/* If there is only one child, be nice to one-line: foo? ( pkg ) */
		if (root->type == DEP_USE)
			fprintf(fp, "? (");
	}
	fprintf(fp, "\n");

	if (root->children)
		_dep_print_tree(fp, root->children, space+1);

	if (root->type == DEP_OR || root->type == DEP_USE) {
		for (s = space; s; --s)
			fprintf(fp, "\t");
		fprintf(fp, ")\n");
	}
 this_node_sucks:
	if (root->neighbor)
		_dep_print_tree(fp, root->neighbor, space);
}

_q_static bool dep_print_depend(FILE *fp, const char *depend)
{
	dep_node *dep_tree;

	dep_tree = dep_grow_tree(depend);
	if (dep_tree == NULL)
		return false;

	if (!quiet)
		fprintf(fp, "DEPEND=\"\n");

	_dep_print_tree(fp, dep_tree, 1);

	dep_burn_tree(dep_tree);
	if (!quiet)
		fprintf(fp, "\"\n");

	return true;
}

void dep_burn_tree(dep_node *root)
{
	assert(root);
	if (root->children) dep_burn_tree(root->children);
	if (root->neighbor) dep_burn_tree(root->neighbor);
	_dep_burn_node(root);
}

_q_static void dep_prune_use(dep_node *root, const char *use)
{
	if (root->neighbor) dep_prune_use(root->neighbor, use);
	if (root->type == DEP_USE) {
		char *useflag = NULL;
		int notfound, invert = (root->info[0] == '!' ? 1 : 0);
		xasprintf(&useflag, " %s ", root->info+invert);
		notfound = (strstr(use, useflag) == NULL ? 1 : 0);
		free(useflag);
		if (notfound ^ invert) {
			root->type = DEP_NULL;
			return;
		}
	}
	if (root->children) dep_prune_use(root->children, use);
}

void _dep_flatten_tree(const dep_node *root, char *buf, size_t *pos)
{
	if (root->type == DEP_NULL) goto this_node_sucks;
	if (root->type == DEP_NORM) {
		size_t len = strlen(root->info);
#if 1
		if (*root->info == 'v')
			if (strncmp(root->info, "virtual/", 8) == 0) {
				if (virtuals == NULL)
					virtuals = resolve_virtuals();
				IF_DEBUG(fprintf(stderr, "(%s->%s)", root->info, virtual(root->info, virtuals)));
			}
#endif
		memcpy(buf + *pos, root->info, len);
		*pos += len+1;
		buf[*pos-1] = ' ';
	}
	if (root->children) _dep_flatten_tree(root->children, buf, pos);
this_node_sucks:
	if (root->neighbor) _dep_flatten_tree(root->neighbor, buf, pos);
}

char *dep_flatten_tree(const dep_node *root)
{
	static char flat[8192];
	size_t pos = 0;
	_dep_flatten_tree(root, flat, &pos);
	if (pos == 0) {
		/* all the nodes were squashed ... for example:
		 * USE=-selinux RDEPEND="selinux? ( sys-libs/libselinux )"
		 */
		return NULL;
	}
	flat[pos-1] = '\0';
	return flat;
}

struct qdepends_opt_state {
	int argc;
	char **argv;
	const char *depend_file;
	const char *query;
};

_q_static int qdepends_main_vdb_cb(q_vdb_pkg_ctx *pkg_ctx, void *priv)
{
	struct qdepends_opt_state *state = priv;
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	size_t len;
	int i;
	char *ptr;
	char buf[_Q_PATH_MAX];
	char depend[65536], use[8192];
	dep_node *dep_tree;

	/* see if this cat/pkg is requested */
	for (i = optind; i < state->argc; ++i) {
		snprintf(buf, sizeof(buf), "%s/%s", catname, pkgname);
		if (rematch(state->argv[i], buf, REG_EXTENDED) == 0)
			break;
		if (rematch(state->argv[i], pkgname, REG_EXTENDED) == 0)
			break;
	}
	if (i == state->argc)
		return 0;

	IF_DEBUG(warn("matched %s/%s", catname, pkgname));

	if (!eat_file_at(pkg_ctx->fd, state->depend_file, depend, sizeof(depend)))
		return 0;

	IF_DEBUG(warn("growing tree..."));
	dep_tree = dep_grow_tree(depend);
	if (dep_tree == NULL)
		return 0;
	IF_DEBUG(puts(depend));
	IF_DEBUG(dep_dump_tree(dep_tree));

	if (qdep_name_only) {
		depend_atom *atom = NULL;
		snprintf(buf, sizeof(buf), "%s/%s", catname, pkgname);
		if ((atom = atom_explode(buf)) != NULL) {
			printf("%s%s/%s%s%s: ", BOLD, catname, BLUE, atom->PN, NORM);
			atom_implode(atom);
		}
	} else {
		printf("%s%s/%s%s%s: ", BOLD, catname, BLUE, pkgname, NORM);
	}

	if (!eat_file_at(pkg_ctx->fd, "USE", use, sizeof(use))) {
		warn("Could not eat_file(%s), you'll prob have incorrect output", buf);
	} else {
		for (ptr = use; *ptr; ++ptr)
			if (*ptr == '\n' || *ptr == '\t')
				*ptr = ' ';
		len = strlen(use);
		assert(len+1 < sizeof(use));
		use[len] = ' ';
		use[len+1] = '\0';
		memmove(use+1, use, len);
		use[0] = ' ';

		dep_prune_use(dep_tree, use);
	}

	/*dep_dump_tree(dep_tree);*/
	ptr = dep_flatten_tree(dep_tree);
	printf("%s\n", (ptr == NULL ? "" : ptr));

	dep_burn_tree(dep_tree);

	return EXIT_SUCCESS;
}

_q_static int qdepends_vdb_deep_cb(q_vdb_pkg_ctx *pkg_ctx, void *priv)
{
	struct qdepends_opt_state *state = priv;
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	size_t len;
	char *ptr;
	char buf[_Q_PATH_MAX];
	char depend[16384], use[8192];
	dep_node *dep_tree;

	IF_DEBUG(warn("matched %s/%s", catname, pkgname));

	if (!eat_file_at(pkg_ctx->fd, state->depend_file, depend, sizeof(depend)))
		return 0;

	IF_DEBUG(warn("growing tree..."));
	dep_tree = dep_grow_tree(depend);
	if (dep_tree == NULL)
		return 0;
	IF_DEBUG(puts(depend));
	IF_DEBUG(dep_dump_tree(dep_tree));

	if (eat_file_at(pkg_ctx->fd, "USE", use, sizeof(use)) == 1)
		use[0] = ' ';

	for (ptr = use; *ptr; ++ptr)
		if (*ptr == '\n' || *ptr == '\t')
			*ptr = ' ';
	len = strlen(use);
	assert(len+1 < sizeof(use));
	use[len] = ' ';
	use[len+1] = '\0';
	memmove(use+1, use, len);
	use[0] = ' ';

	dep_prune_use(dep_tree, use);

	ptr = dep_flatten_tree(dep_tree);
	if (ptr && rematch(state->query, ptr, REG_EXTENDED) == 0) {
		if (qdep_name_only) {
			depend_atom *atom = NULL;
			snprintf(buf, sizeof(buf), "%s/%s", catname, pkgname);
			if ((atom = atom_explode(buf)) != NULL) {
				printf("%s%s/%s%s%s%c", BOLD, catname, BLUE, atom->PN, NORM, verbose ? ':' : '\n');
				atom_implode(atom);
			}
		} else {
			printf("%s%s/%s%s%s%c", BOLD, catname, BLUE, pkgname, NORM, verbose ? ':' : '\n');
		}
		if (verbose)
			printf(" %s\n", ptr);
	}
	dep_burn_tree(dep_tree);

	return EXIT_SUCCESS;
}

int qdepends_main(int argc, char **argv)
{
	struct qdepends_opt_state state = {
		.argc = argc,
		.argv = argv,
	};
	q_vdb_pkg_cb *cb;
	int i;
	bool do_format = false;
	const char *query = NULL;
	const char *depend_file;
	const char *depend_files[] = { "DEPEND", "RDEPEND", "PDEPEND", NULL, NULL };

	depend_file = depend_files[0];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QDEPENDS, qdepends, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qdepends)

		case 'd': depend_file = depend_files[0]; break;
		case 'r': depend_file = depend_files[1]; break;
		case 'p': depend_file = depend_files[2]; break;
		case 'k': depend_file = optarg; break;
		case 'a': depend_file = NULL; break;
		case 'Q': query = optarg; break;
		case 'N': qdep_name_only = 1; break;
		case 'f': do_format = true; break;
		}
	}
	if ((argc == optind) && (query == NULL) && !do_format)
		qdepends_usage(EXIT_FAILURE);

	if (do_format) {
		while (optind < argc) {
			if (!dep_print_depend(stdout, argv[optind++]))
				return EXIT_FAILURE;
			if (optind < argc)
				fprintf(stdout, "\n");
		}
		return EXIT_SUCCESS;
	}

	state.depend_file = depend_file;
	state.query = query;
	cb = query ? qdepends_vdb_deep_cb : qdepends_main_vdb_cb;

	if (!depend_file) {
		int ret = 0;
		for (i = 0; depend_files[i]; ++i) {
			printf(" %s*%s %s\n", GREEN, NORM, depend_files[i]);
			state.depend_file = depend_files[i];
			ret += q_vdb_foreach_pkg(cb, &state, NULL);
		}
		return ret;
	}

	return q_vdb_foreach_pkg(cb, &state, NULL);
}

#else
DEFINE_APPLET_STUB(qdepends)
#endif
