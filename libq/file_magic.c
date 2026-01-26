/*
 * Copyright 2025-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2025-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <unistd.h>

#include "file_magic.h"

file_magic_type file_magic_guess_fd
(
	int fd
)
{
	unsigned char   magic[257+6];
	ssize_t         mlen;
	file_magic_type ret   = FMAGIC_UNKNOWN;

	/* using libmagic would probably be much more complete, but since we
	 * want to keep the dependencies minimal, we do some simple probing
	 * here ourselves */

	/* bz2: 3-byte: 'B' 'Z' 'h'              at byte 0
	 * gz:  2-byte:  1f  8b                  at byte 0
	 * xz:  4-byte: '7' 'z' 'X' 'Z'          at byte 1
	 * tar: 6-byte: 'u' 's' 't' 'a' 'r' \0   at byte 257
	 * lz4: 4-byte:   4  22  4d  18          at byte 0
	 * zst: 4-byte: 22-28 b5 2f  fd          at byte 0
	 * lz:  4-byte: 'L' 'Z' 'I' 'P'          at byte 0
	 * lzo: 9-byte:  89 'L' 'Z' 'O' 0 d a 1a a at byte 0
	 * br:  Brotli is undetectcble */

	if (fd < 0 ||
		(mlen = read(fd, magic, sizeof(magic))) <= 0)
	{
		/* do nothing */
		return ret;
	} else if (mlen >= 3 &&
			   magic[0] == 'B' &&
			   magic[1] == 'Z' &&
			   magic[2] == 'h')
	{
		ret = FMAGIC_BZIP2;
	} else if (mlen >= 2 &&
			   magic[0] == 037 &&
			   magic[1] == 0213)
	{
		ret = FMAGIC_GZIP;
	} else if (mlen >= 5 &&
			   magic[1] == '7' &&
			   magic[2] == 'z' &&
			   magic[3] == 'X' &&
			   magic[4] == 'Z')
	{
		ret = FMAGIC_XZ;
	} else if (mlen == 257+6 &&
			   magic[257] == 'u' &&
			   magic[258] == 's' &&
			   magic[259] == 't' &&
			   magic[260] == 'a' &&
			   magic[261] == 'r' &&
			   (magic[262] == '\0' ||
			   	magic[262] == ' '))
	{
		ret = FMAGIC_TAR;
	} else if (mlen >= 4 &&
			   magic[0] == 0x04 &&
			   magic[1] == 0x22 &&
			   magic[2] == 0x4D &&
			   magic[3] == 0x18)
	{
		ret = FMAGIC_LZ4;
	} else if (mlen >= 4 &&
			   magic[0] >= 0x22 &&
			   magic[0] <= 0x28 &&
			   magic[1] == 0xB5 &&
			   magic[2] == 0x2F &&
			   magic[3] == 0xFD)
	{
		ret = FMAGIC_ZSTD;
	} else if (mlen >= 4 &&
			   magic[0] == 'L' &&
			   magic[1] == 'Z' &&
			   magic[2] == 'I' &&
			   magic[3] == 'P')
	{
		ret = FMAGIC_LZIP;
	} else if (mlen >= 9 &&
			   magic[0] == 0x89 &&
			   magic[1] == 'L' &&
			   magic[2] == 'Z' &&
			   magic[3] == 'O' &&
			   magic[4] == 0x00 &&
			   magic[5] == 0x0D &&
			   magic[6] == 0x0A &&
			   magic[7] == 0x1A &&
			   magic[8] == 0x0A)
	{
		ret = FMAGIC_LZO;
	}

	/* try to rewind, if this fails, what can we do? we still have found
	 * what it should be... */
	(void)lseek(fd, SEEK_CUR, (off_t)-mlen);
	return ret;
}
