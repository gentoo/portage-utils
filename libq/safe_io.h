/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _SAFE_IO_H
#define _SAFE_IO_H 1

#include <stdio.h>
#include <stdlib.h>

size_t safe_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
#define fwrite safe_fwrite

ssize_t safe_read(int fd, void *buf, size_t len);
ssize_t safe_write(int fd, const void *buf, size_t len);

#endif
