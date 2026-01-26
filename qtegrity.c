/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2017-2018 Sam Besselink
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"
#include "libq/hash.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <xalloc.h>

#define QTEGRITY_FLAGS "a:is" COMMON_FLAGS
static struct option const qtegrity_long_opts[] = {
	{"add",			a_argument, NULL, 'a'},
	{"ignore-non-existent", no_argument, NULL, 'i'},
	{"show-matches", no_argument, NULL, 's'},
/* TODO, add this functionality
	{"convert", a_argument, NULL, 'c'}
*/
	COMMON_LONG_OPTS
};
static const char * const qtegrity_opts_help[] = {
	"Add file to store of known-good digests",
	"Be silent if recorded file no longer exists",
	"Show recorded digests that match with known-good digests",
/* TODO
	"Convert known good digests to different hash function",
*/
	COMMON_OPTS_HELP
};
#define qtegrity_usage(ret) usage(ret, QTEGRITY_FLAGS, qtegrity_long_opts, qtegrity_opts_help, NULL, lookup_applet_idx("qtegrity"))

struct qtegrity_opt_state {
	bool ima;
	bool add;
	char* add_file;
	bool ignore_non_exist;
	bool show_matches;
/* TODO
	bool convert;
*/
};

#define FILE_SUCCESS 1
#define FILE_EMPTY 2
#define FILE_RELATIVE 3

static void
check_sha(char *ret_digest, char *path, char *algo)
{
	int hashes = 0;
	size_t flen = 0;

	if (strcmp(algo, "sha256") == 0) {
		hashes |= HASH_SHA256;
	} else if (strcmp(algo, "sha512") == 0) {
		hashes |= HASH_SHA512;
	} else {
		/* no matching hash? (we could support whirlpool and blake2b) */
		return;
	}

	hash_compute_file(path, ret_digest, ret_digest, NULL, &flen, hashes);
	(void)flen;  /* we don't use the file size */

	return;
}

static void get_fname_from_line(char * line, char **ret, int digest_size, int offset)
{
	size_t dlenstr = strlen(line);
	char *p;
	/* Skip first 123 chars to get to file depends on digest_func in IMA */
	size_t skip = ((digest_size == SHA256_DIGEST_LENGTH) ||
			(digest_size == SHA512_DIGEST_LENGTH)) ?
		digest_size+offset+8 : digest_size+offset+6;

	if (dlenstr > skip) { /* assume file is at least two chars long */
		int segment_size = dlenstr - skip - 1;
		p = xmalloc(segment_size+1);
		memcpy(p, line + skip, segment_size);
		p[segment_size] = '\0';
	} else {
		/* E.g. digest used wrong hash algo, or malformed input */
		p = NULL;
	}

	*ret = p;
}

static void get_digest_from_line(char * line, char * ret, int digest_size, int offset)
{
	size_t dlenstr = strlen(line);
	/* Skip first chars to get to digest depends on digest_func in IMA */
	size_t skip = ((digest_size == SHA256_DIGEST_LENGTH) ||
			(digest_size == SHA512_DIGEST_LENGTH)) ?
		offset+8 : offset+6;

	if (dlenstr > (digest_size+skip+1)) {
		memcpy(ret, line+skip, digest_size);
		ret[digest_size] = '\0';
	}
}

static void get_known_good_digest(const char * fn_store, char * recorded_fname, char * ret, int recorded_digest_size)
{
	/* Open file with known good hashes */
	int fd_store;
	FILE *fp_store;

	fd_store = open(fn_store, O_RDONLY|O_CLOEXEC, 0);
	if (fd_store == -1) {
		warnp("unable to open(%s)", fn_store);
		exit(0);
	}
	if ((fp_store = fdopen(fd_store, "r")) == NULL) {
		warnp("unable to fopen(%s, r)", fn_store);
		close(fd_store);
		exit(0);
	}

	char *buffered_line, *line, *fname;
	size_t linelen;

	/* Iterate over lines in known-good-hashes-file; per line: if fname
	 * matches, grab hash. */
	buffered_line = line = fname = NULL;
	while (getline(&line, &linelen, fp_store) != -1) {
		free(buffered_line);
		buffered_line = xstrdup(line);

		get_fname_from_line(line, &fname, recorded_digest_size, 15);

		if (fname == NULL) {
			/* probably line without digest (e.g. symlink) */
			continue;
		}

		if (strcmp(recorded_fname, fname) == 0) {
			get_digest_from_line(line, ret, recorded_digest_size, 9);

			free(fname);
			break;
		}

		free(fname);
	}

	free(line);
	free(buffered_line);

	close(fd_store);
	fclose(fp_store);
}

static int get_size_digest(char * line)
{
	int ret = 0;

	char *pfound;
	/* find colon; it is boundary between end of hash func & begin of
	 * digest */
	pfound = strchr(line, ':');
	if (pfound != NULL) {
		int dpfound = pfound - line;
		int cutoff_prefix = 0;

		if (dpfound == 55 || dpfound == 6) {
			ret = SHA1_DIGEST_LENGTH;
		} else if (dpfound == 57) {
			cutoff_prefix = 51;
		} else if (dpfound == 8) {
			cutoff_prefix = 0;
		}

		int dsegment = dpfound - cutoff_prefix;

		char *line_segment;
		line_segment = xmalloc(dsegment + 1);
		/* chop off the first chars to get to the hash func */
		memcpy(line_segment, line + cutoff_prefix, dsegment);
		line_segment[dsegment] = '\0';

		/* If line segment equals name of hash func, then return
		 * relevant const. */
		if (strcmp(line_segment, "sha512") == 0) {
			ret = SHA512_DIGEST_LENGTH;
		} else if (strcmp(line_segment, "sha256") == 0) {
			ret = SHA256_DIGEST_LENGTH;
		} else {
			printf("Expected sha algo, got %s", line_segment);
		}

		free(line_segment);
	}

	return ret;
}

static int check_file(char * filename)
{
	if (strlen(filename) > _Q_PATH_MAX)
		err("Filename too long");

	if (filename[0] != '/') {
		return FILE_RELATIVE;
	}

	return FILE_SUCCESS;
}

int qtegrity_main(int argc, char **argv)
{
	int i;

	struct qtegrity_opt_state state = {
		.ima = true,
		.add = false,
		.ignore_non_exist = false,
		.show_matches = false,
/* TODO
		.convert = false;
*/
	};

	while ((i = GETOPT_LONG(QTEGRITY, qtegrity, "")) != -1) {
		switch (i) {
			COMMON_GETOPTS_CASES(qtegrity)
			case 'a':
				state.ima = false;
				state.add = true;
				if (check_file(optarg) == FILE_SUCCESS) {
					free(state.add_file);
					state.add_file = xstrdup(optarg);
				} else {
					err("Expected absolute file as argument, got '%s'", optarg);
				}
				break;
			case 'i': state.ignore_non_exist = true; break;
			case 's': state.show_matches = true; break;
		}
	}

	if (state.ima) {
		const char *fn_ima =
			"/sys/kernel/security/ima/ascii_runtime_measurements";
		int fd_ima;
		FILE *fp_ima;
		struct stat st;

		fd_ima = open(fn_ima, O_RDONLY|O_CLOEXEC, 0);
		if (fd_ima == -1) {
			/* TODO, shouldn't we explicitly remind user IMA/securityfs
			 * is needed? */
			warnp("Unable to open(%s)", fn_ima);
			exit(0);
		}
		if ((fp_ima = fdopen(fd_ima, "r")) == NULL) {
			warnp("Unable to fopen(%s, r)", fn_ima);
			close(fd_ima);
			exit(0);
		}

		char *buffered_line, *line, *recorded_fname;
		int recorded_digest_size = 0;
		size_t linelen;

		/* Iterate over IMA file, grab fname and digest, get known good
		 * digest for fname and compare */
		buffered_line = line = recorded_fname = NULL;
		while (getline(&line, &linelen, fp_ima) != -1) {
			char *recorded_digest;
			char *digest;

			free(buffered_line);
			buffered_line = xstrdup(line);

			if (buffered_line[0] != '1' || buffered_line[1] != '0')
				continue;

			recorded_digest_size = get_size_digest(buffered_line);
			recorded_digest = xmalloc(recorded_digest_size+1);
			recorded_digest[0] = '\0';

			/* grab fname from IMA file line */
			get_fname_from_line(buffered_line, &recorded_fname,
					recorded_digest_size, 51);
			/* grab digest from IMA file line, @TODO, check whether
			 * digest == 000etc */
			get_digest_from_line(buffered_line, recorded_digest,
					recorded_digest_size, 50);

			if (recorded_fname == NULL || *recorded_digest == '\0') {
				printf("Empty recorded filename: %s\n", line);

				if (recorded_fname != NULL)
					free(recorded_fname);

				free(recorded_digest);

				continue;
			}

			if (check_file(recorded_fname) == FILE_RELATIVE) {
				printf("Seems like a kernel process: %s\n", recorded_fname);

				free(recorded_fname);
				free(recorded_digest);
				continue;
			}

			if (stat(recorded_fname, &st) < 0) {
				if (!state.ignore_non_exist)
					printf("Couldn't access recorded file '%s'\n",
							recorded_fname);

				free(recorded_fname);
				free(recorded_digest);
				continue;
			}

			if (!(st.st_mode & S_IXUSR ||
						st.st_mode & S_IXGRP ||
						st.st_mode & S_IXOTH))
			{
				free(recorded_fname);
				free(recorded_digest);
				continue;
			}

			digest = xmalloc(recorded_digest_size+1);
			digest[0] = '\0';

			/* first try custom known good digests for fname */
			get_known_good_digest("/var/db/QTEGRITY_custom",
					recorded_fname, digest, recorded_digest_size);

			if (digest[0] == '\0') {
				digest[0] = '\0';
				/* then try from OS source */
				get_known_good_digest("/var/db/QTEGRITY",
						recorded_fname, digest, recorded_digest_size);

				if (digest[0] == '\0') {
					printf("No digest found for: %s\n", line);

					free(recorded_fname);
					free(recorded_digest);
					free(digest);
					continue;
				}
			}

			if (strcmp(recorded_digest, digest) != 0) {
				printf("Digest didn't match for %s\n", recorded_fname);
				printf("Known-good: '%s'...\nRecorded: '%s'\n\n",
						digest, recorded_digest);
			} else if (state.show_matches) {
				printf("Success! Digest matched for %s\n", recorded_fname);
			}

			free(recorded_fname);
			free(recorded_digest);
			free(digest);
		}

		free(line);
		free(buffered_line);

		close(fd_ima);
		fclose(fp_ima);
	} else if (state.add) {
		/* Add a single executable file+digest to the custom digest store */
		const char *fn_qtegrity_custom = "/var/db/QTEGRITY_custom";
		int fd_qtegrity_custom;
		FILE *fp_qtegrity_custom;
		struct stat st;
		int flush_status;

		fd_qtegrity_custom =
			open(fn_qtegrity_custom, O_RDWR|O_CREAT|O_CLOEXEC, 0);
		if (fd_qtegrity_custom == -1) {
			warnp("Unable to open(%s)", fn_qtegrity_custom);
			exit(0);
		}
		if ((fp_qtegrity_custom = fdopen(fd_qtegrity_custom, "w+")) == NULL) {
			warnp("Unable to fopen(%s, r)", fn_qtegrity_custom);
			close(fd_qtegrity_custom);
			exit(0);
		}

		printf("Adding %s to %s\n", state.add_file, fn_qtegrity_custom);

		if (stat(state.add_file, &st) < 0)
			err("Couldn't access file '%s'\n", state.add_file);

		if (!(st.st_mode & S_IXUSR ||
					st.st_mode & S_IXGRP ||
					st.st_mode & S_IXOTH))
			err("File '%s' is not executable\n", state.add_file);

		/* add digest */
		char *hash_algo = (char *)"sha256";
		char *file_digest;
		file_digest = xmalloc(SHA256_DIGEST_LENGTH+1);
		file_digest[0] = '\0';
		check_sha(file_digest, state.add_file, hash_algo);

		/* Iterate over lines; if fname matches, exit-loop */
		char *line, *fname;
		size_t linelen;
		int recorded_digest_size = 0;
		int skip = 0;
		line = fname = NULL;
		while (getline(&line, &linelen, fp_qtegrity_custom) != -1) {
			recorded_digest_size = get_size_digest(line);
			get_fname_from_line(line, &fname, recorded_digest_size, 5);

			/* probably line without digest (e.g. symlink) */
			if (fname == NULL)
				continue;

			if (strcmp(state.add_file, fname) == 0) {
				printf("Executable already recorded, "
						"replacing digest with %s\n", file_digest);
				skip = ((recorded_digest_size == SHA256_DIGEST_LENGTH) ||
						(recorded_digest_size == SHA512_DIGEST_LENGTH)) ?
					recorded_digest_size+6+8 : recorded_digest_size+6+6;
				if (fseek(fp_qtegrity_custom,
							-skip-strlen(fname), SEEK_CUR) == -1)
					err("seek failed: %s\n", strerror(errno));
				free(fname);
				break;
			}

			free(fname);
		}

		free(line);

		fputs(hash_algo, fp_qtegrity_custom);
		fputs(":", fp_qtegrity_custom);
		fputs(file_digest, fp_qtegrity_custom);
		fputs(" file:", fp_qtegrity_custom);
		fputs(state.add_file, fp_qtegrity_custom);
		fputs("\n", fp_qtegrity_custom);

		flush_status = fflush(fp_qtegrity_custom);
		if (flush_status != 0)
			puts("Error flushing stream!");

		free(file_digest);
		fclose(fp_qtegrity_custom);
	}

	if (state.add)
		free(state.add_file);

	return EXIT_SUCCESS;
}
