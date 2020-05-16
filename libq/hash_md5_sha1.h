/*
 * Copyright 2005-2020 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _HASH_FD_H
#define _HASH_FD_H 1

/* type to hold the SHA1 context  */
struct sha1_ctx_t {
	uint32_t count[2];
	uint32_t hash[5];
	uint32_t wbuf[16];
};

void sha1_begin(struct sha1_ctx_t *ctx);
void sha1_hash(const void *data, size_t len, void *ctx_v);
void sha1_end(unsigned char hval[], struct sha1_ctx_t *ctx);

/* Structure to save state of computation between the single steps.  */
struct md5_ctx_t {
	uint32_t A;
	uint32_t B;
	uint32_t C;
	uint32_t D;
	uint32_t total[2];
	uint32_t buflen;
	char buffer[128];
};

void md5_begin(struct md5_ctx_t *ctx);
void md5_hash(const void *buffer, size_t length, void *md5_ctx);
void *md5_end(void *resbuf, struct md5_ctx_t *ctx);

#endif
