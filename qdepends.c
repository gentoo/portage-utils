/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
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
#define qdepends_usage(ret) usage(ret, QDEPENDS_FLAGS, qdepends_long_opts, qdepends_opts_help, NULL, lookup_applet_idx("qdepends"))

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
static void _dep_print_tree(FILE *fp, const dep_node *root, size_t space);
static void dep_burn_tree(dep_node *root);
static char *dep_flatten_tree(const dep_node *root);
static void _dep_attach(dep_node *root, dep_node *attach_me, int type);
static void _dep_burn_node(dep_node *node);

#ifdef EBUG
static void
print_word(const char *ptr, size_t num)
{
	while (num--)
		printf("%c", *ptr++);
	printf("\n");
}
#endif

static dep_node *
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

static void
_dep_burn_node(dep_node *node)
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

static void
_dep_attach(dep_node *root, dep_node *attach_me, int type)
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

static dep_node *
dep_grow_tree(const char *depend)
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

static void
_dep_print_tree(FILE *fp, const dep_node *root, size_t space)
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

static bool
dep_print_depend(FILE *fp, const char *depend)
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

static void
dep_burn_tree(dep_node *root)
{
	assert(root);
	if (root->children) dep_burn_tree(root->children);
	if (root->neighbor) dep_burn_tree(root->neighbor);
	_dep_burn_node(root);
}

static void
dep_prune_use(dep_node *root, const char *use)
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

static char *
_dep_flatten_tree(const dep_node *root, char *buf)
{
	if (root->type == DEP_NULL) goto this_node_sucks;
	if (root->type == DEP_NORM) {
		buf[0] = ' ';
		buf = stpcpy(buf + 1, root->info);
	}
	if (root->children)
		buf = _dep_flatten_tree(root->children, buf);
this_node_sucks:
	if (root->neighbor)
		buf = _dep_flatten_tree(root->neighbor, buf);
	return buf;
}

static char *
dep_flatten_tree(const dep_node *root)
{
	static char flat[1024 * 1024];
	char *buf = _dep_flatten_tree(root, flat);
	if (buf == flat) {
		/* all the nodes were squashed ... for example:
		 * USE=-selinux RDEPEND="selinux? ( sys-libs/libselinux )"
		 */
		return NULL;
	}
	return flat + 1;
}

struct qdepends_opt_state {
	array_t *atoms;
	const char *depend_file;
	const char *query;
};

static int
qdepends_main_vdb_cb(q_vdb_pkg_ctx *pkg_ctx, void *priv)
{
	struct qdepends_opt_state *state = priv;
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	size_t i, len;
	int ret;
	char *ptr;
	char buf[_Q_PATH_MAX];
	static char *depend, *use;
	static size_t depend_len, use_len;
	depend_atom *atom, *datom;
	dep_node *dep_tree;

	datom = NULL;
	ret = 0;

	/* see if this cat/pkg is requested */
	array_for_each(state->atoms, i, atom) {
		bool matched = false;
		snprintf(buf, sizeof(buf), "%s/%s", catname, pkgname);
		datom = atom_explode(buf);
		if (datom) {
			matched = (atom_compare(atom, datom) == EQUAL);
			if (matched)
				goto matched;
		}
		atom_implode(datom);
	}
	return ret;
 matched:

	if (!q_vdb_pkg_eat(pkg_ctx, state->depend_file, &depend, &depend_len))
		goto done;

	dep_tree = dep_grow_tree(depend);
	if (dep_tree == NULL)
		goto done;

	if (qdep_name_only)
		printf("%s%s/%s%s%s: ", BOLD, catname, BLUE, atom->PN, NORM);
	else
		printf("%s%s/%s%s%s: ", BOLD, catname, BLUE, pkgname, NORM);

	if (!q_vdb_pkg_eat(pkg_ctx, "USE", &use, &use_len)) {
		warn("Could not eat_file(%s), you'll prob have incorrect output", buf);
	} else {
		for (ptr = use; *ptr; ++ptr)
			if (*ptr == '\n' || *ptr == '\t')
				*ptr = ' ';
		len = ptr - use;
		if (len + 1 >= use_len) {
			use_len += BUFSIZE;
			use = xrealloc(use, use_len);
		}
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

	ret = 1;

 done:
	atom_implode(datom);
	return ret;
}

static int
qdepends_vdb_deep_cb(q_vdb_pkg_ctx *pkg_ctx, void *priv)
{
	struct qdepends_opt_state *state = priv;
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	size_t len;
	char *ptr;
	char buf[_Q_PATH_MAX];
	static char *depend, *use;
	static size_t depend_len, use_len;
	dep_node *dep_tree;

	if (!q_vdb_pkg_eat(pkg_ctx, state->depend_file, &depend, &depend_len))
		return 0;

	dep_tree = dep_grow_tree(depend);
	if (dep_tree == NULL)
		return 0;

	if (q_vdb_pkg_eat(pkg_ctx, "USE", &use, &use_len))
		use[0] = '\0';

	for (ptr = use; *ptr; ++ptr)
		if (*ptr == '\n' || *ptr == '\t')
			*ptr = ' ';
	len = ptr - use;
	if (len + 1 >= use_len) {
		use_len += BUFSIZE;
		use = xrealloc(use, use_len);
	}
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

	return 1;
}

int qdepends_main(int argc, char **argv)
{
	depend_atom *atom;
	DECLARE_ARRAY(atoms);
	struct qdepends_opt_state state = {
		.atoms = atoms,
	};
	q_vdb_pkg_cb *cb;
	size_t i;
	int ret;
	bool do_format = false;
	const char *query = NULL;
	const char *depend_file;
	const char *depend_files[] = { "DEPEND", "RDEPEND", "PDEPEND", NULL, NULL };

	depend_file = depend_files[0];

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

	argc -= optind;
	argv += optind;

	state.depend_file = depend_file;
	state.query = query;
	if (query)
		cb = qdepends_vdb_deep_cb;
	else {
		cb = qdepends_main_vdb_cb;

		for (i = 0; i < (size_t)argc; ++i) {
			atom = atom_explode(argv[i]);
			if (!atom)
				warn("invalid atom: %s", argv[i]);
			else
				xarraypush_ptr(atoms, atom);
		}
	}

	if (!depend_file) {
		ret = 0;
		for (i = 0; depend_files[i]; ++i) {
			printf(" %s*%s %s\n", GREEN, NORM, depend_files[i]);
			state.depend_file = depend_files[i];
			ret |= q_vdb_foreach_pkg(cb, &state, NULL);
		}
	} else
		ret = q_vdb_foreach_pkg(cb, &state, NULL);

	array_for_each(atoms, i, atom)
		atom_implode(atom);
	xarrayfree_int(atoms);

	if (!ret)
		warn("no matches found for your query");
	return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}

#else
DEFINE_APPLET_STUB(qdepends)
#endif
