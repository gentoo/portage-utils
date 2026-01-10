/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <sys/types.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "atom.h"
#include "contents.h"
#include "copy_file.h"
#include "hash.h"
#include "prelink.h"
#include "tree.h"
#include "xarray.h"
#include "xasprintf.h"
#include "xregex.h"

#define QCHECK_FORMAT "%[CATEGORY]%[PN]"
#define QCHECK_FORMAT_VERBOSE "%[CATEGORY]%[PF]"

#define QCHECK_FLAGS "F:s:uABHTPp" COMMON_FLAGS
static struct option const qcheck_long_opts[] = {
	{"format",          a_argument, NULL, 'F'},
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
	"Custom output format (default: " QCHECK_FORMAT ")",
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

#define qcprintf(fmt, args...) do { if (!state->bad_only) printf(fmt, ## args); } while (0)

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
	const char *fmt;
};

static int
qcheck_cb(tree_pkg_ctx *pkg_ctx, void *priv)
{
	struct qcheck_opt_state *state = priv;
	struct stat              st;
	depend_atom             *atom;
	FILE                    *fp_contents_update = NULL;
	size_t                   num_files          = 0;
	size_t                   num_files_ok       = 0;
	size_t                   num_files_unknown  = 0;
	size_t                   num_files_ignored  = 0;
	char                    *buffer;
	char                    *line;
	char                    *savep;
	char                    *eprefix            = NULL;
	size_t                   eprefix_len        = 0;
	int                      cp_argc;
	int                      cpm_argc;
	char                   **cp_argv;
	char                   **cpm_argv;

	/* get CONTENTS from meta */
	line = tree_pkg_meta_get(pkg_ctx, CONTENTS);
	if (line == NULL)
		return EXIT_FAILURE;

	atom = tree_get_atom(pkg_ctx, false);

	qcprintf("%sing %s ...\n",
		(state->qc_update ? "Updat" : "Check"),
		atom_format(state->fmt, atom));

	/* Open contents_update, if needed */
	if (state->qc_update) {
		char tempfile[] = "qcheck-tmp-XXXXXX";
		mode_t mask;
		int fd;

		mask = umask(0077);
		fd = mkstemp(tempfile);
		umask(mask);
		if (fd == -1 || (fp_contents_update = fdopen(fd, "w+")) == NULL) {
			if (fd >= 0)
				close(fd);
			warnp("unable to temp file");
			return EXIT_FAILURE;
		}
		/* like tmpfile() does, but Coverity thinks it is unsafe */
		unlink(tempfile);
	}

	if (!state->chk_config_protect) {
		makeargv(config_protect, &cp_argc, &cp_argv);
		makeargv(config_protect_mask, &cpm_argc, &cpm_argv);

		eprefix = tree_pkg_meta_get(pkg_ctx, EPREFIX);
		if (eprefix != NULL)
			eprefix_len = strlen(eprefix);
	}

	buffer = NULL;
	for (; (line = strtok_r(line, "\n", &savep)) != NULL; line = NULL) {
		contents_entry *entry;
		free(buffer);
		buffer = xstrdup(line);

		entry = contents_parse_line(line);
		if (!entry)
			continue;

		num_files++;

		/* handle skips */
		if (array_cnt(state->regex_arr)) {
			size_t n;
			regex_t *regex;
			array_for_each(state->regex_arr, n, regex)
				if (!regexec(regex, entry->name, 0, NULL, 0))
					break;
			if (n < array_cnt(state->regex_arr)) {
				num_files--;
				num_files_ignored++;
				if (verbose)
					qcprintf(" %sSKIP%s %s: matches regex\n",
							 YELLOW, NORM, entry->name);
				if (state->qc_update)
					fprintf(fp_contents_update, "%s\n", buffer);
				continue;
			}
		}

		/* handle CONFIG_PROTECT-ed files */
		if (!state->chk_config_protect) {
			int    i;
			char  *p;

			/* compute path without EPREFIX */
			p = entry->name;
			if (strlen(p) > eprefix_len)
				p += eprefix_len;

			/* if in CONFIG_PROTECT_MASK, handle like normal */
			for (i = 1; i < cpm_argc; ++i) {
				if (strncmp(cpm_argv[i], p, strlen(cpm_argv[i])) == 0)
					break;
			}
			if (i == cpm_argc) {
				/* not explicitly unmasked, check if it's protected */
				for (i = 1; i < cp_argc; ++i) {
					if (strncmp(cp_argv[i], p, strlen(cp_argv[i])) == 0) {
						num_files--;
						num_files_ignored++;
						if (verbose)
							qcprintf(" %sSKIP%s %s: protected via %s\n",
							 		 YELLOW, NORM, entry->name, cp_argv[i]);
						if (state->qc_update)
							fprintf(fp_contents_update, "%s\n", buffer);
						break;
					}
				}
				if (i != cp_argc)
					continue;
			}
		}

		/* check file existence */
		if (fstatat(pkg_ctx->cat_ctx->ctx->portroot_fd, entry->name + 1,
					&st, AT_SYMLINK_NOFOLLOW) != 0)
		{
			if (state->chk_afk) {
				if (errno == ENOENT)
					qcprintf(" %sAFK%s: %s\n", RED, NORM, entry->name);
				else
					qcprintf(" %sERROR (%s)%s: %s\n", RED,
							strerror(errno), NORM, entry->name);
			} else {
				num_files--;
				num_files_ignored++;
				if (verbose)
					qcprintf(" %sSKIP%s %s: %s\n",
						 	 YELLOW, NORM, entry->name, strerror(errno));
				if (state->qc_update)
					fprintf(fp_contents_update, "%s\n", buffer);
			}
			continue;
		}

		/* for certain combinations of flags and filetypes, a file
		 * won't get checks and should be ignored */
		if (!state->chk_mtime && entry->type == CONTENTS_SYM) {
			num_files--;
			num_files_ignored++;
			if (verbose)
				qcprintf(" %sSKIP%s %s: symlink and no mtime check\n",
						 YELLOW, NORM, entry->name);
			if (state->qc_update)
				fprintf(fp_contents_update, "%s\n", buffer);
			continue;
		}

		/* Digest checks only work on regular files
		 * Note: We don't check for state->chk_hash when entering
		 * but rather in digest-check #3, because we only succeed
		 * tests/qcheck/list04.good if when chk_hash is false, we
		 * do check hashes, but only print mismatched digests as
		 * 'ignored file'. */
		if (entry->digest && S_ISREG(st.st_mode)) {
			char *f_digest;
			int   hash_algo;

			/* Validate digest (handles MD5 / SHA1)
			 * Digest-check 1/3:
			 * should we check digests? */
			switch (strlen(entry->digest)) {
				case 32:  hash_algo = (int)HASH_MD5;   break;
				case 40:  hash_algo = (int)HASH_SHA1;  break;
				default:  hash_algo = 0;               break;
			}

			if (hash_algo == 0) {
				if (state->chk_hash) {
					qcprintf(" %sUNKNOWN DIGEST%s: '%s' for '%s'\n",
							 RED, NORM, entry->digest, entry->name);
					num_files_unknown++;
				} else {
					num_files--;
					num_files_ignored++;
					if (verbose)
						qcprintf(" %sSKIP%s %s: unknown digest\n",
						 	 	 YELLOW, NORM, entry->name);
					if (state->qc_update)
						fprintf(fp_contents_update, "%s\n", buffer);
				}
				continue;
			}

			/* compute hash for file */
			hash_cb_t hash_cb =
				state->undo_prelink ? hash_cb_prelink_undo : NULL;
			f_digest = hash_file_at_cb(
					pkg_ctx->cat_ctx->ctx->portroot_fd,
					entry->name + 1, hash_algo, hash_cb);

			/* Digest-check 2/3:
			 * do we have digest of the file? */
			if (f_digest == NULL) {
				num_files_unknown++;

				if (state->qc_update)
					fprintf(fp_contents_update, "%s\n", buffer);

				if (verbose)
					qcprintf(" %sPERM %4o%s: %s\n",
							 RED, (unsigned int)(st.st_mode & 07777),
							 NORM, entry->name);

				continue;
			}

			/* Digest-check 3/3:
			 * does the digest equal what portage recorded? */
			if (strcmp(entry->digest, f_digest) != 0) {
				if (state->chk_hash) {
					const char *digest_disp;

					if (state->qc_update)
						fprintf(fp_contents_update, "obj %s %s %llu\n",
								entry->name, f_digest,
								(long long int)st.st_mtime);

					switch (hash_algo) {
						case HASH_MD5:   digest_disp = "MD5";   break;
						case HASH_SHA1:  digest_disp = "SHA1";  break;
						default:         digest_disp = "UNK";   break;
					}

					qcprintf(" %s%s-DIGEST%s: %s",
							 RED, digest_disp, NORM, entry->name);
					if (verbose)
						qcprintf(" (recorded '%s' != actual '%s')",
								 entry->digest, f_digest);
					qcprintf("\n");
				} else {
					num_files--;
					num_files_ignored++;
					if (verbose)
						qcprintf(" %sSKIP%s %s: digest mismatch "
								 "but check disabled\n",
						 	 	 YELLOW, NORM, entry->name);
					if (state->qc_update)
						fprintf(fp_contents_update, "%s\n", buffer);
				}

				continue;
			}
		}

		/* validate mtimes */
		if (entry->mtime && entry->mtime != st.st_mtime) {
			if (state->chk_mtime) {
				qcprintf(" %sMTIME%s: %s", RED, NORM, entry->name);
				if (verbose)
					qcprintf(" (recorded '%llu' != actual '%llu')",
						 	 (long long int)entry->mtime,
						 	 (long long int)st.st_mtime);
				qcprintf("\n");

				/* Update mtime */
				if (state->qc_update) {
					if (entry->type == CONTENTS_SYM) {
						fprintf(fp_contents_update, "sym %s -> %s %llu\n",
								entry->name, entry->sym_target,
								(long long int)st.st_mtime);
					} else {
						fprintf(fp_contents_update, "obj %s %s %llu\n",
								entry->name, entry->digest,
								(long long int)st.st_mtime);
					}
				}

				continue;
			} else {
				num_files--;
				num_files_ignored++;
				if (verbose)
					qcprintf(" %sSKIP%s %s: mtime mismatch "
						 	 "but check disabled\n",
						 	 YELLOW, NORM, entry->name);
				if (state->qc_update)
					fprintf(fp_contents_update, "%s\n", buffer);
				continue;
			}
		}

		/* success! */
		if (state->qc_update)
			fprintf(fp_contents_update, "%s\n", buffer);

		num_files_ok++;
	}
	free(buffer);

	if (!state->chk_config_protect) {
		freeargv(cp_argc, cp_argv);
		freeargv(cpm_argc, cpm_argv);
	}

	if (state->qc_update) {
		int fd_contents;
		FILE *fp_contents;

		/* O_TRUNC truncates, but file owner and mode are unchanged */
		fd_contents = openat(pkg_ctx->fd, "CONTENTS", O_WRONLY | O_TRUNC);
		if (fd_contents < 0 ||
				(fp_contents = fdopen(fd_contents, "w")) == NULL)
		{
			fclose(fp_contents_update);
			warn("could not open CONTENTS for writing");
			return EXIT_FAILURE;
		}

		/* rewind tempfile */
		fseek(fp_contents_update, 0, SEEK_SET);

		copy_file(fp_contents_update, fp_contents);

		fclose(fp_contents_update);
		fclose(fp_contents);

		if (!verbose)
			return EXIT_SUCCESS;
	}

	if (state->bad_only && num_files_ok != num_files)
		printf("%s\n", atom_format(state->fmt, atom));
	qcprintf("  %2$s*%1$s %3$s%4$zu%1$s out of %3$s%5$zu%1$s file%6$s are good",
		NORM, BOLD, BLUE, num_files_ok, num_files,
		(num_files != 1 ? "s" : ""));
	if (num_files_unknown)
		qcprintf(" (Unable to digest %2$s%3$zu%1$s file%4$s)",
			NORM, BLUE, num_files_unknown,
			(num_files_unknown > 1 ? "s" : ""));
	if (num_files_ignored)
		qcprintf(" (%2$s%3$zu%1$s file%4$s ignored)",
			NORM, BLUE, num_files_ignored,
			(num_files_ignored > 1 ? "s were" : " was"));
	qcprintf("\n");

	if (num_files_ok != num_files && !state->qc_update)
		return EXIT_FAILURE;
	else
		return EXIT_SUCCESS;
}

int qcheck_main(int argc, char **argv)
{
	size_t i;
	int ret;
	tree_ctx *vdb;
	depend_atom *atom;
	array_t regex_arr;
	array_t atoms;
	struct qcheck_opt_state state = {
		.atoms = &atoms,
		.regex_arr = &regex_arr,
		.bad_only = false,
		.qc_update = false,
		.chk_afk = true,
		.chk_hash = true,
		.chk_mtime = true,
		.chk_config_protect = true,
		.undo_prelink = false,
		.fmt = NULL,
	};

	VAL_CLEAR(regex_arr);
	VAL_CLEAR(atoms);

	while ((ret = GETOPT_LONG(QCHECK, qcheck, "")) != -1) {
		switch (ret) {
		COMMON_GETOPTS_CASES(qcheck)
		case 's': {
			regex_t preg;
			xregcomp(&preg, optarg, REG_EXTENDED | REG_NOSUB);
			xarraypush(state.regex_arr, &preg, sizeof(preg));
			break;
		}
		case 'u': state.qc_update = true;                    break;
		case 'A': state.chk_afk = false;                     break;
		case 'B': state.bad_only = true;                     break;
		case 'H': state.chk_hash = false;                    break;
		case 'T': state.chk_mtime = false;                   break;
		case 'P': state.chk_config_protect = false;          break;
		case 'p': state.undo_prelink = prelink_available();  break;
		case 'F': state.fmt = optarg;                        break;
		}
	}

	if (state.fmt == NULL)
		state.fmt = verbose ? QCHECK_FORMAT_VERBOSE : QCHECK_FORMAT;

	argc -= optind;
	argv += optind;
	for (i = 0; i < (size_t)argc; ++i) {
		atom = atom_explode(argv[i]);
		if (!atom)
			warn("invalid atom: %s", argv[i]);
		else
			xarraypush_ptr(state.atoms, atom);
	}

	vdb = tree_open_vdb(portroot, portvdb);
	ret = -1;
	if (vdb != NULL) {
		if (array_cnt(state.atoms) != 0) {
			ret = 0;
			array_for_each(state.atoms, i, atom) {
				ret |= tree_foreach_pkg_sorted(vdb, qcheck_cb, &state, atom);
			}
		} else {
			ret = tree_foreach_pkg_sorted(vdb, qcheck_cb, &state, NULL);
		}
		tree_close(vdb);
	}
	if (array_cnt(state.regex_arr) > 0) {
		void *preg;
		array_for_each(state.regex_arr, i, preg)
			regfree(preg);
	}
	xarrayfree(state.regex_arr);
	array_for_each(state.atoms, i, atom)
		atom_implode(atom);
	xarrayfree_int(state.atoms);
	return ret != 0;
}
