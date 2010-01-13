/*
 * Copyright 2005-2007 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qmerge.c,v 1.89 2010/01/13 18:48:00 vapier Exp $
 *
 * Copyright 2005-2007 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2007 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qmerge

#include <fnmatch.h>
#include <glob.h>

#ifndef GLOB_BRACE
# define GLOB_BRACE     (1 << 10)	/* Expand "{a,b}" to "a" "b".  */
#endif

/*
  --nofiles                        don't verify files in package
  --noscript                       don't execute pkg_{pre,post}{inst,rm} (if any)
*/

/* #define BUSYBOX "/bin/busybox" */
#define BUSYBOX ""

int old_repo = 0;

#define QMERGE_FLAGS "fFsKUpuyO5" COMMON_FLAGS
static struct option const qmerge_long_opts[] = {
	{"fetch",   no_argument, NULL, 'f'},
	{"force",   no_argument, NULL, 'F'},
	{"search",  no_argument, NULL, 's'},
	{"install", no_argument, NULL, 'K'},
	{"unmerge", no_argument, NULL, 'U'},
	{"pretend", no_argument, NULL, 'p'},
	{"update",  no_argument, NULL, 'u'},
	{"yes",     no_argument, NULL, 'y'},
	{"nodeps",  no_argument, NULL, 'O'},
	{"nomd5",   no_argument, NULL, '5'},
	COMMON_LONG_OPTS
};

static const char *qmerge_opts_help[] = {
	"Fetch package and newest Packages metadata",
	"Fetch package (skipping Packages)",
	"Search available packages",
	"Install package",
	"Uninstall package",
	"Pretend only",
	"Update only",
	"Don't prompt before overwriting",
	"Don't merge dependencies",
	"Don't verify MD5 digest of files",
	COMMON_OPTS_HELP
};

static const char qmerge_rcsid[] = "$Id: qmerge.c,v 1.89 2010/01/13 18:48:00 vapier Exp $";
#define qmerge_usage(ret) usage(ret, QMERGE_FLAGS, qmerge_long_opts, qmerge_opts_help, lookup_applet_idx("qmerge"))

char search_pkgs = 0;
char interactive = 1;
char install = 0;
char uninstall = 0;
char force_download = 0;
char follow_rdepends = 1;
char nomd5 = 0;
char qmerge_strict = 0;
char update_only = 0;

/*
	"CHOST", "DEPEND", "DESCRIPTION", "EAPI",
	"IUSE", "KEYWORDS", "LICENSE", "PDEPEND",
	"PROVIDE", "RDEPEND", "SLOT", "USE"
*/
struct pkg_t {
	char PF[64];
	char CATEGORY[64];
	char DESC[126];
	char LICENSE[64];
	char RDEPEND[BUFSIZ];
	char MD5[34];
	char SHA1[42];
	char SLOT[64];
	size_t SIZE;
	char USE[BUFSIZ];
	char REPO[64];
} Pkg;

struct llist_char_t {
	char *data;
	struct llist_char_t *next;
};

typedef struct llist_char_t llist_char;

int interactive_rename(const char *, const char *, struct pkg_t *);
void fetch(const char *, const char *);
void qmerge_initialize(const char *);
char *best_version(const char *, const char  *);
void pkg_fetch(int, depend_atom *, struct pkg_t *);
void pkg_merge(int, depend_atom *, struct pkg_t *);
int pkg_unmerge(char *, char *);
int unlink_empty(char *);
void pkg_process(int, char **, struct pkg_t *);
void print_Pkg(int, struct pkg_t *);
int parse_packages(const char *, int, char **);
int config_protected(const char *, int, char **);
int match_pkg(const char *, struct pkg_t *);
int pkg_verify_checksums(char *, struct pkg_t *, depend_atom *, int strict, int display);
int unmerge_packages(int, char **);
char *find_binpkg(const char *);

struct pkg_t *grab_binpkg_info(const char *);

int mkdirhier(char *dname, mode_t mode);
int mkdirhier(char *dname, mode_t mode) {
	char buf[BUFSIZ];
	int i;
	strncpy(buf, dname, sizeof(buf));
	for (i = 0; i < strlen(buf); i++) {
		if (buf[i] == '/') {
			buf[i] = 0;
			if (*buf)
				mkdir(buf, mode);
			buf[i] = '/';
		}
	}
	return mkdir(dname, mode);
}

int q_unlink_q(char *, const char *, int);
int q_unlink_q(char *path, const char *func, int line)
{
	if ((strcmp(path, "/bin/sh") == 0) || (strcmp(path, BUSYBOX) == 0)) {
		warn("Oh hell no: unlink(%s) from %s line %d", path, func, line);
		return 1;
	}
	if (pretend)
		return 0;
	return unlink(path);
}

#define unlink_q(path) q_unlink_q(path, __FUNCTION__, __LINE__)

/* rewrite using copyfile() utime() stat(), lstat(), read() and perms. */
int interactive_rename(const char *src, const char *dst, struct pkg_t *pkg)
{
	FILE *fp;
	char *p;
	int ret;
	char buf[1024];
	struct stat st;
	char check_interactive = interactive;

	if (check_interactive && (stat(dst, &st) != (-1))) {
		snprintf(buf, sizeof(buf), "qfile -Cqev %s 2>/dev/null", dst);
		if ((fp = popen(buf, "r")) != NULL) {
			buf[0] = '\0';
			if ((fgets(buf, sizeof(buf), fp)) != NULL)
				if ((p = strchr(buf, '\n')) != NULL) {
					*p = 0;
					p = xstrdup(buf);
					snprintf(buf, sizeof(buf), "%s/%s", pkg->CATEGORY, pkg->PF);
					if (strcmp(buf, p) == 0)
						check_interactive = 0;
					else
						warn("%s owns %s", p, dst);
					free(p);
				}
			pclose(fp);
		}
	}
	snprintf(buf, sizeof(buf), BUSYBOX " mv %s %s %s", check_interactive ? "-i" : "", src, dst);
	ret = (system(buf) << 8);
	if (ret == 0) {
		qprintf("%s>>>%s %s\n", GREEN, NORM, dst);
	} else
		warn("%s!!!%s %s ret=%d", RED, NORM, dst, ret);
	return ret;
}

void fetch(const char *destdir, const char *src)
{
	char buf[BUFSIZ];

	fflush(stdout);
	fflush(stderr);

#if 0
	if (getenv("FETCHCOMMAND") != NULL) {
		snprintf(buf, sizeof(buf), "(export DISTDIR='%s' URI='%s/%s'; %s)",
			destdir, binhost, src, getenv("FETCHCOMMAND"));
	} else
#endif
	{
		snprintf(buf, sizeof(buf), "%s " BUSYBOX " wget %s -P %s %s/%s", (force_download || install) ? "" : pretend ? "echo " : "",
			(quiet ? "-q" : ""), destdir, binhost, src);
	}
	system(buf);
	fflush(stdout);
	fflush(stderr);
}

void qmerge_initialize(const char *Packages)
{
	if (strlen(BUSYBOX))
		if (access(BUSYBOX, X_OK) != 0)
			err(BUSYBOX " must be installed");

	if (access("/bin/sh", X_OK) != 0)
		err("/bin/sh must be installed");

	if (pkgdir[0] != '/')
		errf("PKGDIR='%s' does not appear to be valid", pkgdir);

	if (!binhost[0])
		errf("PORTAGE_BINHOST= does not appear to be valid");

	if (!search_pkgs && !pretend) {
		if ((access(pkgdir, R_OK|W_OK|X_OK)) != 0)
			errf("Wrong perms on PKGDIR='%s'", pkgdir);
		mkdir(port_tmpdir, 0755);
	}

	xchdir(port_tmpdir);
	xchdir("portage");

	if (force_download && force_download != 2)
		unlink(Packages);

	if ((access(Packages, R_OK) != 0) && (force_download != 2)) {
			char *tbuf = NULL;
			xasprintf(&tbuf, "%s/portage/", port_tmpdir);
		        if ((access(Packages, R_OK) != 0))
				fetch(tbuf, Packages);
			free(tbuf);
	}
}

char *best_version(const char *CATEGORY, const char *PN)
{
	static char buf[1024];
	FILE *fp;
	char *p;

	/* if defined(EBUG) this spits out incorrect versions. */
	snprintf(buf, sizeof(buf), "qlist -CIev '%s%s%s' 2>/dev/null | tr '\n' ' '",
		(CATEGORY != NULL ? CATEGORY : ""), (CATEGORY != NULL ? "/" : ""), PN);

	if ((fp = popen(buf, "r")) == NULL)
		return NULL;

	buf[0] = '\0';
	if ((fgets(buf, sizeof(buf), fp)) != NULL)
		if ((p = strchr(buf, '\n')) != NULL)
			*p = 0;
	pclose(fp);
	rmspace(buf);
	return (char *) buf;
}

int config_protected(const char *buf, int ARGC, char **ARGV)
{
	int i;
	char dest[_Q_PATH_MAX];
	snprintf(dest, sizeof(dest), "%s%s", portroot, buf);

	for (i = 1; i < ARGC; i++)
		if ((strncmp(ARGV[i], buf, strlen(ARGV[i]))) == 0)
			if ((access(dest, R_OK)) == 0)
				return 1;

	if ((strncmp("/etc", buf, 4)) == 0)
		if ((access(dest, R_OK)) == 0)
			return 1;
	if ((strcmp("/bin/sh", buf)) == 0)
		return 1;
	return 0;
}

void crossmount_rm(char *, const size_t size, const char *, const struct stat);
void crossmount_rm(char *buf, const size_t size, const char *fname, const struct stat st)
{
	struct stat lst;

	assert(pretend == 0);

	if (lstat(buf, &lst) == (-1))
		return;
	if (lst.st_dev != st.st_dev) {
		warn("skipping crossmount install masking: %s", buf);
		return;
	}
	qprintf("%s<<<%s %s\n", YELLOW, NORM, buf);
	snprintf(buf, size, BUSYBOX " rm -rf ./%s", fname);
	system(buf);
}

void install_mask_pwd(int argc, char **argv, const struct stat st);
void install_mask_pwd(int iargc, char **iargv, const struct stat st)
{
	char buf[1024];
	int i;

	for (i = 1; i < iargc; i++) {

		if (iargv[i][0] != '/')
			continue;

		snprintf(buf, sizeof(buf), ".%s", iargv[i]);

		if ((strchr(iargv[i], '*') != NULL) || (strchr(iargv[i], '{') != NULL)) {
			int g;
			glob_t globbuf;

			globbuf.gl_offs = 0;
			if ((glob(buf, GLOB_DOOFFS|GLOB_BRACE, NULL, &globbuf)) == 0) {
				for (g = 0; g < globbuf.gl_pathc; g++) {
					strncpy(buf, globbuf.gl_pathv[g], sizeof(buf));
					/* qprintf("globbed: %s\n", globbuf.gl_pathv[g]); */
					crossmount_rm(buf, sizeof(buf), globbuf.gl_pathv[g], st);
				}
				globfree(&globbuf);
			}
			continue;
		}
		crossmount_rm(buf, sizeof(buf), iargv[i], st);
	}
}

char *atom2str(depend_atom *atom, char *buf, size_t size);
char *atom2str(depend_atom *atom, char *buf, size_t size)
{
	if (atom->PR_int)
		snprintf(buf, size, "%s-%s-r%i", atom->PN, atom->PV, atom->PR_int);
	else
		snprintf(buf, size, "%s-%s", atom->PN, atom->PV);
	return buf;
}

char qprint_tree_node(int level, depend_atom *atom, struct pkg_t *pkg);
char qprint_tree_node(int level, depend_atom *atom, struct pkg_t *pkg)
{
	char buf[1024];
	char *p;
	int i, ret;

	char install_ver[126] = "";
	char c = 'N';

	if (!pretend)
		return 0;

	p = best_version(pkg->CATEGORY, atom->PN);
	if (strlen(p) < 1) {
		c = 'N';
		snprintf(buf, sizeof(buf), "%sN%s", GREEN, NORM);
	} else {
		depend_atom *subatom = atom_explode(p);
		if (subatom != NULL) {
			atom2str(subatom, buf, sizeof(buf));
			atom2str(atom, install_ver, sizeof(install_ver));
			ret = atom_compare_str(install_ver, buf);
			switch (ret) {
				case EQUAL: c = 'R'; break;
				case NEWER: c = 'U'; break;
				case OLDER: c = 'D'; break;
				default: c = '?'; break;
			}
			strncpy(buf, subatom->P, sizeof(buf));
			snprintf(install_ver, sizeof(install_ver), "[%s%s%s] ", DKBLUE, buf, NORM);
			atom_implode(subatom);
		}
		if (update_only && c != 'U')
			return c;
		if (((c == 'R') || (c == 'D')) && update_only && level)
			return c;
		if (c == 'R')
			snprintf(buf, sizeof(buf), "%s%c%s", YELLOW, c, NORM);
		if ((c == 'U') || (c == 'D'))
			snprintf(buf, sizeof(buf), "%s%c%s", BLUE, c, NORM);
#if 0
		if (level) {
			switch (c) {
				case 'N':
				case 'U': break;
				default:
					qprintf("[%c] %d %s\n", c, level, pkg->PF); return;
					break;
			}
		}
#endif
	}
	printf("[%s] ", buf);
	for (i = 0; i < level; i++) putchar(' ');
	if (verbose)
		printf("%s%s/%s%s %s%s%s%s%s%s\n", DKGREEN, pkg->CATEGORY, pkg->PF, NORM,
			install_ver, strlen(pkg->USE) > 0 ? "(" : "", RED, pkg->USE, NORM, strlen(pkg->USE) > 0 ? ")" : "");
	else
		printf("%s%s/%s%s\n", DKGREEN, pkg->CATEGORY, pkg->PF, NORM);
	return c;
}

/* oh shit getting into pkg mgt here. FIXME: write a real dep resolver. */
void pkg_merge(int level, depend_atom *atom, struct pkg_t *pkg)
{
	FILE *fp, *contents;
	char buf[1024];
	char tarball[255];
	char *p;
	int i;
	char **ARGV = NULL;
	int ARGC = 0;
	const char *saved_argv0 = argv0;
	int ret, saved_optind = optind;
	struct stat st, lst;
	char c;
	char **iargv = NULL;
	int iargc = 0;

	if (!install) return;
	if (!pkg) return;
	if (!atom) return;

	if (!pkg->PF[0] || !pkg->CATEGORY) {
		if (verbose) warn("CPF is really NULL at level %d", level);
		return;
	}

	c = qprint_tree_node(level, atom, pkg);

	/* if (((c == 'R') || (c == 'D')) && update_only)
		return;
	*/

	if (pkg->RDEPEND[0] && follow_rdepends) {
		IF_DEBUG(fprintf(stderr, "\n+Parent: %s/%s\n", pkg->CATEGORY, pkg->PF));
		IF_DEBUG(fprintf(stderr, "+Depstring: %s\n", pkg->RDEPEND));

		/* <hack> */
		if (strncmp(pkg->RDEPEND, "|| ", 3) == 0) {
			if (verbose)
				qfprintf(stderr, "fix this rdepend hack %s\n", pkg->RDEPEND);
			strcpy(pkg->RDEPEND, "");
		}
		/* </hack> */

		makeargv(pkg->RDEPEND, &ARGC, &ARGV);
		/* Walk the rdepends here. Merging what need be. */
		for (i = 1; i < ARGC; i++) {
			depend_atom *subatom, *ratom;
			char *name = ARGV[i];
			switch (*name) {
				case '|':
				case '!':
				case '<':
				case '>':
				case '=':
					if (verbose)
						qfprintf(stderr, "Unhandled depstring %s\n", name);
					break;
				default:
					if (*name == '~') {
						name = ARGV[i] + 1;
						/* warn("newname = %s", name); */
					}
					if ((subatom = atom_explode(name)) != NULL) {
						char *dep;
						struct pkg_t *subpkg;
						char *resolved = NULL;

						dep = NULL;
						dep = find_binpkg(name);

						if (strncmp(name, "virtual/", 8) == 0) {
							if (virtuals == NULL)
								virtuals = resolve_virtuals();
							resolved = find_binpkg(virtual(name, virtuals));
							if ((resolved == NULL) || (!strlen(resolved)))
								resolved = find_binpkg(name);
						} else
							resolved = NULL;

						if (resolved == NULL)
							resolved = dep;

						IF_DEBUG(fprintf(stderr, "+Atom: argv0(%s) dep(%s) resolved(%s)\n", name, dep, resolved));

						if (strlen(resolved) < 1) {
							warn("Cant find a binpkg for %s from rdepend(%s)", name, pkg->RDEPEND);
							continue;
						}

						/* ratom = atom_explode(resolved); */
						subpkg = grab_binpkg_info(resolved);	/* free me later */

						assert(subpkg != NULL);
						IF_DEBUG(fprintf(stderr, "+Subpkg: %s/%s\n", subpkg->CATEGORY, subpkg->PF));

						/* look at installed versions now. If NULL or < merge this pkg */
						snprintf(buf, sizeof(buf), "%s/%s", subpkg->CATEGORY, subpkg->PF);

						ratom = atom_explode(buf);

						p = best_version(subpkg->CATEGORY, subpkg->PF);
						/* we dont want to remerge equal versions here */
						IF_DEBUG(fprintf(stderr, "+Installed: %s\n", p));
						if (strlen(p) < 1)
							if (!((strcmp(pkg->PF, subpkg->PF) == 0) && (strcmp(pkg->CATEGORY, subpkg->CATEGORY) == 0)))
								pkg_fetch(level+1, ratom, subpkg);

						atom_implode(subatom);
						atom_implode(ratom);
						free(subpkg);
					} else {
						qfprintf(stderr, "Cant explode atom %s\n", name);
					}
					break;
			}
		}
		freeargv(ARGC, ARGV);
		ARGC = 0; ARGV = NULL;
	}
	if (pretend)
		return;

	xchdir(port_tmpdir);

	mkdir(pkg->PF, 0755);
	/* mkdir(pkg->PF, 0710); */
	xchdir(pkg->PF);

	system(BUSYBOX " rm -rf ./*"); /* this line does funny things to nano's highlighting. */

	/* split the tbz and xpak data */
	snprintf(tarball, sizeof(tarball), "%s.tbz2", pkg->PF);
	snprintf(buf, sizeof(buf), "%s/%s/%s", pkgdir, pkg->CATEGORY, tarball);
	unlink(tarball);
	symlink(buf, tarball);
	mkdir("vdb", 0755);
	mkdir("image", 0755);

	/* unpack the tarball using our internal qxpak */
	ARGV = xmalloc(sizeof(char *));
	ARGV[0] = (char *) "qtbz2";
	ARGV[1] = (char *) "-s";
	ARGV[2] = tarball;
	argv0 = ARGV[0];
	optind = 0;
	assert((ret = qtbz2_main(3, ARGV)) == 0);
	argv0 = saved_argv0;
	optind = saved_optind;
	free(ARGV);
	ARGV = NULL;

	/* list and extract vdb files from the xpak */
	snprintf(buf, sizeof(buf), "qxpak -d %s/%s/vdb -x %s.xpak `qxpak -l %s.xpak`",
		port_tmpdir, pkg->PF, pkg->PF, pkg->PF);
	system(buf);

	/* extrct the binary package data */
	snprintf(buf, sizeof(buf), BUSYBOX " tar -jx%sf %s.tar.bz2 -C image/", ((verbose > 1) ? "v" : ""), pkg->PF);
	system(buf);
	fflush(stdout);

	/* check for an already installed pkg */
	snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, pkg->PF);

	/* Unmerge the other versions */
	p = best_version(atom->CATEGORY, atom->PN);
	if (*p) {
		ARGC = 0; ARGV = NULL;
		makeargv(p, &ARGC, &ARGV);
		for (i = 1; i < ARGC; i++) {
			char pf[126];
			char *slot = NULL;
			char u;

			strncpy(pf, ARGV[i], sizeof(pf));
			switch ((ret = atom_compare_str(buf, pf))) {
				case ERROR: break;
				case NOT_EQUAL: break;
				case NEWER:
				case OLDER:
				case EQUAL:
					u = 1;
					slot = grab_vdb_item("SLOT", atom->CATEGORY, basename(pf));
					if (pkg->SLOT[0] && slot) {
						if (strcmp(pkg->SLOT, slot) != 0)
							u = 0;
					}
					if (u) pkg_unmerge(atom->CATEGORY, basename(pf)); /* We need to really set this unmerge pending after we look at contents of the new pkg */
					break;
				default:
					warn("no idea how we reached here.");
					break;
			}
			qprintf("%s+++%s %s %s %s\n", GREEN, NORM, buf, booga[ret], ARGV[i]);
		}
		freeargv(ARGC, ARGV);
		ARGC = 0; ARGV = NULL;
	}
	xchdir("image");

	if (stat("./", &st) == (-1))
		err("Cant stat pwd");

	makeargv(install_mask, &iargc, &iargv);
	install_mask_pwd(iargc, iargv, st);

	if ((strstr(features, "noinfo")) != NULL) if (access("./usr/share/info", R_OK) == 0) system(BUSYBOX " rm -rf ./usr/share/info");
	if ((strstr(features, "noman"))  != NULL) if (access("./usr/share/man",  R_OK) == 0) system(BUSYBOX " rm -rf ./usr/share/man");
	if ((strstr(features, "nodoc"))  != NULL) if (access("./usr/share/doc",  R_OK) == 0) system(BUSYBOX " rm -rf ./usr/share/doc");

	/* we dont care about the return code */
	rmdir("./usr/share");

	if ((fp = popen(BUSYBOX" find .", "r")) == NULL)
		errf("come on wtf no find?");

	if ((contents = fopen("../vdb/CONTENTS", "w")) == NULL)
		errf("come on wtf?");

	makeargv(config_protect, &ARGC, &ARGV);

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		char line[BUFSIZ];
		int protected = 0;
		char matched = 0;
		char dest[_Q_PATH_MAX];

		if ((p = strrchr(buf, '\n')) != NULL)
			*p = 0;

		if (buf[0] != '.')
			continue;

		if (((strcmp(buf, ".")) == 0) || ((strcmp(buf, "..")) == 0))
			continue;

		for (i = 1; i < iargc; i++) {
			int fn;
			if ((fn = fnmatch(iargv[i], buf, 0)) == 0)
				matched = 1;
		}
		if (matched) {
			unlink(buf);
			continue;
		}
		/* use lstats for symlinks */
		lstat(buf, &st);

		line[0] = 0;

		snprintf(dest, sizeof(dest), "%s%s", portroot, &buf[1]);

		/* portage has code that handes fifo's but it looks unused */

		if ((S_ISCHR(st.st_mode)) || \
			(S_ISBLK(st.st_mode))); /* block or character device */
		if (S_ISFIFO(st.st_mode));	/* FIFO (named pipe) */
		if (S_ISSOCK(st.st_mode));	/* socket? (Not in POSIX.1-1996.) */

		/* syntax: dir dirname */
		if (S_ISDIR(st.st_mode)) {
			mkdir(dest, st.st_mode);
			chown(dest, st.st_uid, st.st_gid);
			snprintf(line, sizeof(line), "dir %s", &buf[1]);
		}

		/* syntax: obj filename hash mtime */
		if (S_ISREG(st.st_mode)) { /* regular file */
			struct timeval tv;
			unsigned char *hash;

			hash = hash_file(buf, HASH_MD5);

			snprintf(line, sizeof(line), "obj %s %s %lu", &buf[1], hash, st.st_mtime);
			/* /etc /usr/kde/2/share/config /usr/kde/3/share/config	/var/qmail/control */

			protected = config_protected(&buf[1], ARGC, ARGV);
			if (protected) {
				unsigned char *target_hash = hash_file(dest, HASH_MD5);
				if (memcmp(target_hash, hash, 16) != 0) {
					char newbuf[sizeof(buf)];
					qprintf("%s***%s %s\n", BRYELLOW, NORM, &buf[1]);
					if (verbose) {
						snprintf(newbuf, sizeof(newbuf), "diff -u %s %s", buf, dest);
						system(newbuf);
					}
					continue;
				}
			}
			if ((lstat(dest, &lst)) != (-1)) {
				if (S_ISLNK(lst.st_mode)) {
					warn("%s exists and is a symlink and we are going to overwrite it with a file", dest);
					unlink_q(dest);
				}
			}
			if (interactive_rename(buf, dest, pkg) != 0)
				continue;

			assert(chmod(dest, st.st_mode) == 0);
			assert(chown(dest, st.st_uid, st.st_gid) == 0);

			tv.tv_sec = st.st_mtime;
			tv.tv_usec = 0;
			/* utime(&buf[1], &tv); */
		}

		/* symlinks are unfinished */
		/* syntax: sym src -> dst */
		if (S_ISLNK(st.st_mode)) { /* (Not in POSIX.1-1996.) */
			/*
			  save pwd
			  get the dirname of the symlink from buf1
			  chdir to it's dirname unless it's a dir itself
			  symlink src dest
			  report any errors along the way
			*/
			char path[sizeof(buf)];
			char pwd[sizeof(buf)];
			char tmp[sizeof(buf)];

			memset(&path, 0, sizeof(path));

			assert(strlen(dest));
			readlink(buf, path, sizeof(path));
			assert(strlen(path));;

			snprintf(line, sizeof(line), "sym %s -> %s%s%lu", &buf[1], path, " ", st.st_mtime);
			/* snprintf(line, sizeof(line), "sym %s -> %s", &buf[1], path); */
			/* we better have one byte here to play with */
			strcpy(tmp, &buf[1]);
			if (tmp[0] != '/') errf("sym does not start with /");

			xgetcwd(pwd, sizeof(pwd));
			xchdir(dirname(tmp)); /* tmp gets eatten up now by the dirname call */
			if (lstat(path, &lst) != (-1))
				unlink_q(dest);
			/* if (path[0] != '/')
				puts("path does not start with /");
			*/
			if ((symlink(path, dest)) != 0)
				if (errno != EEXIST)
					warnp("symlink failed %s -> %s", path, &buf[1]);
			xchdir(pwd);
		}
		/* Save the line to the contents file */
		if (*line) fprintf(contents, "%s\n", line);
	}

	freeargv(ARGC, ARGV);	/* config_protect */
	freeargv(iargc, iargv);	/* install_mask */

	iargv = 0;
	iargv = NULL;

	fclose(contents);
	pclose(fp);

	xchdir(port_tmpdir);
	xchdir(pkg->PF);

	snprintf(buf, sizeof(buf), "%s/var/db/pkg/%s/", portroot, pkg->CATEGORY);
	if (access(buf, R_OK|W_OK|X_OK) != 0)
		mkdirhier(buf, 0755);
	strncat(buf, pkg->PF, sizeof(buf)-strlen(buf)-1);

	/* FIXME */ /* move unmerging to around here ? */
	/* not perfect when a version is already installed */
	if (access(buf, X_OK) == 0) {
		char buf2[sizeof(buf)] = "";
		/* we need to compare CONTENTS in it and remove any file not provided by our CONTENTS */
		snprintf(buf2, sizeof(buf2), BUSYBOX " rm -rf %s", buf);
		system(buf2);
	}
	if ((fp = fopen("vdb/COUNTER", "w")) != NULL) {
		fputs("0", fp);
		fclose(fp);
	}
	interactive_rename("vdb", buf, pkg);

	/* run postinst on non embedded systems */
	if (which("ebuild") != NULL) {
		char *tbuf;
		xasprintf(&tbuf, "ebuild %s/%s.ebuild postinst", buf, basename(buf));
		system(tbuf);
		free(tbuf);
	}

	snprintf(buf, sizeof(buf), BUSYBOX " %s.tar.bz2", pkg->PF);
	unlink_q(buf);
	xchdir(port_tmpdir);
	snprintf(buf, sizeof(buf), "rm -rf %s", pkg->PF);
	system(buf);

	snprintf(buf, sizeof(buf), "%s/%s.tbz2", pkgdir, pkg->PF);
	unlink(buf);
}

int pkg_unmerge(char *cat, char *pkgname)
{
	char buf[BUFSIZ];
	FILE *fp;
	int argc;
	char **argv;
	llist_char *dirs = NULL;

	if ((strchr(pkgname, ' ') != NULL) || (strchr(cat, ' ') != NULL)) {
		qfprintf(stderr, "%s!!!%s '%s' '%s' (ambiguous name) specify fully-qualified pkgs\n", RED, NORM, cat, pkgname);
		qfprintf(stderr, "%s!!!%s %s/%s (ambiguous name) specify fully-qualified pkgs\n", RED, NORM, cat, pkgname);
		/* qfprintf(stderr, "%s!!!%s %s %s (ambiguous name) specify fully-qualified pkgs\n", RED, NORM, pkgname); */
		return 1;
	}
	printf("%s===%s %s%s%s/%s%s%s\n", YELLOW, NORM, WHITE, cat, NORM, CYAN, pkgname, NORM);

	snprintf(buf, sizeof(buf), "%s/%s/%s/%s/CONTENTS", portroot, portvdb, cat, pkgname);

	if ((fp = fopen(buf, "r")) == NULL)
		return 1;

	argc = 0;
	argv = NULL;
	makeargv(config_protect, &argc, &argv);

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		contents_entry *e;
		char zing[20];
		int protected = 0;
		struct stat lst;
		char dst[_Q_PATH_MAX];

		e = contents_parse_line(buf);
		if (!e) continue;
		snprintf(dst, sizeof(dst), "%s%s", portroot, e->name);
		protected = config_protected(e->name, argc, argv);
		snprintf(zing, sizeof(zing), "%s%s%s", protected ? YELLOW : GREEN, protected ? "***" : "<<<" , NORM);
		/* Should we remove in order symlinks,objects,dirs ? */
		switch (e->type) {
			case CONTENTS_DIR:
				if (!protected) {
					/* since the dir contains files, we remove it later */
					llist_char *list = xmalloc(sizeof(llist_char));
					list->data = xstrdup(dst);
					list->next = dirs;
					dirs = list;
				}
				qprintf("%s %s%s%s/\n", zing, DKBLUE, dst, NORM);
				break;
			case CONTENTS_OBJ:
				if (!protected)	unlink_q(dst);
				qprintf("%s %s\n", zing, dst);
				break;
			case CONTENTS_SYM:
				if (protected) break;
				if (e->name[0] != '/') break;
				if (e->sym_target[0] != '/') {
					if (lstat(dst, &lst) != (-1)) {
						if (S_ISLNK(lst.st_mode)) {
							qprintf("%s %s%s -> %s%s\n", zing, CYAN, dst, e->sym_target, NORM);
							unlink_q(dst);
							break;
						}
					} else {
						warnp("lstat failed for %s -> '%s'", dst, e->sym_target);
					}
					warn("!!! %s -> %s", e->name, e->sym_target);
					break;
				}
				if (lstat(dst, &lst) != (-1)) {
					if (S_ISLNK(lst.st_mode)) {
						qprintf("%s %s%s -> %s%s\n", zing, CYAN, dst, e->sym_target, NORM);
						unlink_q(dst);
					} else {
						warn("%s is not a symlink", dst);
					}
				} else {
					warnp("lstat failed for '%s' -> %s", dst, e->sym_target);
				}
				break;
			default:
				warn("%s???%s %s%s%s (%d)", RED, NORM, WHITE, dst, NORM, e->type);
				break;
		}
	}

	fclose(fp);

	/* remove all dirs in reverse order */
	while (dirs != NULL) {
		llist_char *list = dirs;
		dirs = dirs->next;
		rmdir(list->data);
		free(list->data);
		free(list);
	}

	freeargv(argc, argv);

	if (!pretend) {
		snprintf(buf, sizeof(buf), BUSYBOX " rm -rf %s/%s/%s/%s", portroot, portvdb, cat, pkgname);
		system(buf);
	}
	return 1;
}

int unlink_empty(char *buf)
{
	struct stat st;
	if ((stat(buf, &st)) != (-1))
		if (st.st_size == 0)
			return unlink(buf);
	return (-1);
}

int match_pkg(const char *name, struct pkg_t *pkg)
{
	depend_atom *atom;
	char buf[255], buf2[255];
	int match = 0;
	char *ptr;

	snprintf(buf, sizeof(buf), "%s/%s", pkg->CATEGORY, pkg->PF);
	if ((atom = atom_explode(buf)) == NULL)
		errf("%s/%s is not a valid atom", pkg->CATEGORY, pkg->PF);

	/* verify this is the requested package */
	if ((strcmp(name, buf)) == 0)
		match = 1;

	if ((strcmp(name, pkg->PF)) == 0)
		match = 2;

	snprintf(buf2, sizeof(buf2), "%s/%s", pkg->CATEGORY, atom->PN);

	if ((strcmp(name, buf2)) == 0)
		match = 3;

	if ((strcmp(name, atom->PN)) == 0)
		match = 4;

	if (match)
		goto match_done;

	if ((ptr = strchr(name, ':')) != NULL) {
		depend_atom *subatom = atom_explode(name);
		if (subatom == NULL)
			goto match_done;
		if (strcmp(atom->PN, subatom->PN) == 0)
			match = 1;
		atom_implode(subatom);
	}

match_done:
	atom_implode(atom);

	return match;
}

int pkg_verify_checksums(char *fname, struct pkg_t *pkg, depend_atom *atom, int strict, int display)
{
	char *hash = NULL;
	int ret = 0;

	if (nomd5)
		return ret;

	if (pkg->MD5[0]) {
		if ((hash = (char*) hash_file(fname, HASH_MD5)) == NULL) {
			errf("hash is NULL for %s", fname);
		}
		if (strcmp(hash, pkg->MD5) == 0) {
			if (display)
				printf("MD5:  [%sOK%s] %s %s/%s\n", GREEN, NORM, hash, atom->CATEGORY, pkg->PF);
		} else {
			if (display)
				warn("MD5:  [%sER%s] (%s) != (%s) %s/%s", RED, NORM, hash, pkg->MD5, atom->CATEGORY, pkg->PF);
			ret++;
		}
	}

	if (pkg->SHA1[0]) {
		hash = (char*) hash_file(fname, HASH_SHA1);
		if (strcmp(hash, pkg->SHA1) == 0) {
			if (display)
				qprintf("SHA1: [%sOK%s] %s %s/%s\n", GREEN, NORM, hash, atom->CATEGORY, pkg->PF);
		} else {
			if (display)
				warn("SHA1: [%sER%s] (%s) != (%s) %s/%s", RED, NORM, hash, pkg->SHA1, atom->CATEGORY, pkg->PF);
			ret++;
		}
	}

	if (!pkg->SHA1[0] && !pkg->MD5[0])
		return 1;

	if (strict && ret)
		errf("strict is set in features");

	return ret;
}

void pkg_process(int argc, char **argv, struct pkg_t *pkg)
{
	depend_atom *atom;
	char buf[255];
	int i;

	memset(buf, 0, sizeof(buf));

	snprintf(buf, sizeof(buf), "%s/%s", pkg->CATEGORY, pkg->PF);
	if ((atom = atom_explode(buf)) == NULL)
		errf("%s/%s is not a valid atom", pkg->CATEGORY, pkg->PF);

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-')
			continue;

		/* verify this is the requested package */
		if (match_pkg(argv[i], pkg) < 1)
			continue;

		pkg_fetch(0, atom, pkg);
	}
	/* free the atom */
	atom_implode(atom);
}

void pkg_fetch(int level, depend_atom *atom, struct pkg_t *pkg)
{
	char savecwd[_POSIX_PATH_MAX];
	char buf[255], str[255];
	memset(str, 0, sizeof(str));

	/* qmerge -pv patch */
	if (pretend) {
		if (!install) install++;
		/* qprint_tree_node(level, atom, pkg); */
		pkg_merge(level, atom, pkg);
		return;
	}

	/* check to see if file exists and it's checksum matches */
	snprintf(buf, sizeof(buf), "%s/%s/%s.tbz2", pkgdir, pkg->CATEGORY, pkg->PF);
	unlink_empty(buf);

	snprintf(str, sizeof(str), "%s/%s", pkgdir, pkg->CATEGORY);
	mkdir(str, 0755);

	if ((force_download) && (access(buf, R_OK) == 0) && ((pkg->SHA1[0]) || (pkg->MD5[0]))) {
		if ((pkg_verify_checksums(buf, pkg, atom, 0, 0)) != 0)
			unlink(buf);
	}
	if (access(buf, R_OK) == 0) {
		if ((!pkg->SHA1[0]) && (!pkg->MD5[0])) {
			warn("No checksum data for %s", buf);
			return;
		} else {
			if ((pkg_verify_checksums(buf, pkg, atom, qmerge_strict, 1)) == 0) {
				pkg_merge(0, atom, pkg);
				return;
			}
		}
	}
	if (verbose)
		printf("Fetching %s/%s.tbz2\n", atom->CATEGORY, pkg->PF);

	/* fetch the package */
	/* Check CATEGORY first */
	if (!old_repo) {
		snprintf(buf, sizeof(buf), "%s/%s.tbz2", atom->CATEGORY, pkg->PF);
		fetch(str, buf);
	}
	snprintf(buf, sizeof(buf), "%s/%s/%s.tbz2", pkgdir, atom->CATEGORY, pkg->PF);
	if (access(buf, R_OK) != 0) {
		snprintf(buf, sizeof(buf), "%s.tbz2", pkg->PF);
		fetch(str, buf);
		old_repo = 1;
	}

	/* verify the pkg exists now. unlink if zero bytes */
	snprintf(buf, sizeof(buf), "%s/%s/%s.tbz2", pkgdir, atom->CATEGORY, pkg->PF);
	unlink_empty(buf);

	if (access(buf, R_OK) != 0) {
		warn("Failed to fetch %s.tbz2 from %s", pkg->PF, binhost);
		fflush(stderr);
		return;
	}
	xgetcwd(savecwd, sizeof(savecwd));
	xchdir(pkgdir);
	if (chdir("All/") == 0) {
		snprintf(buf, sizeof(buf), "%s.tbz2", pkg->PF);
		snprintf(str, sizeof(str), "../%s/%s.tbz2", atom->CATEGORY, pkg->PF);
		unlink(buf);
		symlink(str, buf);
	}
	xchdir(savecwd);

	snprintf(buf, sizeof(buf), "%s/%s/%s.tbz2", pkgdir, atom->CATEGORY, pkg->PF);
	if ((pkg_verify_checksums(buf, pkg, atom, qmerge_strict, 1)) == 0) {
		pkg_merge(0, atom, pkg);
		return;
	}
}

void print_Pkg(int full, struct pkg_t *pkg)
{
	char *p = NULL;
	char buf[512];
	depend_atom *atom = NULL;

	if (!pkg->CATEGORY[0]) errf("CATEGORY is NULL");
	if (!pkg->PF[0]) errf("PF is NULL");

	printf("%s%s/%s%s%s%s%s%s\n", BOLD, pkg->CATEGORY, BLUE, pkg->PF, NORM,
		!quiet ? " [" : "",
		!quiet ? make_human_readable_str(pkg->SIZE, 1, KILOBYTE) : "",
		!quiet ? "KB]" : "");

	if (full == 0)
		return;

	if (pkg->DESC[0])
		printf(" %sDesc%s:%s %s\n", DKGREEN, YELLOW, NORM, pkg->DESC);
	if (pkg->SHA1[0])
		printf(" %sSha1%s:%s %s\n", DKGREEN, YELLOW, NORM, pkg->SHA1);
	if (pkg->MD5[0])
		printf(" %sMd5%s:%s %s\n", DKGREEN, YELLOW, NORM, pkg->MD5);
	if (!pkg->MD5[0] && !pkg->SHA1[0])
		printf(" %sSums%s:%s %s(MISSING!)%s\n", DKGREEN, YELLOW, NORM, RED, NORM);
	if (pkg->SLOT[0])
		printf(" %sSlot%s:%s %s\n", DKGREEN, YELLOW, NORM, pkg->SLOT);
	if (pkg->LICENSE[0])
		printf(" %sLicense%s:%s %s\n", DKGREEN, YELLOW, NORM, pkg->LICENSE);
	if (pkg->RDEPEND[0])
		printf(" %sRdepend%s:%s %s\n", DKGREEN, YELLOW, NORM, pkg->RDEPEND);
	if (pkg->USE[0])
		printf(" %sUse%s:%s %s\n", DKGREEN, YELLOW, NORM, pkg->USE);
	if (pkg->REPO[0])
		if (strcmp(pkg->REPO, "gentoo") != 0)
			printf(" %sRepo%s:%s %s\n", DKGREEN, YELLOW, NORM, pkg->REPO);

	snprintf(buf, sizeof(buf), "%s/%s", pkg->CATEGORY, pkg->PF);
	atom = atom_explode(buf);
	if ((atom = atom_explode(buf)) == NULL)
		return;
	if ((p = best_version(pkg->CATEGORY, atom->PN)) != NULL) {
		if (*p) {
			int ret;
			char *icolor = (char *) RED;
			ret = atom_compare_str(buf, p);
			switch (ret) {
				case EQUAL: icolor = (char *) RED; break;
				case NEWER: icolor = (char *) YELLOW; break;
				case OLDER: icolor = (char *) BLUE; break;
				default: icolor = (char *) NORM; break;
			}
			printf(" %sInstalled%s:%s %s%s%s\n", DKGREEN, YELLOW, NORM, icolor, p, NORM);
		}
	}
	atom_implode(atom);
}

int unmerge_packages(int argc, char **argv)
{
	depend_atom *atom;
	char *p;
	int i;

	if (argc == optind)
		return 1;

	for (i = 1; i < argc; i++) {
		char buf[512];

		if (argv[i][0] == '-')
			continue;

		p = best_version(NULL, argv[i]);
		if (!*p) continue;
		if ((atom = atom_explode(p)) == NULL) continue;
		atom2str(atom, buf, sizeof(buf));
		pkg_unmerge(atom->CATEGORY, buf);
		atom_implode(atom);
	}
	return 0;
}

struct pkg_t *grab_binpkg_info(const char *name)
{
	FILE *fp;
	char buf[BUFSIZ];
	char value[BUFSIZ];
	char *p;
	depend_atom *atom;

	struct pkg_t *pkg = xmalloc(sizeof(struct pkg_t));
	struct pkg_t *rpkg = xmalloc(sizeof(struct pkg_t));

	static char best_match[sizeof(Pkg.PF)+2+sizeof(Pkg.CATEGORY)];

	best_match[0] = 0;

	memset(pkg, 0, sizeof(struct pkg_t));
	memset(rpkg, 0, sizeof(struct pkg_t));
	snprintf(buf, sizeof(buf), "%s/portage/Packages", port_tmpdir);

	if ((fp = fopen(buf, "r")) == NULL)
		errp("Unable to open package file %s", buf);

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		if (*buf == '\n') {
			if (pkg->PF[0] && pkg->CATEGORY[0]) {
				int ret;

				snprintf(buf, sizeof(buf), "%s/%s", pkg->CATEGORY, pkg->PF);
				if (strstr(buf, name) != NULL) {
					if (!best_match[0])
						strncpy(best_match, buf, sizeof(best_match));

					atom = atom_explode(buf);
					snprintf(buf, sizeof(buf), "%s/%s-%s", atom->CATEGORY, atom->PN, atom->PV);
					if (atom->PR_int)
						snprintf(buf, sizeof(buf), "%s/%s-%s-r%i", atom->CATEGORY, atom->PN, atom->PV, atom->PR_int);
					ret = atom_compare_str(name, buf);
					IF_DEBUG(fprintf(stderr, "=== atom_compare(%s, %s) = %d %s\n", name, buf, ret, booga[ret])); /* buf(%s) depend(%s)\n", ret, pkg->CATEGORY, pkg->PF, name, pkg->RDEPEND); */
					switch (ret) {
						case EQUAL:
						case NEWER:
							snprintf(buf, sizeof(buf), "%s/%s", pkg->CATEGORY, pkg->PF);
							ret = atom_compare_str(buf, best_match);
							if (ret == NEWER || ret == EQUAL) {
								strncpy(best_match, buf, sizeof(best_match));
								memcpy(rpkg, pkg, sizeof(struct pkg_t));
								IF_DEBUG(fprintf(stderr, "--- %s/%s depend(%s)\n", rpkg->CATEGORY, rpkg->PF, rpkg->RDEPEND));
							}
						case OLDER: break;
						default:
							break;
					}
					atom_implode(atom);
				}
				memset(pkg, 0, sizeof(struct pkg_t));
			}
			continue;
		}
		if ((p = strchr(buf, '\n')) != NULL)
			*p = 0;

		memset(value, 0, sizeof(value));
		if ((p = strchr(buf, ':')) == NULL)
			continue;
		if ((p = strchr(buf, ' ')) == NULL)
			continue;
		*p = 0;
		++p;
		strncpy(value, p, sizeof(value));

		if (*buf) {
			/* we dont need all the info */
			if ((strcmp(buf, "RDEPEND:")) == 0)
				strncpy(pkg->RDEPEND, value, sizeof(Pkg.RDEPEND));
			if ((strcmp(buf, "PF:")) == 0)
				strncpy(pkg->PF, value, sizeof(Pkg.PF));
			if ((strcmp(buf, "CATEGORY:")) == 0)
				strncpy(pkg->CATEGORY, value, sizeof(Pkg.CATEGORY));
			if ((strcmp(buf, "REPO:")) == 0)
				strncpy(pkg->REPO, value, sizeof(Pkg.REPO));

			if ((strcmp(buf, "CPV:")) == 0) {
				if ((atom = atom_explode(value)) != NULL) {
					snprintf(buf, sizeof(buf), "%s-%s", atom->PN, atom->PV);
					if (atom->PR_int)
						snprintf(buf, sizeof(buf), "%s-%s-r%i", atom->PN, atom->PV, atom->PR_int);
					strncpy(pkg->PF, buf, sizeof(Pkg.PF));
					strncpy(pkg->CATEGORY, atom->CATEGORY, sizeof(Pkg.CATEGORY));
					atom_implode(atom);
				}
			}
			if ((strcmp(buf, "SLOT:")) == 0)
				strncpy(pkg->SLOT, value, sizeof(Pkg.SLOT));
			if ((strcmp(buf, "USE:")) == 0)
				strncpy(pkg->USE, value, sizeof(Pkg.USE));

			/* checksums. We must have 1 or the other unless --*/
			if ((strcmp(buf, "MD5:")) == 0)
				strncpy(pkg->MD5, value, sizeof(Pkg.MD5));
			if ((strcmp(buf, "SHA1:")) == 0)
				strncpy(pkg->SHA1, value, sizeof(Pkg.SHA1));
		}
	}
	fclose(fp);
	free(pkg);
	return rpkg;
}

char *find_binpkg(const char *name)
{
	FILE *fp;
	char buf[BUFSIZ];
	char value[BUFSIZ];
	char *p;
	char PF[sizeof(Pkg.PF)];
	char CATEGORY[sizeof(Pkg.CATEGORY)];

	static char best_match[sizeof(Pkg.PF)+2+sizeof(Pkg.CATEGORY)];

	best_match[0] = 0;
	if (NULL == name)
		return best_match;

	snprintf(buf, sizeof(buf), "%s/portage/Packages", port_tmpdir);

	if ((fp = fopen(buf, "r")) == NULL)
		errp("Unable to open package file %s", buf);

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		if (*buf == '\n') {
			if (PF[0] && CATEGORY[0]) {
				int ret;
				snprintf(buf, sizeof(buf), "%s/%s", CATEGORY, PF);
				if (strstr(buf, name) != NULL) {
					depend_atom *atom;

					if (!best_match[0])
						strncpy(best_match, buf, sizeof(best_match));

					atom = atom_explode(buf);
					snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, atom->PN);
					ret = atom_compare_str(name, buf);
					switch (ret) {
						case OLDER: break;
						case NEWER:
						case EQUAL:
							snprintf(buf, sizeof(buf), "%s/%s", CATEGORY, PF);
							ret = atom_compare_str(buf, best_match);
							if (ret == NEWER || ret == EQUAL)
								strncpy(best_match, buf, sizeof(best_match));
							/* printf("[%s == %s] = %d; %s/%s\n", name, buf, ret, CATEGORY, PF); */
						default:
							break;
					}
					atom_implode(atom);
				}
			}
			continue;
		}
		if ((p = strchr(buf, '\n')) != NULL)
			*p = 0;

		memset(&value, 0, sizeof(value));
		if ((p = strchr(buf, ':')) == NULL)
			continue;
		if ((p = strchr(buf, ' ')) == NULL)
			continue;
		*p = 0;
		++p;
		strncpy(value, p, sizeof(value));

		if (*buf) {
			if ((strcmp(buf, "CPV:")) == 0) {
				depend_atom *atom;
				if ((atom = atom_explode(value)) != NULL) {
					snprintf(buf, sizeof(buf), "%s-%s", atom->PN, atom->PV);
					if (atom->PR_int)
						snprintf(buf, sizeof(buf), "%s-%s-r%i", atom->PN, atom->PV, atom->PR_int);
					strncpy(PF, buf, sizeof(PF));
					strncpy(CATEGORY, atom->CATEGORY, sizeof(CATEGORY));
					atom_implode(atom);
				}
			}
			if ((strcmp(buf, "PF:")) == 0)
				strncpy(PF, value, sizeof(PF));
			if ((strcmp(buf, "CATEGORY:")) == 0)
				strncpy(CATEGORY, value, sizeof(CATEGORY));
		}
	}
	fclose(fp);
	return best_match;
}

int parse_packages(const char *Packages, int argc, char **argv)
{
	FILE *fp;
	char buf[BUFSIZ];
	char value[BUFSIZ];
	char *p;
	int i;
	long lineno = 0;

	if ((fp = fopen(Packages, "r")) == NULL)
		errp("Unable to open package file %s", Packages);

	memset(&Pkg, 0, sizeof(Pkg));

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		lineno++;
		if (*buf == '\n') {
			if ((strlen(Pkg.PF) > 0) && (strlen(Pkg.CATEGORY) > 0)) {
				struct pkg_t *pkg = xmalloc(sizeof(struct pkg_t));
				memcpy(pkg, &Pkg, sizeof(struct pkg_t));
				if (search_pkgs) {
					if (argc != optind) {
						for (i = 0; i < argc; i++)
							if ((match_pkg(argv[i], pkg) > 0) || (strcmp(argv[i], pkg->CATEGORY) == 0))
								print_Pkg(verbose, pkg);
					} else {
						print_Pkg(verbose, pkg);
					}
				} else {
					pkg_process(argc, argv, pkg);
				}
				free(pkg);
			}
			memset(&Pkg, 0, sizeof(Pkg));
			continue;
		}
		if ((p = strchr(buf, '\n')) != NULL)
			*p = 0;

		memset(&value, 0, sizeof(value));
		if ((p = strchr(buf, ':')) == NULL)
			continue;
		if ((p = strchr(buf, ' ')) == NULL)
			continue;
		*p = 0;
		++p;
		strncpy(value, p, sizeof(value));

		switch (*buf) {
			case 'U':
				if ((strcmp(buf, "USE:")) == 0) strncpy(Pkg.USE, value, sizeof(Pkg.USE));
				break;
			case 'P':
				if ((strcmp(buf, "PF:")) == 0) strncpy(Pkg.PF, value, sizeof(Pkg.PF));
				break;
			case 'S':
				if ((strcmp(buf, "SIZE:")) == 0) Pkg.SIZE = atol(value);
				if ((strcmp(buf, "SLOT:")) == 0) strncpy(Pkg.SLOT, value, sizeof(Pkg.SLOT));
				if ((strcmp(buf, "SHA1:")) == 0) strncpy(Pkg.SHA1, value, sizeof(Pkg.SHA1));
				break;
			case 'M':
				if ((strcmp(buf, "MD5:")) == 0) strncpy(Pkg.MD5, value, sizeof(Pkg.MD5));
				break;
			case 'R':
				if ((strcmp(buf, "REPO:")) == 0) strncpy(Pkg.REPO, value, sizeof(Pkg.REPO));
				if ((strcmp(buf, "RDEPEND:")) == 0) strncpy(Pkg.RDEPEND, value, sizeof(Pkg.RDEPEND));
				break;
			case 'L':
				if ((strcmp(buf, "LICENSE:")) == 0) strncpy(Pkg.LICENSE, value, sizeof(Pkg.LICENSE));
				break;
			case 'C':
				if ((strcmp(buf, "CATEGORY:")) == 0) strncpy(Pkg.CATEGORY, value, sizeof(Pkg.CATEGORY));
				if ((strcmp(buf, "CPV:")) == 0) {
					depend_atom *atom;
					if ((atom = atom_explode(value)) != NULL) {
						snprintf(buf, sizeof(buf), "%s-%s", atom->PN, atom->PV);
						if (atom->PR_int)
							snprintf(buf, sizeof(buf), "%s-%s-r%i", atom->PN, atom->PV, atom->PR_int);
						strncpy(Pkg.PF, buf, sizeof(Pkg.PF));
						strncpy(Pkg.CATEGORY, atom->CATEGORY, sizeof(Pkg.CATEGORY));
						atom_implode(atom);
					}
				}
				break;
			case 'D':
				if ((strcmp(buf, "DESC:")) == 0) strncpy(Pkg.DESC, value, sizeof(Pkg.DESC));
				break;
			default:
				break;
		}
		memset(&buf, 0, sizeof(buf));
	}
	fclose(fp);
	return 0;
}

queue *get_world(void);
queue *get_world(void) {
	FILE *fp;
	char buf[BUFSIZ];
	queue *world = NULL;
	char fname[_Q_PATH_MAX];

	if (*portroot && strcmp(portroot, "/") != 0)
		snprintf(fname, sizeof(fname), "%s/var/lib/portage/world", portroot);
	else
		strncpy(fname,  "/var/lib/portage/world", sizeof(fname));

	if ((fp = fopen(fname, "r")) == NULL) {
		warnp("fopen(\"%s\", \"r\"); = -1", fname);
		return NULL;
	}

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		char *p;
		char *slot = (char *) "0";

		rmspace(buf);

		if ((p = strchr(buf, ':')) != NULL) {
			*p = 0;
			slot = p + 1;
		}
		world = add_set(buf, slot, world);
	}
	fclose(fp);
	return world;
}

queue *qmerge_load_set(char *, queue *);
queue *qmerge_load_set(char *buf, queue *set) {
	if (set != NULL)
		return set;
	if ((strcmp(buf, "world") == 0))
		return get_world();
	if ((strcmp(buf, "all") == 0))
		return get_vdb_atoms(0);
	return NULL;
}

int qmerge_main(int argc, char **argv)
{
	int i;
	const char *Packages = "Packages";
	int ARGC = argc;
	char **ARGV = argv;

	if (argc < 2)
		qmerge_usage(EXIT_FAILURE);

	while ((i = GETOPT_LONG(QMERGE, qmerge, "")) != -1) {
		switch (i) {
			case 'f': force_download = 1; break;
			case 'F': force_download = 2; break;
			case 's': search_pkgs = 1; break;
			/* case 'i': case 'g': */
			case 'K': install = 1; break;
			case 'U': uninstall = 1; break;
			case 'p': pretend = 1; break;
			case 'u': update_only = 1;
			case 'y': interactive = 0; break;
			case 'O': follow_rdepends = 0; break;
			case '5': nomd5 = 1; break;
			COMMON_GETOPTS_CASES(qmerge)
		}
	}

	qmerge_strict = (strstr("strict", features) == 0) ? 1 : 0;

	/* Short circut this. */
	if ((install || uninstall) && !pretend) {
		if (follow_rdepends && getenv("QMERGE") == NULL) {
			uninstall = 0;
			install = 0;
			warn("Using these options are likely to break your system at this point. export QMERGE=1; if you think you know what your doing.");
		}
	}
	if (uninstall)
		return unmerge_packages(argc, argv);

	ARGC = argc;
	ARGV = argv;

	qmerge_initialize(Packages);

	if (optind < argc) {
		queue *world = NULL;
		int ind = optind;
		while (ind < argc) {
			size_t size = 0;
			i = ind;
			ind++;

			if ((world = qmerge_load_set(argv[i], world)) == NULL)
				continue;

			if (world != NULL) {
				queue *ll;
				char *ptr;

				for (ll = world; ll != NULL; ll = ll->next)
					size += (strlen(ll->name) + 1);
				if (size < 1)
					continue;

				size += (strlen(argv[0]) + 1);
				ptr = xmalloc(size);
				sprintf(ptr, "%s ", argv[0]);

				for (ll = world; ll != NULL; ll = ll->next) {
					char *p = NULL;
					xasprintf(&p, "%s ", ll->name);
					strcat(ptr, p);
					free(p);
				}
				ARGC = 0;
				ARGV = NULL;
				/* this will leak mem */
				/* follow_rdepends = 0; */
				makeargv(ptr, &ARGC, &ARGV);

				free(ptr);
				free_sets(world);
				world = NULL;
			}
		}
	}
	return parse_packages(Packages, ARGC, ARGV);
}

#else
DEFINE_APPLET_STUB(qmerge)
#endif
