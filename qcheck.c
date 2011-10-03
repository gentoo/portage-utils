/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qcheck.c,v 1.49 2011/10/03 01:25:54 vapier Exp $
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
static const char qcheck_rcsid[] = "$Id: qcheck.c,v 1.49 2011/10/03 01:25:54 vapier Exp $";
#define qcheck_usage(ret) usage(ret, QCHECK_FLAGS, qcheck_long_opts, qcheck_opts_help, lookup_applet_idx("qcheck"))

short bad_only = 0;
#define qcprintf(fmt, args...) if (!bad_only) printf( _( fmt ), ## args)

static void qcheck_cleanup(regex_t **regex_head, const size_t regex_count)
{
	size_t i;

	if (regex_head == NULL)
		return;

	for (i = 0; i < regex_count; ++i) {
		regfree(regex_head[i]);
		free(regex_head[i]);
	}
	free(regex_head);
}

int qcheck_main(int argc, char **argv)
{
	DIR *dir, *dirp;
	int i, ret;
	struct dirent *dentry, *de;
	char search_all = 0;
	char qc_update = 0;
	char chk_afk = 1;
	char chk_hash = 1;
	char chk_mtime = 1;
	bool undo_prelink = false;
	struct stat st;
	size_t num_files, num_files_ok, num_files_unknown, num_files_ignored;
	char buf[_Q_PATH_MAX], filename[_Q_PATH_MAX];
	char buffer[_Q_PATH_MAX];
	regex_t **regex_head = NULL;
	size_t regex_count = 0;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QCHECK, qcheck, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qcheck)
		case 'a': search_all = 1; break;
		case 'e': exact = 1; break;
		case 's': {
				regex_head = xrealloc(regex_head, (regex_count + 1) * sizeof(*regex_head));
				regex_head[regex_count] = xmalloc(sizeof(*regex_head[0]));
				xregcomp(regex_head[regex_count], optarg, REG_EXTENDED|REG_NOSUB);
				++regex_count;
			}
			break;
		case 'u': qc_update = 1; break;
		case 'A': chk_afk = 0; break;
		case 'B': bad_only = 1; break;
		case 'H': chk_hash = 0; break;
		case 'T': chk_mtime = 0; break;
		case 'p': undo_prelink = prelink_available(); break;
		}
	}
	if ((argc == optind) && !search_all)
		qcheck_usage(EXIT_FAILURE);

	snprintf(buf, sizeof(buf), "%s/%s", portroot, portvdb);
	xchdir(buf);
	if ((dir = opendir(".")) == NULL)
		errp("unable to read '.' !?");

	ret = EXIT_SUCCESS;

	/* open /var/db/pkg */
	while ((dentry = q_vdb_get_next_dir(dir))) {
		if (chdir(dentry->d_name) != 0)
			continue;
		if ((dirp = opendir(".")) == NULL)
			continue;

		/* open the cateogry */
		while ((de = readdir(dirp)) != NULL) {
			FILE *fp, *fpx;
			if (*de->d_name == '.')
				continue;
			fp = fpx = NULL;

			/* see if this cat/pkg is requested */
			if (!search_all) {
				for (i = optind; i < argc; ++i) {
					snprintf(buf, sizeof(buf), "%s/%s", dentry->d_name, de->d_name);
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
				if (i == argc)
					continue;
			}

			snprintf(buf, sizeof(buf), "%s%s/%s/%s/CONTENTS", portroot, portvdb, dentry->d_name, de->d_name);
			if ((fp = fopen(buf, "re")) == NULL)
				continue;
			strncat(buf, "~", sizeof(buf)-strlen(buf)-1);
			num_files = num_files_ok = num_files_unknown = num_files_ignored = 0;
			qcprintf("%sing %s%s/%s%s ...\n",
				(qc_update ? "Updat" : "Check"),
				GREEN, dentry->d_name, de->d_name, NORM);
			if (qc_update) {
				if ((fpx = fopen(buf, "we")) == NULL) {
					fclose(fp);
					warnp("unable to fopen(%s, w)", buf);
					continue;
				}
			}
			while ((fgets(buf, sizeof(buf), fp)) != NULL) {
				contents_entry *e;
				/* safe. buf and buffer are the same size.. */
				strcpy(buffer, buf);
				e = contents_parse_line(buf);
				if (!e)
					continue;
				if (strcmp(portroot, "/") != 0) {
					snprintf(filename, sizeof(filename), "%s%s", portroot, e->name);
					e->name = filename;
				}

				/* run our little checks */
				++num_files;
				if (regex_count) {
					size_t j;
					for (j = 0; j < regex_count; ++j)
						if (!regexec(regex_head[j], e->name, 0, NULL, 0))
							break;
					if (j < regex_count) {
						--num_files;
						++num_files_ignored;
						continue;
					}
				}
				if (lstat(e->name, &st)) {
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
			fclose(fp);
			if (qc_update) {
				fclose(fpx);
				snprintf(buf, sizeof(buf), "%s%s/%s/%s/CONTENTS", portroot, portvdb,
					dentry->d_name, de->d_name);
				strcpy(buffer, buf);
				strncat(buffer, "~", sizeof(buffer)-strlen(buf)-1);
				rename(buffer, buf);
				if (!verbose)
					continue;
			}
			if (bad_only && num_files_ok != num_files) {
				if (verbose)
					printf("%s/%s\n", dentry->d_name, de->d_name);
				else {
					depend_atom *atom = NULL;
					snprintf(buf, sizeof(buf), "%s/%s", dentry->d_name, de->d_name);
					if ((atom = atom_explode(buf)) != NULL) {
						printf("%s/%s\n", dentry->d_name, atom->PN);
						atom_implode(atom);
					} else  {
						printf("%s/%s\n", dentry->d_name, de->d_name);
					}
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
				ret = EXIT_FAILURE;
		}
		closedir(dirp);
		xchdir("..");
	}

	qcheck_cleanup(regex_head, regex_count);
	return ret;
}

#else
DEFINE_APPLET_STUB(qcheck)
#endif
