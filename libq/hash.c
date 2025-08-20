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

#include "hash.h"

void
hash_hex(char *out, const unsigned char *buf, const int length)
{
	switch (length) {
		case 16: /* MD5_DIGEST_SIZE */
			snprintf(out, 32 + 1,
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x%02x%02x",
					buf[ 0], buf[ 1], buf[ 2], buf[ 3], buf[ 4],
					buf[ 5], buf[ 6], buf[ 7], buf[ 8], buf[ 9],
					buf[10], buf[11], buf[12], buf[13], buf[14],
					buf[15]
					);
			break;
		case 20: /* SHA1_DIGEST_SIZE */
			snprintf(out, 40 + 1,
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
					"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
					buf[ 0], buf[ 1], buf[ 2], buf[ 3], buf[ 4],
					buf[ 5], buf[ 6], buf[ 7], buf[ 8], buf[ 9],
					buf[10], buf[11], buf[12], buf[13], buf[14],
					buf[15], buf[16], buf[17], buf[18], buf[19]
					);
			break;
		case 32: /* SHA256_DIGEST_SIZE */
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
		case 64: /* SHA512_DIGEST_SIZE, BLAKE2B_OUTBYTES */
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

/* len func(dest,destlen,cbctx) */
typedef size_t (*read_cb)(char *,size_t,void *);

static size_t read_stdio(char *dest, size_t destlen, void *ctx)
{
	FILE *io = ctx;

	return fread(dest, 1, destlen, io);
}

struct bufctx {
	const char *buf;
	size_t      buflen;
};

static size_t read_buffer(char *dest, size_t destlen, void *ctx)
{
	struct bufctx *membuf = ctx;
	size_t         readlen;

	readlen = destlen;
	if (readlen > membuf->buflen)
		readlen = membuf->buflen;

	memcpy(dest, membuf->buf, readlen);

	/* update buffer to the remainder */
	membuf->buf    += readlen;
	membuf->buflen -= readlen;

	return readlen;
}

static int
hash_multiple_internal(
		read_cb rcb,
		void   *ctx,
		char   *md5,
		char   *sha1,
		char   *sha256,
		char   *sha512,
		char   *blak2b,
		size_t *flen,
		int     hashes)
{
	size_t            len;
	char              data[8192];

	struct md5_ctx    m5;
	struct sha1_ctx   s1;
	struct sha256_ctx s256;
	struct sha512_ctx s512;
#ifdef HAVE_BLAKE2B
	blake2b_state     bl2b;
#else
	(void)blak2b;
#endif

	*flen = 0;

	md5_init_ctx(&m5);
	sha1_init_ctx(&s1);
	sha256_init_ctx(&s256);
	sha512_init_ctx(&s512);
#ifdef HAVE_BLAKE2B
	blake2b_init(&bl2b, BLAKE2B_OUTBYTES);
#endif

	while ((len = rcb(data, sizeof(data), ctx)) > 0) {
		*flen += len;
#pragma omp parallel sections
		{
#pragma omp section
			{
				if (hashes & HASH_MD5)
					md5_process_bytes(data, len, &m5);
			}
#pragma omp section
			{
				if (hashes & HASH_SHA1)
					sha1_process_bytes(data, len, &s1);
			}
#pragma omp section
			{
				if (hashes & HASH_SHA256)
					sha256_process_bytes(data, len, &s256);
			}
#pragma omp section
			{
				if (hashes & HASH_SHA512)
					sha512_process_bytes(data, len, &s512);
			}
#ifdef HAVE_BLAKE2B
#pragma omp section
			{
				if (hashes & HASH_BLAKE2B)
					blake2b_update(&bl2b, (unsigned char *)data, len);
			}
#endif
		}
	}

#pragma omp parallel sections
	{
#pragma omp section
		{
			if (hashes & HASH_MD5) {
				unsigned char md5buf[MD5_DIGEST_SIZE];
				md5_finish_ctx(&m5, md5buf);
				hash_hex(md5, md5buf, MD5_DIGEST_SIZE);
			}
		}
#pragma omp section
		{
			if (hashes & HASH_SHA1) {
				unsigned char sha1buf[SHA1_DIGEST_SIZE];
				sha1_finish_ctx(&s1, sha1buf);
				hash_hex(sha1, sha1buf, SHA1_DIGEST_SIZE);
			}
		}
#pragma omp section
		{
			if (hashes & HASH_SHA256) {
				unsigned char sha256buf[SHA256_DIGEST_SIZE];
				sha256_finish_ctx(&s256, sha256buf);
				hash_hex(sha256, sha256buf, SHA256_DIGEST_SIZE);
			}
		}
#pragma omp section
		{
			if (hashes & HASH_SHA512) {
				unsigned char sha512buf[SHA512_DIGEST_SIZE];
				sha512_finish_ctx(&s512, sha512buf);
				hash_hex(sha512, sha512buf, SHA512_DIGEST_SIZE);
			}
		}
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
		char *blak2b,
		size_t *flen,
		int hashes)
{
	FILE *f;
	int   ret;

	if ((f = fdopen(fd, "r")) == NULL)
		return -1;

	ret = hash_multiple_internal(read_stdio, f,
								 md5, sha1, sha256, sha512, blak2b,
								 flen, hashes);

	fclose(f);

	return ret;
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
			blak2b, flen, hashes);

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
		case HASH_BLAKE2B:
			if (hash_multiple_file_at_cb(pfd, fname, cb,
					_hash_file_buf, _hash_file_buf, _hash_file_buf,
					_hash_file_buf, _hash_file_buf,
					&dummy, hash) != 0)
				return NULL;
			break;
		default:
			return NULL;
	}

	return _hash_file_buf;
}

char *
hash_string(const char *buf, ssize_t buflen, int hash)
{
	struct bufctx membuf;
	size_t        dummy;

	if (buflen < 0)
		buflen = (ssize_t)strlen(buf);

	membuf.buf    =  buf;
	membuf.buflen = (size_t)buflen;

	switch (hash) {
		case HASH_MD5:
		case HASH_SHA1:
		case HASH_SHA256:
		case HASH_SHA512:
		case HASH_BLAKE2B:
			if (hash_multiple_internal(read_buffer, &membuf,
									   _hash_file_buf, _hash_file_buf,
									   _hash_file_buf, _hash_file_buf,
									   _hash_file_buf,
									   &dummy, hash) != 0)
				return NULL;
			break;
		default:
			return NULL;
	}

	return _hash_file_buf;
}
