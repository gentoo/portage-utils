/*
 *  Copyright (C) 2003 Glenn L. McGrath
 *  Copyright (C) 2003-2004 Erik Andersen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "busybox.h"

#define FLAG_SILENT	1
#define FLAG_CHECK	2
#define FLAG_WARN	4

/* This might be useful elsewhere */
static unsigned char *hash_bin_to_hex(unsigned char *hash_value,
									  unsigned char hash_length)
{
	int x, len, max;
	unsigned char *hex_value;

	max = (hash_length * 2) + 2;
	hex_value = xmalloc(max);
	for (x = len = 0; x < hash_length; x++) {
		len += snprintf((char*)(hex_value + len), max - len, "%02x", hash_value[x]);
	}
	return (hex_value);
}

static unsigned char *hash_file(const char *filename, uint8_t hash_algo)
{
	int fd;
	fd = open(filename, O_RDONLY);
	if (fd != -1) {
		static uint8_t hash_value_bin[20];
		static unsigned char *hash_value;
		hash_value =
			(hash_fd(fd, -1, hash_algo, hash_value_bin) != -2 ?
			 hash_bin_to_hex(hash_value_bin, hash_algo == HASH_MD5 ? 16 : 20) :
			 NULL);
		close(fd);
		return hash_value;
	}
	return NULL;
}
