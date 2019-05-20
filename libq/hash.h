/*
 * Copyright 2018-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _HASH_H
#define _HASH_H 1

enum hash_impls {
	HASH_SHA256    = 1<<0,
	HASH_SHA512    = 1<<1,
	HASH_WHIRLPOOL = 1<<2,
	HASH_BLAKE2B   = 1<<3
};

/* default changed from sha256, sha512, whirlpool
 * to blake2b, sha512 on 2017-11-21 */
#define HASH_DEFAULT  (HASH_BLAKE2B | HASH_SHA512);

void hash_hex(char *out, const unsigned char *buf, const int length);
void hash_compute_file(const char *fname, char *sha256, char *sha512,
		char *whrlpl, char *blak2b, size_t *flen);

#endif
