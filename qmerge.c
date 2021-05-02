/*
 * Copyright 2005-2021 Gentoo Authors
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <stdio.h>
#include <xalloc.h>
#include <fnmatch.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include "stat-time.h"

#include "atom.h"
#include "copy_file.h"
#include "contents.h"
#include "eat_file.h"
#include "hash.h"
#include "human_readable.h"
#include "profile.h"
#include "rmspace.h"
#include "scandirat.h"
#include "set.h"
#include "tree.h"
#include "xasprintf.h"
#include "xchdir.h"
#include "xmkdir.h"
#include "xpak.h"
#include "xsystem.h"

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

#define QMERGE_FLAGS "fFsKUpuyO" COMMON_FLAGS
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
	{"debug",   no_argument, NULL, 128},
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
	"Run shell funcs with `set -x`",
	COMMON_OPTS_HELP
};
#define qmerge_usage(ret) usage(ret, QMERGE_FLAGS, qmerge_long_opts, qmerge_opts_help, NULL, lookup_applet_idx("qmerge"))

char search_pkgs = 0;
char interactive = 1;
char install = 0;
char uninstall = 0;
char force_download = 0;
char follow_rdepends = 1;
char qmerge_strict = 0;
char update_only = 0;
bool debug = false;
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
};

struct llist_char_t {
	char *data;
	struct llist_char_t *next;
};

typedef struct llist_char_t llist_char;

static void pkg_fetch(int, const depend_atom *, const struct pkg_t *);
static void pkg_merge(int, const depend_atom *, const struct pkg_t *);
static int pkg_unmerge(tree_pkg_ctx *, set *, int, char **, int, char **);
static struct pkg_t *grab_binpkg_info(depend_atom *);

static bool
prompt(const char *p)
{
	printf("%s? [Y/n] ", p);
	fflush(stdout);
	switch (getc(stdin)) {
		case '\n':
		case 'y':
		case 'Y':
			return true;
		default:
			return false;
	}
}

static void
fetch(const char *destdir, const char *src)
{
	if (!binhost[0])
		return;

	fflush(stdout);
	fflush(stderr);

#if 0
	if (getenv("FETCHCOMMAND") != NULL) {
		char buf[BUFSIZ];
		snprintf(buf, sizeof(buf), "(export DISTDIR='%s' URI='%s/%s'; %s)",
			destdir, binhost, src, getenv("FETCHCOMMAND"));
		xsystem(buf);
	} else
#endif
	{
		pid_t p;
		int status;
		char *path;

		xasprintf(&path, "%s/%s", binhost, src);

		char prog[] = "wget";
		char argv_c[] = "-c";
		char argv_P[] = "-P";
		char argv_q[] = "-q";
		char *argv_dir = xstrdup(destdir);
		char *argv[] = {
			prog,
			argv_c,
			argv_P,
			argv_dir,
			path,
			quiet ? argv_q : NULL,
			NULL,
		};
		if (!(force_download || install) && pretend)
			strcpy(prog, "echo");

		p = vfork();
		switch (p) {
		case 0:
			_exit(execvp(prog, argv));
		case -1:
			errp("vfork failed");
		}

		free(path);
		free(argv_dir);

		waitpid(p, &status, 0);
#if 0
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			return;
#endif
	}

	fflush(stdout);
	fflush(stderr);
}

static void
qmerge_initialize(void)
{
	char *buf;

	if (strlen(BUSYBOX))
		if (access(BUSYBOX, X_OK) != 0)
			err(BUSYBOX " must be installed");

	if (access("/bin/sh", X_OK) != 0)
		err("/bin/sh must be installed");

	if (pkgdir[0] != '/')
		errf("PKGDIR='%s' does not appear to be valid", pkgdir);

	if (!search_pkgs && !pretend) {
		if (mkdir_p(pkgdir, 0755))
			errp("could not setup PKGDIR: %s", pkgdir);
	}

	xasprintf(&buf, "%s/portage/", port_tmpdir);
	mkdir_p(buf, 0755);
	xchdir(buf);

	if (force_download == 1 /* -f: fetch */) {
		unlink(Packages);
		fetch(buf, Packages);
	}

	free(buf);
}

static char _best_version_retbuf[4096];
static int
qmerge_best_version_cb(tree_pkg_ctx *pkg_ctx, void *priv)
{
	depend_atom *sa = priv;
	depend_atom *a = tree_get_atom(pkg_ctx, true);  /* need SLOT */
	if (atom_compare(a, sa) == EQUAL)
		snprintf(_best_version_retbuf, sizeof(_best_version_retbuf),
				"%s/%s:%s", a->CATEGORY, a->PF, a->SLOT);
	return 0;
}

static char *
best_version(const char *catname, const char *pkgname, const char *slot)
{
	static int vdb_check = 1;
	tree_ctx *vdb;

	/* Make sure these dirs exist before we try walking them */
	switch (vdb_check) {
	case 1: {
		int fd = open(portroot, O_RDONLY|O_CLOEXEC|O_PATH);
		if (fd >= 0) {
			/* skip leading slash */
			vdb_check = faccessat(fd, portvdb + 1, X_OK, 0);
			close(fd);
		} else
			vdb_check = -1;
	}
		if (vdb_check)
	case -1:
			goto done;
	}

	snprintf(_best_version_retbuf, sizeof(_best_version_retbuf),
			"%s%s%s:%s", catname ? : "", catname ? "/" : "", pkgname, slot);
	vdb = tree_open_vdb(portroot, portvdb);
	if (vdb != NULL) {
		depend_atom *sa = atom_explode(_best_version_retbuf);
		tree_foreach_pkg_fast(vdb, qmerge_best_version_cb, sa, sa);
		tree_close(vdb);
		atom_implode(sa);
	}

 done:
	return _best_version_retbuf;
}

static int
config_protected(const char *buf, int cp_argc, char **cp_argv,
                 int cpm_argc, char **cpm_argv)
{
	int i;

	/* Check CONFIG_PROTECT_MASK */
	for (i = 1; i < cpm_argc; ++i)
		if (strncmp(cpm_argv[i], buf, strlen(cpm_argv[i])) == 0)
			return 0;

	/* Check CONFIG_PROTECT */
	for (i = 1; i < cp_argc; ++i)
		if (strncmp(cp_argv[i], buf, strlen(cp_argv[i])) == 0)
			return 1;

	/* this would probably be bad */
	if (strcmp(CONFIG_EPREFIX "bin/sh", buf) == 0)
		return 1;

	return 0;
}

static void
crossmount_rm(const char *fname, const struct stat * const st,
		int fd, char *qpth)
{
	struct stat lst;

	if (fstatat(fd, fname, &lst, AT_SYMLINK_NOFOLLOW) == -1)
		return;
	if (lst.st_dev != st->st_dev) {
		warn("skipping crossmount install masking: %s", fname);
		return;
	}
	qprintf("%s<<<%s %s/%s (INSTALL_MASK)\n", YELLOW, NORM, qpth, fname);
	rm_rf_at(fd, fname);
}

static int
q_merge_filter_self_parent(const struct dirent *de)
{
	if (de->d_name[0] == '.' && (de->d_name[1] == '\0' ||
			 (de->d_name[1] == '.' && de->d_name[2] == '\0')))
		return 0;

	return 1;
}

enum inc_exc { INCLUDE = 1, EXCLUDE = 2 };

static void
install_mask_check_dir(
		char ***maskv,
		int maskc,
		const struct stat * const st,
		int fd,
		ssize_t level,
		enum inc_exc parent_mode,
		char *qpth)
{
	struct dirent **files;
	int cnt;
	int i;
	int j;
	enum inc_exc mode;
	enum inc_exc child_mode;
#ifndef DT_DIR
	struct stat s;
#endif
	char *npth = qpth + strlen(qpth);

	cnt = scandirat(fd, ".", &files, q_merge_filter_self_parent, alphasort);
	for (j = 0; j < cnt; j++) {
		mode = child_mode = parent_mode;
		for (i = 0; i < maskc; i++) {
			if ((ssize_t)maskv[i][0] < 0) {
				/* relative matches need to be a "file", as the Portage
				 * implementation suggests, so that's easy for us here,
				 * since we can just match it against each component in
				 * the path */
				if ((ssize_t)maskv[i][0] < -1)
					continue;  /* this is unsupported, so skip it */
				/* this also works if maskv happens to be a glob */
				if (fnmatch(maskv[i][1], files[j]->d_name, FNM_PERIOD) != 0)
					continue;
				mode = child_mode = maskv[i][2] ? INCLUDE : EXCLUDE;
			} else if ((ssize_t)maskv[i][0] < level) {
				/* either this is a mask that didn't match, or it
				 * matched, but a negative match exists for a deeper
				 * level, parent_mode should reflect this */
				continue;
			} else {
				if (fnmatch(maskv[i][level], files[j]->d_name, FNM_PERIOD) != 0)
					continue;

				if ((ssize_t)maskv[i][0] == level)  /* full mask match */
					mode = child_mode =
						(ssize_t)maskv[i][level + 1] ? INCLUDE : EXCLUDE;
				else if (maskv[i][(ssize_t)maskv[i][0] + 1])
					/* partial include mask */
					mode = INCLUDE;
			}
		}

		DBG("%s/%s: %s/%s", qpth, files[j]->d_name,
				mode == EXCLUDE       ? "EXCLUDE" : "INCLUDE",
				child_mode == EXCLUDE ? "EXCLUDE" : "INCLUDE");
		if (mode == EXCLUDE) {
			crossmount_rm(files[j]->d_name, st, fd, qpth);
			continue;
		}

#ifdef DT_DIR
		if (files[j]->d_type == DT_DIR) {
#else
		if (fstatat(fd, files[j]->d_name, &s, AT_SYMLINK_NOFOLLOW) != 0)
			continue;
		if (S_ISDIR(s.st_mode)) {
#endif
			int subfd = openat(fd, files[j]->d_name, O_RDONLY);
			if (subfd < 0)
				continue;
			snprintf(npth, _Q_PATH_MAX - (npth - qpth),
					"/%s", files[j]->d_name);
			install_mask_check_dir(maskv, maskc, st, subfd,
					level + 1, child_mode, qpth);
			close(subfd);
			*npth = '\0';
		}
	}
	scandir_free(files, cnt);
}

static void
install_mask_pwd(int iargc, char **iargv, const struct stat * const st, int fd)
{
	char *p;
	int i;
	size_t cnt;
	size_t maxdirs;
	char **masks;
	size_t masksc;
	char ***masksv;
	char qpth[_Q_PATH_MAX];

	/* we have to deal with "negative" masks, see
	 * https://archives.gentoo.org/gentoo-portage-dev/message/29e128a9f41122fa0420c1140f7b7f94
	 * which means we'll need to see what thing matches last
	 * (inclusion or exclusion) for *every* file :( */

   /*
	example package contents:
	/e/t1
    /u/b/t1
	/u/b/t2
	/u/l/lt1
	/u/s/d/t1
	/u/s/m/m1/t1
	/u/s/m/m5/t2

	masking rules:     array encoding:
	 /u/s              2 u s 0          relative=0 include=0
	-/u/s/m/m1         4 u s m m1 1     relative=0 include=1
	 e                -1 e 0            relative=1 include=0

	should result in:
	/u/b/t1
	/u/b/t2
	/u/l/lt1
	/u/s/m/m1/t1
	strategy:
	- for each dir level
	  - find if there is a match on that level in rules
	  - if the last match is the full mask
	    - if the mask is negated, do not remove entry
	    - else, remove entry
	  - if the last match is negated, partial and a full mask matched before
	    - do not remove entry
	practice:
	/e | matches "e" -> remove
	/u | matches partial last negated -> continue
	  /b | doesn't match -> leave subtree
	  /l | doesn't match -> leave subtree
	  /s | match, partial last negated match -> remember match, continue
	    /d | doesn't match -> remembered match, remove subtree
		/m | partial match negated -> continue
		  /m1 | match negated -> leave subtree
		  /m5 | doesn't match -> remembered match, remove subtree
	*/

	/* find the longest path so we can allocate a matrix */
	maxdirs = 0;
	for (i = 1; i < iargc; i++) {
		char lastc = '/';

		cnt = 1; /* we always have "something", right? */
		p = iargv[i];
		if (*p == '-')
			p++;
		for (; *p != '\0'; p++) {
			/* eliminate duplicate /-es, also ignore the leading / in
			 * the count */
			if (*p == '/' && *p != lastc)
				cnt++;
			lastc = *p;
		}
		if (cnt > maxdirs)
			maxdirs = cnt;
	}
	maxdirs += 2;  /* allocate plus relative and include elements */

	/* allocate and populate matrix */
	masksc = iargc - 1;
	masks = xmalloc(sizeof(char *) * (maxdirs * masksc));
	masksv = xmalloc(sizeof(char **) * (masksc));
	for (i = 1; i < iargc; i++) {
		masksv[i - 1] = &masks[(i - 1) * maxdirs];
		p = iargv[i];
		cnt = 1;  /* start after count */
		/* ignore include marker */
		if (*p == '-')
			p++;
		/* strip of leading slash(es) */
		while (*p == '/')
			p++;
		masks[((i - 1) * maxdirs) + cnt] = p;
		for (; *p != '\0'; p++) {
			if (*p == '/') {
				/* fold duplicate slashes */
				do {
					*p++ = '\0';
				} while (*p == '/');
				cnt++;
				masks[((i - 1) * maxdirs) + cnt] = p;
			}
		}
		/* brute force cast below values, a pointer basically is size_t,
		 * which is large enough to store what we need here */
		p = iargv[i];
		/* set include bit */
		if (*p == '-') {
			masks[((i - 1) * maxdirs) + cnt + 1] = (char *)1;
			p++;
		} else {
			masks[((i - 1) * maxdirs) + cnt + 1] = (char *)0;
		}
		/* set count */
		masks[((i - 1) * maxdirs) + 0] =
			(char *)((*p == '/' ? 1 : -1) * cnt);
	}

#if EBUG
	fprintf(warnout, "applying install masks:\n");
	for (cnt = 0; cnt < masksc; cnt++) {
		ssize_t plen = (ssize_t)masksv[cnt][0];
		fprintf(warnout, "%3zd  ", plen);
		if (plen < 0)
			plen = -plen;
		for (i = 1; i <= plen; i++)
			fprintf(warnout, "%s ", masksv[cnt][i]);
		fprintf(warnout, " %zd\n", (size_t)masksv[cnt][i]);
	}
#endif

	cnt = snprintf(qpth, _Q_PATH_MAX, "%s", CONFIG_EPREFIX);
	cnt--;
	if (qpth[cnt] == '/')
		qpth[cnt] = '\0';

	install_mask_check_dir(masksv, masksc, st, fd, 1, INCLUDE, qpth);

	free(masks);
	free(masksv);
}

static char
qprint_tree_node(int level, const depend_atom *atom, const struct pkg_t *pkg)
{
	char buf[1024];
	char *p;
	int i, ret;

	char install_ver[126] = "";
	char c = 'N';
	const char *color;

	if (!pretend)
		return 0;

	p = best_version(pkg->CATEGORY, atom->PN, pkg->SLOT);

	if (strlen(p) < 1) {
		c = 'N';
		snprintf(buf, sizeof(buf), "%sN%s", GREEN, NORM);
	} else {
		depend_atom *subatom = atom_explode(p);
		if (subatom != NULL) {
			ret = atom_compare(subatom, atom);
			switch (ret) {
				case EQUAL: c = 'R'; break;
				case NEWER: c = 'U'; break;
				case OLDER: c = 'D'; break;
				default: c = '?'; break;
			}
			snprintf(install_ver, sizeof(install_ver), "[%s%.*s%s] ",
					DKBLUE,
					(int)(sizeof(install_ver) - 4 -
						sizeof(DKBLUE) - sizeof(NORM)),
					subatom->P, NORM);
			atom_implode(subatom);
		}
		if (update_only && c != 'U')
			return c;
		if ((c == 'R' || c == 'D') && update_only && level)
			return c;
		switch (c) {
		case 'R': color = YELLOW; break;
		case 'U': color = BLUE; break;
		case 'D': color = DKBLUE; break;
		default: color = RED; break;
		}
		snprintf(buf, sizeof(buf), "%s%c%s", color, c, NORM);
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

static void
pkg_run_func_at(int dirfd, const char *vdb_path, const char *phases, const char *func, const char *D, const char *T)
{
	const char *phase;
	char *script;

	/* This assumes no func is a substring of another func.
	 * Today, that assumption is valid for all funcs ...
	 * The phases are the func with the "pkg_" chopped off. */
	phase = func + 4;
	if (strstr(phases, phase) == NULL) {
		qprintf("--- %s\n", func);
		return;
	}

	qprintf(">>> %s\n", func);

	xasprintf(&script,
		/* Provide funcs required by the PMS */
		"EBUILD_PHASE=%3$s\n"
		"debug-print() { :; }\n"
		"debug-print-function() { :; }\n"
		"debug-print-section() { :; }\n"
		/* Not quite right */
		"has_version() { [ -n \"$(qlist -ICqe \"$1\")\" ]; }\n"
		"best_version() { qlist -ICqev \"$1\"; }\n"
		"use() { useq \"$@\"; }\n"
		"usex() { useq \"$1\" && echo \"${2-yes}$4\" || echo \"${3-no}$5\"; }\n"
		"useq() { hasq \"$1\" ${USE}; }\n"
		"usev() { hasv \"$1\" ${USE}; }\n"
		"has() { hasq \"$@\"; }\n"
		"hasq() { local h=$1; shift; case \" $* \" in *\" $h \"*) return 0;; *) return 1;; esac; }\n"
		"hasv() { hasq \"$@\" && echo \"$1\"; }\n"
		"elog() { printf ' * %%b\\n' \"$*\"; }\n"
		"einfon() { printf ' * %%b' \"$*\"; }\n"
		"einfo() { elog \"$@\"; }\n"
		"ewarn() { elog \"$@\"; }\n"
		"eqawarn() { elog \"QA: \"\"$@\"; }\n"
		"eerror() { elog \"$@\"; }\n"
		"die() { eerror \"$@\"; exit 1; }\n"
		"fowners() { local f a=$1; shift; for f in \"$@\"; do chown $a \"${ED}/${f}\"; done; }\n"
		"fperms() { local f a=$1; shift; for f in \"$@\"; do chmod $a \"${ED}/${f}\"; done; }\n"
		/* TODO: This should suppress `die` */
		"nonfatal() { \"$@\"; }\n"
		"ebegin() { printf ' * %%b ...' \"$*\"; }\n"
		"eend() { local r=${1:-$?}; [ $# -gt 0 ] && shift; [ $r -eq 0 ] && echo ' [ ok ]' || echo \" $* \"'[ !! ]'; return $r; }\n"
		"dodir() { mkdir -p \"$@\"; }\n"
		"keepdir() { dodir \"$@\" && touch \"$@\"/.keep_${CATEGORY}_${PN}-${SLOT%%/*}; }\n"
		/* TODO: This should be fatal upon error */
		"emake() { ${MAKE:-make} ${MAKEOPTS} \"$@\"; }\n"
		/* Unpack the env if need be */
		"[ -e '%1$s/environment' ] || { bzip2 -dc '%1$s/environment.bz2' > '%1$s/environment' || exit 1; }\n"
		/* Load the main env */
		". '%1$s/environment'\n"
		/* Reload env vars that matter to us */
		"FILESDIR=/.does/not/exist/anywhere\n"
		"MERGE_TYPE=binary\n"
		"ROOT='%4$s'\n"
		"EROOT=\"/${ROOT#/}/${EPREFIX%%/}/\"\n"
		"D=\"%5$s\"\n"
		"ED=\"${D%%/}/${EPREFIX%%/}/\"\n"
		"T=\"%6$s\"\n"
		/* we do not support preserve-libs yet, so force
		 * preserve_old_lib instead */
		"FEATURES=\"${FEATURES/preserve-libs/disabled}\"\n"
		/* Finally run the func */
		"%7$s%2$s\n"
		/* Ignore func return values (not exit values) */
		":",
		/*1*/ vdb_path,
		/*2*/ func,
		/*3*/ phase,
		/*4*/ portroot,
		/*5*/ D,
		/*6*/ T,
		/*7*/ debug ? "set -x;" : "");
	xsystembash(script, dirfd);
	free(script);
}
#define pkg_run_func(...) pkg_run_func_at(AT_FDCWD, __VA_ARGS__)

/* Copy one tree (the single package) to another tree (ROOT) */
static int
merge_tree_at(int fd_src, const char *src, int fd_dst, const char *dst,
              FILE *contents, size_t eprefix_len, set **objs, char **cpathp,
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
	/* Cannot use O_PATH as we want to use fdopendir() */
	subfd_src = openat(fd_src, src, O_RDONLY|O_CLOEXEC);
	if (subfd_src < 0)
		return ret;
	subfd_dst = openat(fd_dst, dst, O_RDONLY|O_CLOEXEC|O_PATH);
	if (subfd_dst < 0) {
		close(subfd_src);
		return ret;
	}

	i = dup(subfd_src);  /* fdopendir closes its argument */
	dir = fdopendir(i);
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
			cpath = *cpathp = xrealloc(*cpathp, clen + 1 + nlen + 1);
			mnlen = nlen;
		}
		strcpy(cpath + clen + 1, name);

		/* Find out what the source path is */
		if (fstatat(subfd_src, name, &st, AT_SYMLINK_NOFOLLOW)) {
			warnp("could not read %s", cpath);
			continue;
		}

		/* Migrate a directory */
		if (S_ISDIR(st.st_mode)) {
			if (!pretend && mkdirat(subfd_dst, name, st.st_mode)) {
				if (errno != EEXIST) {
					warnp("could not create %s", cpath);
					continue;
				}

				/* XXX: update times of dir ? */
			}

			/* syntax: dir dirname */
			if (!pretend)
				fprintf(contents, "dir %s\n", cpath);
			*objs = add_set(cpath, *objs);
			qprintf("%s>>>%s %s%s%s/\n", GREEN, NORM, DKBLUE, cpath, NORM);

			/* Copy all of these contents */
			merge_tree_at(subfd_src, name,
					subfd_dst, name, contents, eprefix_len,
					objs, cpathp, cp_argc, cp_argv, cpm_argc, cpm_argv);
			cpath = *cpathp;
			mnlen = 0;

			/* In case we didn't install anything, prune the empty dir */
			if (!pretend)
				unlinkat(subfd_dst, name, AT_REMOVEDIR);
		} else if (S_ISREG(st.st_mode)) {
			/* Migrate a file */
			struct timespec times[2];
			int fd_srcf, fd_dstf;
			char *hash;
			const char *tmpname, *dname;
			char buf[_Q_PATH_MAX * 2];
			struct stat ignore;

			/* syntax: obj filename hash mtime */
			hash = hash_file_at(subfd_src, name, HASH_MD5);
			if (!pretend)
				fprintf(contents, "obj %s %s %zu""\n",
						cpath, hash, (size_t)st.st_mtime);

			/* Check CONFIG_PROTECT */
			if (config_protected(cpath + eprefix_len,
						cp_argc, cp_argv, cpm_argc, cpm_argv) &&
					fstatat(subfd_dst, name, &ignore, AT_SYMLINK_NOFOLLOW) == 0)
			{
				/* ._cfg####_ */
				char *num;
				dname = buf;
				snprintf(buf, sizeof(buf), "._cfg####_%s", name);
				num = buf + 5;
				for (i = 0; i < 10000; ++i) {
					sprintf(num, "%04i", i);
					num[4] = '_';
					if (fstatat(subfd_dst, dname, &ignore, AT_SYMLINK_NOFOLLOW))
						break;
				}
				qprintf("%s>>>%s %s (%s)\n", GREEN, NORM, cpath, dname);
			} else {
				dname = name;
				qprintf("%s>>>%s %s\n", GREEN, NORM, cpath);
			}
			*objs = add_set(cpath, *objs);

			if (pretend)
				continue;

			/* First try fast path -- src/dst are same device */
			if (renameat(subfd_src, dname, subfd_dst, name) == 0)
				continue;

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
			fd_dstf = openat(subfd_dst, tmpname,
					O_WRONLY|O_CLOEXEC|O_CREAT|O_TRUNC, st.st_mode);
			if (fd_dstf < 0) {
				warnp("could not write %s", cpath);
				close(fd_srcf);
				continue;
			}

			/* Make sure owner/mode is sane before we write out data */
			if (fchown(fd_dstf, st.st_uid, st.st_gid)) {
				warnp("could not set ownership (%zu/%zu) for %s",
						(size_t)st.st_uid, (size_t)st.st_gid, cpath);
				continue;
			}
			if (fchmod(fd_dstf, st.st_mode)) {
				warnp("could not set permission (%u) for %s",
						(int)st.st_mode, cpath);
				continue;
			}

			/* Do the actual data copy */
			if (copy_file_fd(fd_srcf, fd_dstf)) {
				warnp("could not write %s", cpath);
				if (ftruncate(fd_dstf, 0)) {
					/* don't care */;
				}
				close(fd_srcf);
				close(fd_dstf);
				continue;
			}

			/* Preserve the file times */
			times[0] = get_stat_mtime(&st);
			times[1] = get_stat_mtime(&st);
			futimens(fd_dstf, times);

			close(fd_srcf);
			close(fd_dstf);

			/* Move the new tmp dst file to the right place */
			if (renameat(subfd_dst, tmpname, subfd_dst, dname)) {
				warnp("could not rename %s to %s", tmpname, cpath);
				continue;
			}
		} else if (S_ISLNK(st.st_mode)) {
			/* Migrate a symlink */
			size_t len = st.st_size;
			char sym[_Q_PATH_MAX];

			/* Find out what we're pointing to */
			if (readlinkat(subfd_src, name, sym, sizeof(sym)) == -1) {
				warnp("could not read link %s", cpath);
				continue;
			}
			sym[len < _Q_PATH_MAX ? len : _Q_PATH_MAX - 1] = '\0';

			/* syntax: sym src -> dst mtime */
			if (!pretend)
				fprintf(contents, "sym %s -> %s %zu\n",
						cpath, sym, (size_t)st.st_mtime);
			qprintf("%s>>>%s %s%s -> %s%s\n", GREEN, NORM,
					CYAN, cpath, sym, NORM);
			*objs = add_set(cpath, *objs);

			if (pretend)
				continue;

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
			times[0] = get_stat_mtime(&st);
			times[1] = get_stat_mtime(&st);
			utimensat(subfd_dst, name, times, AT_SYMLINK_NOFOLLOW);
		} else {
			/* WTF is this !? */
			warnp("unknown file type %s", cpath);
			continue;
		}
	}

	closedir(dir);
	ret = 0;

 done:
	close(subfd_src);
	close(subfd_dst);

	return ret;
}

static void
pkg_extract_xpak_cb(
	void *ctx,
	char *pathname,
	int pathname_len,
	int data_offset,
	int data_len,
	char *data)
{
	FILE *out;
	int *destdirfd = (int *)ctx;
	(void)pathname_len;

	int fd = openat(*destdirfd, pathname,
			O_WRONLY | O_CLOEXEC | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return;
	out = fdopen(fd, "w");
	if (!out)
		return;

	fwrite(data + data_offset, 1, data_len, out);

	fclose(out);
}

/* oh shit getting into pkg mgt here. FIXME: write a real dep resolver. */
static void
pkg_merge(int level, const depend_atom *atom, const struct pkg_t *pkg)
{
	set *objs;
	tree_ctx *vdb;
	tree_cat_ctx *cat_ctx;
	FILE *fp, *contents;
	static char *phases;
	static size_t phases_len;
	char *eprefix = NULL;
	size_t eprefix_len = 0;
	char buf[1024];
	char *tbz2, *p, *D, *T;
	int i;
	char **ARGV;
	int ARGC;
	struct stat st;
	char **iargv;
	char c;
	int iargc;
	const char *compr;
	int cp_argc;
	int cpm_argc;
	char **cp_argv;
	char **cpm_argv;
	int tbz2size;

	if (!install || !pkg || !atom)
		return;

	if (!pkg->PF[0] || !pkg->CATEGORY[0]) {
		if (verbose) warn("CPF is really NULL at level %d", level);
		return;
	}

	c = qprint_tree_node(level, atom, pkg);

	if (0)
		if (((c == 'R') || (c == 'D')) && update_only)
			return;

	if (pkg->RDEPEND[0] != '\0' && follow_rdepends) {
		const char *rdepend;

		IF_DEBUG(fprintf(stderr, "\n+Parent: %s/%s\n", pkg->CATEGORY, pkg->PF));
		IF_DEBUG(fprintf(stderr, "+Depstring: %s\n", pkg->RDEPEND));

		/* <hack> */
		if (strncmp(pkg->RDEPEND, "|| ", 3) == 0) {
			if (verbose)
				qfprintf(stderr, "fix this rdepend hack %s\n", pkg->RDEPEND);
			rdepend = "";
		} else
			rdepend = pkg->RDEPEND;
		/* </hack> */

		makeargv(rdepend, &ARGC, &ARGV);
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
				case '\0':
					break;
				default:
					if ((subatom = atom_explode(name)) != NULL) {
						struct pkg_t *subpkg;

						subpkg = grab_binpkg_info(subatom); /* free me later */
						if (subpkg == NULL) {
							warn("Cannot find a binpkg for %s from rdepend(%s)",
									name, pkg->RDEPEND);
							atom_implode(subatom);
							continue;
						}

						assert(subpkg != NULL);
						IF_DEBUG(fprintf(stderr, "+Subpkg: %s/%s\n",
									subpkg->CATEGORY, subpkg->PF));

						/* look at installed versions now.
						 * If NULL or < merge this pkg */
						snprintf(buf, sizeof(buf), "%s/%s",
								subpkg->CATEGORY, subpkg->PF);

						ratom = atom_explode(buf);

						p = best_version(subpkg->CATEGORY,
								subpkg->PF, subpkg->SLOT);

						/* we dont want to remerge equal versions here */
						IF_DEBUG(fprintf(stderr, "+Installed: %s\n", p));
						if (strlen(p) < 1)
							if (!((strcmp(pkg->PF, subpkg->PF) == 0) &&
										(strcmp(pkg->CATEGORY,
												subpkg->CATEGORY) == 0)))
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

	/* Get a handle on the main vdb repo */
	vdb = tree_open_vdb(portroot, portvdb);
	if (!vdb)
		return;
	cat_ctx = tree_open_cat(vdb, pkg->CATEGORY);
	if (!cat_ctx) {
		if (errno != ENOENT) {
			tree_close(vdb);
			return;
		}
		mkdirat(vdb->tree_fd, pkg->CATEGORY, 0755);
		cat_ctx = tree_open_cat(vdb, pkg->CATEGORY);
		if (!cat_ctx) {
			tree_close(vdb);
			return;
		}
	}

	/* Set up our temp dir to unpack this stuff */
	xasprintf(&p, "%s/qmerge/%s/%s", port_tmpdir, pkg->CATEGORY, pkg->PF);
	mkdir_p(p, 0755);
	xchdir(p);
	xasprintf(&D, "%s/image", p);
	xasprintf(&T, "%s/temp", p);
	free(p);

	/* Doesn't actually remove $PWD, just everything under it */
	rm_rf(".");

	mkdir("temp", 0755);
	mkdir_p(portroot, 0755);

	xasprintf(&tbz2, "%s/%s/%s.tbz2", pkgdir, pkg->CATEGORY, pkg->PF);
	tbz2size = 0;

	mkdir("vdb", 0755);
	{
		int vdbfd = open("vdb", O_RDONLY);
		if (vdbfd == -1)
			err("failed to open vdb extraction directory");
		tbz2size = xpak_extract(tbz2, &vdbfd, pkg_extract_xpak_cb);
	}
	if (tbz2size <= 0)
		err("%s appears not to be a valid tbz2 file", tbz2);

	/* figure out if the data is compressed differently from what the
	 * name suggests, bug #660508, usage of BINPKG_COMPRESS,
	 * due to the minimal nature of where we run, we cannot rely on file
	 * or GNU tar, so have to do some laymans MAGIC hunting ourselves */
	{
		/* bz2: 3-byte: 'B' 'Z' 'h'              at byte 0
		 * gz:  2-byte:  1f  8b                  at byte 0
		 * xz:  4-byte: '7' 'z' 'X' 'Z'          at byte 1
		 * tar: 6-byte: 'u' 's' 't' 'a' 'r' \0   at byte 257
		 * lz4: 4-byte:   4  22  4d  18          at byte 0
		 * zst: 4-byte: 22-28 b5 2f  fd          at byte 0
		 * lz:  4-byte: 'L' 'Z' 'I' 'P'          at byte 0
		 * lzo: 9-byte:  89 'L' 'Z' 'O' 0 d a 1a a at byte 0
		 * br:  anything else */
		unsigned char magic[257+6];
		FILE *mfd;

		compr = "brotli -dc"; /* default: brotli; has no magic header */
		mfd = fopen(tbz2, "r");
		if (mfd != NULL) {
			size_t mlen = fread(magic, 1, sizeof(magic), mfd);
			fclose(mfd);

			if (mlen >= 3 && magic[0] == 'B' && magic[1] == 'Z' &&
					magic[2] == 'h')
			{
				compr = "bzip2 -dc";
			} else if (mlen >= 2 &&
					magic[0] == 037 && magic[1] == 0213)
			{
				compr = "gzip -dc";
			} else if (mlen >= 5 &&
					magic[1] == '7' && magic[2] == 'z' &&
					magic[3] == 'X' && magic[4] == 'Z')
			{
				compr = "xz -dc";
			} else if (mlen == 257+6 &&
					magic[257] == 'u' && magic[258] == 's' &&
					magic[259] == 't' && magic[260] == 'a' &&
					magic[261] == 'r' &&
					(magic[262] == '\0' || magic[262] == ' '))
			{
				compr = "";
			} else if (mlen >= 4 &&
					magic[0] == 0x04 && magic[1] == 0x22 &&
					magic[2] == 0x4D && magic[3] == 0x18)
			{
				compr = "lz4 -dc";
			} else if (mlen >= 4 &&
					magic[0] >= 0x22 && magic[0] <= 0x28 &&
					magic[1] == 0xB5 && magic[2] == 0x2F &&
					magic[3] == 0xFD)
			{
				/*
				 * --long=31 is needed to uncompress files compressed with
				 * --long=xx where xx>27. The option is "safe" in the sense
				 * that not more memory is allocated than what is really
				 * needed to decompress the file. See
				 * https://bugs.gentoo.org/show_bug.cgi?id=634980,
				 * however, on 32-bits arches this yields an parameter
				 * out of bound error:
				 * https://bugs.gentoo.org/show_bug.cgi?id=710444
				 * https://bugs.gentoo.org/show_bug.cgi?id=754102
				 * so only do this on 64-bits systems */
#if SIZEOF_SIZE_T >= 8
				compr = "zstd --long=31 -dc";
#else
				compr = "zstd -dc";
#endif
				/* If really tar -I would be used we would have to quote:
				 * compr = "I \"zstd --long=31\"";
				 * But actually we use a pipe (see below) */
			} else if (mlen >= 4 &&
					magic[0] == 'L' && magic[1] == 'Z' &&
					magic[2] == 'I' && magic[3] == 'P')
			{
				compr = "lzip -dc";
			} else if (mlen >= 9 &&
					magic[0] == 0x89 && magic[1] == 'L' &&
					magic[2] == 'Z' && magic[3] == 'O' &&
					magic[4] == 0x00 && magic[5] == 0x0D &&
					magic[6] == 0x0A && magic[7] == 0x1A &&
					magic[8] == 0x0A)
			{
				compr = "lzop -dc";
			}
		}
	}

	/* extract the binary package data */
	mkdir("image", 0755);
	/* busybox's tar has no -I option. Thus, although we possibly
	 * use busybox's shell and tar, we thus pipe, expecting the
	 * corresponding (de)compression tool to be in PATH; if not,
	 * a failure will occur.
	 * Since some tools (e.g. zstd) complain about the .bz2
	 * extension, we feed the tool by input redirection. */
	snprintf(buf, sizeof(buf),
		BUSYBOX " sh -c '%s%star -x%sf - -C image/'",
		compr, compr[0] == '\0' ? "" : " | ",
		((verbose > 1) ? "v" : ""));

	/* start the tar pipe and copy tbz2size binpkg bytes into it
	 * "manually" rather than depending on dd or head */
	{
		FILE *tarpipe;
		FILE *tbz2f;
		unsigned char iobuf[8192];
		int piped = 0;
		int err;
		size_t n;
		size_t rd;
		size_t wr;

		if ((tarpipe = popen(buf, "w")) == NULL)
			errp("failed to start %s", buf);

		if ((tbz2f = fopen(tbz2, "r")) == NULL)
			errp("failed to open %s for reading", tbz2);

		for (piped = wr = 0; piped < tbz2size; piped += wr) {
			n = MIN(tbz2size - piped, (ssize_t)sizeof iobuf);
			rd = fread(iobuf, 1, n, tbz2f);
			if (0 == rd) {
				if ((err = ferror(tbz2f)) != 0)
					err("reading %s failed: %s", tbz2, strerror(err));

				if (feof(tbz2f))
					err("unexpected EOF in %s: corrupted binpkg", tbz2);
			}

			for (wr = n = 0; wr < rd; wr += n) {
				n = fwrite(iobuf + wr, 1, rd - wr, tarpipe);
				if (n != rd - wr) {
					if ((err = ferror(tarpipe)) != 0)
						err("failed to unpack binpkg: %s", strerror(err));

					if (feof(tarpipe))
						err("unexpected EOF trying to unpack binpkg");
				}
			}
		}

		fclose(tbz2f);

		err = pclose(tarpipe);
		if (err > 0)
			err("finishing unpack binpkg exited with status %d", err);
		else if (err < 0)
			errp("finishing unpack binpkg unsuccessful");
	}

	free(tbz2);
	fflush(stdout);

	eat_file("vdb/DEFINED_PHASES", &phases, &phases_len);
	pkg_run_func("vdb", phases, "pkg_pretend", D, T);
	pkg_run_func("vdb", phases, "pkg_setup", D, T);
	pkg_run_func("vdb", phases, "pkg_preinst", D, T);

	if (!eat_file("vdb/EPREFIX", &eprefix, &eprefix_len))
		eprefix_len = 0;

	{
		int imagefd = open("image", O_RDONLY);
		size_t masklen = strlen(install_mask) + 1 +
				15 + 1 + 14 + 1 + 14 + 1 + 1;  /* worst case scenario */
		char *imask = xmalloc(masklen);
		size_t maskp;

		if (fstat(imagefd, &st) == -1) {
			close(imagefd);
			err("Cannot stat image dirfd");
		} else if (eprefix_len > 0) {
			int imagepfx = openat(imagefd, eprefix + 1, O_RDONLY);
			if (imagepfx != -1) {
				close(imagefd);
				imagefd = imagepfx;
			}
		}

		/* rely on INSTALL_MASK code to remove optional dirs */
		maskp = snprintf(imask, masklen, "%s ", install_mask);
		if (strstr(features, "noinfo") != NULL)
			maskp += snprintf(imask + maskp, masklen - maskp,
					"/usr/share/info ");
		if (strstr(features, "noman" ) != NULL)
			maskp += snprintf(imask + maskp, masklen - maskp,
					"/usr/share/man ");
		if (strstr(features, "nodoc" ) != NULL)
			maskp += snprintf(imask + maskp, masklen - maskp,
					"/usr/share/doc ");

		/* Initialize INSTALL_MASK and common stuff */
		makeargv(imask, &iargc, &iargv);
		free(imask);
		install_mask_pwd(iargc, iargv, &st, imagefd);
		freeargv(iargc, iargv);

		/* we dont care about the return code, if it's empty, we want it
		 * gone */
		unlinkat(imagefd, "./usr/share", AT_REMOVEDIR);

		close(imagefd);
	}

	if (eprefix != NULL)
		free(eprefix);

	makeargv(config_protect, &cp_argc, &cp_argv);
	makeargv(config_protect_mask, &cpm_argc, &cpm_argv);

	if ((contents = fopen("vdb/CONTENTS", "w")) == NULL)
		errf("come on wtf?");

	objs = NULL;
	{
		char *cpath;
		int ret;

		cpath = xstrdup("");  /* xrealloced in merge_tree_at */

		ret = merge_tree_at(AT_FDCWD, "image",
				AT_FDCWD, portroot, contents, eprefix_len,
				&objs, &cpath, cp_argc, cp_argv, cpm_argc, cpm_argv);

		free(cpath);

		if (ret != 0)
			errp("failed to merge to %s", portroot);
	}
	fclose(contents);

	/* run postinst */
	if (!pretend)
		pkg_run_func("vdb", phases, "pkg_postinst", D, T);

	/* XXX: hmm, maybe we'll want to strip more ? */
	unlink("vdb/environment");

	/* Unmerge any stray pieces from the older version which we didn't
	 * replace */
	/* TODO: Should see about merging with unmerge_packages() */
	while (1) {
		int ret;
		tree_pkg_ctx *pkg_ctx;
		depend_atom *old_atom;

		pkg_ctx = tree_next_pkg(cat_ctx);
		if (!pkg_ctx)
			break;
		old_atom = tree_get_atom(pkg_ctx, 1);  /* retrieve SLOT */
		if (!old_atom)
			goto next_pkg;
		old_atom->SUBSLOT = NULL;  /* just match SLOT */
		old_atom->REPO = NULL;     /* REPO never matters, TODO atom_compare */
		ret = atom_compare(atom, old_atom);
		switch (ret) {
			case NEWER:
			case OLDER:
			case EQUAL:
				/* We need to really set this unmerge pending after we
				 * look at contents of the new pkg */
				break;
			default:
				warn("no idea how we reached here.");
			case ERROR:
			case NOT_EQUAL:
				goto next_pkg;
		}

		printf("%s+++%s %s/%s %s %s/%s\n",
				GREEN, NORM, atom->CATEGORY, pkg->PF,
				booga[ret], cat_ctx->name, pkg_ctx->name);

		pkg_unmerge(pkg_ctx, objs, cp_argc, cp_argv, cpm_argc, cpm_argv);
 next_pkg:
		tree_close_pkg(pkg_ctx);
	}

	freeargv(cp_argc, cp_argv);
	freeargv(cpm_argc, cpm_argv);

	/* Clean up the package state */
	free_set(objs);
	free(D);
	free(T);

	/* Update the magic counter */
	/* FIXME: check Portage's get_counter_tick_core */
	if ((fp = fopen("vdb/COUNTER", "w")) != NULL) {
		fputs("0", fp);
		fclose(fp);
	}

	if (!pretend) {
		/* move the local vdb copy to the final place */
		snprintf(buf, sizeof(buf), "%s%s/%s/",
				portroot, portvdb, pkg->CATEGORY);
		mkdir_p(buf, 0755);
		strcat(buf, pkg->PF);
		rm_rf(buf);  /* get rid of existing dir, empty dir is fine */
		if (rename("vdb", buf) != 0)
			warn("failed to move 'vdb' to '%s': %s", buf, strerror(errno));
	}

	/* clean up our local temp dir */
	xchdir("..");
	rm_rf(pkg->PF);
	/* don't care about return */
	rmdir("../qmerge");

	printf("%s>>>%s %s%s%s/%s%s%s\n",
			YELLOW, NORM, WHITE, pkg->CATEGORY, NORM, CYAN, pkg->PF, NORM);

	tree_close_cat(cat_ctx);
	tree_close(vdb);
}

static int
pkg_unmerge(tree_pkg_ctx *pkg_ctx, set *keep,
		int cp_argc, char **cp_argv, int cpm_argc, char **cpm_argv)
{
	tree_cat_ctx *cat_ctx = pkg_ctx->cat_ctx;
	char *phases;
	char *eprefix;
	size_t eprefix_len;
	const char *T;
	char *buf;
	char *savep;
	int portroot_fd;
	llist_char *dirs = NULL;
	bool unmerge_config_protected;

	buf = phases = NULL;
	T = "${PWD}/temp";

	printf("%s<<<%s %s\n", YELLOW, NORM,
			atom_format("%[CATEGORY]%[PF]", tree_get_atom(pkg_ctx, false)));

	if (pretend == 100)
		return 0;

	/* First get a handle on the things to clean up */
	buf = tree_pkg_meta_get(pkg_ctx, CONTENTS);
	if (buf == NULL)
		return 1;

	portroot_fd = cat_ctx->ctx->portroot_fd;

	/* Then execute the pkg_prerm step */
	if (!pretend) {
		phases = tree_pkg_meta_get(pkg_ctx, DEFINED_PHASES);
		if (phases != NULL) {
			mkdirat(pkg_ctx->fd, "temp", 0755);
			pkg_run_func_at(pkg_ctx->fd, ".", phases, "pkg_prerm", T, T);
		}
	}

	eprefix = tree_pkg_meta_get(pkg_ctx, EPREFIX);
	if (eprefix == NULL)
		eprefix_len = 0;
	else
		eprefix_len = strlen(eprefix);

	unmerge_config_protected =
		strstr(features, "config-protect-if-modified") != NULL;

	for (; (buf = strtok_r(buf, "\n", &savep)) != NULL; buf = NULL) {
		bool del;
		contents_entry *e;
		char zing[20];
		int protected = 0;
		struct stat st;

		e = contents_parse_line(buf);
		if (!e)
			continue;

		protected = config_protected(e->name + eprefix_len,
				cp_argc, cp_argv, cpm_argc, cpm_argv);

		/* This should never happen ... */
		assert(e->name[0] == '/' && e->name[1] != '/');

		/* Should we remove in order symlinks,objects,dirs ? */
		switch (e->type) {
			case CONTENTS_DIR: {
				/* since the dir contains files, we remove it later */
				llist_char *list = xmalloc(sizeof(llist_char));
				list->data = xstrdup(e->name);
				list->next = dirs;
				dirs = list;
				continue;
			}

			case CONTENTS_OBJ:
				if (protected && unmerge_config_protected) {
					/* If the file wasn't modified, unmerge it */
					char *hash = hash_file_at(portroot_fd,
							e->name + 1, HASH_MD5);
					protected = strcmp(e->digest, (const char *)hash);
				}
				break;

			case CONTENTS_SYM:
				if (fstatat(portroot_fd,
							e->name + 1, &st, AT_SYMLINK_NOFOLLOW)) {
					if (errno != ENOENT) {
						warnp("stat failed for %s -> '%s'",
								e->name, e->sym_target);
						continue;
					} else
						break;
				}

				/* Hrm, if it isn't a symlink anymore, then leave it be */
				if (!S_ISLNK(st.st_mode))
					continue;

				break;

			default:
				warn("%s???%s %s%s%s (%d)", RED, NORM,
						WHITE, e->name, NORM, e->type);
				continue;
		}

		snprintf(zing, sizeof(zing), "%s%s%s",
				protected ? YELLOW : GREEN,
				protected ? "***" : "<<<" , NORM);

		if (protected) {
			qprintf("%s %s\n", zing, e->name);
			continue;
		}

		/* See if this was updated */
		del = false;
		if (keep != NULL)
			(void)del_set(e->name, keep, &del);
		if (del)
			strcpy(zing, "---");

		/* No match, so unmerge it */
		if (!quiet)
			printf("%s %s\n", zing, e->name);
		if (!keep || !del) {
			char *p;

			if (!pretend && unlinkat(portroot_fd, e->name + 1, 0)) {
				/* If a file was already deleted, ignore the error */
				if (errno != ENOENT)
					errp("could not unlink: %s%s", portroot, e->name + 1);
			}

			p = strrchr(e->name, '/');
			if (p) {
				*p = '\0';
				if (!pretend)
					rmdir_r_at(portroot_fd, e->name + 1);
			}
		}
	}

	/* Then remove all dirs in reverse order */
	while (dirs != NULL) {
		llist_char *list = dirs;
		char *dir = list->data;
		int rm;

		rm = pretend ? -1 : rmdir_r_at(portroot_fd, dir + 1);
		qprintf("%s%s%s %s%s%s/\n", rm ? YELLOW : GREEN, rm ? "---" : "<<<",
			NORM, DKBLUE, dir, NORM);

		dirs = dirs->next;
		free(list->data);
		free(list);
	}

	if (!pretend) {
		/* Then execute the pkg_postrm step */
		pkg_run_func_at(pkg_ctx->fd, ".", phases, "pkg_postrm", T, T);

		/* Finally delete the vdb entry */
		rm_rf_at(pkg_ctx->fd, ".");
		unlinkat(cat_ctx->fd, pkg_ctx->name, AT_REMOVEDIR);

		/* And prune the category if it's empty */
		unlinkat(cat_ctx->ctx->tree_fd, cat_ctx->name, AT_REMOVEDIR);
	}

	return 0;
}

static int
unlink_empty(const char *buf)
{
	struct stat st;
	int fd;
	int ret = -1;

	fd = open(buf, O_RDONLY);
	if (fd != -1 && stat(buf, &st) != -1) {
		if (st.st_size == 0)
			ret = unlink(buf);
		close(fd);
	}
	return ret;
}

static int
pkg_verify_checksums(
		char *fname,
		const struct pkg_t *pkg,
		int strict,
		int display)
{
	int ret = 0;
	char md5[32+1];
	char sha1[40+1];
	size_t flen;

	if (hash_multiple_file(fname, md5, sha1, NULL, NULL, NULL, NULL,
			&flen, HASH_MD5 | HASH_SHA1) == -1)
		errf("failed to compute hashes for %s/%s: %s\n",
				pkg->CATEGORY, pkg->PF, strerror(errno));

	if (flen != pkg->SIZE) {
		warn("filesize %zu doesn't match requested size %zu for %s/%s\n",
				flen, pkg->SIZE, pkg->CATEGORY, pkg->PF);
		ret++;
	}

	if (pkg->MD5[0]) {
		if (strcmp(md5, pkg->MD5) == 0) {
			if (display)
				printf("MD5:  [%sOK%s] %s %s/%s\n",
						GREEN, NORM, md5, pkg->CATEGORY, pkg->PF);
		} else {
			if (display)
				warn("MD5:  [%sER%s] (%s) != (%s) %s/%s",
						RED, NORM, md5, pkg->MD5, pkg->CATEGORY, pkg->PF);
			ret++;
		}
	}

	if (pkg->SHA1[0]) {
		if (strcmp(sha1, pkg->SHA1) == 0) {
			if (display)
				qprintf("SHA1: [%sOK%s] %s %s/%s\n",
						GREEN, NORM, sha1, pkg->CATEGORY, pkg->PF);
		} else {
			if (display)
				warn("SHA1: [%sER%s] (%s) != (%s) %s/%s",
						RED, NORM, sha1, pkg->SHA1, pkg->CATEGORY, pkg->PF);
			ret++;
		}
	}

	if (!pkg->SHA1[0] && !pkg->MD5[0])
		return 1;

	if (strict && ret)
		errf("strict is set in features");

	return ret;
}

static void
pkg_fetch(int level, const depend_atom *atom, const struct pkg_t *pkg)
{
	char buf[_Q_PATH_MAX], str[_Q_PATH_MAX];

	/* qmerge -pv patch */
	if (pretend) {
		if (!install)
			install++;
		/* qprint_tree_node(level, atom, pkg); */
		pkg_merge(level, atom, pkg);
		return;
	}

	/* check to see if file exists and it's checksum matches */
	snprintf(buf, sizeof(buf), "%s/%s/%s.tbz2", pkgdir, pkg->CATEGORY, pkg->PF);
	unlink_empty(buf);

	snprintf(str, sizeof(str), "%s/%s", pkgdir, pkg->CATEGORY);
	if (mkdir(str, 0755) == -1 && errno != EEXIST) {
		warn("Failed to create %s", str);
		return;
	}

	if (force_download && (access(buf, R_OK) == 0) &&
			(pkg->SHA1[0] || pkg->MD5[0]))
	{
		if (pkg_verify_checksums(buf, pkg, 0, 0) != 0)
			if (getenv("QMERGE") == NULL)
				unlink(buf);
	}
	if (access(buf, R_OK) == 0) {
		if (!pkg->SHA1[0] && !pkg->MD5[0]) {
			warn("No checksum data for %s (try `emaint binhost --fix`)", buf);
			return;
		} else {
			if (pkg_verify_checksums(buf, pkg, qmerge_strict, !quiet)
					== 0)
			{
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
	snprintf(buf, sizeof(buf), "%s/%s/%s.tbz2",
			pkgdir, atom->CATEGORY, pkg->PF);
	if (access(buf, R_OK) != 0) {
		snprintf(buf, sizeof(buf), "%s.tbz2", pkg->PF);
		fetch(str, buf);
		old_repo = 1;
	}

	/* verify the pkg exists now. unlink if zero bytes */
	snprintf(buf, sizeof(buf), "%s/%s/%s.tbz2",
			pkgdir, atom->CATEGORY, pkg->PF);
	unlink_empty(buf);

	if (access(buf, R_OK) != 0) {
		warn("Failed to fetch %s.tbz2 from %s", pkg->PF, binhost);
		fflush(stderr);
		return;
	}

	snprintf(buf, sizeof(buf), "%s/%s/%s.tbz2",
			pkgdir, atom->CATEGORY, pkg->PF);
	if (pkg_verify_checksums(buf, pkg, qmerge_strict, !quiet) == 0) {
		pkg_merge(0, atom, pkg);
		return;
	}
}

static void
print_Pkg(int full, const depend_atom *atom, const struct pkg_t *pkg)
{
	char *p = NULL;
	char buf[512];

	printf("%s%s%s%s\n", atom_format("%[CAT]%[PF]%[SLOT]", atom),
		!quiet ? " [" : "",
		!quiet ? make_human_readable_str(pkg->SIZE, 1, KILOBYTE) : "",
		!quiet ? " KiB]" : "");

	if (full == 0)
		return;

	if (pkg->DESC[0])
		printf(" %sDesc%s:%s %s\n", DKGREEN, YELLOW, NORM, pkg->DESC);
	if (pkg->SHA1[0])
		printf(" %sSha1%s:%s %s\n", DKGREEN, YELLOW, NORM, pkg->SHA1);
	if (pkg->MD5[0])
		printf(" %sMd5%s:%s %s\n", DKGREEN, YELLOW, NORM, pkg->MD5);
	if (!pkg->MD5[0] && !pkg->SHA1[0])
		printf(" %sSums%s:%s %s(MISSING!)%s\n",
				DKGREEN, YELLOW, NORM, RED, NORM);
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

	if ((p = best_version(pkg->CATEGORY, atom->PN, pkg->SLOT)) != NULL) {
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
			printf(" %sInstalled%s:%s %s%s%s\n",
					DKGREEN, YELLOW, NORM, icolor, p, NORM);
		}
	}
}

/* HACK: pull this in, knowing that qlist will be in the final link, we
 * should however figure out how to do what match does here from e.g.
 * atom */
extern bool qlist_match(
		tree_pkg_ctx *pkg_ctx,
		const char *name,
		depend_atom **name_atom,
		bool exact,
		bool applymasks);

static int
qmerge_unmerge_cb(tree_pkg_ctx *pkg_ctx, void *priv)
{
	int cp_argc;
	int cpm_argc;
	char **cp_argv;
	char **cpm_argv;
	char **todo;
	char **p;

	makeargv(config_protect, &cp_argc, &cp_argv);
	makeargv(config_protect_mask, &cpm_argc, &cpm_argv);

	(void)list_set(priv, &todo);
	for (p = todo; *p != NULL; p++) {
		if (qlist_match(pkg_ctx, *p, NULL, true, false))
			pkg_unmerge(pkg_ctx, NULL, cp_argc, cp_argv, cpm_argc, cpm_argv);
	}

	free(todo);
	freeargv(cp_argc, cp_argv);
	freeargv(cpm_argc, cpm_argv);

	return 0;
}

static int
unmerge_packages(set *todo)
{
	tree_ctx *vdb = tree_open_vdb(portroot, portvdb);
	int ret = 1;
	if (vdb != NULL) {
		ret = tree_foreach_pkg_fast(vdb, qmerge_unmerge_cb, todo, NULL);
		tree_close(vdb);
	}
	return ret;
}

static tree_ctx *_grab_binpkg_info_tree = NULL;
static struct pkg_t *
grab_binpkg_info(depend_atom *atom)
{
	tree_ctx *tree = _grab_binpkg_info_tree;
	tree_match_ctx *tpkg;
	struct pkg_t *pkg = NULL;
	char path[BUFSIZ];
	FILE *d;

	/* reuse previously opened tree, so we really employ the cache
	 * from libq/tree */
	if (tree == NULL) {
		snprintf(path, sizeof(path), "%s/portage/%s", port_tmpdir, Packages);
		/* we don't use ROOT on package tree here, operating on ROOT
		 * should be for package merges/unmerges, but be able to pull
		 * binpkgs from current system */
		if ((d = fopen(path, "r")) != NULL) {
			fclose(d);
			snprintf(path, sizeof(path), "%s/portage", port_tmpdir);
			tree = tree_open_binpkg("/", path);
		} else {
			tree = tree_open_binpkg("/", pkgdir);
		}
		_grab_binpkg_info_tree = tree;

		/* if opening the tree failed somehow, we can't return anything */
		if (tree == NULL)
			return NULL;
	}

	tpkg = tree_match_atom(tree, atom,
			TREE_MATCH_FIRST | TREE_MATCH_VIRTUAL | TREE_MATCH_METADATA);
	if (tpkg != NULL) {
		depend_atom *tatom = tpkg->atom;
		tree_pkg_meta *meta = tpkg->meta;
		pkg = xzalloc(sizeof(struct pkg_t));

		snprintf(pkg->PF, sizeof(pkg->PF), "%s", tatom->PF);
		snprintf(pkg->CATEGORY, sizeof(pkg->CATEGORY), "%s", tatom->CATEGORY);
		if (meta->Q_DESCRIPTION != NULL)
			snprintf(pkg->DESC, sizeof(pkg->DESC), "%s", meta->Q_DESCRIPTION);
		if (meta->Q_LICENSE != NULL)
			snprintf(pkg->LICENSE, sizeof(pkg->LICENSE), "%s", meta->Q_LICENSE);
		if (meta->Q_RDEPEND != NULL)
			snprintf(pkg->RDEPEND, sizeof(pkg->RDEPEND), "%s", meta->Q_RDEPEND);
		if (meta->Q_MD5 != NULL)
			snprintf(pkg->MD5, sizeof(pkg->MD5), "%s", meta->Q_MD5);
		if (meta->Q_SHA1 != NULL)
			snprintf(pkg->SHA1, sizeof(pkg->SHA1), "%s", meta->Q_SHA1);
		if (meta->Q_USE != NULL)
			snprintf(pkg->USE, sizeof(pkg->USE), "%s", meta->Q_USE);
		if (meta->Q_repository != NULL)
			snprintf(pkg->REPO, sizeof(pkg->REPO), "%s", meta->Q_repository);
		if (meta->Q_SLOT != NULL)
			snprintf(pkg->REPO, sizeof(pkg->REPO), "%s", meta->Q_SLOT);
		if (meta->Q_SIZE != NULL)
			pkg->SIZE = atoi(meta->Q_SIZE);

		tree_match_close(tpkg);
	}

	return pkg;
}

static set *
qmerge_add_set_file(const char *pfx, const char *dir, const char *file, set *q)
{
	FILE *fp;
	int linelen;
	size_t buflen;
	char *buf, *fname;

	/* Find the file to read */
	xasprintf(&fname, "%s%s%s/%s", portroot, pfx, dir, file);

	if ((fp = fopen(fname, "r")) == NULL) {
		warnp("unable to read set file %s", fname);
		free(fname);
		return NULL;
	}
	free(fname);

	/* Load each entry */
	buf = NULL;
	while ((linelen = getline(&buf, &buflen, fp)) >= 0) {
		rmspace_len(buf, (size_t)linelen);
		q = add_set(buf, q);
	}
	free(buf);

	fclose(fp);

	return q;
}

static void *
qmerge_add_set_system(void *data, char *buf)
{
	set *q = data;
	char *s;

	s = strchr(buf, '#');
	if (s)
		*s = '\0';
	rmspace(buf);

	s = buf;
	if (*s == '*')
		q = add_set(s + 1, q);
	else if (s[0] == '-' && s[1] == '*') {
		bool ok;
		(void)del_set(s + 2, q, &ok);
	}

	return q;
}

/* XXX: note, this doesn't handle more complicated set files like
 *      the portage .ini files in /usr/share/portage/sets/ */
/* XXX: this code does not combine duplicate dependencies */
static set *
qmerge_add_set(char *buf, set *q)
{
	if (strcmp(buf, "world") == 0)
		return qmerge_add_set_file(CONFIG_EPREFIX, "/var/lib/portage",
				"world", q);
	else if (strcmp(buf, "all") == 0) {
		tree_ctx *ctx = tree_open_vdb(portroot, portvdb);
		set *ret = NULL;
		if (ctx != NULL) {
			ret = tree_get_atoms(ctx, false, NULL);
			tree_close(ctx);
		}
		return ret;
	} else if (strcmp(buf, "system") == 0)
		return q_profile_walk("packages", qmerge_add_set_system, q);
	else if (buf[0] == '@')
		/* TODO: use configroot */
		return qmerge_add_set_file(CONFIG_EPREFIX, "/etc/portage", buf+1, q);
	else {
		rmspace(buf);
		return add_set(buf, q);
	}
}

static int
qmerge_run(set *todo)
{
	if (uninstall) {
		return unmerge_packages(todo);
	} else {
		if (todo == NULL || search_pkgs) {
			/* disputable, this should be qlist -kIv or something */
			warn("please use qlist -kI");

			return EXIT_SUCCESS;
		} else {
			char **todo_strs;
			size_t todo_cnt = list_set(todo, &todo_strs);
			size_t i;
			depend_atom *atom;
			struct pkg_t *pkg;
			int ret = EXIT_FAILURE;

			for (i = 0; i < todo_cnt; i++) {
				atom = atom_explode(todo_strs[i]);
				pkg = grab_binpkg_info(atom);
				if (pkg != NULL) {
					if (search_pkgs)
						print_Pkg(verbose, atom, pkg);
					else
						pkg_fetch(0, atom, pkg);
					free(pkg);
					ret = EXIT_SUCCESS;
				}
				atom_implode(atom);
			}
			free(todo_strs);

			return ret;
		}
	}
}

int qmerge_main(int argc, char **argv)
{
	int i, ret;
	set *todo;

	if (argc < 2)
		qmerge_usage(EXIT_FAILURE);

	while ((i = GETOPT_LONG(QMERGE, qmerge, "")) != -1) {
		switch (i) {
			case 'f': force_download = 1; break;
			case 'F': force_download = 2; break;
			case 's': search_pkgs = 1; interactive = 0; break;
			/* case 'i': case 'g': */
			case 'K': install = 1; break;
			case 'U': uninstall = 1; break;
			case 'p': pretend = 1; break;
			case 'u':
				update_only = 1;
				/* fall through */
			case 'y': interactive = 0; break;
			case 'O': follow_rdepends = 0; break;
			case 128: debug = true; break;
			COMMON_GETOPTS_CASES(qmerge)
		}
	}

	qmerge_strict = (strstr("strict", features) == 0) ? 1 : 0;

	/* Short circut this. */
	if (install && !pretend) {
		if (follow_rdepends && getenv("QMERGE") == NULL) {
			install = 0;
			warn("Using these options are likely to break your "
					"system at this point. export QMERGE=1; "
					"if you think you know what your doing.");
		}
	}

	/* Expand any portage sets on the command line */
	todo = NULL;
	for (i = optind; i < argc; ++i)
		todo = qmerge_add_set(argv[i], todo);

	if (search_pkgs == 0 && todo == NULL) {
		warn("need package names to work with");
		return EXIT_FAILURE;
	}

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
				return EXIT_FAILURE;
		} else {
			if (!prompt("OK to merge these packages"))
				return EXIT_FAILURE;
		}

		pretend = save_pretend;
		verbose = save_verbose;
		quiet = save_quiet;
	}

	ret = qmerge_run(todo);
	if (todo != NULL)
		free_set(todo);
	return ret;
}
