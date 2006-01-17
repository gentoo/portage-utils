/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qmerge.c,v 1.15 2006/01/17 15:53:38 solar Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qmerge

#define QMERGE_FLAGS "fsiUpy" COMMON_FLAGS
static struct option const qmerge_long_opts[] = {
	{"fetch",   no_argument, NULL, 'f'},
	{"search",  no_argument, NULL, 's'},
	{"install", no_argument, NULL, 'i'},
	{"unmerge", no_argument, NULL, 'U'},
	{"pretend", no_argument, NULL, 'p'},
	{"yes",     no_argument, NULL, 'y'},
        COMMON_LONG_OPTS
};
static const char *qmerge_opts_help[] = {
	"force download overwriting existing files",
	"search available packages",
	"install package",
	"uninstall package",
	"pretend only",
	"dont prompt before overwriting",
        COMMON_OPTS_HELP
};

static const char qmerge_rcsid[] = "$Id: qmerge.c,v 1.15 2006/01/17 15:53:38 solar Exp $";
#define qmerge_usage(ret) usage(ret, QMERGE_FLAGS, qmerge_long_opts, qmerge_opts_help, lookup_applet_idx("qmerge"))

char pretend = 0;
char search_pkgs = 0;
char interactive = 1; 
char install = 0;
char uninstall = 0;
char force_download = 0;

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
} Pkg;

int interactive_rename(const char *, const char *, struct pkg_t *);
void fetch(const char *, const char *);
void qmerge_initialize(const char *);
char *best_version(const char *, const char  *);
void pkg_merge(depend_atom *, struct pkg_t *);
int pkg_unmerge(char *, char *);
int unlink_empty(char *);
void pkg_fetch(int, char **, struct pkg_t *);
void print_Pkg(int, struct pkg_t *);
int parse_packages(const char *, int, char **);
int config_protected(const char *, int, char **);
int match_pkg(char *, struct pkg_t *); 
int pkg_verify_checksums(char *, struct pkg_t *, depend_atom *);
int unmerge_packages(int, char **);


int interactive_rename(const char *src, const char *dst, struct pkg_t *pkg) {
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
	snprintf(buf, sizeof(buf), "/bin/busybox mv %s %s %s", check_interactive ? "-i" : "", src, dst);
	ret = (system(buf) << 8);
	if (!quiet) {
		if (ret == 0)
			printf("%s>>>%s %s\n", GREEN, NORM, dst);
		else
			printf("%s!!!%s %s\n", RED, NORM, dst);
	}

	return ret;
}

void fetch(const char *destdir, const char *src) {
	char buf[BUFSIZ];

	fflush(stdout);
	fflush(stderr);
	snprintf(buf, sizeof(buf), "%s/bin/busybox wget %s -P %s %s/%s", force_download ? "" : pretend ? "echo " : "",
		(quiet ? "-q" : ""), destdir, binhost, src);
	system(buf);
	fflush(stdout);
	fflush(stderr);
}

void qmerge_initialize(const char *Packages) {

	if (access("/bin/busybox", X_OK) != 0)
		err("/bin/busybox must be installed");

	if (pkgdir[0] == '/') {
		int len = strlen(pkgdir);
		if (len > 5) {
			if ((strcmp(&pkgdir[len-4], "/All")) != 0)
				strncat(pkgdir, "/All", sizeof(pkgdir));
		} else
			errf("PKGDIR='%s' is to short to be valid", pkgdir);
	}

	if (!binhost[0])
		errf("PORTAGE_BINHOST= does not appear to be valid");
	
	if ((access(pkgdir, R_OK|W_OK|X_OK)) != 0)
		errf("Fatal errors with PKGDIR='%s'", pkgdir);

	mkdir(port_tmpdir, 0755);

	if (chdir(port_tmpdir) != 0)
		errf("!!! chdir(PORTAGE_TMPDIR %s) %s", port_tmpdir, strerror(errno));
	if (chdir("portage") != 0)
		errf("!!! chdir(%s/portage) %s", port_tmpdir, strerror(errno));
	if (force_download)
		unlink(Packages);
	if (access(Packages, R_OK) != 0)
		fetch("./", Packages);
}

char *best_version(const char *CATEGORY, const char *PN) {
	static char buf[1024];
	FILE *fp;
	char *p;

	// if defined(EBUG) this spits out incorrect versions.
	snprintf(buf, sizeof(buf), "qlist -CIev %s%s%s 2>/dev/null | tr '\n' ' '", 
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

int config_protected(const char *buf, int ARGC, char **ARGV) {
	int i;

	for (i = 1; i < ARGC; i++)
		if ((strncmp(ARGV[i], buf, strlen(ARGV[i]))) == 0)
			if ((access(buf, R_OK)) == 0)
				return 1;

	if ((strncmp("/etc", buf, 4)) == 0)
		if ((access(buf, R_OK)) == 0)
			return 1;
	return 0;
}

static char *grab_vdb_item(const char *, const char *, const char *);
static char *grab_vdb_item(const char *item, const char *CATEGORY, const char *PF) {
	static char buf[_Q_PATH_MAX];
	char *p;
	FILE *fp;

	snprintf(buf, sizeof(buf), "%s%s/%s/%s/%s", portroot, portvdb, CATEGORY, PF, item);
	if ((fp = fopen(buf, "r")) == NULL)
		return NULL;
	fgets(buf, sizeof(buf), fp);
	if ((p = strchr(buf, '\n')) != NULL)
		*p = 0;
	fclose(fp);
	return buf;
}


/* oh shit getting into pkg mgt here. wishlist vercmp() function */
void pkg_merge(depend_atom *atom, struct pkg_t *pkg) {
	FILE *fp, *contents;
	char buf[1024];
	char tarball[255];
	char *p;
	int i;
	char **iargv = NULL, **ARGV = NULL;
	int iargc = 0, ARGC = 0;
	const char *saved_argv0 = argv0;
	int ret, saved_optind = optind;
	struct stat st, lst;

	if (!install) return;
	if (pretend) return;
#if 0
	if (pkg->RDEPEND[0]) {
		makeargv(pkg->RDEPEND, &ARGC, &ARGV);
		// Walk the rdepends here. Merging what need be.
		for (i = 1; i < ARGC; i++) {
			depend_atom *subatom;
			switch(ARGV[i][0]) {
				case '<':
				case '>':
				case '!':
				case '|':
					fprintf(stderr, "Unhandled depstring %s\n", ARGV[i]);
					break;
				default:
					if ((subatom = atom_explode(ARGV[i])) != NULL) {
						// p = best_version();
						fprintf(stderr, "+dep %s", ARGV[i]);
						atom_implode(subatom);
					} else {
						fprintf(stderr, "Cant explode atom %s\n", ARGV[i]);
					}
					break;
			}
		}
		freeargv(ARGC, ARGV);
		ARGC = 0; ARGV = NULL;
	}
#endif
	if (chdir(port_tmpdir) != 0) errf("!!! chdir(%s) %s", port_tmpdir, strerror(errno));

	mkdir(pkg->PF, 0755);
	// mkdir(pkg->PF, 0710);
	if (chdir(pkg->PF) != 0) errf("!!! chdir(%s) %s", pkg->PF, strerror(errno));

	system("busybox rm -rf ./*"); // this line does funny things to nano's highlighting.

	/* split the tbz and xpak data */
	snprintf(tarball, sizeof(tarball), "%s.tbz2", pkg->PF);
	snprintf(buf, sizeof(buf), "%s/%s", pkgdir, tarball);
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
	snprintf(buf, sizeof(buf), "/bin/busybox tar -jx%sf %s.tar.bz2 -C image/", ((verbose > 1) ? "v" : ""), pkg->PF);
	system(buf);
	fflush(stdout);

	/* check for an already installed pkg */
	snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, pkg->PF);

	// Unmerge the other versions */
	p = best_version(atom->CATEGORY, atom->PN);
	if (*p) {
		ARGC = 0; ARGV = NULL;
		makeargv(p, &ARGC, &ARGV);
		for (i = 1; i < ARGC; i++) {
			char pf[126];
			char *slot = NULL;
			char u;

			strncpy(pf, ARGV[i], sizeof(pf));
			switch((ret = atom_compare_str(buf, pf))) {
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
					if (u) pkg_unmerge(atom->CATEGORY, basename(pf));
					break;
				default:
					warn("no idea how we reached here.");
					break;
			}
			printf("%s+++%s %s %s %s\n", GREEN, NORM, buf, booga[ret], ARGV[i]);
		}
		freeargv(ARGC, ARGV);
		ARGC = 0; ARGV = NULL;
	}
	assert(chdir("image") == 0);

	if (stat("./", &st) == (-1))
		err("Cant stat pwd");

	makeargv(install_mask, &iargc, &iargv);

	for (i = 1; i < iargc; i++) {
		if (iargv[i][0] != '/')
			continue;
		snprintf(buf, sizeof(buf), ".%s", iargv[i]);
		if (lstat(buf, &lst) == (-1))
			continue;
		if (lst.st_dev != st.st_dev) {
			warn("skipping crossmount install masking: %s", buf);
			continue;
		}
		printf("%s<<<%s %s\n", YELLOW, NORM, buf);
		snprintf(buf, sizeof(buf), "busybox rm -rf ./%s", iargv[i]);
		system(buf);
	}
	freeargv(iargc, iargv);	/* install_mask */

	if ((strstr(features, "noinfo")) != NULL) if (access("./usr/share/info", R_OK) == 0) system("busybox rm -rf ./usr/share/info");
	if ((strstr(features, "noman"))  != NULL) if (access("./usr/share/man",  R_OK) == 0) system("busybox rm -rf ./usr/share/man");
	if ((strstr(features, "nodoc"))  != NULL) if (access("./usr/share/doc",  R_OK) == 0) system("busybox rm -rf ./usr/share/doc");

	/* we dont care about the return code */
	rmdir("./usr/share");

	if ((fp = popen("/bin/busybox find .", "r")) == NULL)
		errf("come on wtf!");

	if ((contents = fopen("../vdb/CONTENTS", "w")) == NULL)
		errf("come on wtf?");

	makeargv(config_protect, &ARGC, &ARGV);

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		char line[BUFSIZ];
		int protected = 0;

		if ((p = strrchr(buf, '\n')) != NULL)
			*p = 0;
		if (buf[0] != '.')
			continue;

		if (((strcmp(buf, ".")) == 0) || ((strcmp(buf, "..")) == 0))
			continue;

		/* use lstats for symlinks */
		lstat(buf, &st);

		line[0] = 0;

		/* portage has code that handes fifo's but it looks unused */
    
		if ((S_ISCHR(st.st_mode)) || \
			(S_ISBLK(st.st_mode))); // block or character device
		if (S_ISFIFO(st.st_mode));	// FIFO (named pipe)
		if (S_ISSOCK(st.st_mode));	// socket? (Not in POSIX.1-1996.)

		// syntax: dir dirname
		if (S_ISDIR(st.st_mode)) {
			mkdir(&buf[1], st.st_mode);
			chown(&buf[1], st.st_uid, st.st_gid);
			snprintf(line, sizeof(line), "dir %s", &buf[1]);
		}

		// syntax: obj filename hash mtime
		if (S_ISREG(st.st_mode)) { // regular file
			struct timeval tv;
			char *hash;

			hash = hash_file(buf, HASH_MD5);

			snprintf(line, sizeof(line), "obj %s %s %lu", &buf[1], hash, st.st_mtime);
			/* /etc /usr/kde/2/share/config /usr/kde/3/share/config	/var/qmail/control */

			protected = config_protected(&buf[1], ARGC, ARGV);
			if (protected) {
				char *target_hash = hash_file(&buf[1], HASH_MD5);
				if (strcmp(target_hash, hash) != 0) {
					char newbuf[sizeof(buf)];
					printf("%s***%s %s\n", BRYELLOW, NORM, &buf[1]);
					if (verbose) {
						snprintf(newbuf, sizeof(newbuf), "diff -u %s %s", buf, &buf[1]);
						system(newbuf);
					}
					continue;
				}
			}
			if ((lstat(&buf[1], &lst)) != (-1)) {
				if (S_ISLNK(lst.st_mode)) {
					warn("%s exists and is a symlink and we are going to overwrite it with a file", &buf[1]);
					unlink(&buf[1]);
				}
			}
			if (interactive_rename(buf, &buf[1], pkg) != 0)
				continue;

			assert(chmod(&buf[1], st.st_mode) == 0);
			assert(chown(&buf[1], st.st_uid, st.st_gid) == 0);

			tv.tv_sec = st.st_mtime;
			tv.tv_usec = 0;
			// utimes(&buf[1], &tv);
		}

		/* symlinks are unfinished */
		// syntax: sym src -> dst
		if (S_ISLNK(st.st_mode)) { // (Not in POSIX.1-1996.)
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

			assert(strlen(&buf[1]));
			readlink(buf, path, sizeof(path));
			assert(strlen(path));;
			
			snprintf(line, sizeof(line), "sym %s -> %s%s%lu", &buf[1], path, " ", st.st_mtime);
			// snprintf(line, sizeof(line), "sym %s -> %s", &buf[1], path);
			// we better have one byte here to play with
			strcpy(tmp, &buf[1]);
			if (tmp[0] != '/') errf("sym does not start with /");

			getcwd(pwd, sizeof(pwd));
			if (chdir(dirname(tmp)) != 0) /* tmp gets eatten up now by the dirname call */
				errf("chdir to symbolic dirname %s: %s", tmp, strerror(errno));
			if (lstat(path, &lst) != (-1))
				unlink(&buf[1]);
			// if (path[0] != '/')
			//	puts("path does not start with /");
			if ((symlink(path, &buf[1])) != 0)
				if (errno != EEXIST)
					warn("symlink failed %s -> %s: %s", path, &buf[1], strerror(errno));
			chdir(pwd);
		}
		/* Save the line to the contents file */
		if (*line) fprintf(contents, "%s\n", line);
	}

	freeargv(ARGC, ARGV);	/* config_protect */

	fclose(contents);
	pclose(fp);

	chdir(port_tmpdir);
	chdir(pkg->PF);

	snprintf(buf, sizeof(buf), "/var/db/pkg/%s/", pkg->CATEGORY);
	if (access(buf, R_OK|W_OK|X_OK) != 0) {
		mkdir("/var", 0755);
		mkdir("/var/db", 0755);
		mkdir("/var/db/pkg/", 0755);
		mkdir(buf, 0755);
	}
	strncat(buf, pkg->PF, sizeof(buf));

	/* FIXME */
	/* not perfect when a version is already installed */
	if (access(buf, X_OK) == 0) {
		char buf2[sizeof(buf)] = "";
		/* we need to compare CONTENTS in it and remove any file not provided by our CONTENTS */
		snprintf(buf2, sizeof(buf2), "busybox rm -rf %s", buf);
		system(buf2);
	}
	interactive_rename("vdb", buf, pkg);
	chdir(port_tmpdir);
}

int pkg_unmerge(char *cat, char *pkgname) {
	char buf[BUFSIZ];
	FILE *fp;
	int argc;
	char **argv;

	if ((strstr(pkgname, " ") != NULL) || (strstr(cat, " ") != NULL)) {
#if 0
		queue *vdb = NULL;
		free_sets(vdb);
		vdb = NULL;

		vdb = get_vdb_atoms(vdb);
		free_sets(vdb);
		vdb = NULL;
#endif
		fprintf(stderr, "%s!!!%s %s/%s (ambiguous name) specify fully-qualified pkgs\n", RED, NORM, cat, pkgname);
		// fprintf(stderr, "%s!!!%s %s %s (ambiguous name) specify fully-qualified pkgs\n", RED, NORM, pkgname);
		return 1;
	}
	printf("%s===%s unmerging %s%s%s/%s%s%s\n", YELLOW, NORM, WHITE, cat, NORM, CYAN, pkgname, NORM);

	snprintf(buf, sizeof(buf), "%s%s/%s/%s/CONTENTS", portroot, portvdb, cat, pkgname);

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

		e = contents_parse_line(buf);
		if (!e) continue;

		protected = config_protected(e->name, argc, argv);
		snprintf(zing, sizeof(zing), "%s%s%s", protected ? YELLOW : GREEN, protected ? "***" : "<<<" , NORM);

		switch (e->type) {
			case CONTENTS_DIR:
				if (!protected)	{ rmdir(e->name); }
				printf("%s %s%s%s/\n", zing, DKBLUE, e->name, NORM);
				break;
			case CONTENTS_OBJ:
				if (!protected)	unlink(e->name);
				printf("%s %s\n", zing, e->name);
				break;
			case CONTENTS_SYM:
				if (protected) break;
				if (e->name[0] != '/') break;
				if (e->sym_target[0] != '/') {
					if (lstat(e->name, &lst) != (-1)) {
						if (S_ISLNK(lst.st_mode)) {
							printf("%s %s%s -> %s%s\n", zing, CYAN, e->name, e->sym_target, NORM);
							unlink(e->name);
							break;
						}
					} else {
						warn("lstat failed for %s -> '%s': %s", e->name, e->sym_target, strerror(errno));
					}
					warn("!!! %s -> %s", e->name, e->sym_target);
					break;
				}
				if (lstat(e->name, &lst) != (-1)) {
					if (S_ISLNK(lst.st_mode)) {
						printf("%s %s%s -> %s%s\n", zing, CYAN, e->name, e->sym_target, NORM);
						unlink(e->name);
					} else {
						warn("%s is not a symlink", e->name);
					}
				} else {
					warn("lstat failed for '%s' -> %s: %s", e->name, e->sym_target, strerror(errno));
				}
				break;
			default:
				fprintf(stderr, "%s???%s %s%s%s (%d)\n", RED, NORM, WHITE, e->name, NORM, e->type);
				break;
		}
	}

	fclose(fp);

	freeargv(argc, argv);

	snprintf(buf, sizeof(buf), "busybox rm -rf %s/%s/%s/%s", portroot, portvdb, cat, pkgname);
	system(buf);

	return 1;
}

int unlink_empty(char *buf) {
	struct stat st;
	if ((stat(buf, &st)) != (-1))
		if (st.st_size == 0)
			return unlink(buf);
	return (-1);
}



int match_pkg(char *name, struct pkg_t *pkg) {
	depend_atom *atom;
	char buf[255], buf2[255];
	int match = 0;

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

	atom_implode(atom);

	return match;
}

int pkg_verify_checksums(char *buf, struct pkg_t *pkg, depend_atom *atom) {
	char *hash;
	int ret = 0;

	if (pkg->MD5[0]) {
		hash = (char*) hash_file(buf, HASH_MD5);
		if (strcmp(hash, pkg->MD5) == 0) {
			printf("MD5:  [%sOK%s] %s %s/%s\n", GREEN, NORM, hash, atom->CATEGORY, pkg->PF);
		} else {
			warn("MD5:  [%sER%s] (%s) != (%s) %s/%s\n", RED, NORM, hash, pkg->MD5, atom->CATEGORY, pkg->PF);
			ret++;
		}
	}

	if (pkg->SHA1[0]) {
		hash = (char*) hash_file(buf, HASH_SHA1);
		if (strcmp(hash, pkg->SHA1) == 0) {
			printf("SHA1: [%sOK%s] %s %s/%s\n", GREEN, NORM, hash, atom->CATEGORY, pkg->PF);
		} else {
			warn("SHA1: [%sER%s] (%s) != (%s) %s/%s\n", RED, NORM, hash, pkg->MD5, atom->CATEGORY, pkg->PF);
			ret++;
		}
	}

	if (!pkg->SHA1[0] && !pkg->MD5[0])
		return 1;

	if ((strstr("strict", features) == 0) && ret)
		errf("strict is set in features");

	return ret;
}

void pkg_fetch(int argc, char **argv, struct pkg_t *pkg) {
	depend_atom *atom;
	char buf[255], buf2[255];
	int i;

	snprintf(buf, sizeof(buf), "%s/%s", pkg->CATEGORY, pkg->PF);
	if ((atom = atom_explode(buf)) == NULL)
		errf("%s/%s is not a valid atom", pkg->CATEGORY, pkg->PF);

	snprintf(buf2, sizeof(buf2), "%s/%s", pkg->CATEGORY, atom->PN);

	for (i = 1; i < argc; i++) {

		if (argv[i][0] == '-')
			continue;

		/* verify this is the requested package */
		if (match_pkg(argv[i], pkg) < 1)
			continue;

		/* check to see if file exists and it's checksum matches */
		snprintf(buf, sizeof(buf), "%s/%s.tbz2", pkgdir, pkg->PF);
		unlink_empty(buf);
		if (force_download) unlink(buf);

		if (access(buf, R_OK) == 0) {
			if ((!pkg->SHA1[0]) && (!pkg->MD5[0])) {
				warn("No checksum data for %s", buf);
				continue;
			} else {
				if ((pkg_verify_checksums(buf, pkg, atom)) == 0) {
					pkg_merge(atom, pkg);
					continue;
				}
			}
		}
		if (verbose)
			printf("Fetching %s/%s.tbz2\n", atom->CATEGORY, pkg->PF);

		/* fetch the package */
		snprintf(buf, sizeof(buf), "%s.tbz2", pkg->PF);
		fetch(pkgdir, buf);

		/* verify the pkg exists now. unlink if zero bytes */
		snprintf(buf, sizeof(buf), "%s/%s.tbz2", pkgdir, pkg->PF);
		unlink_empty(buf);

		if (access(buf, R_OK) != 0) {
			warn("Failed to fetch %s.tbz2 from %s", pkg->PF, binhost);
			fflush(stderr);
			continue;
		}

		if ((pkg_verify_checksums(buf, pkg, atom)) == 0) {
			pkg_merge(atom, pkg);
			continue;
		}
	}
	/* free the atom */
	atom_implode(atom);
}

void print_Pkg(int full, struct pkg_t *pkg) {
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

	snprintf(buf, sizeof(buf), "%s/%s", pkg->CATEGORY, pkg->PF);
	atom = atom_explode(buf);
	if ((atom = atom_explode(buf)) == NULL)
		return;
	if ((p = best_version(pkg->CATEGORY, atom->PN)) != NULL)
		if (*p) printf(" %sInstalled%s:%s %s\n", DKGREEN, YELLOW, NORM, p);
	atom_implode(atom);
}

int unmerge_packages(int argc, char **argv) {
	depend_atom *atom;
	char *p;
	int i;

	if (argc == optind)
		return 1;

	for ( i = 1 ; i < argc; i++) {
		char buf[512];

		if (argv[i][0] == '-')
			continue;

		p = best_version(NULL, argv[i]);
		if (!*p) continue;
		if ((atom = atom_explode(p)) == NULL) continue;

		snprintf(buf, sizeof(buf), "%s-%s", atom->PN, atom->PV);
		if (atom->PR_int)
			snprintf(buf, sizeof(buf), "%s-%s-r%i", atom->PN, atom->PV, atom->PR_int);
		pkg_unmerge(atom->CATEGORY, buf);
		atom_implode(atom);
	}
	return 0;
}

int parse_packages(const char *Packages, int argc, char **argv) {
	FILE *fp;
	char buf[BUFSIZ];
	char value[BUFSIZ];
	char *p;
	int i;
	long lineno = 0;

	if ((fp = fopen(Packages, "r")) == NULL)
		err("Unable to open package file %s: %s", Packages, strerror(errno));

	memset(&Pkg, 0, sizeof(Pkg));

	while((fgets(buf, sizeof(buf), fp)) != NULL) {
		lineno++;
		if (*buf == '\n') {
			if ((strlen(Pkg.PF) > 0) && (strlen(Pkg.CATEGORY) > 0)) {
				struct pkg_t *pkg = xmalloc(sizeof(struct pkg_t));
				memcpy(pkg, &Pkg, sizeof(struct pkg_t));
				if (search_pkgs) {
					if (argc != optind) {
						for ( i = 0 ; i < argc; i++)
							if ((match_pkg(argv[i], pkg) > 0) || (strcmp(argv[i], pkg->CATEGORY) == 0))
								print_Pkg(verbose, pkg);
					} else {
						print_Pkg(verbose, pkg);
					}
				} else {
					/* this name is misleading */
					pkg_fetch(argc, argv, pkg);
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

		switch(*buf) {
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
				if ((strcmp(buf, "RDEPEND:")) == 0) strncpy(Pkg.RDEPEND, value, sizeof(Pkg.RDEPEND));
				break;
			case 'L':
				if ((strcmp(buf, "LICENSE:")) == 0) strncpy(Pkg.LICENSE, value, sizeof(Pkg.LICENSE));
				break;
			case 'C':
				if ((strcmp(buf, "CATEGORY:")) == 0) strncpy(Pkg.CATEGORY, value, sizeof(Pkg.CATEGORY));
				break;
			case 'D':
				if ((strcmp(buf, "DESC:")) == 0) strncpy(Pkg.DESC, value, sizeof(Pkg.DESC));
				break;
			default:
				break;
		}
	}
	fclose(fp);
	return 0;
}


int qmerge_main(int argc, char **argv) {
	int i;
	const char *Packages = "Packages";
	if (argc < 2)
		qmerge_usage(EXIT_FAILURE);

	while ((i = GETOPT_LONG(QMERGE, qmerge, "")) != -1) {
		switch (i) {
			case 'f': force_download = 1; break;
			case 's': search_pkgs = 1; break;
			case 'i': install = 1; break;
			case 'U': uninstall = 1; break;
			case 'p': search_pkgs = pretend = 1; break;
			case 'y': interactive = 0; break;
			COMMON_GETOPTS_CASES(qmerge)
		}
	}
	if (uninstall)
		return unmerge_packages(argc, argv);

	qmerge_initialize(Packages);
	return parse_packages(Packages, argc, argv);
}

#else /* ! APPLET_qmerge */
int qmerge_main(int argc, char **argv) {
	errf("%s%s%s", err_noapplet, DKGREEN, NORM);
}
#endif /* APPLET_qmerge */

