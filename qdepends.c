/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qdepends.c,v 1.5 2005/06/20 06:25:29 vapier Exp $
 *
 * 2005 Ned Ludd        - <solar@gentoo.org>
 * 2005 Mike Frysinger  - <vapier@gentoo.org>
 *
 ********************************************************************
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 *
 */



#define QDEPENDS_FLAGS "drpac" COMMON_FLAGS
static struct option const qdepends_long_opts[] = {
	{"depend",    no_argument, NULL, 'd'},
	{"rdepend",   no_argument, NULL, 'r'},
	{"pdepend",   no_argument, NULL, 'p'},
	{"cdepend",   no_argument, NULL, 'c'},
	{"all",       no_argument, NULL, 'a'},
	COMMON_LONG_OPTS
};
static const char *qdepends_opts_help[] = {
	"Show DEPEND info (default)",
	"Show RDEPEND info",
	"Show PDEPEND info",
	"Show CDEPEND info",
	"Show all DEPEND info",
	COMMON_OPTS_HELP
};
#define qdepends_usage(ret) usage(ret, QDEPENDS_FLAGS, qdepends_long_opts, qdepends_opts_help, APPLET_QDEPENDS)



/* structures / types / etc ... */
typedef enum {
	DEP_NULL = 0,
	DEP_NORM = 1,
	DEP_USE = 2,
	DEP_OR = 3
} dep_type;
#ifdef EBUG
static const char *_dep_names[] = { "NULL", "NORM", "USE", "OR" };
#endif

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



/* prototypes */
#define dep_dump_tree(r) _dep_dump_tree(r,0)
void _dep_dump_tree(dep_node *root, int space);
dep_node *dep_grow_tree(char *depend);
void dep_burn_tree(dep_node *root);
void dep_prune_use(dep_node *root, char *use);
char *dep_flatten_tree(dep_node *root);



#ifdef EBUG
void print_word(char *ptr, int num);
void print_word(char *ptr, int num)
{
	while (num--)
		printf("%c", *ptr++);
	printf("\n");
}
#endif

dep_node *_dep_grow_node(dep_type type, char *info, size_t info_len);
dep_node *_dep_grow_node(dep_type type, char *info, size_t info_len)
{
	dep_node *ret;
	size_t len;

	if (type == DEP_OR)
		info = NULL;

	len = sizeof(*ret);
	if (info) {
		if (!info_len)
			info_len = strlen(info);
		len += info_len + 1;
	}
	ret = (dep_node*)xmalloc(len);
	memset(ret, 0x00, len);

	ret->type = type;
	if (info) {
		ret->info = ((char*)ret) + sizeof(*ret);
		memcpy(ret->info, info, info_len);
		if (type == DEP_NORM)
			ret->atom = atom_explode(info);
	}

	return ret;
}
void _dep_burn_node(dep_node *node);
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
void _dep_attach(dep_node *root, dep_node *attach_me, int type);
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

dep_node *dep_grow_tree(char *depend)
{
	signed long paren_balanced;
	char *word, *ptr;
	int curr_attach;
	dep_node *ret, *curr_node, *new_node;

	ret = curr_node = new_node = NULL;
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
	word = NULL; \
	} while (0)

	for (ptr = depend; *ptr; ++ptr) {
		if (isspace(*ptr)) {
			_maybe_consume_word(DEP_NORM);
			continue;
		}

		switch (*ptr) {
		case '?': {
			assert(word != NULL);
			_maybe_consume_word(DEP_USE);
			curr_attach = _DEP_CHILD;
			continue;
		}
		case '|': {
			assert(ptr[1] == '|');
			word = ptr++;
			_maybe_consume_word(DEP_OR);
			curr_attach = _DEP_CHILD;
			continue;
		}
		case '(': {
			++paren_balanced;
			_maybe_consume_word(DEP_NORM);
			break;
		}
		case ')': {
			--paren_balanced;
			_maybe_consume_word(DEP_NORM);
			assert(curr_node->parent);
			curr_node = curr_node->parent;
			curr_attach = _DEP_NEIGH;
			break;
		}
		default:
			if (!word)
				word = ptr;
		}
		assert(paren_balanced >= 0);
	}
	assert(paren_balanced == 0);

#undef _maybe_consume_word

	return ret;
}

#ifdef EBUG
void _dep_dump_tree(dep_node *root, int space)
{
	int spaceit = space;
	assert(root);
	if (root->type == DEP_NULL) goto this_node_sucks;

	while (spaceit--) printf("\t");
	printf("Node [%s]: ", _dep_names[root->type]);
	/*printf("Node %p [%s] %p %p %p: ", root, _dep_names[root->type], root->parent, root->neighbor, root->children);*/
	if (root->info) printf("'%s'", root->info);
	printf("\n");

	if (root->children) _dep_dump_tree(root->children, space+1);
this_node_sucks:
	if (root->neighbor) _dep_dump_tree(root->neighbor, space);
}
#endif

void dep_burn_tree(dep_node *root)
{
	assert(root);
	if (root->children) dep_burn_tree(root->children);
	if (root->neighbor) dep_burn_tree(root->neighbor);
	_dep_burn_node(root);
}

void dep_prune_use(dep_node *root, char *use)
{
	if (root->neighbor) dep_prune_use(root->neighbor, use);
	if (root->type == DEP_USE) {
		char useflag[40+3]; /* use flags shouldnt be longer than 40 ... */
		int notfound, invert = (root->info[0] == '!' ? 1 : 0);
		sprintf(useflag, " %s ", root->info+invert);
		notfound = (strstr(use, useflag) == NULL ? 1 : 0);
		if (notfound ^ invert) {
			root->type = DEP_NULL;
			return;
		}
	}
	if (root->children) dep_prune_use(root->children, use);
}

void _dep_flatten_tree(dep_node *root, char *buf, size_t *pos);
void _dep_flatten_tree(dep_node *root, char *buf, size_t *pos)
{
	if (root->type == DEP_NULL) goto this_node_sucks;
	if (root->type == DEP_NORM) {
		size_t len = strlen(root->info);
		memcpy(buf + *pos, root->info, len);
		*pos += len+1;
		buf[*pos-1] = ' ';
	}
	if (root->children) _dep_flatten_tree(root->children, buf, pos);
this_node_sucks:
	if (root->neighbor) _dep_flatten_tree(root->neighbor, buf, pos);
}
char *dep_flatten_tree(dep_node *root)
{
	static char flat[8192];
	size_t pos = 0;
	_dep_flatten_tree(root, flat, &pos);
	flat[pos-1] = '\0';
	return flat;
}



int qdepends_main(int argc, char **argv)
{
	DIR *dir, *dirp;
	struct dirent *dentry, *de;
	struct stat st;
	signed long len;
	int i;
	const char *depend_file;
	char *ptr;
	char buf[_POSIX_PATH_MAX];
	char depend[8192], use[8192];
	dep_node *dep_tree;
	const char *depend_files[] = { "DEPEND", "RDEPEND", "PDEPEND", "CDEPEND", NULL };
	depend_file = depend_files[0];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QDEPENDS, qdepends, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qdepends)

		case 'd': depend_file = depend_files[0]; break;
		case 'r': depend_file = depend_files[1]; break;
		case 'p': depend_file = depend_files[2]; break;
		case 'c': depend_file = depend_files[3]; break;
		case 'a': depend_file = NULL; break;
		}
	}
	if (argc == optind)
		qdepends_usage(EXIT_FAILURE);

	if (chdir(portvdb) != 0 || (dir = opendir(portvdb)) == NULL)
		return EXIT_FAILURE;

	/* open /var/db/pkg */
	while ((dentry = readdir(dir))) {
		/* search for a category directory */
		if (dentry->d_name[0] == '.')
			continue;
		if (strchr(dentry->d_name, '-') == NULL)
			continue;
		stat(dentry->d_name, &st);
		if (!S_ISDIR(st.st_mode))
			continue;
		if (chdir(dentry->d_name) != 0)
			continue;
		if ((dirp = opendir(".")) == NULL)
			continue;

		/* open the cateogry */
		while ((de = readdir(dirp)) != NULL) {
			if (*de->d_name == '.')
				continue;

			/* see if this cat/pkg is requested */
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

			snprintf(buf, sizeof(buf), "%s/%s/%s/%s", portvdb,
			         dentry->d_name, de->d_name, depend_file);
			if (!eat_file(buf, depend, sizeof(buf)))
				continue;
			IF_DEBUG(puts(depend));

			dep_tree = dep_grow_tree(depend);
			if (dep_tree == NULL) continue;
			/*dep_dump_tree(dep_tree);*/
			/*printf("\n");*/

			printf("%s%s/%s%s%s: ", BOLD, dentry->d_name, BLUE, de->d_name, NORM);

			snprintf(buf, sizeof(buf), "%s/%s/%s/USE", portvdb,
			         dentry->d_name, de->d_name);
			assert(eat_file(buf, use, sizeof(use)) == 0);
			for (ptr = use; *ptr; ++ptr)
				if (*ptr == '\n' || *ptr == '\t')
					*ptr = ' ';
			len = strlen(use);
			assert(len+1 < (signed long)sizeof(use));
			use[len] = ' ';
			use[len+1] = '\0';
			memmove(use+1, use, len);
			use[0] = ' ';

			dep_prune_use(dep_tree, use);
			/*dep_dump_tree(dep_tree);*/
			printf("%s\n", dep_flatten_tree(dep_tree));

			dep_burn_tree(dep_tree);
		}
		closedir(dirp);
		chdir("..");
	}
	closedir(dir);

	return EXIT_SUCCESS;
}
