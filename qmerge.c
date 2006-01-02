#include <stdio.h>
#include <stdlib.h>

static const char *argv0 = "qmerge";
// static const char *argv0 = "";

#include "libq/libq.c"

char binhost[512] = "";		// "ftp://tinderbox.x86.dev.gentoo.org/default-linux/x86/2005.1/All";
char pkgdir[512] = "";		// /usr/portage/packages/
char port_tmpdir[512] = "/var/tmp/portage/portage-pkg/";
char verbose = 0;
char quiet = 0;
char merge = 1;
char pretend = 0;

struct pkg_t {
	char PF[64];
	char CATEGORY[64];
	char LICENSE[64];
	char RDEPEND[BUFSIZ];
	char MD5[34];
	char SLOT[64];
	size_t SIZE;
	char USE[BUFSIZ];
} Pkg;

int interactive_rename(char *src, char *dst) {
	struct stat st;
	int ret = 0;

	char *ARGV[] = { "/bin/busybox", "mv", "-i", src, dst , NULL };

	if (stat(dst, &st) != (-1))
		warn("%s exists\n", dst);

	printf(">>> %s %d\n", dst, ret);

	ret = execv(ARGV[0], ARGV );
	
	return ret;
}

void fetch(char *destdir, char *src) {
	char buf[BUFSIZ];
	snprintf(buf, sizeof(buf), "/bin/busybox wget %s -P %s %s/%s", (quiet ? "-q" : ""), destdir, binhost, src);
	system(buf);
}

void qmerge_initialize(char *Packages) {
	FILE *fp;
	char buf[BUFSIZ];
	char *p;

	if ((fp = fopen("/etc/make.conf", "r")) == NULL)
		exit(1);

	while((fgets(buf, sizeof(buf), fp)) != NULL) {
		int i = 0;
		if ((p = strchr(buf, '\n')) != NULL)
			*p = 0;
		if (*buf != 'P') continue;
		for (i = 0; i < strlen(buf); i++) {
			if ((buf[i] == '\"') || (buf[i] == '\'')) {
				buf[i] = ' ';
				rmspace(&buf[i]);
			}
		}
		if ((strncmp(buf, "PORTAGE_BINHOST=", 16)) == 0) {
			if (strchr(buf, '$') != NULL)
				errf("bash variables can not be used for PORAGE_BINHOST= setting");
			strncpy(binhost, &buf[16], sizeof(binhost));
		}
		if ((strncmp(buf, "PKGDIR=", 7)) == 0) {
			if (strchr(buf, '$') != NULL)
				errf("bash variables can not be used for PKGDIR= setting");
			strncpy(pkgdir, &buf[7], sizeof(pkgdir));
			strncat(pkgdir, "/All", sizeof(pkgdir));
		}
	}
	fclose(fp);

	if ((access(pkgdir, R_OK|W_OK|X_OK)) != 0)
		errf("Fatal errors with %s", pkgdir);

	mkdir(port_tmpdir, 0755);

	if (chdir(port_tmpdir) != 0) errf("!!! chdir(port_tmpdir)");
	if (access(Packages, R_OK) != 0)
		fetch("./", Packages);
}

char *best_version(depend_atom *atom) {
	static char buf[1024];
	FILE *fp;
	char *p;

	snprintf(buf, sizeof(buf), "qlist -CIev %s/%s", atom->CATEGORY, atom->PN);
	if ((fp = popen(buf, "r")) == NULL)
		return NULL;

	buf[0] = '\0';
	if ((fgets(buf, sizeof(buf), fp)) != NULL)
		if ((p = strchr(buf, '\n')) != NULL)
			*p = 0;
	pclose(fp);
	return (char *) buf;
}

void pkg_merge(depend_atom *atom) {
	FILE *fp, *contents;
	struct dirent **namelist;
	char buf[1024];
	char tarball[255];
	char *p;

	if (!merge) return;

	if (chdir(port_tmpdir) != 0) errf("!!! chdir(port_tmpdir)");

	mkdir(Pkg.PF, 0710);
	if (chdir(Pkg.PF) != 0) errf("!!! chdir(%s)", Pkg.PF);

	
	/* split the tbz and xpak data */
	snprintf(tarball, sizeof(tarball), "%s.tbz2", Pkg.PF);
	snprintf(buf, sizeof(buf), "%s/%s", pkgdir, tarball);
	unlink(tarball);
	symlink(buf, tarball);
	snprintf(buf, sizeof(buf), "q tbz2 -s %s", tarball);
	system(buf);

	mkdir("vdb", 0755);
	mkdir("image", 0755);

	/* list and extract vdb files from the xpak */
	snprintf(buf, sizeof(buf), "q xpak -d %s/%s/vdb -x %s.xpak `q xpak -l %s.xpak`", 
		port_tmpdir, Pkg.PF, Pkg.PF, Pkg.PF);
	system(buf);

	/* extrct the binary package data */
	snprintf(buf, sizeof(buf), "/bin/busybox tar -jx%sf %s.tar.bz2 -C image/", ((verbose > 1) ? "v" : ""), Pkg.PF);
	system(buf);
	fflush(stdout);
	/* check for an already install pkg */
	snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, Pkg.PF);
	p = best_version(atom);
	if (*p) {
		if ((strcmp(p, buf)) == 0) {
			if (verbose) fprintf(stderr, "%s already installed\n", buf);
		} else {
			if (verbose) printf("local: %s remote %s\n", p, buf);
		}
	} else {
		if (verbose) printf("Installing %s\n", buf);
		
	}

	if ((contents = fopen("vdb/CONTENTS", "w")) == NULL)
		errf("come on wtf?");

	chdir("image");
	if ((fp = popen("/bin/busybox find .", "r")) == NULL)
		errf("come on wtf!");

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		struct stat st;
		char line[BUFSIZ];

		if ((p = strrchr(buf, '\n')) != NULL)
			*p = 0;
		if (buf[0] != '.')
			continue;

		if (((strcmp(buf, ".")) == 0) || ((strcmp(buf, "..")) == 0))
			continue;

		/* use lstats for symlinks */
		lstat(buf, &st);

		line[0] = 0;

		if (pretend) continue;

		if (S_ISDIR(st.st_mode)) {
			mkdir(&buf[1], st.st_mode);
			chown(&buf[1], st.st_uid, st.st_gid);
			snprintf(line, sizeof(line), "dir %s", &buf[1]);
		}
		if (S_ISREG(st.st_mode)) {
			struct timeval tv;
			char *hash;

			hash = hash_file(buf, HASH_MD5);

			snprintf(line, sizeof(line), "obj %s %s %lu", &buf[1], hash, st.st_mtime);

			if ((strncmp("/etc/", &buf[1], 5)) == 0) {
				if ((access(&buf[1], R_OK)) == 0) {
					printf("CFG: %s\n", &buf[1]);
					continue;
				}
			}

			if (interactive_rename(buf, &buf[1]) != 0)
				continue;

			chmod(&buf[1], st.st_mode);
			chown(&buf[1], st.st_uid, st.st_gid);

			tv.tv_sec = st.st_mtime;
			tv.tv_usec = st.st_mtime;
			// utimes(&buf[1], &tv);

		}
		if (S_ISLNK(st.st_mode)) {
			char path[sizeof(buf)];

			/* symlinks are unfinished */
			readlink(buf, path, sizeof(path));
			snprintf(line, sizeof(line), "sym %s -> %s", &buf[1], path);
			warnf("%s", line);
		}
		/* Save the line to the contents file */
		if (*line) fprintf(contents, "%s\n", line);
	}

	fclose(contents);
	fclose(fp);

	chdir(port_tmpdir);
	chdir(Pkg.PF);

	if (!pretend) {
		snprintf(buf, sizeof(buf), "/var/db/pkg/%s/", Pkg.CATEGORY);
		if (access(buf, R_OK|W_OK|X_OK) != 0) {
			mkdir("/var", 0755);
			mkdir("/var/db", 0755);
			mkdir("/var/db/pkg/", 0755);
			mkdir(buf, 0755);
		}
		strncat(buf, Pkg.PF, sizeof(buf));
		interactive_rename("vdb", buf);
	}
	chdir(port_tmpdir);
}

int unlink_empty(char *buf) {
	struct stat st;
	if ((stat(buf, &st)) != (-1))
		if (st.st_size == 0)
			return unlink(buf);
	return (-1);
}

void pkg_fetch(int argc, char **argv) {
	depend_atom *atom;
	char buf[255];
	char *hash;
	int i;

	int installed = 0;

	snprintf(buf, sizeof(buf), "%s/%s", Pkg.CATEGORY, Pkg.PF);
	if ((atom = atom_explode(buf)) == NULL)
		errf("%s is not a valud atom");

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-')
			continue;

		/* verify this is the requested package */
		if ((strcmp(argv[i], atom->PN)) != 0)
			continue;

		/* check to see if file exists and it's checksum matches */
		snprintf(buf, sizeof(buf), "%s/%s.tbz2", pkgdir, Pkg.PF);
		unlink_empty(buf);

		if (access(buf, R_OK) == 0) {
			hash = (char*) hash_file(buf, HASH_MD5);
			if (strcmp(hash, Pkg.MD5) == 0) {
				if (!quiet) printf("MD5: [%sOK%s] %s %s/%s\n", GREEN, NORM, hash, atom->CATEGORY, Pkg.PF);
				/* attempt to merge it */
				pkg_merge(atom);
				continue;
			}
		}
		if (!quiet)
			printf("Fetching %s/%s.tbz2\n", atom->CATEGORY, Pkg.PF);

		fflush(stdout);
		fflush(stderr);

		/* fetch the package */
		snprintf(buf, sizeof(buf), "%s.tbz2", Pkg.PF);
		fetch(pkgdir, buf);

		fflush(stdout);
		fflush(stderr);

		/* verify the pkg exists now. unlink if zero bytes */
		snprintf(buf, sizeof(buf), "%s/%s.tbz2", pkgdir, Pkg.PF);
		unlink_empty(buf);

		if (access(buf, R_OK) != 0) {
			warn("Failed to fetch %s.tbz2 from %s", Pkg.PF, binhost);
			fflush(stderr);
			continue;
		}

		/* verify it's checksum */
		hash = (char*) hash_file(buf, HASH_MD5);
		if ((strcmp(hash, Pkg.MD5)) != 0) {
			printf("MD5: [%sER%s] %s != %s %s/%s\n", RED, NORM, hash, Pkg.MD5, atom->CATEGORY, Pkg.PF);
			continue;
		}

		if (!quiet) printf("MD5: [%sOK%s] %s %s/%s\n", GREEN, NORM, hash, atom->CATEGORY, Pkg.PF);
		fflush(stdout);
		/* attempt to merge it */
		pkg_merge(atom);
	}
	/* free the atom */
	atom_implode(atom);
}

void parse_packages(char *Packages, int argc, char **argv) {
	FILE *fp;
	char buf[BUFSIZ];
	char value[BUFSIZ];
	char *p;
	long lineno = 0;

	if ((fp = fopen(Packages, "r")) == NULL)
		exit(1);

	memset(&Pkg, 0, sizeof(Pkg));

	while((fgets(buf, sizeof(buf), fp)) != NULL) {
		lineno++;
		if (*buf == '\n') {
			if (strlen(Pkg.PF) > 0)
				pkg_fetch(argc, argv);
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
		// printf("%s=%s\n", buf, value);

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
			default:
				fprintf(stderr, "Unhandled parms %s\n", buf);
				break;
		}
	}
	fclose(fp);
}

void usage() {
	warn("qmerge <pkg> <pkg>...");
	exit(1);
}

int qmerge_main(int argc, char **argv) {
	char *Packages = "Packages";
	int i;
	if (argc < 2)
		usage();
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') 
			continue;
		if ((strcmp(argv[i], "-v") == 0) || (strcmp(argv[i], "--verbose") == 0))
			verbose++;
		if ((strcmp(argv[i], "-q") == 0) || (strcmp(argv[i], "--quiet") == 0))
			quiet = 1;
		if ((strcmp(argv[i], "-f") == 0) || (strcmp(argv[i], "--fetch") == 0))
			merge = 0;
		if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0))
			usage();
		if ((strcmp(argv[i], "-p") == 0) || (strcmp(argv[i], "--pretend") == 0))
			pretend = 1;
	}
	qmerge_initialize(Packages);
	parse_packages(Packages, argc, argv);
	return 0;
}

int main(int argc, char **argv) {
	return qmerge_main(argc, argv);
}

#if 0
      struct stat {
             dev_t     st_dev;     /* ID of device containing file */
             ino_t     st_ino;     /* inode number */
             mode_t    st_mode;    /* protection */
             nlink_t   st_nlink;   /* number of hard links */
             uid_t     st_uid;     /* user ID of owner */
             gid_t     st_gid;     /* group ID of owner */
             dev_t     st_rdev;    /* device ID (if special file) */
             off_t     st_size;    /* total size, in bytes */
             blksize_t st_blksize; /* blocksize for filesystem I/O */
             blkcnt_t  st_blocks;  /* number of blocks allocated */
             time_t    st_atime;   /* time of last access */
             time_t    st_mtime;   /* time of last modification */
             time_t    st_ctime;   /* time of last status change */
         };
#define S_ISDIR(mode)    __S_ISTYPE((mode), __S_IFDIR)
#define S_ISCHR(mode)    __S_ISTYPE((mode), __S_IFCHR)
#define S_ISBLK(mode)    __S_ISTYPE((mode), __S_IFBLK)
#define S_ISREG(mode)    __S_ISTYPE((mode), __S_IFREG)
#ifdef __S_IFIFO
# define S_ISFIFO(mode)  __S_ISTYPE((mode), __S_IFIFO)
#endif
#ifdef __S_IFLNK
# define S_ISLNK(mode)   __S_ISTYPE((mode), __S_IFLNK)
#endif

#if defined __USE_BSD && !defined __S_IFLNK
# define S_ISLNK(mode)  0
#endif

#endif /* if 0 */
