/* we need the space before the last comma or we trigger a bug in gcc-2 :( */
#if defined OPTIMIZE_FOR_SIZE && (OPTIMIZE_FOR_SIZE > 1)
#define warn(fmt, args...)
#else
#define warn(fmt, args...) \
	fprintf(stderr, _("%s%s%s: " fmt "\n"), RED, argv0, NORM , ## args)
#endif
#define warnf(fmt, args...) warn("%s%s()%s: " fmt, YELLOW, __func__, NORM , ## args)
#define warnl(fmt, args...) warn("%s%i()%s: " fmt, YELLOW, __LINE__, NORM , ## args)
#define warnp(fmt, args...) warn(fmt ": %s" , ## args , strerror(errno))
#define warnfp(fmt, args...) warnf(fmt ": %s" , ## args , strerror(errno))
#define _err(wfunc, fmt, args...) \
	do { \
	wfunc(fmt , ## args); \
	exit(EXIT_FAILURE); \
	} while (0)
#define err(fmt, args...) _err(warn, fmt , ## args)
#define errf(fmt, args...) _err(warnf, fmt , ## args)
#define errp(fmt, args...) _err(warnp, fmt , ## args)
#define ARR_SIZE(arr) (sizeof(arr) / sizeof(*arr))

#ifndef BUFSIZE
# define BUFSIZE 8192
#endif
