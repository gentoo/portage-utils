/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _EAT_FILE_H
#define _EAT_FILE_H 1

#include "porting.h"

bool eat_file_fd(int, char **, size_t *);
bool eat_file(const char *, char **, size_t *);
bool eat_file_at(int, const char *, char **, size_t *);

#endif
