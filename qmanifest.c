/*
 * Copyright 2018-2024 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 *
 * The contents of this file was taken from:
 *   https://github.com/grobian/hashgen
 * which was discontinued at the time the sources were incorporated into
 * portage-utils as qmanifest.
 */

#include "main.h"

#ifdef ENABLE_QMANIFEST

#include "applets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <openssl/sha.h>
#include <blake2.h>
#include <zlib.h>
#include <gpgme.h>

#include "eat_file.h"
#include "hash.h"

#define QMANIFEST_FLAGS "gs:pdo" COMMON_FLAGS
static struct option const qmanifest_long_opts[] = {
	{"generate",   no_argument, NULL, 'g'},
	{"signas",      a_argument, NULL, 's'},
	{"passphrase", no_argument, NULL, 'p'},
	{"dir",        no_argument, NULL, 'd'},
	{"overlay",    no_argument, NULL, 'o'},
	COMMON_LONG_OPTS
};
static const char * const qmanifest_opts_help[] = {
	"Generate thick Manifests",
	"Sign generated Manifest using GPG key",
	"Ask for GPG key password (instead of relying on gpg-agent)",
	"Treat arguments as directories",
	"Treat arguments as overlay names",
	COMMON_OPTS_HELP
};
#define qmanifest_usage(ret) usage(ret, QMANIFEST_FLAGS, qmanifest_long_opts, qmanifest_opts_help, NULL, lookup_applet_idx("qmanifest"))

static int hashes = HASH_DEFAULT;
static char *gpg_sign_key = NULL;
static bool gpg_get_password = false;

/* linked list structure to hold verification complaints */
typedef struct verify_msg {
	char *msg;
	struct verify_msg *next;
} verify_msg;

typedef struct _gpg_signature {
	char *algo;
	char *fingerprint;
	char isgood:1;
	char *timestamp;
	char *signer;
	char *pkfingerprint;
	char *reason;
} gpg_sig;

gpg_sig *verify_gpg_sig(const char *path, verify_msg **msgs);
char *verify_timestamp(const char *ts);
char verify_manifest(const char *dir, const char *manifest, verify_msg **msgs);

/* Generate thick Manifests based on thin Manifests, or verify a tree. */

/* In order to build this program, the following packages are required:
 * - sys-libs/zlib (for compressing/decompressing Manifest files)
 * - app-crypt/gpgme (for signing/verifying the top level manifest)
 */

static inline void
update_times(struct timeval *tv, struct stat *s)
{
#if defined (__MACH__) && defined __APPLE__
# define st_mtim st_mtimespec
# define st_atim st_atimespec
#endif
	if (tv[1].tv_sec < s->st_mtim.tv_sec ||
			(tv[1].tv_sec == s->st_mtim.tv_sec &&
			 tv[1].tv_usec < s->st_mtim.tv_nsec / 1000))
	{
		tv[0].tv_sec = s->st_atim.tv_sec;
		tv[0].tv_usec = s->st_atim.tv_nsec / 1000;
		tv[1].tv_sec = s->st_mtim.tv_sec;
		tv[1].tv_usec = s->st_mtim.tv_nsec / 1000;
	}
}

#define LISTSZ 64

/**
 * qsort comparator which runs strcmp.
 */
static int
compare_strings(const void *l, const void *r)
{
	const char **strl = (const char **)l;
	const char **strr = (const char **)r;
	return strcmp(*strl, *strr);
}

/**
 * Return a sorted list of entries in the given directory.  All entries
 * starting with a dot are ignored, and not present in the returned
 * list.  The list and all entries are allocated using xmalloc() and need
 * to be freed.
 * This function returns 0 when everything is fine, non-zero otherwise.
 */
static char
list_dir(char ***retlist, size_t *retcnt, const char *path)
{
	DIR *d;
	struct dirent *e;
	size_t rlen = 0;
	size_t rsize = 0;
	char **rlist = NULL;

	if ((d = opendir(path)) != NULL) {
		while ((e = readdir(d)) != NULL) {
			/* skip all dotfiles */
			if (e->d_name[0] == '.')
				continue;

			if (rlen == rsize) {
				rsize += LISTSZ;
				rlist = xrealloc(rlist,
						rsize * sizeof(rlist[0]));
				if (rlist == NULL) {
					fprintf(stderr, "out of memory\n");
					return 1;
				}
			}
			rlist[rlen] = xstrdup(e->d_name);
			if (rlist[rlen] == NULL) {
				fprintf(stderr, "out of memory\n");
				return 1;
			}
			rlen++;
		}
		closedir(d);

		if (rlen > 1)
			qsort(rlist, rlen, sizeof(rlist[0]), compare_strings);

		*retlist = rlist;
		*retcnt = rlen;
		return 0;
	} else {
		return 1;
	}
}

/**
 * Write hashes in Manifest format to the file open for writing m, or
 * gzipped file open for writing gm.  The hashes written are for a file
 * in root found by name.  The Manifest entry will be using type as
 * first component.
 */
static void
write_hashes(
		struct timeval *tv,
		const char *root,
		const char *name,
		const char *type,
		FILE *m,
		gzFile gm)
{
	size_t flen = 0;
	char sha256[(SHA256_DIGEST_LENGTH * 2) + 1];
	char sha512[(SHA512_DIGEST_LENGTH * 2) + 1];
	char blak2b[(BLAKE2B_OUTBYTES * 2) + 1];
	char data[8192];
	char fname[8192];
	size_t len;
	struct stat s;

	snprintf(fname, sizeof(fname), "%s/%s", root, name);

	if (stat(fname, &s) != 0)
		return;

	update_times(tv, &s);

	hash_compute_file(fname, sha256, sha512, blak2b, &flen, hashes);

	len = snprintf(data, sizeof(data), "%s %s %zd", type, name, flen);
	if (hashes & HASH_BLAKE2B)
		len += snprintf(data + len, sizeof(data) - len,
				" BLAKE2B %s", blak2b);
	if (hashes & HASH_SHA256)
		len += snprintf(data + len, sizeof(data) - len,
				" SHA256 %s", sha256);
	if (hashes & HASH_SHA512)
		len += snprintf(data + len, sizeof(data) - len,
				" SHA512 %s", sha512);
	len += snprintf(data + len, sizeof(data) - len, "\n");

	if (m != NULL)
		fwrite(data, len, 1, m);
	if (gm != NULL && gzwrite(gm, data, len) == 0)
		fprintf(stderr, "failed to write to compressed stream\n");
}

/**
 * Walk through a directory recursively and write hashes for each file
 * found to the gzipped open stream for writing zm.  The Manifest
 * entries generated will all be of DATA type.
 */
static char
write_hashes_dir(
		struct timeval *tv,
		const char *root,
		const char *name,
		gzFile zm)
{
	char path[8192];
	char **dentries;
	size_t dentrieslen;
	size_t i;

	snprintf(path, sizeof(path), "%s/%s", root, name);
	if (list_dir(&dentries, &dentrieslen, path) == 0) {
		for (i = 0; i < dentrieslen; i++) {
			snprintf(path, sizeof(path), "%s/%s", name, dentries[i]);
			free(dentries[i]);
			if (write_hashes_dir(tv, root, path, zm) == 0)
				continue;
			/* regular file */
			write_hashes(tv, root, path, "DATA", NULL, zm);
		}
		free(dentries);
		return 0;
	} else {
		return 1;
	}
}

/**
 * Walk through directory recursively and write hashes for each file
 * found to the open stream for writing m.  All files will not use the
 * "files/" prefix and Manifest entries will be of AUX type.
 */
static char
process_files(struct timeval *tv, const char *dir, const char *off, FILE *m)
{
	char path[8192];
	char **dentries;
	size_t dentrieslen;
	size_t i;

	snprintf(path, sizeof(path), "%s/%s", dir, off);
	if (list_dir(&dentries, &dentrieslen, path) == 0) {
		for (i = 0; i < dentrieslen; i++) {
			snprintf(path, sizeof(path), "%s%s%s",
					off, *off == '\0' ? "" : "/", dentries[i]);
			free(dentries[i]);
			if (process_files(tv, dir, path, m) == 0)
				continue;
			/* regular file */
			write_hashes(tv, dir, path, "AUX", m, NULL);
		}
		free(dentries);
		return 0;
	} else {
		return 1;
	}
}

/**
 * Read layout.conf file specified by path and extract the
 * manifest-hashes property from this file.  The hash set specified for
 * this property will be returned.  When the property isn't found the
 * returned hash set will be the HASH_DEFAULT set.  When the file isn't
 * found, 0 will be returned (which means /no/ hashes).
 */
static int
parse_layout_conf(const char *path)
{
	FILE *f;
	char buf[8192];
	size_t len = 0;
	size_t sz;
	char *p;
	char *q;
	char *tok;
	char *last_nl;
	char *start;
	int ret = 0;

	if ((f = fopen(path, "r")) == NULL)
		return 0;

	/* read file, examine lines after encountering a newline, that is,
	 * if the file doesn't end with a newline, the final bit is ignored */
	while ((sz = fread(buf + len, 1, sizeof(buf) - len, f)) > 0) {
		len += sz;
		start = buf;
		last_nl = NULL;
		for (p = buf; (size_t)(p - buf) < len; p++) {
			if (*p == '\n') {
				if (last_nl != NULL)
					start = last_nl + 1;
				last_nl = p;
				do {
					sz = strlen("manifest-hashes");
					if (strncmp(start, "manifest-hashes", sz))
						break;
					if ((q = strchr(start + sz, '=')) == NULL)
						break;
					q++;
					while (isspace((int)*q))
						q++;
					/* parse the tokens, whitespace separated */
					tok = q;
					do {
						while (!isspace((int)*q))
							q++;
						sz = q - tok;
						if (strncmp(tok, "SHA256", sz) == 0) {
							ret |= HASH_SHA256;
						} else if (strncmp(tok, "SHA512", sz) == 0) {
							ret |= HASH_SHA512;
						} else if (strncmp(tok, "WHIRLPOOL", sz) == 0) {
							ret |= HASH_WHIRLPOOL;
						} else if (strncmp(tok, "BLAKE2B", sz) == 0) {
							ret |= HASH_BLAKE2B;
						} else {
							fprintf(stderr, "warning: unsupported hash from "
									"layout.conf: %.*s\n", (int)sz, tok);
						}
						while (isspace((int)*q) && *q != '\n')
							q++;
						tok = q;
					} while (*q != '\n');
					/* got it, expect only once, so stop processing */
					fclose(f);
					return ret;
				} while (0);
			}
		}
		if (last_nl != NULL) {
			last_nl++;  /* skip \n */
			len = last_nl - buf;
			memmove(buf, last_nl, len);
		} else {
			/* skip too long line */
			len = 0;
		}
	}

	fclose(f);
	/* if we didn't find anything, return the default set */
	return HASH_DEFAULT;
}

static const char *str_manifest = "Manifest";
static const char *str_manifest_gz = "Manifest.gz";
static const char *str_manifest_files_gz = "Manifest.files.gz";
enum type_manifest {
	GLOBAL_MANIFEST,   /* Manifest.files.gz + Manifest */
	SUBTREE_MANIFEST,  /* Manifest.gz for recursive list of files */
	EBUILD_MANIFEST,   /* Manifest thick from thin */
	CATEGORY_MANIFEST  /* Manifest.gz with Manifest entries */
};
static const char *
generate_dir(const char *dir, enum type_manifest mtype)
{
	FILE *f;
	char path[8192];
	struct stat s;
	struct timeval tv[2];
	char **dentries;
	size_t dentrieslen;
	size_t i;

	/* our timestamp strategy is as follows:
	 * - when a Manifest exists, use its timestamp
	 * - when a meta-Manifest is written (non-ebuilds) use the timestamp
	 *   of the latest Manifest referenced
	 * - when a Manifest is written for something like eclasses, use the
	 *   timestamp of the latest file in the dir
	 * this way we should keep updates limited to where changes are, and
	 * also get reproducible mtimes. */
	tv[0].tv_sec = 0;
	tv[0].tv_usec = 0;
	tv[1].tv_sec = 0;
	tv[1].tv_usec = 0;

	if (mtype == GLOBAL_MANIFEST) {
		const char *mfest;
		size_t len;
		gzFile mf;
		time_t rtime;

		snprintf(path, sizeof(path), "%s/%s", dir, str_manifest_files_gz);
		if ((mf = gzopen(path, "wb9")) == NULL) {
			fprintf(stderr, "failed to open file '%s' for writing: %s\n",
					path, strerror(errno));
			return NULL;
		}

		/* These "IGNORE" entries are taken from gx86, there is no
		 * standardisation on this, on purpose, apparently. */
		len = snprintf(path, sizeof(path),
				"IGNORE distfiles\n"
				"IGNORE local\n"
				"IGNORE lost+found\n"
				"IGNORE packages\n"
				"IGNORE snapshots\n");
		if (gzwrite(mf, path, len) == 0) {
			fprintf(stderr, "failed to write to file '%s/%s': %s\n",
					dir, str_manifest_files_gz, strerror(errno));
			gzclose(mf);
			return NULL;
		}

		if (list_dir(&dentries, &dentrieslen, dir) != 0) {
			gzclose(mf);
			return NULL;
		}

		for (i = 0; i < dentrieslen; i++) {
			/* ignore existing Manifests */
			if (strcmp(dentries[i], str_manifest_files_gz) == 0 ||
					strcmp(dentries[i], str_manifest) == 0)
			{
				free(dentries[i]);
				continue;
			}

			snprintf(path, sizeof(path), "%s/%s", dir, dentries[i]);

			mfest = NULL;
			if (!stat(path, &s)) {
				if (s.st_mode & S_IFDIR) {
					if (
							strcmp(dentries[i], "eclass")   == 0 ||
							strcmp(dentries[i], "licenses") == 0 ||
							strcmp(dentries[i], "metadata") == 0 ||
							strcmp(dentries[i], "profiles") == 0 ||
							strcmp(dentries[i], "scripts")  == 0
					   )
					{
						mfest = generate_dir(path, SUBTREE_MANIFEST);
					} else {
						mfest = generate_dir(path, CATEGORY_MANIFEST);
					}

					if (mfest == NULL) {
						fprintf(stderr, "generating Manifest for %s failed!\n",
								path);
						gzclose(mf);
						for (; i < dentrieslen; i++)
							free(dentries[i]);
						free(dentries);
						return NULL;
					}

					snprintf(path, sizeof(path), "%s/%s",
							dentries[i], mfest);
					write_hashes(tv, dir, path, "MANIFEST", NULL, mf);
				} else if (s.st_mode & S_IFREG) {
					write_hashes(tv, dir, dentries[i], "DATA", NULL, mf);
				} /* ignore other "things" (like symlinks) as they
					 don't belong in a tree */
			} else {
				fprintf(stderr, "stat(%s) failed: %s\n",
						path, strerror(errno));
			}
			free(dentries[i]);
		}
		free(dentries);
		gzclose(mf);

		if (tv[0].tv_sec != 0) {
			snprintf(path, sizeof(path), "%s/%s", dir, str_manifest_files_gz);
			utimes(path, tv);
		}

		/* create global Manifest */
		snprintf(path, sizeof(path), "%s/%s", dir, str_manifest);
		if ((f = fopen(path, "w")) == NULL) {
			fprintf(stderr, "failed to open file '%s' for writing: %s\n",
					path, strerror(errno));
			return NULL;
		}

		write_hashes(tv, dir, str_manifest_files_gz, "MANIFEST", f, NULL);
		time(&rtime);
		len = strftime(path, sizeof(path),
				"TIMESTAMP %Y-%m-%dT%H:%M:%SZ\n", gmtime(&rtime));
		fwrite(path, len, 1, f);
		fflush(f);
		fclose(f);

		/* because we write a timestamp in Manifest, we don't mess with
		 * its mtime, else it would obviously lie */
		return str_manifest_files_gz;
	} else if (mtype == SUBTREE_MANIFEST) {
		const char *ldir;
		gzFile mf;

		snprintf(path, sizeof(path), "%s/%s", dir, str_manifest_gz);
		if ((mf = gzopen(path, "wb9")) == NULL) {
			fprintf(stderr, "failed to open file '%s' for writing: %s\n",
					path, strerror(errno));
			return NULL;
		}

		ldir = strrchr(dir, '/');
		if (ldir == NULL)
			ldir = dir;
		if (strcmp(ldir, "metadata") == 0) {
			size_t len;
			len = snprintf(path, sizeof(path),
					"IGNORE timestamp\n"
					"IGNORE timestamp.chk\n"
					"IGNORE timestamp.commit\n"
					"IGNORE timestamp.x\n");
			if (gzwrite(mf, path, len) == 0) {
				fprintf(stderr, "failed to write to file '%s/%s': %s\n",
						dir, str_manifest_gz, strerror(errno));
				gzclose(mf);
				return NULL;
			}
		}

		if (list_dir(&dentries, &dentrieslen, dir) != 0) {
			gzclose(mf);
			return NULL;
		}

		for (i = 0; i < dentrieslen; i++) {
			/* ignore existing Manifests */
			if (strcmp(dentries[i], str_manifest_gz) == 0) {
				free(dentries[i]);
				continue;
			}

			if (write_hashes_dir(tv, dir, dentries[i], mf) != 0)
				write_hashes(tv, dir, dentries[i], "DATA", NULL, mf);
			free(dentries[i]);
		}

		free(dentries);
		gzclose(mf);

		if (tv[0].tv_sec != 0) {
			/* set Manifest and dir mtime to most recent file found */
			snprintf(path, sizeof(path), "%s/%s", dir, str_manifest_gz);
			utimes(path, tv);
			utimes(dir, tv);
		}

		return str_manifest_gz;
	} else if (mtype == CATEGORY_MANIFEST) {
		const char *mfest;
		gzFile mf;
		const char *ret = str_manifest_gz;

		snprintf(path, sizeof(path), "%s/%s", dir, str_manifest_gz);
		if ((mf = gzopen(path, "wb9")) == NULL) {
			fprintf(stderr, "failed to open file '%s' for writing: %s\n",
					path, strerror(errno));
			return NULL;
		}

		if (list_dir(&dentries, &dentrieslen, dir) != 0) {
			gzclose(mf);
			return NULL;
		}

		for (i = 0; i < dentrieslen; i++) {
			/* ignore existing Manifests */
			if (strcmp(dentries[i], str_manifest_gz) == 0) {
				free(dentries[i]);
				continue;
			}

			snprintf(path, sizeof(path), "%s/%s", dir, dentries[i]);
			if (!stat(path, &s)) {
				if (s.st_mode & S_IFDIR) {
					mfest = generate_dir(path, EBUILD_MANIFEST);

					if (mfest == NULL) {
						fprintf(stderr, "generating Manifest for %s failed!\n",
								path);
						tv[0].tv_sec = 0;
						ret = NULL;
					} else {
						snprintf(path, sizeof(path), "%s/%s",
								dentries[i], mfest);
						write_hashes(tv, dir, path, "MANIFEST", NULL, mf);
					}
				} else if (s.st_mode & S_IFREG) {
					write_hashes(tv, dir, dentries[i], "DATA", NULL, mf);
				} /* ignore other "things" (like symlinks) as they
					 don't belong in a tree */
			} else {
				fprintf(stderr, "stat(%s) failed: %s\n",
						path, strerror(errno));
			}
			free(dentries[i]);
		}

		free(dentries);
		gzclose(mf);

		if (tv[0].tv_sec != 0) {
			/* set Manifest and dir mtime to most ebuild dir found */
			snprintf(path, sizeof(path), "%s/%s", dir, str_manifest_gz);
			utimes(path, tv);
			utimes(dir, tv);
		}

		return ret;
	} else if (mtype == EBUILD_MANIFEST) {
		char newmanifest[8192];
		FILE *m;

		snprintf(newmanifest, sizeof(newmanifest), "%s/.Manifest.new", dir);
		if ((m = fopen(newmanifest, "w")) == NULL) {
			fprintf(stderr, "failed to open file '%s' for writing: %s\n",
					newmanifest, strerror(errno));
			return NULL;
		}

		/* we know the Manifest is sorted, and stuff in files/ is
		 * prefixed with AUX, hence, if it exists, we need to do it
		 * first */
		snprintf(path, sizeof(path), "%s/files", dir);
		process_files(tv, path, "", m);

		/* the Manifest file may be missing in case there are no DIST
		 * entries to be stored */
		snprintf(path, sizeof(path), "%s/%s", dir, str_manifest);
		if (!stat(path, &s))
			update_times(tv, &s);
		f = fopen(path, "r");
		if (f != NULL) {
			/* copy the DIST entries, we could do it unconditional, but this
			 * way we can re-run without producing invalid Manifests */
			while (fgets(path, sizeof(path), f) != NULL) {
				if (strncmp(path, "DIST ", 5) == 0)
					if (fwrite(path, strlen(path), 1, m) != 1) {
						fprintf(stderr, "failed to write to "
								"%s/.Manifest.new: %s\n",
								dir, strerror(errno));
						fclose(f);
						fclose(m);
						return NULL;
					}
			}
			fclose(f);
		}

		if (list_dir(&dentries, &dentrieslen, dir) == 0) {
			for (i = 0; i < dentrieslen; i++) {
				if (strcmp(dentries[i] + strlen(dentries[i]) - 7,
							".ebuild") != 0)
				{
					free(dentries[i]);
					continue;
				}
				write_hashes(tv, dir, dentries[i], "EBUILD", m, NULL);
				free(dentries[i]);
			}
			free(dentries);
		}

		write_hashes(tv, dir, "ChangeLog", "MISC", m, NULL);
		write_hashes(tv, dir, "metadata.xml", "MISC", m, NULL);

		fflush(m);
		fclose(m);

		snprintf(path, sizeof(path), "%s/%s", dir, str_manifest);
		if (rename(newmanifest, path) == -1) {
			fprintf(stderr, "failed to rename file '%s' to '%s': %s\n",
					newmanifest, path, strerror(errno));
			return NULL;
		}

		if (tv[0].tv_sec != 0) {
			/* set Manifest and dir mtime to most recent file we found */
			utimes(path, tv);
			utimes(dir, tv);
		}

		return str_manifest;
	} else {
		return NULL;
	}
}

static gpgme_error_t
gpgme_pw_cb(void *opaque, const char *uid_hint, const char *pw_info,
		int last_was_bad, int fd)
{
	char *pass = (char *)opaque;
	size_t passlen = strlen(pass);
	ssize_t ret;

	(void)uid_hint;
	(void)pw_info;
	(void)last_was_bad;

	do {
		ret = write(fd, pass, passlen);
		if (ret > 0) {
			pass += ret;
			passlen -= ret;
		}
	} while (passlen > 0 && ret > 0);

	return passlen == 0 ? GPG_ERR_NO_ERROR : gpgme_error_from_errno(errno);
}

static const char *
process_dir_gen(void)
{
	char path[_Q_PATH_MAX];
	int newhashes;
	struct termios termio;
	char *gpg_pass;

	if ((newhashes = parse_layout_conf("metadata/layout.conf")) != 0) {
		hashes = newhashes;
	} else {
		return "generation must be done on a full tree";
	}

	if (generate_dir(".\0", GLOBAL_MANIFEST) == NULL)
		return "generation failed";

	if (gpg_sign_key != NULL) {
		gpgme_ctx_t gctx;
		gpgme_error_t gerr;
		gpgme_key_t gkey;
		gpgme_data_t manifest;
		gpgme_data_t out;
		FILE *f;
		size_t dlen;

		gerr = gpgme_new(&gctx);
		if (gerr != GPG_ERR_NO_ERROR)
			return "GPG setup failed";

		gerr = gpgme_get_key(gctx, gpg_sign_key, &gkey, 0);
		if (gerr != GPG_ERR_NO_ERROR)
			return "failed to get GPG key";
		gerr = gpgme_signers_add(gctx, gkey);
		if (gerr != GPG_ERR_NO_ERROR)
			return "failed to add GPG key to sign list, is it a suitable key?";
		gpgme_key_unref(gkey);

		gpg_pass = NULL;
		if (gpg_get_password) {
			if (isatty(fileno(stdin))) {
				/* disable terminal echo; the printing of what you type */
				tcgetattr(fileno(stdin), &termio);
				termio.c_lflag &= ~ECHO;
				tcsetattr(fileno(stdin), TCSANOW, &termio);

				printf("Password for GPG-key %s: ", gpg_sign_key);
			}

			gpg_pass = fgets(path, sizeof(path), stdin);

			if (isatty(fileno(stdin))) {
				printf("\n");
				/* restore echoing, for what it's worth */
				termio.c_lflag |= ECHO;
				tcsetattr(fileno(stdin), TCSANOW, &termio);
			}

			if (gpg_pass == NULL || *gpg_pass == '\0')
				warn("no GPG password given, gpg might ask for it again");
				/* continue for the case where gpg-agent holds the pass */
			else {
				gpgme_set_pinentry_mode(gctx, GPGME_PINENTRY_MODE_LOOPBACK);
				gpgme_set_passphrase_cb(gctx, gpgme_pw_cb, gpg_pass);
			}
		}

		if ((f = fopen(str_manifest, "r+")) == NULL)
			return "could not open top-level Manifest file";

		/* finally, sign the Manifest */
		if (gpgme_data_new_from_stream(&manifest, f) != GPG_ERR_NO_ERROR)
			return "failed to create GPG data from Manifest";

		if (gpgme_data_new(&out) != GPG_ERR_NO_ERROR)
			return "failed to create GPG output buffer";

		gerr = gpgme_op_sign(gctx, manifest, out, GPGME_SIG_MODE_CLEAR);
		if (gerr != GPG_ERR_NO_ERROR) {
			warn("%s: %s", gpgme_strsource(gerr), gpgme_strerror(gerr));
			return "failed to GPG sign Manifest";
		}

		/* write back signed Manifest */
		rewind(f);
		gpgme_data_seek(out, 0, SEEK_SET);
		do {
			dlen = gpgme_data_read(out, path, sizeof(path));
			fwrite(path, dlen, 1, f);
		} while (dlen == sizeof(path));
		fclose(f);

		gpgme_data_release(out);
		gpgme_data_release(manifest);
		gpgme_release(gctx);
	}

	return NULL;
}

static void
msgs_add(
		verify_msg **msgs,
		const char *manifest,
		const char *ebuild,
		const char *fmt, ...)
{
	char buf[4096];
	int len;
	va_list ap;
	verify_msg *msg;

	if (msgs == NULL || *msgs == NULL)
		return;

	msg = (*msgs)->next = xmalloc(sizeof(verify_msg));

	len = snprintf(buf, sizeof(buf), "%s:%s:",
			manifest ? manifest : "",
			ebuild   ? ebuild   : "");

	va_start(ap, fmt);
	vsnprintf(buf + len, sizeof(buf) - len, fmt, ap);
	va_end(ap);

	msg->msg = xstrdup(buf);
	msg->next = NULL;
	*msgs = msg;
}

gpg_sig *
verify_gpg_sig(const char *path, verify_msg **msgs)
{
	gpgme_ctx_t g_ctx;
	gpgme_data_t manifest;
	gpgme_data_t out;
	gpgme_verify_result_t vres;
	gpgme_signature_t sig;
	gpgme_key_t key;
	char buf[64];
	FILE *f;
	struct tm *ctime;
	gpg_sig *ret = NULL;

	if (gpgme_new(&g_ctx) != GPG_ERR_NO_ERROR) {
		msgs_add(msgs, path, NULL, "failed to create gpgme context");
		return NULL;
	}

	if (gpgme_data_new(&out) != GPG_ERR_NO_ERROR) {
		msgs_add(msgs, path, NULL, "failed to create gpgme data");
		gpgme_release(g_ctx);
		return NULL;
	}

	if ((f = fopen(path, "r")) == NULL) {
		msgs_add(msgs, path, NULL, "failed to open: %s", strerror(errno));
		gpgme_data_release(out);
		gpgme_release(g_ctx);
		return NULL;
	}

	if (gpgme_data_new_from_stream(&manifest, f) != GPG_ERR_NO_ERROR) {
		msgs_add(msgs, path, NULL,
				"failed to create new gpgme data from stream");
		gpgme_data_release(out);
		gpgme_release(g_ctx);
		fclose(f);
		return NULL;
	}

	if (gpgme_op_verify(g_ctx, manifest, NULL, out) != GPG_ERR_NO_ERROR) {
		msgs_add(msgs, path, NULL, "failed to verify signature");
		gpgme_data_release(out);
		gpgme_data_release(manifest);
		gpgme_release(g_ctx);
		fclose(f);
		return NULL;
	}

	vres = gpgme_op_verify_result(g_ctx);
	fclose(f);

	if (vres == NULL || vres->signatures == NULL) {
		msgs_add(msgs, path, NULL,
				"verification failed due to a missing gpg keyring");
		gpgme_data_release(out);
		gpgme_data_release(manifest);
		gpgme_release(g_ctx);
		return NULL;
	}

	/* we only check/return the first signature */
	if ((sig = vres->signatures) != NULL) {
		ret = xmalloc(sizeof(gpg_sig));

		if (sig->fpr != NULL) {
			snprintf(buf, sizeof(buf),
					"%.4s %.4s %.4s %.4s %.4s  %.4s %.4s %.4s %.4s %.4s",
					sig->fpr +  0, sig->fpr +  4, sig->fpr +  8, sig->fpr + 12,
					sig->fpr + 16, sig->fpr + 20, sig->fpr + 24, sig->fpr + 28,
					sig->fpr + 32, sig->fpr + 36);
		} else {
			snprintf(buf, sizeof(buf), "<fingerprint not found>");
		}

		if (sig->status != GPG_ERR_NO_PUBKEY) {
			ret->algo = xstrdup(gpgme_pubkey_algo_name(sig->pubkey_algo));
			ret->fingerprint = xstrdup(buf);
			ret->isgood = sig->status == GPG_ERR_NO_ERROR ? 1 : 0;
			ctime = gmtime((time_t *)&sig->timestamp);
			strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", ctime);
			ret->timestamp = xstrdup(buf);

			if (gpgme_get_key(g_ctx, sig->fpr, &key, 0) == GPG_ERR_NO_ERROR) {
				if (key->uids != NULL)
					ret->signer = xstrdup(key->uids->uid);
				if (key->subkeys != NULL) {
					snprintf(buf, sizeof(buf),
							"%.4s %.4s %.4s %.4s %.4s  "
							"%.4s %.4s %.4s %.4s %.4s",
							key->subkeys->fpr +  0, key->subkeys->fpr +  4,
							key->subkeys->fpr +  8, key->subkeys->fpr + 12,
							key->subkeys->fpr + 16, key->subkeys->fpr + 20,
							key->subkeys->fpr + 24, key->subkeys->fpr + 28,
							key->subkeys->fpr + 32, key->subkeys->fpr + 36);
					ret->pkfingerprint = xstrdup(buf);
				}
				gpgme_key_release(key);
			}
		}

		switch (sig->status) {
			case GPG_ERR_NO_ERROR:
				/* nothing */
				ret->reason = NULL;
				break;
			case GPG_ERR_SIG_EXPIRED:
				ret->reason = xstrdup("the signature is valid but expired");
				break;
			case GPG_ERR_KEY_EXPIRED:
				ret->reason = xstrdup("the signature is valid but the key "
						"used to verify the signature has expired");
				break;
			case GPG_ERR_CERT_REVOKED:
				ret->reason = xstrdup("the signature is valid but the key "
						"used to verify the signature has been revoked");
				break;
			case GPG_ERR_BAD_SIGNATURE:
				free(ret);
				ret = NULL;
				printf("the signature is invalid\n");
				break;
			case GPG_ERR_NO_PUBKEY:
				free(ret);
				ret = NULL;
				printf("the signature could not be verified due to a "
						"missing key for:\n  %s\n", buf);
				break;
			default:
				free(ret);
				ret = NULL;
				printf("there was some error which prevented the "
						"signature verification:\n  %s: %s\n",
						buf, gpgme_strerror(sig->status));
				break;
		}
	}

	gpgme_data_release(out);
	gpgme_data_release(manifest);
	gpgme_release(g_ctx);

	return ret;
}

static size_t checked_manifests = 0;
static size_t checked_files = 0;
static size_t failed_files = 0;
static char strict = 0;

static char
verify_file(const char *dir, char *mfline, const char *mfest, verify_msg **msgs)
{
	char *path;
	char *size;
	long long int fsize;
	char *hashtype;
	char *hash;
	char *p;
	char buf[8192];
	size_t flen = 0;
	char sha256[(SHA256_DIGEST_LENGTH * 2) + 1];
	char sha512[(SHA512_DIGEST_LENGTH * 2) + 1];
	char blak2b[(BLAKE2B_OUTBYTES * 2) + 1];
	char ret = 0;

	/* mfline is a Manifest file line with type and leading path
	 * stripped, something like:
	 * file <SIZE> <HASHTYPE HASH ...>
	 * we parse this, and verify the size and hashes */

	path = mfline;
	p = strchr(path, ' ');
	if (p == NULL) {
		msgs_add(msgs, mfest, NULL, "corrupt manifest line: %s", path);
		return 1;
	}
	*p++ = '\0';

	size = p;
	p = strchr(size, ' ');
	if (p == NULL) {
		msgs_add(msgs, mfest, NULL, "corrupt manifest line, need size");
		return 1;
	}
	*p++ = '\0';
	fsize = strtoll(size, NULL, 10);
	if (fsize == 0 && errno == EINVAL) {
		msgs_add(msgs, mfest, NULL, "corrupt manifest line, "
				"size is not a number: %s", size);
		return 1;
	}

	sha256[0] = sha512[0] = blak2b[0] = '\0';
	snprintf(buf, sizeof(buf), "%s/%s", dir, path);
	hash_compute_file(buf, sha256, sha512, blak2b, &flen, hashes);

	if (flen == 0) {
		msgs_add(msgs, mfest, path, "cannot open file!");
		return 1;
	}

	checked_files++;

	if (flen != (size_t)fsize) {
		msgs_add(msgs, mfest, path,
				"file size mismatch\n"
				"     got: %zd\n"
				"expected: %lld",
				flen, fsize);
		failed_files++;
		return 1;
	}

	/* now we are in free territory, we read TYPE HASH pairs until we
	 * drained the string, and match them against what we computed */
	while (p != NULL && *p != '\0') {
		hashtype = p;
		p = strchr(hashtype, ' ');
		if (p == NULL) {
			msgs_add(msgs, mfest, path,
					"corrupt manifest line, missing hash type");
			return 1;
		}
		*p++ = '\0';

		hash = p;
		p = strchr(hash, ' ');
		if (p != NULL)
			*p++ = '\0';

		if (strcmp(hashtype, "SHA256") == 0) {
			if (!(hashes & HASH_SHA256)) {
				if (strict)
					msgs_add(msgs, mfest, path,
							"hash SHA256 is not "
							"enabled for this repository");
			} else if (strcmp(hash, sha256) != 0) {
				msgs_add(msgs, mfest, path,
						"SHA256 hash mismatch\n"
						"computed: '%s'\n"
						"Manifest: '%s'",
						sha256, hash);
				ret = 1;
			}
			sha256[0] = '\0';
		} else if (strcmp(hashtype, "SHA512") == 0) {
			if (!(hashes & HASH_SHA512)) {
				if (strict)
					msgs_add(msgs, mfest, path,
							"hash SHA512 is not "
							"enabled for this repository");
			} else if (strcmp(hash, sha512) != 0) {
				msgs_add(msgs, mfest, path,
						"SHA512 hash mismatch\n"
						"computed: '%s'\n"
						"Manifest: '%s'",
						sha512, hash);
				ret = 1;
			}
			sha512[0] = '\0';
		} else if (strcmp(hashtype, "WHIRLPOOL") == 0) {
			if (!(hashes & HASH_WHIRLPOOL)) {
				if (strict)
					msgs_add(msgs, mfest, path,
							"hash WHIRLPOOL is not "
							"enabled for this repository");
			} else {
				if (strict)
					msgs_add(msgs, mfest, path,
							"hash WHIRLPOOL is not "
							"supported by qmanifest");
			}
		} else if (strcmp(hashtype, "BLAKE2B") == 0) {
			if (!(hashes & HASH_BLAKE2B)) {
				if (strict)
					msgs_add(msgs, mfest, path,
							"hash BLAKE2B is not "
							"enabled for this repository");
			} else if (strcmp(hash, blak2b) != 0) {
				msgs_add(msgs, mfest, path,
						"BLAKE2B hash mismatch\n"
						"computed: '%s'\n"
						"Manifest: '%s'",
						blak2b, hash);
				ret = 1;
			}
			blak2b[0] = '\0';
		} else {
			msgs_add(msgs, mfest, path, "unsupported hash: %s", hashtype);
			ret = 1;
		}
	}

	if (sha256[0] != '\0') {
		msgs_add(msgs, mfest, path, "missing hash: SHA256");
		ret = 1;
	}
	if (sha512[0] != '\0') {
		msgs_add(msgs, mfest, path, "missing hash: SHA512");
		ret = 1;
	}
	if (blak2b[0] != '\0') {
		msgs_add(msgs, mfest, path, "missing hash: BLAKE2B");
		ret = 1;
	}

	failed_files += ret;
	return ret;
}

static int
compare_elems(const void *l, const void *r)
{
	const char *strl = *((const char **)l) + 2;
	const char *strr = *((const char **)r) + 2;
	unsigned char cl;
	unsigned char cr;
	/* compare treating / as end of string */
	while ((cl = *strl++) == (cr = *strr++))
		if (cl == '\0')
			return 0;
	if (cl == '/')
		cl = '\0';
	if (cr == '/')
		cr = '\0';
	return cl - cr;
}

struct subdir_workload {
	size_t subdirlen;
	size_t elemslen;
	char **elems;
};

static char
verify_dir(
		const char *dir,
		char **elems,
		size_t elemslen,
		size_t skippath,
		const char *mfest,
		verify_msg **msgs)
{
	char **dentries = NULL;
	size_t dentrieslen = 0;
	size_t curelem = 0;
	size_t curdentry = 0;
	char *entry;
	char *slash;
	char etpe;
	char ret = 0;
	int cmp;
	struct subdir_workload **subdir = NULL;
	size_t subdirsize = 0;
	size_t subdirlen = 0;
	size_t elem;

	/* shortcut a single Manifest entry pointing to the same dir
	 * (happens at top-level) */
	if (elemslen == 1 && skippath == 0 &&
			**elems == 'M' && strchr(*elems + 2, '/') == NULL)
	{
		if ((ret = verify_file(dir, *elems + 2, mfest, msgs)) == 0) {
			slash = strchr(*elems + 2, ' ');
			if (slash != NULL)
				*slash = '\0';
			/* else, verify_manifest will fail, so ret will be handled */
			ret = verify_manifest(dir, *elems + 2, msgs);
		}
		return ret;
	}

	/*
	 * We have a list of entries from the manifest just read, now we
	 * need to match these onto the directory layout.  From what we got
	 * - we can ignore TIMESTAMP and DIST entries
	 * - IGNOREs need to be handled separate (shortcut)
	 * - MANIFESTs need to be handled on their own, for memory
	 *   consumption reasons, we defer them to until we've verified
	 *   what's left, we treat the path the Manifest refers to as IGNORE
	 * - DATAs, EBUILDs and MISCs needs verifying
	 * - AUXs need verifying, but in files/ subdir
	 * If we sort both lists, we should be able to do a merge-join, to
	 * easily flag missing entries in either list without hashing or
	 * anything.
	 */
	if (list_dir(&dentries, &dentrieslen, dir) == 0) {
		while (curdentry < dentrieslen) {
			if (strcmp(dentries[curdentry], str_manifest) == 0 ||
					strcmp(dentries[curdentry], str_manifest_gz) == 0 ||
					strcmp(dentries[curdentry], str_manifest_files_gz) == 0)
			{
				curdentry++;
				continue;
			}

			if (curelem < elemslen) {
				entry = elems[curelem] + 2 + skippath;
				etpe = *elems[curelem];
			} else {
				entry = (char *)"";
				etpe = 'I';
			}

			/* handle subdirs first */
			if ((slash = strchr(entry, '/')) != NULL) {
				size_t sublen = slash - entry;
				int elemstart = curelem;
				char **subelems = &elems[curelem];

				/* collect all entries like this one (same subdir) into
				 * a sub-list that we can verify */
				curelem++;
				while (curelem < elemslen &&
						strncmp(entry, elems[curelem] + 2 + skippath,
							sublen + 1) == 0)
					curelem++;

				if (subdirlen == subdirsize) {
					subdirsize += LISTSZ;
					subdir = xrealloc(subdir,
							subdirsize * sizeof(subdir[0]));
					if (subdir == NULL) {
						msgs_add(msgs, mfest, NULL, "out of memory allocating "
								"sublist for %.*s", (int)sublen, entry);
						return 1;
					}
				}
				subdir[subdirlen] = xmalloc(sizeof(struct subdir_workload));
				subdir[subdirlen]->subdirlen = sublen;
				subdir[subdirlen]->elemslen = curelem - elemstart;
				subdir[subdirlen]->elems = subelems;
				subdirlen++;

				curelem--; /* move back, see below */

				/* modify the last entry to be the subdir, such that we
				 * can let the code below synchronise with dentries */
				elems[curelem][2 + skippath + sublen] = ' ';
				entry = elems[curelem] + 2 + skippath;
				etpe = 'S';  /* flag this was a subdir */
			}

			/* does this entry exist in list? */
			if (*entry == '\0') {
				/* end of list reached, force dir to catch up */
				cmp = 1;
			} else {
				slash = strchr(entry, ' ');
				if (slash != NULL)
					*slash = '\0';
				cmp = strcmp(entry, dentries[curdentry]);
				if (slash != NULL)
					*slash = ' ';
			}
			if (cmp == 0) {
				/* equal, so yay */
				if (etpe == 'D') {
					ret |= verify_file(dir, entry, mfest, msgs);
				}
				/* else this is I(GNORE) or S(ubdir), which means it is
				 * ok in any way (M shouldn't happen) */
				curelem++;
				curdentry++;
			} else if (cmp < 0) {
				/* entry is missing from dir */
				if (etpe == 'I') {
					/* right, we can ignore this */
				} else {
					ret |= 1;
					slash = strchr(entry, ' ');
					if (slash != NULL)
						*slash = '\0';
					msgs_add(msgs, mfest, entry, "%s file listed in Manifest, "
							"but not found", etpe == 'M' ? "MANIFEST" : "DATA");
					if (slash != NULL)
						*slash = ' ';
					failed_files++;
				}
				curelem++;
			} else if (cmp > 0) {
				/* dir has extra element */
				ret |= 1;
				msgs_add(msgs, mfest, NULL,
						"file not listed: %s", dentries[curdentry]);
				curdentry++;
				failed_files++;
			}
		}

		while (dentrieslen-- > 0)
			free(dentries[dentrieslen]);
		free(dentries);

#pragma omp parallel for shared(ret) private(entry, etpe, slash)
		for (elem = 0; elem < subdirlen; elem++) {
			char ndir[8192];

			entry = subdir[elem]->elems[0] + 2 + skippath;
			etpe = subdir[elem]->elems[0][0];

			/* restore original entry format */
			subdir[elem]->elems[subdir[elem]->elemslen - 1]
				[2 + skippath + subdir[elem]->subdirlen] = '/';

			if (etpe == 'M') {
				size_t skiplen = strlen(dir) + 1 + subdir[elem]->subdirlen;
				/* sub-Manifest, we need to do a proper recurse */
				slash = strrchr(entry, '/');  /* cannot be NULL */
				snprintf(ndir, sizeof(ndir), "%s/%s", dir, entry);
				ndir[skiplen] = '\0';
				slash = strchr(ndir + skiplen + 1, ' ');
				if (slash != NULL)  /* path should fit in ndir ... */
					*slash = '\0';
				if (verify_file(dir, entry, mfest, msgs) != 0 ||
						verify_manifest(ndir, ndir + skiplen + 1, msgs) != 0)
					ret |= 1;
			} else {
				snprintf(ndir, sizeof(ndir), "%s/%.*s", dir,
						(int)subdir[elem]->subdirlen, entry);
				ret |= verify_dir(ndir, subdir[elem]->elems,
						subdir[elem]->elemslen,
						skippath + subdir[elem]->subdirlen + 1, mfest, msgs);
			}

			free(subdir[elem]);
		}

		if (subdir)
			free(subdir);

		return ret;
	} else {
		return 1;
	}
}

char
verify_manifest(
		const char *dir,
		const char *manifest,
		verify_msg **msgs)
{
	char buf[8192];
	FILE *f;
	gzFile mf;
	char ret = 0;

	size_t elemssize = 0;
	size_t elemslen = 0;
	char **elems = NULL;
#define append_list(STR) \
	if (strncmp(STR, "TIMESTAMP ", 10) != 0 || strncmp(STR, "DIST ", 5) != 0) {\
		char *endp = STR + strlen(STR) - 1;\
		while (isspace(*endp))\
			*endp-- = '\0';\
		if (elemslen == elemssize) {\
			elemssize += LISTSZ;\
			elems = xrealloc(elems, elemssize * sizeof(elems[0]));\
		}\
		if (strncmp(STR, "IGNORE ", 7) == 0) {\
			STR[5] = 'I';\
			elems[elemslen] = xstrdup(STR + 5);\
			elemslen++;\
		} else if (strncmp(STR, "MANIFEST ", 9) == 0) {\
			STR[7] = 'M';\
			elems[elemslen] = xstrdup(STR + 7);\
			elemslen++;\
		} else if (strncmp(STR, "DATA ", 5) == 0 ||\
				strncmp(STR, "MISC ", 5) == 0 ||\
				strncmp(STR, "EBUILD ", 7) == 0)\
		{\
			if (*STR == 'E') {\
				STR[5] = 'D';\
				elems[elemslen] = xstrdup(STR + 5);\
			} else {\
				STR[3] = 'D';\
				elems[elemslen] = xstrdup(STR + 3);\
			}\
			elemslen++;\
		} else if (strncmp(STR, "AUX ", 4) == 0) {\
			/* translate directly into what it is: DATA in files/ */\
			size_t slen = strlen(STR + 2) + sizeof("files/");\
			elems[elemslen] = xmalloc(slen);\
			snprintf(elems[elemslen], slen, "D files/%s", STR + 4);\
			elemslen++;\
		}\
	}

	snprintf(buf, sizeof(buf), "%s/%s", dir, manifest);
	if (strcmp(manifest, str_manifest) == 0) {
		if ((f = fopen(buf, "r")) == NULL) {
			msgs_add(msgs, buf, NULL, "failed to open %s: %s\n",
					manifest, strerror(errno));
			return 1;
		}
		while (fgets(buf, sizeof(buf), f) != NULL) {
			append_list(buf);
		}
		fclose(f);
	} else if (strcmp(manifest, str_manifest_files_gz) == 0 ||
			strcmp(manifest, str_manifest_gz) == 0)
	{
		if ((mf = gzopen(buf, "rb9")) == NULL) {
			msgs_add(msgs, buf, NULL, "failed to open %s: %s\n",
					manifest, strerror(errno));
			return 1;
		}
		while (gzgets(mf, buf, sizeof(buf)) != NULL) {
			append_list(buf);
		}
		gzclose(mf);
	}

	/* The idea:
	 * - Manifest without MANIFEST entries, we need to scan the entire
	 *   subtree
	 * - Manifest with MANIFEST entries, assume they are just one level
	 *   deeper, thus ignore that subdir, further like above
	 * - Manifest at top-level, needs to be igored as it only points to
	 *   the larger Manifest.files.gz
	 */
	if (elemslen > 1)
		qsort(elems, elemslen, sizeof(elems[0]), compare_elems);
	snprintf(buf, sizeof(buf), "%s/%s", dir, manifest);
	ret = verify_dir(dir, elems, elemslen, 0, buf + 2, msgs);
	checked_manifests++;

	while (elemslen-- > 0)
		free(elems[elemslen]);
	free(elems);

	return ret;
}

char *
verify_timestamp(const char *ts)
{
	char buf[8192];
	FILE *f;
	char *ret = NULL;

	if ((f = fopen(ts, "r")) != NULL) {
		while (fgets(buf, sizeof(buf), f) != NULL) {
			if (strncmp(buf, "TIMESTAMP ", 10) == 0) {
				char *endp = buf + strlen(buf) - 1;
				while (isspace(*endp))
					*endp-- = '\0';
				ret = xstrdup(buf + 10);
				break;
			}
		}
		fclose(f);
	}

	return ret;
}

static void
format_line(const char *pfx, const char *msg)
{
	size_t msglen = strlen(pfx) + strlen(msg);

	if (*pfx == '-') {
		fprintf(stdout, "%s%s%s%s\n", pfx, RED, msg, NORM);
	} else {
		if (!verbose && twidth > 0 && msglen > (size_t)twidth) {
			int to_remove = 3 + (msglen - twidth);
			int first_half = msglen / 2 - to_remove / 2;
			int remainder = msglen / 2 + (to_remove + 1) / 2;
			fprintf(stdout, "%s%.*s...%s\n",
					pfx, first_half, msg, msg + remainder);
		} else {
			fprintf(stdout, "%s%s\n", pfx, msg);
		}
	}
}

static const char *
process_dir_vrfy(void)
{
	char buf[8192];
	int newhashes;
	const char *ret = NULL;
	struct timeval startt;
	struct timeval finisht;
	double etime;
	char *timestamp;
	verify_msg topmsg;
	verify_msg *walk = &topmsg;
	verify_msg *next;
	gpg_sig *gs;

	walk->next = NULL;
	gettimeofday(&startt, NULL);

	snprintf(buf, sizeof(buf), "metadata/layout.conf");
	if ((newhashes = parse_layout_conf(buf)) != 0) {
		hashes = newhashes;
	} else {
		return "verification must be done on a full tree";
	}

	if ((gs = verify_gpg_sig(str_manifest, &walk)) == NULL) {
		ret = "gpg signature invalid";
	} else {
		fprintf(stdout,
				"%s%s%s signature made %s by\n"
				"  %s%s%s\n"
				"primary key fingerprint %s\n"
				"%4s subkey fingerprint %s\n",
				gs->isgood ? GREEN : RED,
				gs->isgood ? "good": "BAD",
				NORM, gs->timestamp,
				DKBLUE, gs->signer, NORM,
				gs->pkfingerprint,
				gs->algo, gs->fingerprint);
		if (!gs->isgood)
			fprintf(stdout, "reason: %s%s%s\n", RED, gs->reason, NORM);
		free(gs->algo);
		free(gs->fingerprint);
		free(gs->timestamp);
		free(gs->signer);
		free(gs->pkfingerprint);
		if (!gs->isgood)
			free(gs->reason);
		free(gs);
	}

	if ((timestamp = verify_timestamp(str_manifest)) != NULL) {
		fprintf(stdout, "%s timestamp: %s\n", str_manifest, timestamp);
		free(timestamp);
	} else {
		ret = "manifest timestamp entry missing";
	}

	/* verification goes like this:
	 * - verify the signature of the top-level Manifest file (done
	 *   above)
	 * - read the contents of the Manifest file, and process the
	 *   entries - verify them, check there are no files which shouldn't
	 *   be there
	 * - recurse into directories for which Manifest files are defined
	 */
	if (verify_manifest(".\0", str_manifest, &walk) != 0)
		ret = "manifest verification failed";

	gettimeofday(&finisht, NULL);

	/* produce a report */
	{
		char *mfest;
		char *ebuild;
		char *msg;
		char *lastmfest = (char *)"-";
		char *lastebuild = (char *)"-";
		char *msgline;
		const char *pfx;

		for (walk = topmsg.next; walk != NULL; walk = walk->next) {
			mfest = walk->msg;
			ebuild = strchr(mfest, ':');
			if (ebuild != NULL) {
				*ebuild++ = '\0';
				msg = strchr(ebuild, ':');
				if (msg != NULL)
					*msg++ = '\0';
			}
			if (ebuild != NULL && msg != NULL) {
				if (strcmp(mfest, lastmfest) != 0 ||
						strcmp(ebuild, lastebuild) != 0)
				{
					char *mycat = mfest;
					char *mypkg = NULL;

					if ((mfest = strchr(mycat, '/')) != NULL) {
						*mfest++ = '\0';
						mypkg = mfest;
						if ((mfest = strchr(mypkg, '/')) != NULL) {
							*mfest++ = '\0';
						} else {
							mfest = mypkg;
							mypkg = NULL;
						}
					} else {
						mfest = mycat;
						mycat = NULL;
					}

					fprintf(stdout, "%s%s%s" "%s%s%s%s" "%s%s" "%s%s%s%s\n",
							mycat == NULL ? "" : BOLD,
							mycat == NULL ? "" : mycat,
							mycat == NULL ? "" : "/",
							mypkg == NULL ? "" : BLUE,
							mypkg == NULL ? "" : mypkg,
							mypkg == NULL ? "" : NORM,
							mypkg == NULL ? "" : "/",
							mfest, *ebuild == '\0' ? ":" : "::",
							CYAN, ebuild, NORM, *ebuild == '\0' ? "" : ":");
				}

				lastmfest = mfest;
				lastebuild = ebuild;

				pfx = "- ";
				msgline = msg;
				while ((msgline = strchr(msgline, '\n')) != NULL) {
					*msgline++ = '\0';
					format_line(pfx, msg);
					pfx = "  ";
					msg = msgline;
				}
				format_line(pfx, msg);
			}
		}
	}

	/* clean up messages */
	walk = topmsg.next;
	while (walk != NULL) {
		next = walk->next;
		free(walk->msg);
		free(walk);
		walk = next;
	}

	etime = ((double)((finisht.tv_sec - startt.tv_sec) * 1000000 +
				finisht.tv_usec) - (double)startt.tv_usec) / 1000000.0;
	printf("checked %zd Manifests, %zd files, %zd failures in %.02fs\n",
			checked_manifests, checked_files, failed_files, etime);
	return ret;
}

int
qmanifest_main(int argc, char **argv)
{
	char *prog;
	const char *(*runfunc)(void);
	int ret;
	const char *rsn;
	bool isdir = false;
	bool isoverlay = false;
	char *overlay;
	char path[_Q_PATH_MAX];
	char path2[_Q_PATH_MAX];
	size_t n;
	int i;
	int curdirfd;

	if ((prog = strrchr(argv[0], '/')) == NULL) {
		prog = argv[0];
	} else {
		prog++;
	}
	if (*prog == 'q')
		prog++;

	runfunc = NULL;
	if (strcmp(prog, "hashverify") == 0) {
		runfunc = process_dir_vrfy;
	} else if (strcmp(prog, "hashgen") == 0) {
		runfunc = process_dir_gen;
	}

	while ((ret = GETOPT_LONG(QMANIFEST, qmanifest, "")) != -1) {
		switch (ret) {
			COMMON_GETOPTS_CASES(qmanifest)
			case 'g': runfunc = process_dir_gen;  break;
			case 's': gpg_sign_key = optarg;      break;
			case 'p': gpg_get_password = true;    break;
			case 'd': isdir = true;               break;
			case 'o': isoverlay = true;           break;
		}
	}

	if (isdir && isoverlay) {
		warn("cannot specify both directories (-d) and overlays (-o), "
				"continuing using overlays only");
		isdir = false;
	}

	if (runfunc == NULL)
		/* default mode: verify */
		runfunc = process_dir_vrfy;

	gpgme_check_version(NULL);

	if (isoverlay || (!isdir && !isoverlay)) {
		char *repo;
		size_t repolen;

		array_for_each(overlays, n, overlay) {
			repo = xarrayget(overlay_names, n);
			if (strcmp(repo, "<PORTDIR>") == 0) {
				repo = NULL;
				repolen = 0;
				snprintf(path, sizeof(path), "%s/profiles/repo_name", overlay);
				if (eat_file(path, &repo, &repolen)) {
					free(array_get_elem(overlays, n));
					array_get_elem(overlays, n) = repo;
				} else {
					free(repo);
				}
			}
		}
	}

	if ((curdirfd = open(".", O_RDONLY)) < 0) {
		warn("cannot open current directory?!? %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	ret = EXIT_SUCCESS;
	argc -= optind;
	argv += optind;
	for (i = 0; i < argc; i++) {
		array_for_each(overlay_names, n, overlay) {
			if (strcmp(overlay, argv[i]) == 0) {
				overlay = xarrayget(overlays, n);
				break;
			}
			overlay = NULL;
		}

		/* behaviour:
		 * if isdir is set: treat argument as directory
		 * if isoverlay is set: treat argument as overlay (look it up)
		 * if neither is set: treat argument as overlay if it exists,
		 *                    else as directory */

		if (isoverlay && overlay == NULL) {
			warn("no such overlay: %s", argv[i]);
			ret |= 1;
			continue;
		}
		if (isdir || (!isoverlay && overlay == NULL)) /* !isdir && !isoverlay */
			overlay = argv[i];

		if (*overlay != '/') {
			if (portroot[1] == '\0') {
				/* resolve the path */
				if (fchdir(curdirfd) != 0)
					continue;  /* this shouldn't happen */
				if (realpath(overlay, path) == NULL && *path == '\0') {
					warn("could not resolve %s", overlay);
					continue;  /* very unlikely */
				}
			} else {
				snprintf(path, sizeof(path), "./%s", overlay);
			}
		} else {
			snprintf(path, sizeof(path), "%s", overlay);
		}

		snprintf(path2, sizeof(path2), "%s%s", portroot, path);
		if (chdir(path2) != 0) {
			warn("cannot change directory to %s: %s", overlay, strerror(errno));
			ret |= 1;
			continue;
		}

		if (runfunc == process_dir_vrfy)
			printf("verifying %s%s%s...\n", BOLD, overlay, NORM);

		rsn = runfunc();
		if (rsn != NULL) {
			printf("%s%s%s\n", RED, rsn, NORM);
			ret |= 2;
		}
	}

	if (i == 0) {
		snprintf(path, sizeof(path), "%s%s", portroot, main_overlay);
		if (chdir(path) != 0) {
			warn("cannot change directory to %s: %s",
					main_overlay, strerror(errno));
			ret |= 1;
		} else {
			if (runfunc == process_dir_vrfy)
				printf("verifying %s%s%s...\n", BOLD, main_overlay, NORM);

			rsn = runfunc();
			if (rsn != NULL) {
				printf("%s%s%s\n", RED, rsn, NORM);
				ret |= 2;
			}
		}
	}

	/* return to where we were before we called this function */
	if (fchdir(curdirfd) != 0 && verbose > 1)
		warn("could not move back to original directory");
	close(curdirfd);

	return ret;
}

#endif
