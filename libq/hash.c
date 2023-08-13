/*
 * Copyright 2018-2020 Gentoo Foundation
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
# include <openssl/md5.h>
# include <openssl/sha.h>
# include <openssl/whrlpool.h>
#else
# include "hash_md5_sha1.h"
#endif
#ifdef HAVE_BLAKE2B
# include <blake2.h>
#endif

#include "hash.h"

void
hash_hex(char *out, const unsigned char *buf, const int length)
{
	switch (length) {
		/* MD5_DIGEST_LENGTH */
		case 16:
			snprintf(out, 32 + 1,
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x%02x%02x",
					buf[ 0], buf[ 1], buf[ 2], buf[ 3], buf[ 4],
					buf[ 5], buf[ 6], buf[ 7], buf[ 8], buf[ 9],
					buf[10], buf[11], buf[12], buf[13], buf[14],
					buf[15]
					);
			break;
		/* SHA1_DIGEST_LENGTH */
		case 20:
			snprintf(out, 40 + 1,
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
					buf[ 0], buf[ 1], buf[ 2], buf[ 3], buf[ 4],
					buf[ 5], buf[ 6], buf[ 7], buf[ 8], buf[ 9],
					buf[10], buf[11], buf[12], buf[13], buf[14],
					buf[15], buf[16], buf[17], buf[18], buf[19]
					);
			break;
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
int
hash_multiple_file_fd(
		int fd,
		char *md5,
		char *sha1,
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
	MD5_CTX m5;
	SHA_CTX s1;
	SHA256_CTX s256;
	SHA512_CTX s512;
	WHIRLPOOL_CTX whrl;
#else
	struct md5_ctx_t m5;
	struct sha1_ctx_t s1;
	(void)sha256;
	(void)sha512;
	(void)whrlpl;
#endif
#ifdef HAVE_BLAKE2B
	blake2b_state bl2b;
#else
	(void)blak2b;
#endif

	*flen = 0;
	if ((f = fdopen(fd, "r")) == NULL)
		return -1;

#ifdef HAVE_SSL
	MD5_Init(&m5);
	SHA1_Init(&s1);
	SHA256_Init(&s256);
	SHA512_Init(&s512);
	WHIRLPOOL_Init(&whrl);
#else
	md5_begin(&m5);
	sha1_begin(&s1);
#endif
#ifdef HAVE_BLAKE2B
	blake2b_init(&bl2b, BLAKE2B_OUTBYTES);
#endif

	while ((len = fread(data, 1, sizeof(data), f)) > 0) {
		*flen += len;
#pragma omp parallel sections
		{
#ifdef HAVE_SSL
#pragma omp section
			{
				if (hashes & HASH_MD5)
					MD5_Update(&m5, data, len);
			}
#pragma omp section
			{
				if (hashes & HASH_SHA1)
					SHA1_Update(&s1, data, len);
			}
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
#else
#pragma omp section
			{
				if (hashes & HASH_MD5)
					md5_hash(data, len, &m5);
			}
#pragma omp section
			{
				if (hashes & HASH_SHA1)
					sha1_hash(data, len, &s1);
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
	}
	fclose(f);

#pragma omp parallel sections
	{
#ifdef HAVE_SSL
#pragma omp section
		{
			if (hashes & HASH_MD5) {
				unsigned char md5buf[MD5_DIGEST_LENGTH];
				MD5_Final(md5buf, &m5);
				hash_hex(md5, md5buf, MD5_DIGEST_LENGTH);
			}
		}
#pragma omp section
		{
			if (hashes & HASH_SHA1) {
				unsigned char sha1buf[SHA_DIGEST_LENGTH];
				SHA1_Final(sha1buf, &s1);
				hash_hex(sha1, sha1buf, SHA_DIGEST_LENGTH);
			}
		}
#pragma omp section
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
#else
#pragma omp section
		{
			if (hashes & HASH_MD5) {
				unsigned char md5buf[16];
				md5_end(md5buf, &m5);
				hash_hex(md5, md5buf, 16);
			}
		}
#pragma omp section
		{
			if (hashes & HASH_SHA1) {
				unsigned char sha1buf[20];
				sha1_end(sha1buf, &s1);
				hash_hex(sha1, sha1buf, 20);
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

	return 0;
}

int
hash_multiple_file_at_cb(
		int pfd,
		const char *fname,
		hash_cb_t cb,
		char *md5,
		char *sha1,
		char *sha256,
		char *sha512,
		char *whrlpl,
		char *blak2b,
		size_t *flen,
		int hashes)
{
	int ret;
	int fd = openat(pfd, fname, O_RDONLY | O_CLOEXEC);

	if (fd == -1)
		return -1;

	if (cb != NULL) {
		fd = cb(fd, fname);
		if (fd == -1)
			return -1;
	}

	ret = hash_multiple_file_fd(fd, md5, sha1, sha256, sha512,
			whrlpl, blak2b, flen, hashes);

	if (ret != 0)
		close(fd);

	return ret;
}

static char _hash_file_buf[128 + 1];
char *
hash_file_at_cb(int pfd, const char *fname, int hash, hash_cb_t cb)
{
	size_t dummy;

	switch (hash) {
		case HASH_MD5:
		case HASH_SHA1:
		case HASH_SHA256:
		case HASH_SHA512:
		case HASH_WHIRLPOOL:
		case HASH_BLAKE2B:
			if (hash_multiple_file_at_cb(pfd, fname, cb,
					_hash_file_buf, _hash_file_buf, _hash_file_buf,
					_hash_file_buf, _hash_file_buf, _hash_file_buf,
					&dummy, hash) != 0)
				return NULL;
			break;
		default:
			return NULL;
	}

	return _hash_file_buf;
}


/*
*Generate and alloc in heap a hash with MD5.
*Param: str :string to hash the string must be \0 terminating
*       len : len of the string str, if <=0 the function will compute the len itself
*/
char *
hash_from_string(char *str,size_t len)
{
  unsigned int hash_size = 32;
  unsigned char hex_buf[hash_size+1];
  char *hash_final;
  MD5_CTX ctx;

  if(len <= 0){
    len=strlen(str);
  }

  hash_final=xmalloc(hash_size+1*sizeof(*hash_final));
  hash_final[hash_size]='\0';
  hex_buf[hash_size]='\0';
  MD5_Init(&ctx);
  MD5_Update(&ctx,str,len);
  MD5_Final(hex_buf,&ctx); 
  hash_hex(hash_final,hex_buf,(hash_size>>1));

  return hash_final;
}


