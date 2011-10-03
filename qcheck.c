/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qcheck.c,v 1.51 2011/10/03 16:18:25 vapier Exp $
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2010 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qcheck

#define QCHECK_FLAGS "aes:uABHTp" COMMON_FLAGS
static struct option const qcheck_long_opts[] = {
	{"all",     no_argument, NULL, 'a'},
	{"exact",   no_argument, NULL, 'e'},
	{"skip",     a_argument, NULL, 's'},
	{"update",  no_argument, NULL, 'u'},
	{"noafk",   no_argument, NULL, 'A'},
	{"badonly", no_argument, NULL, 'B'},
	{"nohash",  no_argument, NULL, 'H'},
	{"nomtime", no_argument, NULL, 'T'},
	{"prelink", no_argument, NULL, 'p'},
	COMMON_LONG_OPTS
};
static const char * const qcheck_opts_help[] = {
	"List all packages",
	"Exact match (only CAT/PN or PN without PV)",
	"Ignore files matching the regular expression <arg>",
	"Update missing files, chksum and mtimes for packages",
	"Ignore missing files",
	"Only print pkgs containing bad files",
	"Ignore differing/unknown file chksums",
	"Ignore differing file mtimes",
	"Undo prelink when calculating checksums",
	COMMON_OPTS_HELP
};
static const char qcheck_rcsid[] = "$Id: qcheck.c,v 1.51 2011/10/03 16:18:25 vapier Exp $";
#define qcheck_usage(ret) usage(ret, QCHECK_FLAGS, qcheck_long_opts, qcheck_opts_help, lookup_applet_idx("qcheck"))

static bool bad_only = false;
#define qcprintf(fmt, args...) if (!bad_only) printf(_(fmt), ## args)

static int qcheck_process_contents(int portroot_fd, int pkg_fd,
	const char *catname, const char *pkgname, array_t *regex_arr,
	bool qc_update, bool chk_afk, bool chk_hash, bool chk_mtime,
	bool undo_prelink)
{
	int fd;
	FILE *fp, *fpx;
	size_t num_files, num_files_ok, num_files_unknown, num_files_ignored;
	char *buffer, *line;
	size_t linelen;
	struct stat st, cst;

	fpx = NULL;

	fd = openat(pkg_fd, "CONTENTS", O_RDONLY|O_CLOEXEC);
	if (fd == -1)
		return EXIT_SUCCESS;
	if (fstat(fd, &cst)) {
		close(fd);
		return EXIT_SUCCESS;
	}
	if ((fp = fdopen(fd, "r")) == NULL) {
		close(fd);
		return EXIT_SUCCESS;
	}

	num_files = num_files_ok = num_files_unknown = num_files_ignored = 0;
	qcprintf("%sing %s%s/%s%s ...\n",
		(qc_update ? "Updat" : "Check"),
		GREEN, catname, pkgname, NORM);
	if (qc_update) {
		fd = openat(pkg_fd, "CONTENTS~", O_RDWR|O_CLOEXEC|O_CREAT|O_TRUNC, 0644);
		if (fd == -1 || (fpx = fdopen(fd, "w")) == NULL) {
			fclose(fp);
			warnp("unable to fopen(%s/%s, w)", pkgname, "CONTENTS~");
			return EXIT_FAILURE;
		}
	}

	buffer = line = NULL;
	while (getline(&line, &linelen, fp) != -1) {
		contents_entry *e;
		free(buffer);
		buffer = xstrdup(line);
		e = contents_parse_line(line);
		if (!e)
			continue;

		/* run our little checks */
		++num_files;
		if (array_cnt(regex_arr)) {
			size_t n;
			regex_t *regex;
			array_for_each(regex_arr, n, regex)
				if (!regexec(regex, e->name, 0, NULL, 0))
					break;
			if (n < array_cnt(regex_arr)) {
				--num_files;
				++num_files_ignored;
				continue;
			}
		}
		if (fstatat(portroot_fd, e->name + 1, &st, AT_SYMLINK_NOFOLLOW)) {
			/* make sure file exists */
			if (chk_afk) {
				qcprintf(" %sAFK%s: %s\n", RED, NORM, e->name);
			} else {
				--num_files;
				++num_files_ignored;
				if (qc_update)
					fputs(buffer, fpx);
			}
			continue;
		}
		if (e->digest && S_ISREG(st.st_mode)) {
			/* validate digest (handles MD5 / SHA1) */
			uint8_t hash_algo;
			char *hashed_file;
			hash_cb_t hash_cb = undo_prelink ? hash_cb_prelink_undo : hash_cb_default;
			switch (strlen(e->digest)) {
				case 32: hash_algo = HASH_MD5; break;
				case 40: hash_algo = HASH_SHA1; break;
				default: hash_algo = 0; break;
			}
			if (!hash_algo) {
				if (chk_hash) {
					qcprintf(" %sUNKNOWN DIGEST%s: '%s' for '%s'\n", RED, NORM, e->digest, e->name);
					++num_files_unknown;
				} else {
					--num_files;
					++num_files_ignored;
					if (qc_update)
						fputs(buffer, fpx);
				}
				continue;
			}
			hashed_file = (char*)hash_file_cb(e->name, hash_algo, hash_cb);
			if (!hashed_file) {
				++num_files_unknown;
				free(hashed_file);
				if (qc_update) {
					fputs(buffer, fpx);
					if (!verbose)
						continue;
				}
				qcprintf(" %sPERM %4o%s: %s\n", RED, (unsigned int)(st.st_mode & 07777), NORM, e->name);
				continue;
			} else if (strcmp(e->digest, hashed_file)) {
				if (chk_hash) {
					const char *digest_disp;
					if (qc_update)
						fprintf(fpx, "obj %s %s %lu\n", e->name, hashed_file, st.st_mtime);
					switch (hash_algo) {
						case HASH_MD5:  digest_disp = "MD5"; break;
						case HASH_SHA1: digest_disp = "SHA1"; break;
						default:        digest_disp = "UNK"; break;
					}
					qcprintf(" %s%s-DIGEST%s: %s", RED, digest_disp, NORM, e->name);
					if (verbose)
						qcprintf(" (recorded '%s' != actual '%s')", e->digest, hashed_file);
					qcprintf("\n");
				} else {
					--num_files;
					++num_files_ignored;
					if (qc_update)
						fputs(buffer, fpx);
				}
				free(hashed_file);
				continue;
			} else if (e->mtime && e->mtime != st.st_mtime) {
				if (chk_mtime) {
					qcprintf(" %sMTIME%s: %s", RED, NORM, e->name);
					if (verbose)
						qcprintf(" (recorded '%lu' != actual '%lu')", e->mtime, (unsigned long)st.st_mtime);
					qcprintf("\n");

					/* This can only be an obj, dir and sym have no digest */
					if (qc_update)
						fprintf(fpx, "obj %s %s %lu\n", e->name, e->digest, st.st_mtime);
				} else {
					--num_files;
					++num_files_ignored;
					if (qc_update)
						fputs(buffer, fpx);
				}
				free(hashed_file);
				continue;
			} else {
				if (qc_update)
					fputs(buffer, fpx);
				free(hashed_file);
			}
		} else if (e->mtime && e->mtime != st.st_mtime) {
			if (chk_mtime) {
				qcprintf(" %sMTIME%s: %s", RED, NORM, e->name);
				if (verbose)
					qcprintf(" (recorded '%lu' != actual '%lu')", e->mtime, (unsigned long)st.st_mtime);
				qcprintf("\n");

				/* This can only be a sym */
				if (qc_update)
					fprintf(fpx, "sym %s -> %s %lu\n", e->name, e->sym_target, st.st_mtime);
			} else {
				--num_files;
				++num_files_ignored;
				if (qc_update)
					fputs(buffer, fpx);
			}
			continue;
		} else {
			if (qc_update)
				fputs(buffer, fpx);
		}
		++num_files_ok;
	}
	free(line);
	free(buffer);
	fclose(fp);

	if (qc_update) {
		if (fchown(fd, cst.st_uid, cst.st_gid))
			/* meh */;
		if (fchmod(fd, cst.st_mode))
			/* meh */;
		fclose(fpx);
		if (renameat(pkg_fd, "CONTENTS~", pkg_fd, "CONTENTS"))
			unlinkat(pkg_fd, "CONTENTS~", 0);
		if (!verbose)
			return EXIT_SUCCESS;
	}
	if (bad_only && num_files_ok != num_files) {
		if (verbose)
			printf("%s/%s\n", catname, pkgname);
		else {
			depend_atom *atom = NULL;
			char *buf;
			xasprintf(&buf, "%s/%s", catname, pkgname);
			if ((atom = atom_explode(buf)) != NULL) {
				printf("%s/%s\n", catname, atom->PN);
				atom_implode(atom);
			} else  {
				printf("%s/%s\n", catname, pkgname);
			}
			free(buf);
		}
	}
	qcprintf("  %2$s*%1$s %3$s%4$zu%1$s out of %3$s%5$zu%1$s file%6$s are good",
		NORM, BOLD, BLUE, num_files_ok, num_files,
		(num_files > 1 ? "s" : ""));
	if (num_files_unknown)
		qcprintf(" (Unable to digest %2$s%3$zu%1$s file%4$s)",
			NORM, BLUE, num_files_unknown,
			(num_files_unknown > 1 ? "s" : ""));
	if (num_files_ignored)
		qcprintf(" (%2$s%3$zu%1$s file%4$s ignored)",
			NORM, BLUE, num_files_ignored,
			(num_files_ignored > 1 ? "s were" : " was"));
	qcprintf("\n");

	if (num_files_ok != num_files)
		return EXIT_FAILURE;
	else
		return EXIT_SUCCESS;
}

int qcheck_main(int argc, char **argv)
{
	DIR *dir, *dirp;
	int i, ret;
	int portroot_fd, vdb_fd, cat_fd, pkg_fd;
	struct dirent *dentry, *de;
	bool search_all = 0;
	bool qc_update = false;
	bool chk_afk = true;
	bool chk_hash = true;
	bool chk_mtime = true;
	bool undo_prelink = false;
	DECLARE_ARRAY(regex_arr);

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QCHECK, qcheck, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qcheck)
		case 'a': search_all = true; break;
		case 'e': exact = 1; break;
		case 's': {
			regex_t regex;
			xregcomp(&regex, optarg, REG_EXTENDED|REG_NOSUB);
			xarraypush(regex_arr, &regex, sizeof(regex));
			break;
		}
		case 'u': qc_update = true; break;
		case 'A': chk_afk = false; break;
		case 'B': bad_only = true; break;
		case 'H': chk_hash = false; break;
		case 'T': chk_mtime = false; break;
		case 'p': undo_prelink = prelink_available(); break;
		}
	}
	if ((argc == optind) && !search_all)
		qcheck_usage(EXIT_FAILURE);

	portroot_fd = open(portroot, O_RDONLY|O_CLOEXEC);
	if (portroot_fd == -1)
		errp("unable to read %s !?", portroot);
	vdb_fd = openat(portroot_fd, portvdb + 1, O_RDONLY|O_CLOEXEC);
	if (vdb_fd == -1 || (dir = fdopendir(vdb_fd)) == NULL)
		errp("unable to read %s !?", portvdb);

	ret = EXIT_SUCCESS;

	/* open /var/db/pkg */
	while ((dentry = q_vdb_get_next_dir(dir))) {
		cat_fd = openat(vdb_fd, dentry->d_name, O_RDONLY|O_CLOEXEC);
		if (cat_fd == -1)
			continue;
		if ((dirp = fdopendir(cat_fd)) == NULL) {
			close(cat_fd);
			continue;
		}

		/* open the cateogry */
		while ((de = readdir(dirp)) != NULL) {
			if (*de->d_name == '.')
				continue;

			/* see if this cat/pkg is requested */
			if (!search_all) {
				char *buf = NULL;
				for (i = optind; i < argc; ++i) {
					free(buf);
					xasprintf(&buf, "%s/%s", dentry->d_name, de->d_name);
					if (!exact) {
						if (rematch(argv[i], buf, REG_EXTENDED) == 0)
							break;
						if (rematch(argv[i], de->d_name, REG_EXTENDED) == 0)
							break;
					} else {
						depend_atom *atom;
						char swap[_Q_PATH_MAX];
						if ((atom = atom_explode(buf)) == NULL) {
							warn("invalid atom %s", buf);
							continue;
						}
						snprintf(swap, sizeof(swap), "%s/%s", atom->CATEGORY, atom->PN);
						atom_implode(atom);
						if ((strcmp(argv[i], swap) == 0) || (strcmp(argv[i], buf) == 0))
							break;
						if ((strcmp(argv[i], strstr(swap, "/") + 1) == 0) || (strcmp(argv[i], strstr(buf, "/") + 1) == 0))
							break;
					}
				}
				free(buf);
				if (i == argc)
					continue;
			}

			pkg_fd = openat(cat_fd, de->d_name, O_RDONLY|O_CLOEXEC);
			if (pkg_fd == -1)
				continue;

			ret = qcheck_process_contents(portroot_fd, pkg_fd,
				dentry->d_name, de->d_name, regex_arr,
				qc_update, chk_afk, chk_hash, chk_mtime, undo_prelink);
			close(pkg_fd);
		}
		closedir(dirp);
	}

	xarrayfree(regex_arr);
	close(portroot_fd);
	return ret;
}

#else
DEFINE_APPLET_STUB(qcheck)
#endif
