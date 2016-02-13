/* Solaris compatible code */
#ifdef __sun__

#include <string.h>
#include <ctype.h>
#include <alloca.h>
#include <sys/dklabel.h>

#define S_BLKSIZE DK_DEVID_BLKSIZE

#elif defined(__hpux__) || defined(__MINT__)
	/* must not include both dir.h and dirent.h on hpux11..11 & FreeMiNT */
#elif defined(__linux__)
	/* Linux systems do not need sys/dir.h as they are generally POSIX sane */
#else /* __sun__ */
# include <sys/dir.h>
#endif

/* AIX */
#ifdef _AIX
# include <sys/stat.h>
# define S_BLKSIZE DEV_BSIZE
#endif

/* Windows Interix */
#ifdef __INTERIX
# define S_BLKSIZE S_BLOCK_SIZE
#endif

/* HP-UX */
#ifdef __hpux
# define S_BLKSIZE st.st_blksize
#endif

/* Everyone else */
#ifndef S_BLKSIZE
# define S_BLKSIZE 512
#endif
