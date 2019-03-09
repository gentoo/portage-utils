/*
 * Copyright 2005-2018 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#if defined(__linux__)
# include <endian.h>
# include <byteswap.h>
#elif defined(__FreeBSD__)
# include <sys/endian.h>
#endif

/* busybox imports */
#include "busybox.h"
#include "i18n.h"
#include "libq.h"
#include "colors.c"
#include "xmalloc.c"
#include "xasprintf.c"
#include "hash_fd.c"
#include "md5_sha1_sum.c"
#include "human_readable.c"
#include "rmspace.c"
#include "compat.c"

#include "copy_file.c"
#include "safe_io.c"
#include "xchdir.c"
#include "xmkdir.c"
#include "xregex.c"
#include "xsystem.c"
#include "xarray.c"

/* custom libs */
#include "atom_explode.c"
#include "atom_compare.c"
#include "basename.c"
#include "scandirat.c"
#include "prelink.c"

#ifndef _LIB_Q
# include "profile.c"
# include "vdb.c"
# include "vdb_get_next_dir.c"
# include "set.c"
#endif
