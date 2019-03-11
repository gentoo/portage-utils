/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifdef APPLET_qcheck

#define QCHECK_FLAGS "s:uABHTPp" COMMON_FLAGS
static struct option const qcheck_long_opts[] = {
	{"skip",            a_argument, NULL, 's'},
	{"update",         no_argument, NULL, 'u'},
	{"noafk",          no_argument, NULL, 'A'},
	{"badonly",        no_argument, NULL, 'B'},
	{"nohash",         no_argument, NULL, 'H'},
	{"nomtime",        no_argument, NULL, 'T'},
	{"skip-protected", no_argument, NULL, 'P'},
	{"prelink",        no_argument, NULL, 'p'},
	COMMON_LONG_OPTS
};
static const char * const qcheck_opts_help[] = {
	"Ignore files matching the regular expression <arg>",
	"Update missing files, chksum and mtimes for packages",
	"Ignore missing files",
	"Only print pkgs containing bad files",
	"Ignore differing/unknown file chksums",
	"Ignore differing file mtimes",
	"Ignore files in CONFIG_PROTECT-ed paths",
	"Undo prelink when calculating checksums",
	COMMON_OPTS_HELP
};
#define qcheck_usage(ret) usage(ret, QCHECK_FLAGS, qcheck_long_opts, qcheck_opts_help, NULL, lookup_applet_idx("qcheck"))

#define qcprintf(fmt, args...) do { if (!state->bad_only) printf(_(fmt), ## args); } while (0)

struct qcheck_opt_state {
	array_t *atoms;
	array_t *regex_arr;
	bool bad_only;
	bool qc_update;
	bool chk_afk;
	bool chk_hash;
	bool chk_mtime;
	bool chk_config_protect;
	bool undo_prelink;
};

static int
qcheck_process_contents(q_vdb_pkg_ctx *pkg_ctx, struct qcheck_opt_state *state)
{
	int fd_contents;
	FILE *fp_contents, *fp_contents_update;
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	size_t num_files, num_files_ok, num_files_unknown, num_files_ignored;
	char *buffer, *line;
	size_t linelen;
	struct stat st, cst;
	int cp_argc, cpm_argc;
	char **cp_argv, **cpm_argv;

	fp_contents_update = NULL;

	/* Open contents */
	fd_contents = q_vdb_pkg_openat(pkg_ctx, "CONTENTS", O_RDONLY|O_CLOEXEC, 0);
	if (fd_contents == -1)
		return EXIT_SUCCESS;
	if (fstat(fd_contents, &cst)) {
		close(fd_contents);
		return EXIT_SUCCESS;
	}
	if ((fp_contents = fdopen(fd_contents, "r")) == NULL) {
		close(fd_contents);
		return EXIT_SUCCESS;
	}

	/* Open contents_update, if needed */
	num_files = num_files_ok = num_files_unknown = num_files_ignored = 0;
	qcprintf("%sing %s%s/%s%s ...\n",
		(state->qc_update ? "Updat" : "Check"),
		GREEN, catname, pkgname, NORM);
	if (state->qc_update) {
		fp_contents_update = q_vdb_pkg_fopenat_rw(pkg_ctx, "CONTENTS~");
		if (fp_contents_update == NULL) {
			fclose(fp_contents);
			warnp("unable to fopen(%s/%s, w)", pkgname, "CONTENTS~");
			return EXIT_FAILURE;
		}
	}

	if (!state->chk_config_protect) {
		makeargv(config_protect, &cp_argc, &cp_argv);
		makeargv(config_protect_mask, &cpm_argc, &cpm_argv);
	}

	buffer = line = NULL;
	while (getline(&line, &linelen, fp_contents) != -1) {
		contents_entry *entry;
		free(buffer);
		buffer = xstrdup(line);

		entry = contents_parse_line(line);

		if (!entry)
			continue;

		/* run initial checks */
		++num_files;
		if (array_cnt(state->regex_arr)) {
			size_t n;
			regex_t *regex;
			array_for_each(state->regex_arr, n, regex)
				if (!regexec(regex, entry->name, 0, NULL, 0))
					break;
			if (n < array_cnt(state->regex_arr)) {
				--num_files;
				++num_files_ignored;
				continue;
			}
		}
		if (fstatat(pkg_ctx->cat_ctx->ctx->portroot_fd, entry->name + 1,
					&st, AT_SYMLINK_NOFOLLOW))
		{
			/* make sure file exists */
			if (state->chk_afk) {
				if (errno == ENOENT)
					qcprintf(" %sAFK%s: %s\n", RED, NORM, entry->name);
				else
					qcprintf(" %sERROR (%s)%s: %s\n", RED,
							strerror(errno), NORM, entry->name);
			} else {
				--num_files;
				++num_files_ignored;
				if (state->qc_update)
					fputs(buffer, fp_contents_update);
			}
			continue;
		}

		/* Handle CONFIG_PROTECT-ed files */
		if (!state->chk_config_protect) {
			int i;
			/* If in CONFIG_PROTECT_MASK, handle like normal */
			for (i = 1; i < cpm_argc; ++i)
				if (strncmp(cpm_argv[i], entry->name, strlen(cpm_argv[i])) == 0)
					break;
			if (i == cpm_argc) {
				/* Not explicitly masked, so it's protected */
				for (i = 1; i < cp_argc; ++i)
					if (strncmp(cp_argv[i], entry->name,
								strlen(cp_argv[i])) == 0)
					{
						num_files_ok++;
						continue;
					}
			}
		}

		/* For certain combinations of flags and filetypes, a file
		 * won't get checks and should be ignored */
		if (!state->chk_mtime && entry->type == CONTENTS_SYM) {
			--num_files;
			++num_files_ignored;
			if (state->qc_update)
				fputs(buffer, fp_contents_update);

			continue;
		}

		/* Digest checks only work on regular files
		 * Note: We don't check for state->chk_hash when entering
		 * but rather in digest-check #3, because we only succeed
		 * tests/qcheck/list04.good if when chk_hash is false, we
		 * do check hashes, but only print mismatched digests as
		 * 'ignored file'. */
		if (entry->digest && S_ISREG(st.st_mode)) {
			/* Validate digest (handles MD5 / SHA1)
			 * Digest-check 1/3:
			 * Should we check digests? */
			char *f_digest;
			uint8_t hash_algo;
			switch (strlen(entry->digest)) {
				case 32: hash_algo = HASH_MD5; break;
				case 40: hash_algo = HASH_SHA1; break;
				default: hash_algo = 0; break;
			}

			if (!hash_algo) {
				if (state->chk_hash) {
					qcprintf(" %sUNKNOWN DIGEST%s: '%s' for '%s'\n",
							RED, NORM, entry->digest, entry->name);
					++num_files_unknown;
				} else {
					--num_files;
					++num_files_ignored;
					if (state->qc_update)
						fputs(buffer, fp_contents_update);
				}
				continue;
			}

			hash_cb_t hash_cb =
				state->undo_prelink ? hash_cb_prelink_undo : hash_cb_default;
			f_digest = (char *)hash_file_at_cb(
					pkg_ctx->cat_ctx->ctx->portroot_fd,
					entry->name + 1, hash_algo, hash_cb);

			/* Digest-check 2/3:
			 * Can we get a digest of the file? */
			if (!f_digest) {
				++num_files_unknown;
				free(f_digest);

				if (state->qc_update)
					fputs(buffer, fp_contents_update);

				if (verbose)
					qcprintf(" %sPERM %4o%s: %s\n",
							RED, (unsigned int)(st.st_mode & 07777),
							NORM, entry->name);

				continue;
			}

			/* Digest-check 3/3:
			 * Does the digest equal what portage recorded? */
			if (strcmp(entry->digest, f_digest) != 0) {
				if (state->chk_hash) {
					if (state->qc_update)
						fprintf(fp_contents_update, "obj %s %s %"PRIu64"\n",
								entry->name, f_digest, (uint64_t)st.st_mtime);

					const char *digest_disp;
					switch (hash_algo) {
						case HASH_MD5:	digest_disp = "MD5"; break;
						case HASH_SHA1:	digest_disp = "SHA1"; break;
						default: digest_disp = "UNK"; break;
					}

					qcprintf(" %s%s-DIGEST%s: %s",
							RED, digest_disp, NORM, entry->name);
					if (verbose)
						qcprintf(" (recorded '%s' != actual '%s')",
								entry->digest, f_digest);
					qcprintf("\n");
				} else {
					--num_files;
					++num_files_ignored;
					if (state->qc_update)
						fputs(buffer, fp_contents_update);
				}

				free(f_digest);
				continue;
			}

			free(f_digest);
		}

		/* Validate mtimes */
		if (state->chk_mtime && entry->mtime && entry->mtime != st.st_mtime) {
			qcprintf(" %sMTIME%s: %s", RED, NORM, entry->name);
			if (verbose)
				qcprintf(" (recorded '%"PRIu64"' != actual '%"PRIu64"')",
						(uint64_t)entry->mtime, (uint64_t)st.st_mtime);
			qcprintf("\n");

			/* Update mtime */
			if (state->qc_update) {
				if (entry->type == CONTENTS_SYM) {
					fprintf(fp_contents_update, "sym %s -> %s %"PRIu64"\n",
							entry->name, entry->sym_target,
							(uint64_t)st.st_mtime);
				} else {
					fprintf(fp_contents_update, "obj %s %s %"PRIu64"\n",
							entry->name, entry->digest, (uint64_t)st.st_mtime);
				}
			}

			continue;
		}

		/* Success! */
		if (state->qc_update)
			fputs(buffer, fp_contents_update);

		num_files_ok++;
	}
	free(line);
	free(buffer);
	fclose(fp_contents);

	if (!state->chk_config_protect) {
		freeargv(cp_argc, cp_argv);
		freeargv(cpm_argc, cpm_argv);
	}

	if (state->qc_update) {
		if (fchown(fd_contents, cst.st_uid, cst.st_gid)) {
			/* meh */;
		}
		if (fchmod(fd_contents, cst.st_mode)) {
			/* meh */;
		}
		fclose(fp_contents_update);
		if (renameat(pkg_ctx->fd, "CONTENTS~", pkg_ctx->fd, "CONTENTS"))
			unlinkat(pkg_ctx->fd, "CONTENTS~", 0);
		if (!verbose)
			return EXIT_SUCCESS;
	}
	if (state->bad_only && num_files_ok != num_files) {
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

static int
qcheck_cb(q_vdb_pkg_ctx *pkg_ctx, void *priv)
{
	struct qcheck_opt_state *state = priv;
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	bool showit = false;

	/* see if this cat/pkg is requested */
	if (array_cnt(state->atoms)) {
		char *buf;
		size_t i;
		depend_atom *qatom, *atom;

		xasprintf(&buf, "%s/%s", catname, pkgname);
		qatom = atom_explode(buf);
		array_for_each(state->atoms, i, atom)
			if (atom_compare(atom, qatom) == EQUAL) {
				showit = true;
				break;
			}
		atom_implode(qatom);
		free(buf);
	} else
		showit = true;

	return showit ? qcheck_process_contents(pkg_ctx, priv) : 0;
}

int qcheck_main(int argc, char **argv)
{
	size_t i;
	int ret;
	DECLARE_ARRAY(regex_arr);
	depend_atom *atom;
	DECLARE_ARRAY(atoms);
	struct qcheck_opt_state state = {
		.atoms = atoms,
		.regex_arr = regex_arr,
		.bad_only = false,
		.qc_update = false,
		.chk_afk = true,
		.chk_hash = true,
		.chk_mtime = true,
		.chk_config_protect = true,
		.undo_prelink = false,
	};

	while ((ret = GETOPT_LONG(QCHECK, qcheck, "")) != -1) {
		switch (ret) {
		COMMON_GETOPTS_CASES(qcheck)
		case 's': {
			regex_t regex;
			xregcomp(&regex, optarg, REG_EXTENDED|REG_NOSUB);
			xarraypush(regex_arr, &regex, sizeof(regex));
			break;
		}
		case 'u': state.qc_update = true; break;
		case 'A': state.chk_afk = false; break;
		case 'B': state.bad_only = true; break;
		case 'H': state.chk_hash = false; break;
		case 'T': state.chk_mtime = false; break;
		case 'P': state.chk_config_protect = false; break;
		case 'p': state.undo_prelink = prelink_available(); break;
		}
	}

	argc -= optind;
	argv += optind;
	for (i = 0; i < (size_t)argc; ++i) {
		atom = atom_explode(argv[i]);
		if (!atom)
			warn("invalid atom: %s", argv[i]);
		else
			xarraypush_ptr(atoms, atom);
	}

	ret = q_vdb_foreach_pkg_sorted(qcheck_cb, &state);
	{
		void *regex;
		array_for_each(regex_arr, i, regex)
			regfree(regex);
	}
	xarrayfree(regex_arr);
	array_for_each(atoms, i, atom)
		atom_implode(atom);
	xarrayfree_int(atoms);
	return ret;
}

#else
DEFINE_APPLET_STUB(qcheck)
#endif
