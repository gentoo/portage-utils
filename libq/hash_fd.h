
/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _HASH_FD_H
#define _HASH_FD_H 1

int hash_fd(int src_fd, const size_t size, const uint8_t hash_algo,
				   uint8_t * hashval);

#endif
