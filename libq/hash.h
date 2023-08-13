/*
 * Copyright 2018-2020 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _HASH_H
#define _HASH_H 1

/* for AT_FDCWD */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

enum hash_impls {
	HASH_MD5       = 1<<0,
	HASH_SHA1      = 1<<1,
	HASH_SHA256    = 1<<2,
	HASH_SHA512    = 1<<3,
	HASH_WHIRLPOOL = 1<<4,
	HASH_BLAKE2B   = 1<<5
};

/* default changed from sha256, sha512, whirlpool
 * to blake2b, sha512 on 2017-11-21 */
#define HASH_DEFAULT  (HASH_BLAKE2B | HASH_SHA512);

/* pass in a fd and get back a fd; filename is for display only */
typedef int (*hash_cb_t) (int, const char *);

void hash_hex(char *out, const unsigned char *buf, const int length);
int hash_multiple_file_fd(
		int fd, char *md5, char *sha1, char *sha256, char *sha512,
		char *whrlpl, char *blak2b, size_t *flen, int hashes);
int hash_multiple_file_at_cb(
		int pfd, const char *fname, hash_cb_t cb, char *md5,
		char *sha1, char *sha256, char *sha512, char *whrlpl,
		char *blak2b, size_t *flen, int hashes);
#define hash_multiple_file(f, m, s1, s2, s5, w, b, l, h) \
	hash_multiple_file_at_cb(AT_FDCWD, f, NULL, m, s1, s2, s5, w, b, l, h)
#define hash_compute_file(f, s2, s5, w, b, l, h) \
	hash_multiple_file_at_cb(AT_FDCWD, f, NULL, NULL, NULL, s2, s5, w, b, l, h)
char *hash_file_at_cb(int pfd, const char *filename, int hash_algo, hash_cb_t cb);
#define hash_file(f, h) hash_file_at_cb(AT_FDCWD, f, h, NULL)
#define hash_file_at(fd, f, h) hash_file_at_cb(fd, f, h, NULL)
char * hash_from_string(char *str,size_t len);

#endif
