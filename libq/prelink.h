/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _PRELINK_H
#define _PRELINK_H 1

#include "porting.h"

bool prelink_available(void);
int hash_cb_prelink_undo(int fd, const char *filename);

#endif
