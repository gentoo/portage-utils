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
#ifndef __INTERIX
#include <inttypes.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "busybox.h"

/* pass in a fd and get back a fd; filename is for display only */
typedef int (*hash_cb_t) (int, const char *);

static int hash_cb_default(int fd, const char *filename) { return fd; }

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

static unsigned char *hash_file_at_cb(int dfd, const char *filename, uint8_t hash_algo, hash_cb_t cb)
{
	int fd;
	fd = openat(dfd, filename, O_RDONLY|O_CLOEXEC);
	if (fd != -1) {
		static uint8_t hash_value_bin[20];
		static unsigned char *hash_value;
		fd = cb(fd, filename);
		if (hash_fd(fd, -1, hash_algo, hash_value_bin) != -2)
			hash_value = hash_bin_to_hex(hash_value_bin, hash_algo == HASH_MD5 ? 16 : 20);
		else
			hash_value = NULL;
		close(fd);
		return hash_value;
	}
	return NULL;
}

static unsigned char *hash_file_at(int dfd, const char *filename, uint8_t hash_algo)
{
	return hash_file_at_cb(dfd, filename, hash_algo, hash_cb_default);
}

static unsigned char *hash_file_cb(const char *filename, uint8_t hash_algo, hash_cb_t cb)
{
	return hash_file_at_cb(AT_FDCWD, filename, hash_algo, cb);
}

static unsigned char *hash_file(const char *filename, uint8_t hash_algo)
{
	return hash_file_cb(filename, hash_algo, hash_cb_default);
}
