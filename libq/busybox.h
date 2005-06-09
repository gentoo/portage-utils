/* some hax defines to help import busybox */
#ifndef _BUSYBOX_H
#define _BUSYBOX_H

#define CONFIG_MD5SUM
#undef CONFIG_SHA1SUM

#define HASH_SHA1	1
#define HASH_MD5	2

#define bb_full_read(fd, buf, count) read(fd, buf, count)

#endif /* _BUSYBOX_H */
