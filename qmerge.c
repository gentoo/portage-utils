/*
 * Copyright 2005-2018 Gentoo Authors
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifdef APPLET_qmerge

/* This is a GNUlib hack, because GNUlib doesn't provide st_mtim members
 * of struct stat, but instead provides wrappers to retrieve the time
 * fields (stat-time module). We just define a macro in case people are
 * building without autoconf. */
#ifdef _GL_SYS_STAT_H
# include "stat-time.h"
#else
# define get_stat_mtime(X) ((X)->st_mtim)
#endif

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
static int pkg_unmerge(q_vdb_pkg_ctx *, queue *, int, char **, int, char **);
static struct pkg_t *grab_binpkg_info(const char *);
static char *find_binpkg(const char *);

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

	char *buf;
	xasprintf(&buf, "%s/portage/", port_tmpdir);
	mkdir_p(buf, 0755);
	xchdir(buf);

	if (force_download != 2) {
		if (force_download)
			unlink(Packages);

		if (access(Packages, R_OK) != 0)
			fetch(buf, Packages);
	}
	free(buf);
}

struct qmerge_bv_state {
	const char *catname;
	const char *pkgname;
	const char *slot;
	char buf[4096];
	char *retbuf;
};

static int
qmerge_filter_cat(q_vdb_cat_ctx *cat_ctx, void *priv)
{
	struct qmerge_bv_state *state = priv;
	return !state->catname || strcmp(cat_ctx->name, state->catname) == 0;
}

static int
qmerge_best_version_cb(q_vdb_pkg_ctx *pkg_ctx, void *priv)
{
	struct qmerge_bv_state *state = priv;
	if (qlist_match(pkg_ctx, state->buf, NULL, true))
		snprintf(state->retbuf, sizeof(state->buf), "%s/%s:%s",
			 pkg_ctx->cat_ctx->name, pkg_ctx->name, state->slot);
	return 0;
}

static char *
best_version(const char *catname, const char *pkgname, const char *slot)
{
	static int vdb_check = 1;
	static char retbuf[4096];
	struct qmerge_bv_state state = {
		.catname = catname,
		.pkgname = pkgname,
		.slot = slot,
		.retbuf = retbuf,
	};

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

	retbuf[0] = '\0';
	snprintf(state.buf, sizeof(state.buf), "%s%s%s:%s",
		 catname ? : "", catname ? "/" : "", pkgname, slot);
	q_vdb_foreach_pkg(qmerge_best_version_cb, &state, qmerge_filter_cat);

 done:
	return retbuf;
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
	printf("applying install masks:\n");
	for (cnt = 0; cnt < masksc; cnt++) {
		ssize_t plen = (ssize_t)masksv[cnt][0];
		printf("%3zd  ", plen);
		if (plen < 0)
			plen = -plen;
		for (i = 1; i <= plen; i++)
			printf("%s ", masksv[cnt][i]);
		printf(" %zd\n", (size_t)masksv[cnt][i]);
	}
#endif

	cnt = snprintf(qpth, _Q_PATH_MAX, "%s", CONFIG_EPREFIX);
	cnt--;
	if (qpth[cnt] == '/')
		qpth[cnt] = '\0';

	install_mask_check_dir(masksv, masksc, st, fd, 1, INCLUDE, qpth);
}

static char *
atom2str(const depend_atom *atom, char *buf, size_t size)
{
	if (atom->PR_int)
		snprintf(buf, size, "%s-%s-r%i", atom->PN, atom->PV, atom->PR_int);
	else
		snprintf(buf, size, "%s-%s", atom->PN, atom->PV);
	return buf;
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
		/* best_version() */
		"use() { useq \"$@\"; }\n"
		"usex() { useq \"$1\" && echo \"${2-yes}$4\" || echo \"${3-no}$5\"; }\n"
		"useq() { hasq \"$1\" ${USE}; }\n"
		"usev() { hasv \"$1\" ${USE}; }\n"
		"has() { hasq \"$@\"; }\n"
		"hasq() { local h=$1; shift; case \" $* \" in *\" $h \"*) return 0;; *) return 1;; esac; }\n"
		"hasv() { hasq \"$@\" && echo \"$1\"; }\n"
		"elog() { printf ' * %%b\\n' \"$*\"; }\n"
		"einfo() { elog \"$@\"; }\n"
		"ewarn() { elog \"$@\"; }\n"
		"eqawarn() { elog \"QA: \"\"$@\"; }\n"
		"eerror() { elog \"$@\"; }\n"
		"die() { eerror \"$@\"; exit 1; }\n"
		/* TODO: This should suppress `die` */
		"nonfatal() { \"$@\"; }\n"
		"ebegin() { printf ' * %%b ...' \"$*\"; }\n"
		"eend() { local r=${1:-$?}; [ $# -gt 0 ] && shift; [ $r -eq 0 ] && echo ' [ ok ]' || echo \" $* \"'[ !! ]'; return $r; }\n"
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
		"EROOT=\"${EPREFIX%%/}/${ROOT#/}/\"\n"
		"D=\"%5$s\"\n"
		"ED=\"${EPREFIX%%/}/${D#/}/\"\n"
		"T=\"%6$s\"\n"
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
              FILE *contents, queue **objs, char **cpathp,
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
			merge_tree_at(subfd_src, name, subfd_dst, name, contents, objs,
					cpathp, cp_argc, cp_argv, cpm_argc, cpm_argv);
			cpath = *cpathp;
			mnlen = 0;

			/* In case we didn't install anything, prune the empty dir */
			if (!pretend)
				unlinkat(subfd_dst, name, AT_REMOVEDIR);
		} else if (S_ISREG(st.st_mode)) {
			/* Migrate a file */
			struct timespec times[2];
			int fd_srcf, fd_dstf;
			unsigned char *hash;
			const char *tmpname, *dname;

			/* syntax: obj filename hash mtime */
			hash = hash_file_at(subfd_src, name, HASH_MD5);
			if (!pretend)
				fprintf(contents, "obj %s %s %"PRIu64"\n",
						cpath, hash, (uint64_t)st.st_mtime);
			free(hash);

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
			char *sym = alloca(len + 1);

			/* Find out what we're pointing to */
			if (readlinkat(subfd_src, name, sym, len) == -1) {
				warnp("could not read link %s", cpath);
				continue;
			}
			sym[len] = '\0';

			/* syntax: sym src -> dst mtime */
			if (!pretend)
				fprintf(contents, "sym %s -> %s %"PRIu64"\n",
						cpath, sym, (uint64_t)st.st_mtime);
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

	ret = 0;

 done:
	close(subfd_src);
	close(subfd_dst);

	return ret;
}

/* oh shit getting into pkg mgt here. FIXME: write a real dep resolver. */
static void
pkg_merge(int level, const depend_atom *atom, const struct pkg_t *pkg)
{
	queue *objs;
	q_vdb_ctx *vdb_ctx;
	q_vdb_cat_ctx *cat_ctx;
	FILE *fp, *contents;
	static char *phases;
	static size_t phases_len;
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

	if (pkg->RDEPEND[0] && follow_rdepends) {
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
					if (*name == '~') {
						name = ARGV[i] + 1;
						/* warn("newname = %s", name); */
					}
					if ((subatom = atom_explode(name)) != NULL) {
						struct pkg_t *subpkg;
						char *resolved = NULL;

						resolved = find_binpkg(name);

						IF_DEBUG(fprintf(stderr,
									"+Atom: argv0(%s) resolved(%s)\n",
									name, resolved));

						if (strlen(resolved) < 1) {
							warn("Cant find a binpkg for %s from rdepend(%s)",
									name, pkg->RDEPEND);
							continue;
						}

						/* ratom = atom_explode(resolved); */
						subpkg = grab_binpkg_info(resolved); /* free me later */

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
	vdb_ctx = q_vdb_open();
	if (!vdb_ctx)
		return;
	cat_ctx = q_vdb_open_cat(vdb_ctx, pkg->CATEGORY);
	if (!cat_ctx) {
		if (errno != ENOENT) {
			q_vdb_close(vdb_ctx);
			return;
		}
		mkdirat(vdb_ctx->vdb_fd, pkg->CATEGORY, 0755);
		cat_ctx = q_vdb_open_cat(vdb_ctx, pkg->CATEGORY);
		if (!cat_ctx) {
			q_vdb_close(vdb_ctx);
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

	/* XXX: maybe some day we should have this step operate on the
	 *      tarball directly rather than unpacking it first. */

	/* split the tbz and xpak data */
	xasprintf(&tbz2, "%s/%s/%s.tbz2", pkgdir, pkg->CATEGORY, pkg->PF);
	if (run_applet_l("qtbz2", "-s", tbz2, NULL) != 0)
		err("`qtbz2 -s %s` failed", tbz2);

	mkdir("vdb", 0755);
	sprintf(tbz2, "%s.xpak", pkg->PF);
	if (run_applet_l("qxpak", "-d", "vdb", "-x", tbz2, NULL) != 0)
		err("`qxpak -d vdb -x %s` failed", tbz2);

	/* figure out if the data is compressed differently from what the
	 * name suggests, bug #660508, usage of BINPKG_COMPRESS,
	 * due to the minimal nature of where we run, we cannot rely on file
	 * or GNU tar, so have to do some laymans MAGIC hunting ourselves */
	compr = "I brotli"; /* default: brotli; has no magic header */
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

		sprintf(tbz2, "%s.tar.bz2", pkg->PF);
		mfd = fopen(tbz2, "r");
		if (mfd != NULL) {
			size_t mlen = fread(magic, 1, sizeof(magic), mfd);
			fclose(mfd);

			if (mlen >= 3 && magic[0] == 'B' && magic[1] == 'Z' &&
					magic[2] == 'h')
			{
				compr = "j";
			} else if (mlen >= 2 &&
					magic[0] == 037 && magic[1] == 0213)
			{
				compr = "z";
			} else if (mlen >= 5 &&
					magic[1] == '7' && magic[2] == 'z' &&
					magic[3] == 'X' && magic[4] == 'Z')
			{
				compr = "J";
			} else if (mlen == 257+6 &&
					magic[257] == 'u' && magic[258] == 's' &&
					magic[259] == 't' && magic[260] == 'a' &&
					magic[261] == 'r' && magic[262] == '\0')
			{
				compr = "";
			} else if (mlen >= 4 &&
					magic[0] == 0x04 && magic[1] == 0x22 &&
					magic[2] == 0x4D && magic[3] == 0x18)
			{
				compr = "I lz4";
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
				 * https://bugs.gentoo.org/show_bug.cgi?id=634980 */
				compr = "I zstd --long=31";
				/*
				 * If really tar -I would be used we would have to quote:
				 * compr = "I \"zstd --long=31\"";
				 * But actually we use a pipe (see below) */
			} else if (mlen >= 4 &&
					magic[0] == 'L' && magic[1] == 'Z' &&
					magic[2] == 'I' && magic[3] == 'P')
			{
				compr = "I lzip";
			} else if (mlen >= 9 &&
					magic[0] == 0x89 && magic[1] == 'L' &&
					magic[2] == 'Z' && magic[3] == 'O' &&
					magic[4] == 0x00 && magic[5] == 0x0D &&
					magic[6] == 0x0A && magic[7] == 0x1A &&
					magic[8] == 0x0A)
			{
				compr = "I lzop";
			}
		}
	}

	free(tbz2);

	/* extract the binary package data */
	mkdir("image", 0755);
	if (compr[0] != 'I') {
		snprintf(buf, sizeof(buf),
			BUSYBOX " tar -x%s%s -f %s.tar.bz2 -C image/",
			((verbose > 1) ? "v" : ""), compr, pkg->PF);
	} else {
		/* busybox's tar has no -I option. Thus, although we possibly
		 * use busybox's shell and tar, we thus pipe, expecting the
		 * corresponding (de)compression tool to be in PATH; if not,
		 * a failure will occur.
		 * Since some tools (e.g. zstd) complain about the .bz2
		 * extension, we feed the tool by input redirection. */
		snprintf(buf, sizeof(buf),
			BUSYBOX " sh -c '%s -dc <%s.tar.bz2 | tar -x%sf - -C image/'",
			compr + 2, pkg->PF, ((verbose > 1) ? "v" : ""));
	}
	xsystem(buf);
	fflush(stdout);

	eat_file("vdb/DEFINED_PHASES", &phases, &phases_len);
	pkg_run_func("vdb", phases, "pkg_pretend", D, T);
	pkg_run_func("vdb", phases, "pkg_setup", D, T);
	pkg_run_func("vdb", phases, "pkg_preinst", D, T);

	{
		int imagefd = open("image" CONFIG_EPREFIX, O_RDONLY);
		size_t masklen = strlen(install_mask) + 1 +
				15 + 1 + 14 + 1 + 14 + 1 + 1;  /* worst case scenario */
		char *imask = xmalloc(masklen);
		size_t maskp;

		if (fstat(imagefd, &st) == -1) {
			close(imagefd);
			err("Cannot stat image dirfd");
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

	makeargv(config_protect, &cp_argc, &cp_argv);
	makeargv(config_protect_mask, &cpm_argc, &cpm_argv);

	if ((contents = fopen("vdb/CONTENTS", "w")) == NULL)
		errf("come on wtf?");
	objs = NULL;
	{
		char *cpath;
		int ret;

		cpath = xstrdup("");  /* xrealloced in merge_tree_at */

		ret = merge_tree_at(AT_FDCWD, "image", AT_FDCWD, portroot, contents,
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
		q_vdb_pkg_ctx *pkg_ctx;
		depend_atom *old_atom;

		pkg_ctx = q_vdb_next_pkg(cat_ctx);
		if (!pkg_ctx)
			break;

		old_atom = atom_explode(pkg_ctx->name);
		/* This cast sucks, but we know for now the field isn't modified */
		old_atom->CATEGORY = (char *)cat_ctx->name;
		ret = atom_compare(atom, old_atom);
		atom_implode(old_atom);
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

		qprintf("%s+++%s %s/%s %s %s/%s\n",
				GREEN, NORM, atom->CATEGORY, pkg->PF,
				booga[ret], cat_ctx->name, pkg_ctx->name);

		pkg_unmerge(pkg_ctx, objs, cp_argc, cp_argv, cpm_argc, cpm_argv);
 next_pkg:
		q_vdb_close_pkg(pkg_ctx);
	}

	freeargv(cp_argc, cp_argv);
	freeargv(cpm_argc, cpm_argv);

	/* Clean up the package state */
	free_sets(objs);
	free(D);
	free(T);

	/* Update the magic counter */
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
		if (rename("vdb", buf)) {
			xasprintf(&p, "mv vdb '%s'", buf);
			xsystem(p);
			free(p);
		}
	}

	/* clean up our local temp dir */
	xchdir("..");
	rm_rf(pkg->PF);
	/* don't care about return */
	rmdir("../qmerge");

	printf("%s>>>%s %s%s%s/%s%s%s\n",
			YELLOW, NORM, WHITE, atom->CATEGORY, NORM, CYAN, atom->PN, NORM);

	q_vdb_close(vdb_ctx);
}

static int
pkg_unmerge(q_vdb_pkg_ctx *pkg_ctx, queue *keep,
		int cp_argc, char **cp_argv, int cpm_argc, char **cpm_argv)
{
	q_vdb_cat_ctx *cat_ctx = pkg_ctx->cat_ctx;
	const char *cat = cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	size_t buflen;
	static char *phases;
	static size_t phases_len;
	const char *T;
	char *buf;
	FILE *fp;
	int ret, portroot_fd;
	llist_char *dirs = NULL;
	bool unmerge_config_protected;

	ret = 1;
	buf = phases = NULL;
	T = "${PWD}/temp";

	printf("%s<<<%s %s%s%s/%s%s%s\n",
			YELLOW, NORM, WHITE, cat, NORM, CYAN, pkgname, NORM);

	if (pretend == 100)
		return 0;

	/* First get a handle on the things to clean up */
	fp = q_vdb_pkg_fopenat_ro(pkg_ctx, "CONTENTS");
	if (fp == NULL)
		return ret;

	portroot_fd = cat_ctx->ctx->portroot_fd;

	/* Then execute the pkg_prerm step */
	if (!pretend) {
		q_vdb_pkg_eat(pkg_ctx, "DEFINED_PHASES", &phases, &phases_len);
		mkdirat(pkg_ctx->fd, "temp", 0755);
		pkg_run_func_at(pkg_ctx->fd, ".", phases, "pkg_prerm", T, T);
	}

	unmerge_config_protected =
		strstr(features, "config-protect-if-modified") != NULL;

	while (getline(&buf, &buflen, fp) != -1) {
		queue *q;
		contents_entry *e;
		char zing[20];
		int protected = 0;
		struct stat st;

		e = contents_parse_line(buf);
		if (!e)
			continue;

		protected = config_protected(e->name,
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
					unsigned char *hash = hash_file_at(portroot_fd,
							e->name + 1, HASH_MD5);
					protected = strcmp(e->digest, (const char *)hash);
					free(hash);
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
		q = keep;
		while (q) {
			if (!strcmp(q->name, e->name)) {
				/* XXX: could remove this from the queue */
				strcpy(zing, "---");
				q = NULL;
				break;
			}
			q = q->next;
		}

		/* No match, so unmerge it */
		if (!quiet)
			printf("%s %s\n", zing, e->name);
		if (!keep || q) {
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

	fclose(fp);

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
		unlinkat(cat_ctx->ctx->vdb_fd, cat_ctx->name, AT_REMOVEDIR);
	}

	ret = 0;
	free(phases);
	free(buf);

	return ret;
}

static int
unlink_empty(const char *buf)
{
	struct stat st;
	if (stat(buf, &st) != -1)
		if (st.st_size == 0)
			return unlink(buf);
	return -1;
}

static int
pkg_verify_checksums(char *fname, const struct pkg_t *pkg, const depend_atom *atom,
                     int strict, int display)
{
	char *hash = NULL;
	int ret = 0;

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
		free(hash);
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
		free(hash);
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

	/* XXX: should do a size check here for partial downloads */

	if (force_download && (access(buf, R_OK) == 0) && (pkg->SHA1[0] || pkg->MD5[0])) {
		if (pkg_verify_checksums(buf, pkg, atom, 0, 0) != 0)
			unlink(buf);
	}
	if (access(buf, R_OK) == 0) {
		if (!pkg->SHA1[0] && !pkg->MD5[0]) {
			warn("No checksum data for %s (try `emaint binhost --fix`)", buf);
			return;
		} else {
			if (pkg_verify_checksums(buf, pkg, atom, qmerge_strict, !quiet) == 0) {
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

	snprintf(buf, sizeof(buf), "%s/%s/%s.tbz2", pkgdir, atom->CATEGORY, pkg->PF);
	if (pkg_verify_checksums(buf, pkg, atom, qmerge_strict, !quiet) == 0) {
		pkg_merge(0, atom, pkg);
		return;
	}
}

static void
print_Pkg(int full, const depend_atom *atom, const struct pkg_t *pkg)
{
	char *p = NULL;
	char buf[512];

	printf("%s%s/%s%s%s%s%s%s\n", BOLD, atom->CATEGORY, BLUE, pkg->PF, NORM,
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
			printf(" %sInstalled%s:%s %s%s%s\n", DKGREEN, YELLOW, NORM, icolor, p, NORM);
		}
	}
}

static int
qmerge_unmerge_cb(q_vdb_pkg_ctx *pkg_ctx, void *priv)
{
	queue *todo = priv;
	int cp_argc;
	int cpm_argc;
	char **cp_argv;
	char **cpm_argv;

	makeargv(config_protect, &cp_argc, &cp_argv);
	makeargv(config_protect_mask, &cpm_argc, &cpm_argv);

	while (todo) {
		if (qlist_match(pkg_ctx, todo->name, NULL, true))
			pkg_unmerge(pkg_ctx, NULL, cp_argc, cp_argv, cpm_argc, cpm_argv);
		todo = todo->next;
	}

	freeargv(cp_argc, cp_argv);
	freeargv(cpm_argc, cpm_argv);

	return 0;
}

static int
unmerge_packages(queue *todo)
{
	return q_vdb_foreach_pkg(qmerge_unmerge_cb, todo, NULL);
}

static FILE *
open_binpkg_index(void)
{
	FILE *fp;
	char *path;

	xasprintf(&path, "%s/portage/%s", port_tmpdir, Packages);
	fp = fopen(path, "r");
	if (fp)
		goto done;
	free(path);

	xasprintf(&path, "%s/%s", pkgdir, Packages);
	fp = fopen(path, "r");
	if (fp)
		goto done;

	/* This is normal when installing from local repo only. */
	warnp("Unable to open package file %s in %s/portage or %s",
		Packages, port_tmpdir, pkgdir);
	warn("Attempting to manually regen via `emaint binhost`");

	pid_t p;
	int status;

	char argv_emaint[] = "emaint";
	char argv_binhost[] = "binhost";
	char argv_fix[] = "--fix";
	char *argv[] = {
		argv_emaint,
		argv_binhost,
		argv_fix,
		NULL,
	};

	p = vfork();
	switch (p) {
	case 0:
		_exit(execvp(argv[0], argv));
	case -1:
		errp("vfork failed");
	}
	waitpid(p, &status, 0);

	fp = fopen(path, "r");

 done:
	free(path);
	return fp;
}

static struct pkg_t *
grab_binpkg_info(const char *name)
{
	FILE *fp;
	char buf[BUFSIZ];
	char *p;
	depend_atom *atom;

	struct pkg_t Pkg;
	struct pkg_t *pkg = xzalloc(sizeof(struct pkg_t));
	struct pkg_t *rpkg = xzalloc(sizeof(struct pkg_t));

	static char best_match[sizeof(Pkg.PF)+2+sizeof(Pkg.CATEGORY)];

	best_match[0] = 0;
	strcpy(pkg->SLOT,"0");

	fp = open_binpkg_index();

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
				strcpy(pkg->SLOT,"0");
			}
			continue;
		}

		if ((p = strchr(buf, '\n')) != NULL)
			*p = 0;
		if ((p = strchr(buf, ':')) == NULL)
			continue;
		if (p[1] != ' ')
			continue;
		*p = 0;
		p += 2;

		if (*buf) {
			/* we dont need all the info */
			if (strcmp(buf, "RDEPEND") == 0)
				strncpy(pkg->RDEPEND, p, sizeof(Pkg.RDEPEND));
			if (strcmp(buf, "PF") == 0)
				strncpy(pkg->PF, p, sizeof(Pkg.PF));
			if (strcmp(buf, "CATEGORY") == 0)
				strncpy(pkg->CATEGORY, p, sizeof(Pkg.CATEGORY));
			if (strcmp(buf, "REPO") == 0)
				strncpy(pkg->REPO, p, sizeof(Pkg.REPO));

			if (strcmp(buf, "CPV") == 0) {
				if ((atom = atom_explode(p)) != NULL) {
					snprintf(buf, sizeof(buf), "%s-%s", atom->PN, atom->PV);
					if (atom->PR_int)
						snprintf(buf, sizeof(buf), "%s-%s-r%i", atom->PN, atom->PV, atom->PR_int);
					strncpy(pkg->PF, buf, sizeof(Pkg.PF));
					strncpy(pkg->CATEGORY, atom->CATEGORY, sizeof(Pkg.CATEGORY));
					atom_implode(atom);
				}
			}
			if (strcmp(buf, "SLOT") == 0)
				strncpy(pkg->SLOT, p, sizeof(Pkg.SLOT));
			if (strcmp(buf, "USE") == 0)
				strncpy(pkg->USE, p, sizeof(Pkg.USE));

			/* checksums. We must have 1 or the other unless --*/
			if (strcmp(buf, "MD5") == 0)
				strncpy(pkg->MD5, p, sizeof(Pkg.MD5));
			if (strcmp(buf, "SHA1") == 0)
				strncpy(pkg->SHA1, p, sizeof(Pkg.SHA1));
		}
	}
	fclose(fp);
	free(pkg);
	return rpkg;
}

static char *
find_binpkg(const char *name)
{
	FILE *fp;
	char buf[BUFSIZ];
	char *p;
	struct pkg_t Pkg;
	char PF[sizeof(Pkg.PF)];
	char CATEGORY[sizeof(Pkg.CATEGORY)];

	static char best_match[sizeof(Pkg.PF)+2+sizeof(Pkg.CATEGORY)];

	best_match[0] = 0;
	if (NULL == name)
		return best_match;

	fp = open_binpkg_index();

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
		if (p[1] != ' ')
			continue;
		*p = 0;
		p += 2;

		if (*buf) {
			if (strcmp(buf, "CPV") == 0) {
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
			if (strcmp(buf, "PF") == 0)
				strncpy(PF, p, sizeof(PF));
			if (strcmp(buf, "CATEGORY") == 0)
				strncpy(CATEGORY, p, sizeof(CATEGORY));
		}
	}
	fclose(fp);
	return best_match;
}

static int
parse_packages(queue *todo)
{
	FILE *fp;
	int linelen;
	size_t buflen;
	char *buf, *p;
	struct pkg_t Pkg;
	depend_atom *pkg_atom;
	char repo[sizeof(Pkg.REPO)];

	fp = open_binpkg_index();

	buf = NULL;
	repo[0] = '\0';

	/* First consume the header with the common data. */
	while ((linelen = getline(&buf, &buflen, fp)) >= 0) {
		rmspace_len(buf, (size_t)linelen);
		if (buf[0] == '\0')
			break;

		if ((p = strchr(buf, ':')) == NULL)
			continue;
		if (p[1] != ' ')
			continue;
		*p = 0;
		p += 2;

		switch (*buf) {
		case 'R':
			if (!strcmp(buf, "REPO"))
				strncpy(repo, p, sizeof(repo));
			break;
		}
	}

	pkg_atom = NULL;
	memset(&Pkg, 0, sizeof(Pkg));
	strcpy(Pkg.SLOT, "0");

	/* Then walk all the package entries. */
	while (getline(&buf, &buflen, fp) != -1) {
		if (*buf == '\n') {
			if (pkg_atom) {
				if (search_pkgs && !todo) {
					print_Pkg(verbose, pkg_atom, &Pkg);
				} else {
					const queue *ll = todo;
					depend_atom *todo_atom;
					while (ll) {
						todo_atom = atom_explode(ll->name);
						pkg_atom->REPO = todo_atom->REPO ? Pkg.REPO : NULL;
						pkg_atom->SLOT = todo_atom->SLOT ? Pkg.SLOT : NULL;
						if (atom_compare(todo_atom, pkg_atom) == EQUAL) {
							if (search_pkgs)
								print_Pkg(verbose, pkg_atom, &Pkg);
							else
								pkg_fetch(0, pkg_atom, &Pkg);
						}
						atom_implode(todo_atom);
						ll = ll->next;
					}
				}

				atom_implode(pkg_atom);
				pkg_atom = NULL;
			}
			memset(&Pkg, 0, sizeof(Pkg));
			strcpy(Pkg.SLOT, "0");
			strcpy(Pkg.REPO, repo);
			continue;
		}

		if ((p = strchr(buf, '\n')) != NULL)
			*p = 0;
		if ((p = strchr(buf, ':')) == NULL)
			continue;
		if (p[1] != ' ')
			continue;
		*p = 0;
		p += 2;

		switch (*buf) {
			case 'U':
				if (strcmp(buf, "USE") == 0) strncpy(Pkg.USE, p, sizeof(Pkg.USE));
				break;
			case 'P':
				if (strcmp(buf, "PF") == 0) strncpy(Pkg.PF, p, sizeof(Pkg.PF));
				break;
			case 'S':
				if (strcmp(buf, "SIZE") == 0) Pkg.SIZE = atol(p);
				if (strcmp(buf, "SLOT") == 0) strncpy(Pkg.SLOT, p, sizeof(Pkg.SLOT));
				if (strcmp(buf, "SHA1") == 0) strncpy(Pkg.SHA1, p, sizeof(Pkg.SHA1));
				break;
			case 'M':
				if (strcmp(buf, "MD5") == 0) strncpy(Pkg.MD5, p, sizeof(Pkg.MD5));
				break;
			case 'R':
				if (strcmp(buf, "REPO") == 0) strncpy(Pkg.REPO, p, sizeof(Pkg.REPO));
				if (strcmp(buf, "RDEPEND") == 0) strncpy(Pkg.RDEPEND, p, sizeof(Pkg.RDEPEND));
				break;
			case 'L':
				if (strcmp(buf, "LICENSE") == 0) strncpy(Pkg.LICENSE, p, sizeof(Pkg.LICENSE));
				break;
			case 'C':
				if (strcmp(buf, "CATEGORY") == 0) strncpy(Pkg.CATEGORY, p, sizeof(Pkg.CATEGORY));
				if (strcmp(buf, "CPV") == 0) {
					if ((pkg_atom = atom_explode(p)) != NULL) {
						if (pkg_atom->PR_int)
							snprintf(Pkg.PF, sizeof(Pkg.PF), "%s-%s-r%i", pkg_atom->PN, pkg_atom->PV, pkg_atom->PR_int);
						else
							snprintf(Pkg.PF, sizeof(Pkg.PF), "%s-%s", pkg_atom->PN, pkg_atom->PV);
						strncpy(Pkg.CATEGORY, pkg_atom->CATEGORY, sizeof(Pkg.CATEGORY));
					}
				}
				break;
			case 'D':
				if (strcmp(buf, "DESC") == 0) strncpy(Pkg.DESC, p, sizeof(Pkg.DESC));
				break;
			default:
				break;
		}
	}

	free(buf);
	fclose(fp);
	if (pkg_atom)
		atom_implode(pkg_atom);

	return EXIT_SUCCESS;
}

static queue *
qmerge_add_set_file(const char *dir, const char *file, queue *set)
{
	FILE *fp;
	int linelen;
	size_t buflen;
	char *buf, *fname;

	/* Find the file to read */
	xasprintf(&fname, "%s%s/%s", portroot, dir, file);

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
		set = add_set(buf, set);
	}
	free(buf);

	fclose(fp);

	return set;
}

static void *
qmerge_add_set_system(void *data, char *buf)
{
	queue *set = data;
	char *s;

	s = strchr(buf, '#');
	if (s)
		*s = '\0';
	rmspace(buf);

	s = buf;
	if (*s == '*')
		set = add_set(s + 1, set);
	else if (s[0] == '-' && s[1] == '*') {
		int ok;
		set = del_set(s + 2, set, &ok);
	}

	return set;
}

/* XXX: note, this doesn't handle more complicated set files like
 *      the portage .ini files in /usr/share/portage/sets/ */
/* XXX: this code does not combine duplicate dependencies */
static queue *
qmerge_add_set(char *buf, queue *set)
{
	if (strcmp(buf, "world") == 0)
		return qmerge_add_set_file("/var/lib/portage", "world", set);
	else if (strcmp(buf, "all") == 0)
		return get_vdb_atoms(0);
	else if (strcmp(buf, "system") == 0)
		return q_profile_walk("packages", qmerge_add_set_system, set);
	else if (buf[0] == '@')
		return qmerge_add_set_file("/etc/portage", buf+1, set);
	else
		return add_set(buf, set);
}

static int
qmerge_run(queue *todo)
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
