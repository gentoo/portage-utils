/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _MD5_SHA1_SUM_H
#define _MD5_SHA1_SUM_H 1

/* pass in a fd and get back a fd; filename is for display only */
typedef int (*hash_cb_t) (int, const char *);

int hash_cb_default(int fd, const char *filename);
unsigned char *hash_file_at_cb(
		int dfd,
		const char *filename,
		uint8_t hash_algo,
		hash_cb_t cb);
unsigned char *hash_file_at(int dfd, const char *filename, uint8_t hash_algo);
unsigned char *hash_file(const char *filename, uint8_t hash_algo);

#endif
