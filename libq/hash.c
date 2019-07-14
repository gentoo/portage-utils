/*
 * Copyright 2018-2019 Gentoo Foundation
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

#ifdef HAVE_SSL
#include <openssl/sha.h>
#include <openssl/whrlpool.h>
#endif
#ifdef HAVE_BLAKE2B
#include <blake2.h>
#endif

#include "hash.h"

void
hash_hex(char *out, const unsigned char *buf, const int length)
{
	switch (length) {
		/* SHA256_DIGEST_LENGTH */
		case 32:
			snprintf(out, 64 + 1,
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x",
					buf[ 0], buf[ 1], buf[ 2], buf[ 3], buf[ 4],
					buf[ 5], buf[ 6], buf[ 7], buf[ 8], buf[ 9],
					buf[10], buf[11], buf[12], buf[13], buf[14],
					buf[15], buf[16], buf[17], buf[18], buf[19],
					buf[20], buf[21], buf[22], buf[23], buf[24],
					buf[25], buf[26], buf[27], buf[28], buf[29],
					buf[30], buf[31]
					);
			break;
		/* SHA512_DIGEST_LENGTH, WHIRLPOOL_DIGEST_LENGTH, BLAKE2B_OUTBYTES */
		case 64:
			snprintf(out, 128 + 1,
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x",
					buf[ 0], buf[ 1], buf[ 2], buf[ 3], buf[ 4],
					buf[ 5], buf[ 6], buf[ 7], buf[ 8], buf[ 9],
					buf[10], buf[11], buf[12], buf[13], buf[14],
					buf[15], buf[16], buf[17], buf[18], buf[19],
					buf[20], buf[21], buf[22], buf[23], buf[24],
					buf[25], buf[26], buf[27], buf[28], buf[29],
					buf[30], buf[31], buf[32], buf[33], buf[34],
					buf[35], buf[36], buf[37], buf[38], buf[39],
					buf[40], buf[41], buf[42], buf[43], buf[44],
					buf[45], buf[46], buf[47], buf[48], buf[49],
					buf[50], buf[51], buf[52], buf[53], buf[54],
					buf[55], buf[56], buf[57], buf[58], buf[59],
					buf[60], buf[61], buf[62], buf[63]
					);
			break;
		/* fallback case, should never be necessary */
		default:
			{
				int i;
				for (i = 0; i < length; i++) {
					snprintf(&out[i * 2], 3, "%02x", buf[i]);
				}
			}
			break;
	}
}

/**
 * Computes the hashes for file fname and writes the hex-representation
 * for those hashes into the address space pointed to by the return
 * pointers for these hashes.  The caller should ensure enough space is
 * available.  Only those hashes which are in the global hashes variable
 * are computed, the address space pointed to for non-used hashes are
 * left untouched, e.g. they can be NULL.  The number of bytes read from
 * the file pointed to by fname is returned in the flen argument.
 */
void
hash_compute_file(
		const char *fname,
		char *sha256,
		char *sha512,
		char *whrlpl,
		char *blak2b,
		size_t *flen,
		int hashes)
{
	FILE *f;
	char data[8192];
	size_t len;
#ifdef HAVE_SSL
	SHA256_CTX s256;
	SHA512_CTX s512;
	WHIRLPOOL_CTX whrl;
#else
	(void)sha256;
	(void)sha512;
	(void)whrlpl;
#endif
#ifdef HAVE_BLAKE2B
	blake2b_state bl2b;
#else
	(void)blak2b;
#endif

	if ((f = fopen(fname, "r")) == NULL)
		return;

#ifdef HAVE_SSL
	SHA256_Init(&s256);
	SHA512_Init(&s512);
	WHIRLPOOL_Init(&whrl);
#endif
#ifdef HAVE_BLAKE2B
	blake2b_init(&bl2b, BLAKE2B_OUTBYTES);
#endif

	while ((len = fread(data, 1, sizeof(data), f)) > 0) {
		*flen += len;
#if defined(HAVE_SSL) || defined(HAVE_BLAKE2B)
#pragma omp parallel sections
		{
#ifdef HAVE_SSL
#pragma omp section
			{
				if (hashes & HASH_SHA256)
					SHA256_Update(&s256, data, len);
			}
#pragma omp section
			{
				if (hashes & HASH_SHA512)
					SHA512_Update(&s512, data, len);
			}
#pragma omp section
			{
				if (hashes & HASH_WHIRLPOOL)
					WHIRLPOOL_Update(&whrl, data, len);
			}
#endif
#ifdef HAVE_BLAKE2B
#pragma omp section
			{
				if (hashes & HASH_BLAKE2B)
					blake2b_update(&bl2b, (unsigned char *)data, len);
			}
#endif
		}
#endif /* HAVE_SSL || HAVE_BLAKE2B */
	}
	fclose(f);

#if defined(HAVE_SSL) || defined(HAVE_BLAKE2B)
#pragma omp parallel sections
	{
#ifdef HAVE_SSL
		{
			if (hashes & HASH_SHA256) {
				unsigned char sha256buf[SHA256_DIGEST_LENGTH];
				SHA256_Final(sha256buf, &s256);
				hash_hex(sha256, sha256buf, SHA256_DIGEST_LENGTH);
			}
		}
#pragma omp section
		{
			if (hashes & HASH_SHA512) {
				unsigned char sha512buf[SHA512_DIGEST_LENGTH];
				SHA512_Final(sha512buf, &s512);
				hash_hex(sha512, sha512buf, SHA512_DIGEST_LENGTH);
			}
		}
#pragma omp section
		{
			if (hashes & HASH_WHIRLPOOL) {
				unsigned char whrlplbuf[WHIRLPOOL_DIGEST_LENGTH];
				WHIRLPOOL_Final(whrlplbuf, &whrl);
				hash_hex(whrlpl, whrlplbuf, WHIRLPOOL_DIGEST_LENGTH);
			}
		}
#endif
#ifdef HAVE_BLAKE2B
#pragma omp section
		{
			if (hashes & HASH_BLAKE2B) {
				unsigned char blak2bbuf[BLAKE2B_OUTBYTES];
				blake2b_final(&bl2b, blak2bbuf, BLAKE2B_OUTBYTES);
				hash_hex(blak2b, blak2bbuf, BLAKE2B_OUTBYTES);
			}
		}
#endif
	}
#endif /* HAVE_SSL || HAVE_BLAKE2B */
}
