/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _RMSPACE_H
#define _RMSPACE_H 1

#include <stdlib.h>

char *rmspace_len(char *s, size_t len);
char *rmspace(char *s);
char *remove_extra_space(char *s);

#endif
