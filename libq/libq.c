#if defined(__linux__)
# include <endian.h>
# include <byteswap.h>
#elif defined(__FreeBSD__)
# include <sys/endian.h>
#endif

/* busybox imports */
#include "busybox.h"
#include "xmalloc.c"
#include "xstrdup.c"
#include "xasprintf.c"
#include "hash_fd.c"
#include "md5_sha1_sum.c"
#include "human_readable.c"

/* custom libs */
#include "atom_explode.c"
// #include "virtuals.c"
