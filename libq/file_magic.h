/*
 * Copyright 2025 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2025-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _FILE_MAGIC_H
#define _FILE_MAGIC_H 1

typedef enum _file_magic_type {
	FMAGIC_UNKNOWN = 0,
	FMAGIC_BZIP2,
	FMAGIC_GZIP,
	FMAGIC_XZ,
	FMAGIC_LZ4,
	FMAGIC_ZSTD,
	FMAGIC_LZIP,
	FMAGIC_LZO,
	FMAGIC_TAR
} file_magic_type;

file_magic_type file_magic_guess_fd(int fd);

#endif
