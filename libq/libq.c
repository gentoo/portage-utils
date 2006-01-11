#if defined(__linux__)
# include <endian.h>
# include <byteswap.h>
#elif defined(__FreeBSD__)
# include <sys/endian.h>
#endif

/* busybox imports */
#include "busybox.h"
#include "libq.h"
#include "colors.c"
#include "xmalloc.c"
#include "xstrdup.c"
#include "xasprintf.c"
#include "hash_fd.c"
#include "md5_sha1_sum.c"
#include "human_readable.c"
#include "rmspace.c"

/* custom libs */
#include "atom_explode.c"
#include "atom_compare.c"

#ifndef _LIB_Q
# include "vdb_get_next_dir.c"
# include "virtuals.c"
#endif
