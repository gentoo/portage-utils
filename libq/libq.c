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
#include "xstrdup.c"
#include "xasprintf.c"
#include "hash_fd.c"
#include "md5_sha1_sum.c"
#include "human_readable.c"
#include "rmspace.c"
#include "which.c"
#include "compat.c"

#include "copy_file.c"
#include "safe_io.c"
#include "xchdir.c"
#include "xgetcwd.c"
#include "xmkdir.c"
#include "xreadlink.c"
#include "xregex.c"
#include "xsystem.c"
#include "xarray.c"

/* custom libs */
#include "atom_explode.c"
#include "atom_compare.c"
#include "basename.c"
#include "prelink.c"

#ifndef _LIB_Q
# include "profile.c"
# include "vdb_get_next_dir.c"
# include "virtuals.c"
#endif
