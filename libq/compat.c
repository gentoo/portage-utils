/* Solaris compatible code */
#ifdef __sun__

#include <string.h>
#include <ctype.h>
#include <alloca.h>
#include <sys/dklabel.h>

#define S_BLKSIZE DK_DEVID_BLKSIZE

/* strcasestr is a GNU extention */
char* strcasestr(const char *big, const char *little) {
	char* b = alloca((strlen(big) + 1) * sizeof(char));
	char* l = alloca((strlen(little) + 1) * sizeof(char));
	char* off;
	size_t i;
	for (i = 0; big[i]; i++) b[i] = (char)tolower(big[i]);
	for (i = 0; little[i]; i++) l[i] = (char)tolower(little[i]);
	off = strstr(b, l);
	return(off == NULL ? off : (char*)(big + (off - b)));
}

#undef  xasprintf
#define xasprintf(strp, fmt, args...) \
	do { /* xasprintf() */ \
		char str[BUFSIZ]; \
		if ((snprintf(str, sizeof(str)-1, fmt , ## args)) == -1) \
			err("Out of stack space?"); \
		str[sizeof(str)-1] = '\0'; \
		*strp = xstrdup(str); \
	} while (0)

#elif defined(__hpux__) || defined(__MINT__)
	/* must not include both dir.h and dirent.h on hpux11..11 & FreeMiNT */
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

