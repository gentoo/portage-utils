/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qmerge.c,v 1.104 2011/02/24 01:29:27 vapier Exp $
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2010 Mike Frysinger  - <vapier@gentoo.org>
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

static const char * const qmerge_opts_help[] = {
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

static const char qmerge_rcsid[] = "$Id: qmerge.c,v 1.104 2011/02/24 01:29:27 vapier Exp $";
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
const char Packages[] = "Packages";

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
char *best_version(const char *, const char  *);
void pkg_fetch(int, depend_atom *, struct pkg_t *);
void pkg_merge(int, depend_atom *, struct pkg_t *);
int pkg_unmerge(const char *, const char *);
int unlink_empty(char *);
void print_Pkg(int, struct pkg_t *);
int pkg_verify_checksums(char *, struct pkg_t *, depend_atom *, int strict, int display);
char *find_binpkg(const char *);

struct pkg_t *grab_binpkg_info(const char *);

static int q_unlink_q(char *path, const char *func, int line)
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

	if (check_interactive && (stat(dst, &st) != -1)) {
		snprintf(buf, sizeof(buf), "qfile -Cqev %s 2>/dev/null", dst);
		if ((fp = popen(buf, "r")) != NULL) {
			buf[0] = '\0';
			if (fgets(buf, sizeof(buf), fp) != NULL)
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
	xsystem(buf);
	fflush(stdout);
	fflush(stderr);
}

static void qmerge_initialize(void)
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
		if (access(pkgdir, R_OK|W_OK|X_OK) != 0)
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
		if (access(Packages, R_OK) != 0)
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
	if (fgets(buf, sizeof(buf), fp) != NULL)
		if ((p = strchr(buf, '\n')) != NULL)
			*p = 0;
	pclose(fp);
	rmspace(buf);
	return (char *) buf;
}

static int
config_protected(const char *buf, int cp_argc, char **cp_argv,
                 int cpm_argc, char **cpm_argv)
{
	int i;
	char dest[_Q_PATH_MAX];
	snprintf(dest, sizeof(dest), "%s%s", portroot, buf);

	/* Check CONFIG_PROTECT_MASK */
	for (i = 1; i < cpm_argc; ++i)
		if (strncmp(cpm_argv[i], buf, strlen(cpm_argv[i])) == 0)
			return 0;

	/* Check CONFIG_PROTECT */
	for (i = 1; i < cp_argc; ++i)
		if (strncmp(cp_argv[i], buf, strlen(cp_argv[i])) == 0)
			if (access(dest, R_OK) == 0)
				return 1;

	/* this would probably be bad */
	if (strcmp("/bin/sh", buf) == 0)
		return 1;

	return 0;
}

static void crossmount_rm(const char *fname, const struct stat st)
{
	struct stat lst;

	assert(pretend == 0);

	if (lstat(fname, &lst) == -1)
		return;
	if (lst.st_dev != st.st_dev) {
		warn("skipping crossmount install masking: %s", fname);
		return;
	}
	qprintf("%s<<<%s %s\n", YELLOW, NORM, fname);
	rm_rf(fname);
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
			if (glob(buf, GLOB_DOOFFS|GLOB_BRACE, NULL, &globbuf) == 0) {
				for (g = 0; g < globbuf.gl_pathc; g++) {
					strncpy(buf, globbuf.gl_pathv[g], sizeof(buf));
					/* qprintf("globbed: %s\n", globbuf.gl_pathv[g]); */
					crossmount_rm(globbuf.gl_pathv[g], st);
				}
				globfree(&globbuf);
			}
			continue;
		}
		crossmount_rm(iargv[i], st);
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
		if ((c == 'R' || c == 'D') && update_only && level)
			return c;
		if (c == 'R')
			snprintf(buf, sizeof(buf), "%s%c%s", YELLOW, c, NORM);
		if (c == 'U' || c == 'D')
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
	for (i = 0; i < level; ++i)
		putchar(' ');
	if (verbose)
		printf("%s%s/%s%s %s%s%s%s%s%s\n", DKGREEN, pkg->CATEGORY, pkg->PF, NORM,
			install_ver, strlen(pkg->USE) > 0 ? "(" : "", RED, pkg->USE, NORM, strlen(pkg->USE) > 0 ? ")" : "");
	else
		printf("%s%s/%s%s\n", DKGREEN, pkg->CATEGORY, pkg->PF, NORM);
	return c;
}

static void pkg_run_func(const char *vdb_path, const char *phases, const char *func)
{
	char *script;

	/* This assumes no func is a substring of another func.
	 * Today, that assumption is valid for all funcs ...
	 * The phases are the func with the "pkg_" chopped off. */
	if (strstr(phases, func + 4) == NULL)
		return;

	qprintf(">>> %s\n", func);

	xasprintf(&script,
		/* Provide funcs required by the PMS */
		"debug-print() { :; }\n"
		"debug-print-function() { :; }\n"
		"debug-print-section() { :; }\n"
		"use() { useq \"$@\"; }\n"
		"useq() { hasq $1 $USE; }\n"
		"has() { hasq \"$@\"; }\n"
		"hasq() { local h=$1; shift; case \" $* \" in *\" $h \"*) return 0;; *) return 1;; esac; }\n"
		/* Unpack the env if need be */
		"[ -e '%1$s/environment' ] || { bzip2 -dc '%1$s/environment.bz2' > '%1$s/environment' || exit 1; }\n"
		/* Load the env and run the func */
		". %1$s/environment && %2$s",
		vdb_path, func);
	xsystembash(script);
	free(script);
}

/* Copy one tree (the single package) to another tree (ROOT) */
static int merge_tree_at(int fd_src, const char *src, int fd_dst, const char *dst,
                         FILE *contents, char **cpathp, int iargc, char **iargv,
                         int cp_argc, char **cp_argv, int cpm_argc, char **cpm_argv)
{
	int i, ret, subfd_src, subfd_dst;
	DIR *dir;
	struct dirent *de;
	struct stat st;
	char *cpath;
	size_t clen, nlen, mnlen;

	ret = -1;

	/* Get handles to these subdirs */
	subfd_src = openat(fd_src, src, O_RDONLY|O_CLOEXEC);
	if (subfd_src < 0)
		return ret;
	subfd_dst = openat(fd_dst, dst, O_RDONLY|O_CLOEXEC);
	if (subfd_dst < 0) {
		close(subfd_src);
		return ret;
	}

	dir = fdopendir(subfd_src);
	if (!dir)
		goto done;

	cpath = *cpathp;
	clen = strlen(cpath);
	cpath[clen] = '/';
	nlen = mnlen = 0;

	while ((de = readdir(dir)) != NULL) {
		const char *name = de->d_name;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;

		/* Build up the full path for this entry */
		nlen = strlen(name);
		if (mnlen < nlen) {
			cpath = *cpathp = realloc(*cpathp, nlen + clen + 2);
			mnlen = nlen;
		}
		strcpy(cpath + clen + 1, name);

		/* Check INSTALL_MASK */
		for (i = 1; i < iargc; ++i) {
			if (fnmatch(iargv[i], cpath, 0) == 0) {
				unlinkat(subfd_src, name, 0);
				unlinkat(subfd_dst, name, 0);
				continue;
			}
		}

		/* Find out what the source path is */
		if (fstatat(subfd_src, name, &st, AT_SYMLINK_NOFOLLOW)) {
			warnp("could not read %s", cpath);
			continue;
		}

		/* Migrate a directory */
		if (S_ISDIR(st.st_mode)) {
			if (mkdirat(subfd_dst, name, st.st_mode)) {
				if (errno != EEXIST) {
					warnp("could not read %s", cpath);
					continue;
				}

				/* XXX: update times of dir ? */
			}

			/* syntax: dir dirname */
			fprintf(contents, "dir %s\n", cpath);
			qprintf("%s>>>%s %s%s%s/\n", GREEN, NORM, DKBLUE, cpath, NORM);

			/* Copy all of these contents */
			merge_tree_at(subfd_src, name, subfd_dst, name, contents, cpathp,
				iargc, iargv, cp_argc, cp_argv, cpm_argc, cpm_argv);
		}

		/* Migrate a file */
		else if (S_ISREG(st.st_mode)) {
			struct timespec times[2];
			int fd_srcf, fd_dstf;
			unsigned char *hash;
			const char *tmpname, *dname;

			/* syntax: obj filename hash mtime */
			hash = hash_file_at(subfd_src, name, HASH_MD5);
			fprintf(contents, "obj %s %s %lu\n", cpath, hash, (unsigned long)st.st_mtime);

			/* Check CONFIG_PROTECT */
			if (config_protected(cpath, cp_argc, cp_argv, cpm_argc, cpm_argv)) {
				/* ._cfg####_ */
				char *num;
				dname = num = alloca(nlen + 10 + 1);
				*num++ = '.';
				*num++ = '_';
				*num++ = 'c';
				*num++ = 'f';
				*num++ = 'g';
				strcpy(num + 5, name);
				for (i = 0; i < 10000; ++i) {
					sprintf(num, "%04i", i);
					num[4] = '_';
					if (faccessat(subfd_dst, dname, F_OK, AT_SYMLINK_NOFOLLOW))
						break;
				}
				qprintf("%s>>>%s %s (%s)\n", GREEN, NORM, cpath, dname);
			} else {
				dname = name;
				qprintf("%s>>>%s %s\n", GREEN, NORM, cpath);
			}

			/* First try fast path -- src/dst are same device */
			if (renameat(subfd_src, dname, subfd_dst, name))
				;

			/* Fall back to slow path -- manual read/write */
			fd_srcf = openat(subfd_src, name, O_RDONLY|O_CLOEXEC);
			if (fd_srcf < 0) {
				warnp("could not read %s", cpath);
				continue;
			}

			/* Do not write the file in place ...
			 * will fail with files that are in use.
			 * XXX: Should we make this random ?
			 */
			tmpname = ".qmerge.update";
			fd_dstf = openat(subfd_dst, tmpname, O_WRONLY|O_CLOEXEC|O_CREAT|O_TRUNC, st.st_mode);
			if (fd_dstf < 0) {
				warnp("could not write %s", cpath);
				close(fd_srcf);
				continue;
			}

			/* Make sure owner/mode is sane before we write out data */
			if (fchown(fd_dstf, st.st_uid, st.st_gid)) {
				warnp("could not set ownership %s", cpath);
				continue;
			}
			if (fchmod(fd_dstf, st.st_mode)) {
				warnp("could not set permission %s", cpath);
				continue;
			}

			/* Do the actual data copy */
			if (copy_file_fd(fd_srcf, fd_dstf)) {
				warnp("could not write %s", cpath);
				if (ftruncate(fd_dstf, 0))
					/* don't care */;
				close(fd_srcf);
				close(fd_dstf);
				continue;
			}

			/* Preserve the file times */
			times[0] = st.st_mtim;
			times[1] = st.st_mtim;
			futimens(fd_dstf, times);

			close(fd_srcf);
			close(fd_dstf);

			/* Move the new tmp dst file to the right place */
			if (renameat(subfd_dst, tmpname, subfd_dst, dname)) {
				warnp("could not rename %s to %s", tmpname, cpath);
				continue;
			}
		}

		/* Migrate a symlink */
		else if (S_ISLNK(st.st_mode)) {
			size_t len = st.st_size;
			char *sym = alloca(len + 1);

			/* Find out what we're pointing to */
			if (readlinkat(subfd_src, name, sym, len) == -1) {
				warnp("could not read link %s", cpath);
				continue;
			}
			sym[len] = '\0';

			/* syntax: sym src -> dst mtime */
			fprintf(contents, "sym %s -> %s %lu\n", cpath, sym, (unsigned long)st.st_mtime);
			qprintf("%s>>>%s %s%s -> %s%s\n", GREEN, NORM, CYAN, cpath, sym, NORM);

			/* Make it in the dest tree */
			if (symlinkat(sym, subfd_dst, name)) {
				/* If the symlink exists, unlink it and try again */
				if (errno != EEXIST ||
				    unlinkat(subfd_dst, name, 0) ||
				    symlinkat(sym, subfd_dst, name)) {
					warnp("could not create link %s to %s", cpath, sym);
					continue;
				}
			}

			struct timespec times[2];
			times[0] = st.st_mtim;
			times[1] = st.st_mtim;
			utimensat(subfd_dst, name, times, AT_SYMLINK_NOFOLLOW);
		}

		/* WTF is this !? */
		else {
			warnp("unknown file type %s", cpath);
			continue;
		}
	}

 done:
	close(subfd_src);
	close(subfd_dst);

	return ret;
}

/* Copy one tree (the single package) to another tree (ROOT) */
static int merge_tree(const char *src, const char *dst, FILE *contents,
                      int iargc, char **iargv)
{
	int ret;
	int cp_argc, cpm_argc;
	char **cp_argv, **cpm_argv;
	char *cpath;

	/* XXX: be nice to pull this out of the current func
	 *      so we don't keep reparsing the same env var
	 *      when unmerging multiple packages.
	 */
	makeargv(config_protect, &cp_argc, &cp_argv);
	makeargv(config_protect_mask, &cpm_argc, &cpm_argv);

	cpath = xstrdup("");
	ret = merge_tree_at(AT_FDCWD, src, AT_FDCWD, dst, contents, &cpath,
		iargc, iargv, cp_argc, cp_argv, cpm_argc, cpm_argv);
	free(cpath);

	freeargv(cp_argc, cp_argv);
	freeargv(cpm_argc, cpm_argv);

	return ret;
}

/* oh shit getting into pkg mgt here. FIXME: write a real dep resolver. */
void pkg_merge(int level, depend_atom *atom, struct pkg_t *pkg)
{
	FILE *fp, *contents;
	char buf[1024], phases[128];
	char tarball[255];
	char *p;
	int i;
	char **ARGV;
	int ARGC;
	const char *saved_argv0 = argv0;
	int ret, saved_optind = optind;
	struct stat st;
	char c;
	char **iargv;
	int iargc;

	if (!install || !pkg || !atom)
		return;

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
							if (resolved == NULL || !strlen(resolved))
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
	}
	if (pretend)
		return;

	xchdir(port_tmpdir);

	mkdir(pkg->PF, 0755);
	/* mkdir(pkg->PF, 0710); */
	xchdir(pkg->PF);

	/* Doesn't actually remove $PWD, just everything under it */
	rm_rf(".");

	/* XXX: maybe some day we should have this step operate on the
	 *      tarball directly rather than unpacking it first. */

	/* split the tbz and xpak data */
	snprintf(tarball, sizeof(tarball), "%s.tbz2", pkg->PF);
	snprintf(buf, sizeof(buf), "%s/%s/%s", pkgdir, pkg->CATEGORY, tarball);
	unlink(tarball);
	if (symlink(buf, tarball))
		errp("symlink(%s, %s) failed", buf, tarball);
	mkdir("vdb", 0755);
	mkdir("image", 0755);

	/* unpack the tarball using our internal qxpak */
	ARGC = 3;
	ARGV = xmalloc(sizeof(*ARGV) * ARGC);
	ARGV[0] = (char *)"qtbz2";
	ARGV[1] = (char *)"-s";
	ARGV[2] = tarball;
	argv0 = ARGV[0];
	optind = 0;
	assert(qtbz2_main(ARGC, ARGV) == 0);
	argv0 = saved_argv0;
	optind = saved_optind;
	free(ARGV);

	/* list and extract vdb files from the xpak */
	snprintf(buf, sizeof(buf), "qxpak -d %s/%s/vdb -x %s.xpak `qxpak -l %s.xpak`",
		port_tmpdir, pkg->PF, pkg->PF, pkg->PF);
	xsystem(buf);

	/* extrct the binary package data */
	snprintf(buf, sizeof(buf), BUSYBOX " tar -jx%sf %s.tar.bz2 -C image/", ((verbose > 1) ? "v" : ""), pkg->PF);
	xsystem(buf);
	fflush(stdout);

	/* check for an already installed pkg */
	snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, pkg->PF);

	/* Unmerge the other versions */
	p = best_version(atom->CATEGORY, atom->PN);
	if (*p) {
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
					/* We need to really set this unmerge pending after we look at contents of the new pkg */
					if (u)
						pkg_unmerge(atom->CATEGORY, basename(pf));
					break;
				default:
					warn("no idea how we reached here.");
					break;
			}
			qprintf("%s+++%s %s %s %s\n", GREEN, NORM, buf, booga[ret], ARGV[i]);
		}
		freeargv(ARGC, ARGV);
	}

	xchdir("image");

	eat_file("../vdb/DEFINED_PHASES", phases, sizeof(phases));
	pkg_run_func("../vdb", phases, "pkg_preinst");

	if (stat(".", &st) == -1)
		err("Cant stat pwd");

	/* Initialize INSTALL_MASK and common stuff */
	makeargv(install_mask, &iargc, &iargv);
	install_mask_pwd(iargc, iargv, st);

	if (strstr(features, "noinfo")) rm_rf("./usr/share/info");
	if (strstr(features, "noman" )) rm_rf("./usr/share/man");
	if (strstr(features, "nodoc" )) rm_rf("./usr/share/doc");

	/* we dont care about the return code */
	rmdir("./usr/share");

	if ((contents = fopen("../vdb/CONTENTS", "w")) == NULL)
		errf("come on wtf?");

	merge_tree(".", portroot, contents, iargc, iargv);

	fclose(contents);

	freeargv(iargc, iargv);

	xchdir(port_tmpdir);
	xchdir(pkg->PF);

	/* run postinst */
	pkg_run_func("./vdb", phases, "pkg_postinst");

	/* FIXME */ /* move unmerging to around here ? */
	/* not perfect when a version is already installed */
	snprintf(buf, sizeof(buf), "%s/var/db/pkg/%s/", portroot, pkg->CATEGORY);
	if (access(buf, R_OK|W_OK|X_OK) != 0)
		mkdir_p(buf, 0755);
	strncat(buf, pkg->PF, sizeof(buf)-strlen(buf)-1);
	if (access(buf, X_OK) == 0) {
		/* we need to compare CONTENTS in it and remove any file not provided by our CONTENTS */
		rm_rf(buf);
	}

	if ((fp = fopen("vdb/COUNTER", "w")) != NULL) {
		fputs("0", fp);
		fclose(fp);
	}
	/* move the local vdb copy to the final place */
	interactive_rename("vdb", buf, pkg);

	snprintf(buf, sizeof(buf), BUSYBOX " %s.tar.bz2", pkg->PF);
	unlink_q(buf);
	xchdir(port_tmpdir);
	rm_rf(pkg->PF);

	snprintf(buf, sizeof(buf), "%s/%s.tbz2", pkgdir, pkg->PF);
	unlink(buf);

	printf("%s>>>%s %s%s%s/%s%s%s\n", YELLOW, NORM, WHITE, atom->CATEGORY, NORM, CYAN, atom->PN, NORM);
}

int pkg_unmerge(const char *cat, const char *pkgname)
{
	size_t buflen;
	char *buf, *vdb_path, phases[128];
	FILE *fp;
	int ret, fd, vdb_fd;
	int cp_argc, cpm_argc;
	char **cp_argv, **cpm_argv;
	llist_char *dirs = NULL;

	ret = 1;
	buf = NULL;
	vdb_path = NULL;
	vdb_fd = fd = -1;

	if ((strchr(pkgname, ' ') != NULL) || (strchr(cat, ' ') != NULL)) {
		qfprintf(stderr, "%s!!!%s '%s' '%s' (ambiguous name) specify fully-qualified pkgs\n", RED, NORM, cat, pkgname);
		qfprintf(stderr, "%s!!!%s %s/%s (ambiguous name) specify fully-qualified pkgs\n", RED, NORM, cat, pkgname);
		/* qfprintf(stderr, "%s!!!%s %s %s (ambiguous name) specify fully-qualified pkgs\n", RED, NORM, pkgname); */
		return 1;
	}
	printf("%s<<<%s %s%s%s/%s%s%s\n", YELLOW, NORM, WHITE, cat, NORM, CYAN, pkgname, NORM);

	if (pretend == 100)
		return 0;

	/* Get a handle on the vdb path which we'll use everywhere else */
	xasprintf(&vdb_path, "%s/%s/%s/%s/", portroot, portvdb, cat, pkgname);
	vdb_fd = open(vdb_path, O_RDONLY | O_CLOEXEC);
	if (vdb_fd == -1) {
		warnp("unable to read %s", vdb_path);
		goto done;
	}

	/* First execute the pkg_prerm step */
	if (!pretend) {
		eat_file_at(vdb_fd, "DEFINED_PHASES", phases, sizeof(phases));
		pkg_run_func(vdb_path, phases, "pkg_prerm");
	}

	/* Now start removing all the installed files */
	fd = openat(vdb_fd, "CONTENTS", O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		warnp("unable to read %s", "CONTENTS");
		goto done;
	}
	fp = fdopen(fd, "r");
	if (fp == NULL)
		goto done;

	/* XXX: be nice to pull this out of the current func
	 *      so we don't keep reparsing the same env var
	 *      when unmerging multiple packages.
	 */
	makeargv(config_protect, &cp_argc, &cp_argv);
	makeargv(config_protect_mask, &cpm_argc, &cpm_argv);

	while (getline(&buf, &buflen, fp) != -1) {
		contents_entry *e;
		char zing[20];
		int protected = 0;
		struct stat lst;
		char dst[_Q_PATH_MAX];

		e = contents_parse_line(buf);
		if (!e) continue;
		snprintf(dst, sizeof(dst), "%s%s", portroot, e->name);
		protected = config_protected(e->name, cp_argc, cp_argv, cpm_argc, cpm_argv);
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
				break;
			case CONTENTS_OBJ:
				if (!protected)
					unlink_q(dst);
				qprintf("%s %s\n", zing, dst);
				break;
			case CONTENTS_SYM:
				if (protected)
					break;
				if (e->name[0] != '/')
					break;
				if (e->sym_target[0] != '/') {
					if (lstat(dst, &lst) != -1) {
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
				if (lstat(dst, &lst) != -1) {
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
	fd = -1;

	/* Then remove all dirs in reverse order */
	while (dirs != NULL) {
		llist_char *list = dirs;
		char *dir = list->data;
		int rm;

		rm = pretend ? -1 : rmdir(dir);
		qprintf("%s%s%s %s%s%s/\n", rm ? YELLOW : GREEN, rm ? "---" : "<<<",
			NORM, DKBLUE, dir, NORM);

		free(list->data);
		free(list);
		dirs = dirs->next;
	}

	freeargv(cp_argc, cp_argv);
	freeargv(cpm_argc, cpm_argv);

	if (!pretend) {
		char *dir;

		/* Then execute the pkg_postrm step */
		pkg_run_func(vdb_path, phases, "pkg_postrm");

		/* Finally delete the vdb entry */
		rm_rf(vdb_path);

		/* And prune any empty vdb dirs */
		dir = strrchr(vdb_path, '/');
		*dir = '\0';
		rmdir(vdb_path);
		dir = strrchr(vdb_path, '/');
		*dir = '\0';
		rmdir(vdb_path);
	}

	ret = 0;
 done:
	if (fd != -1)
		close(fd);
	if (vdb_fd != -1)
		close(vdb_fd);
	free(buf);
	free(vdb_path);

	return ret;
}

int unlink_empty(char *buf)
{
	struct stat st;
	if (stat(buf, &st) != -1)
		if (st.st_size == 0)
			return unlink(buf);
	return -1;
}

static int match_pkg(queue *ll, struct pkg_t *pkg)
{
	depend_atom *atom;
	char buf[255], buf2[255];
	int match = 0;

	snprintf(buf, sizeof(buf), "%s/%s", pkg->CATEGORY, pkg->PF);
	if ((atom = atom_explode(buf)) == NULL)
		errf("%s/%s is not a valid atom", pkg->CATEGORY, pkg->PF);

	/* verify this is the requested package */
	if (strcmp(ll->name, buf) == 0)
		match = 1;

	if (strcmp(ll->name, pkg->PF) == 0)
		match = 2;

	snprintf(buf2, sizeof(buf2), "%s/%s", pkg->CATEGORY, atom->PN);

	if (strcmp(ll->name, buf2) == 0)
		match = 3;

	if (strcmp(ll->name, atom->PN) == 0)
		match = 4;

	if (match)
		goto match_done;

	if (ll->item) {
		depend_atom *subatom = atom_explode(ll->name);
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

static void pkg_process(queue *todo, struct pkg_t *pkg)
{
	queue *ll;
	depend_atom *atom;
	char buf[255];

	snprintf(buf, sizeof(buf), "%s/%s", pkg->CATEGORY, pkg->PF);
	if ((atom = atom_explode(buf)) == NULL)
		errf("%s/%s is not a valid atom", pkg->CATEGORY, pkg->PF);

	ll = todo;
	while (ll) {
		if (ll->name[0] != '-' && match_pkg(ll, pkg)) {
			/* fetch all requested packages */
			pkg_fetch(0, atom, pkg);
		}

		ll = ll->next;
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

	if (force_download && (access(buf, R_OK) == 0) && (pkg->SHA1[0] || pkg->MD5[0])) {
		if (pkg_verify_checksums(buf, pkg, atom, 0, 0) != 0)
			unlink(buf);
	}
	if (access(buf, R_OK) == 0) {
		if (!pkg->SHA1[0] && !pkg->MD5[0]) {
			warn("No checksum data for %s", buf);
			return;
		} else {
			if (pkg_verify_checksums(buf, pkg, atom, qmerge_strict, 1) == 0) {
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
		if (symlink(str, buf))
			errp("symlink(%s, %s) failed", str, buf);
	}
	xchdir(savecwd);

	snprintf(buf, sizeof(buf), "%s/%s/%s.tbz2", pkgdir, atom->CATEGORY, pkg->PF);
	if (pkg_verify_checksums(buf, pkg, atom, qmerge_strict, 1) == 0) {
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
			const char *icolor = RED;
			ret = atom_compare_str(buf, p);
			switch (ret) {
				case EQUAL: icolor = RED;    break;
				case NEWER: icolor = YELLOW; break;
				case OLDER: icolor = BLUE;   break;
				default:    icolor = NORM;   break;
			}
			printf(" %sInstalled%s:%s %s%s%s\n", DKGREEN, YELLOW, NORM, icolor, p, NORM);
		}
	}
	atom_implode(atom);
}

static int unmerge_packages(queue *todo)
{
	depend_atom *atom;
	char *p;

	while (todo) {
		char buf[512];

		if (todo->name[0] == '-')
			goto next;

		p = best_version(NULL, todo->name);
		if (!*p)
			goto next;
		if ((atom = atom_explode(p)) == NULL)
			goto next;
		atom2str(atom, buf, sizeof(buf));
		pkg_unmerge(atom->CATEGORY, buf);
		atom_implode(atom);

 next:
		todo = todo->next;
	}

	return 0;
}

struct pkg_t *grab_binpkg_info(const char *name)
{
	FILE *fp;
	char buf[BUFSIZ];
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

	while (fgets(buf, sizeof(buf), fp) != NULL) {
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

		if ((p = strchr(buf, ':')) == NULL)
			continue;
		if ((p = strchr(buf, ' ')) == NULL)
			continue;
		*p = 0;
		++p;

		if (*buf) {
			/* we dont need all the info */
			if (strcmp(buf, "RDEPEND:") == 0)
				strncpy(pkg->RDEPEND, p, sizeof(Pkg.RDEPEND));
			if (strcmp(buf, "PF:") == 0)
				strncpy(pkg->PF, p, sizeof(Pkg.PF));
			if (strcmp(buf, "CATEGORY:") == 0)
				strncpy(pkg->CATEGORY, p, sizeof(Pkg.CATEGORY));
			if (strcmp(buf, "REPO:") == 0)
				strncpy(pkg->REPO, p, sizeof(Pkg.REPO));

			if (strcmp(buf, "CPV:") == 0) {
				if ((atom = atom_explode(p)) != NULL) {
					snprintf(buf, sizeof(buf), "%s-%s", atom->PN, atom->PV);
					if (atom->PR_int)
						snprintf(buf, sizeof(buf), "%s-%s-r%i", atom->PN, atom->PV, atom->PR_int);
					strncpy(pkg->PF, buf, sizeof(Pkg.PF));
					strncpy(pkg->CATEGORY, atom->CATEGORY, sizeof(Pkg.CATEGORY));
					atom_implode(atom);
				}
			}
			if (strcmp(buf, "SLOT:") == 0)
				strncpy(pkg->SLOT, p, sizeof(Pkg.SLOT));
			if (strcmp(buf, "USE:") == 0)
				strncpy(pkg->USE, p, sizeof(Pkg.USE));

			/* checksums. We must have 1 or the other unless --*/
			if (strcmp(buf, "MD5:") == 0)
				strncpy(pkg->MD5, p, sizeof(Pkg.MD5));
			if (strcmp(buf, "SHA1:") == 0)
				strncpy(pkg->SHA1, p, sizeof(Pkg.SHA1));
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

	while (fgets(buf, sizeof(buf), fp) != NULL) {
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

		if ((p = strchr(buf, ':')) == NULL)
			continue;
		if ((p = strchr(buf, ' ')) == NULL)
			continue;
		*p = 0;
		++p;

		if (*buf) {
			if (strcmp(buf, "CPV:") == 0) {
				depend_atom *atom;
				if ((atom = atom_explode(p)) != NULL) {
					snprintf(buf, sizeof(buf), "%s-%s", atom->PN, atom->PV);
					if (atom->PR_int)
						snprintf(buf, sizeof(buf), "%s-%s-r%i", atom->PN, atom->PV, atom->PR_int);
					strncpy(PF, buf, sizeof(PF));
					strncpy(CATEGORY, atom->CATEGORY, sizeof(CATEGORY));
					atom_implode(atom);
				}
			}
			if (strcmp(buf, "PF:") == 0)
				strncpy(PF, p, sizeof(PF));
			if (strcmp(buf, "CATEGORY:") == 0)
				strncpy(CATEGORY, p, sizeof(CATEGORY));
		}
	}
	fclose(fp);
	return best_match;
}

static int parse_packages(queue *todo)
{
	FILE *fp;
	size_t buflen;
	char *buf, *p;
	long lineno = 0;

	if ((fp = fopen(Packages, "r")) == NULL)
		errp("Unable to open package file %s", Packages);

	memset(&Pkg, 0, sizeof(Pkg));

	buf = NULL;
	while (getline(&buf, &buflen, fp) != -1) {
		lineno++;
		if (*buf == '\n') {
			if ((strlen(Pkg.PF) > 0) && (strlen(Pkg.CATEGORY) > 0)) {
				struct pkg_t *pkg = xmalloc(sizeof(*pkg));
				*pkg = Pkg;

				if (search_pkgs) {
					if (todo) {
						queue *ll = todo;
						while (ll) {
							if ((match_pkg(ll, pkg) > 0) || (strcmp(ll->name, pkg->CATEGORY) == 0))
								print_Pkg(verbose, pkg);
							ll = ll->next;
						}
					} else
						print_Pkg(verbose, pkg);
				} else
					pkg_process(todo, pkg);

				free(pkg);
			}
			memset(&Pkg, 0, sizeof(Pkg));
			continue;
		}
		if ((p = strchr(buf, '\n')) != NULL)
			*p = 0;

		if ((p = strchr(buf, ':')) == NULL)
			continue;
		if ((p = strchr(buf, ' ')) == NULL)
			continue;
		*p = 0;
		++p;

		switch (*buf) {
			case 'U':
				if (strcmp(buf, "USE:") == 0) strncpy(Pkg.USE, p, sizeof(Pkg.USE));
				break;
			case 'P':
				if (strcmp(buf, "PF:") == 0) strncpy(Pkg.PF, p, sizeof(Pkg.PF));
				break;
			case 'S':
				if (strcmp(buf, "SIZE:") == 0) Pkg.SIZE = atol(p);
				if (strcmp(buf, "SLOT:") == 0) strncpy(Pkg.SLOT, p, sizeof(Pkg.SLOT));
				if (strcmp(buf, "SHA1:") == 0) strncpy(Pkg.SHA1, p, sizeof(Pkg.SHA1));
				break;
			case 'M':
				if (strcmp(buf, "MD5:") == 0) strncpy(Pkg.MD5, p, sizeof(Pkg.MD5));
				break;
			case 'R':
				if (strcmp(buf, "REPO:") == 0) strncpy(Pkg.REPO, p, sizeof(Pkg.REPO));
				if (strcmp(buf, "RDEPEND:") == 0) strncpy(Pkg.RDEPEND, p, sizeof(Pkg.RDEPEND));
				break;
			case 'L':
				if (strcmp(buf, "LICENSE:") == 0) strncpy(Pkg.LICENSE, p, sizeof(Pkg.LICENSE));
				break;
			case 'C':
				if (strcmp(buf, "CATEGORY:") == 0) strncpy(Pkg.CATEGORY, p, sizeof(Pkg.CATEGORY));
				if (strcmp(buf, "CPV:") == 0) {
					depend_atom *atom;
					if ((atom = atom_explode(p)) != NULL) {
						if (atom->PR_int)
							snprintf(Pkg.PF, sizeof(Pkg.PF), "%s-%s-r%i", atom->PN, atom->PV, atom->PR_int);
						else
							snprintf(Pkg.PF, sizeof(Pkg.PF), "%s-%s", atom->PN, atom->PV);
						strncpy(Pkg.CATEGORY, atom->CATEGORY, sizeof(Pkg.CATEGORY));
						atom_implode(atom);
					}
				}
				break;
			case 'D':
				if (strcmp(buf, "DESC:") == 0) strncpy(Pkg.DESC, p, sizeof(Pkg.DESC));
				break;
			default:
				break;
		}
	}

	free(buf);
	fclose(fp);

	return 0;
}

static queue *qmerge_add_set_atom(char *satom, queue *set)
{
	char *p;
	const char *slot;

	if ((p = strchr(satom, ':')) != NULL) {
		*p = 0;
		slot = p + 1;
	} else
		slot = "0";

	return add_set(satom, slot, set);
}

static queue *qmerge_add_set_file(const char *file, queue *set)
{
	FILE *fp;
	size_t buflen;
	char *buf;
	const char *fname;

	/* Find the file to read */
	if (*portroot && strcmp(portroot, "/") != 0) {
		size_t len, flen;
		char *f;

		len = strlen(portroot);
		flen = strlen(file) + 1;
		fname = f = alloca(len + flen);
		memcpy(f, portroot, len);
		memcpy(f + len, file, flen);
	} else
		fname = file;

	if ((fp = fopen(fname, "r")) == NULL) {
		warnp("unable to read set file %s", fname);
		return NULL;
	}

	/* Load each entry */
	buf = NULL;
	while (getline(&buf, &buflen, fp) != -1) {
		rmspace(buf);
		set = qmerge_add_set_atom(buf, set);
	}
	free(buf);

	fclose(fp);

	return set;
}

static queue *qmerge_add_set(char *buf, queue *set)
{
	if (strcmp(buf, "world") == 0)
		return qmerge_add_set_file("/var/lib/portage/world", set);
	if (strcmp(buf, "all") == 0)
		return get_vdb_atoms(0);
	/* XXX: Should load custom sets here */
	return qmerge_add_set_atom(buf, set);
}

static int qmerge_run(queue *todo)
{
	if (uninstall)
		return unmerge_packages(todo);
	else
		return parse_packages(todo);
}

int qmerge_main(int argc, char **argv)
{
	int i, ret;
	queue *todo;

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

	/* Expand any portage sets on the command line */
	todo = NULL;
	for (i = optind; i < argc; ++i)
		todo = qmerge_add_set(argv[i], todo);

	if (!uninstall)
		qmerge_initialize();

	/* Make sure the user wants to do it */
	if (interactive) {
		int save_pretend = pretend;
		int save_verbose = verbose;
		int save_quiet = quiet;

		pretend = 100;
		verbose = 0;
		quiet = 1;
		ret = qmerge_run(todo);
		if (ret || save_pretend)
			return ret;

		if (uninstall) {
			if (!prompt("OK to unmerge these packages"))
				return 0;
		} else {
			if (!prompt("OK to merge these packages"))
				return 0;
		}

		pretend = save_pretend;
		verbose = save_verbose;
		quiet = save_quiet;
	}

	ret = qmerge_run(todo);
	free_sets(todo);
	return ret;
}

#else
DEFINE_APPLET_STUB(qmerge)
#endif
