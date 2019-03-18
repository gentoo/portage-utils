/*
 * June 30, 2001                 Manuel Novoa III
 *
 * All-integer version (hey, not everyone has floating point) of
 * make_human_readable_str, modified from similar code I had written
 * for busybox several months ago.
 *
 * Notes:
 *   1) I'm using an unsigned long long to hold the product size * block_size,
 *      as df (which calls this routine) could request a representation of a
 *      partition size in bytes > max of unsigned long.  If long longs aren't
 *      available, it would be possible to do what's needed using polynomial
 *      representations (say, powers of 1024) and manipulating coefficients.
 *      The base ten "bytes" output could be handled similarly.
 *
 *   2) This routine outputs a decimal point and a tenths digit when
 *      display_unit == 0.  Hence, it isn't uncommon for the returned string
 *      to have a length of 5 or 6.
 *
 *      If block_size is also 0, no decimal digits are printed.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

#include <stdio.h>

#include "human_readable.h"

const char *
make_human_readable_str(unsigned long long val,
	unsigned long block_size, unsigned long display_unit)
{
	static const char unit_chars[] = {
		'\0', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'
	};

	unsigned frac; /* 0..9 - the fractional digit */
	const char *u;
	const char *fmt;

	static char str[21];		/* Sufficient for 64 bit unsigned integers. */

	if (val == 0)
		return "0";

	fmt = "%llu";
	if (block_size > 1)
		val *= block_size;
	frac = 0;
	u = unit_chars;

	if (display_unit) {
		val += display_unit/2;  /* Deal with rounding */
		val /= display_unit;    /* Don't combine with the line above! */
		/* will just print it as ulonglong (below) */
	} else {
		while ((val >= 1024)
		 /* && (u < unit_chars + sizeof(unit_chars) - 1) - always true */
		) {
			fmt = "%llu.%u%c";
			u++;
			frac = (((unsigned)val % 1024) * 10 + 1024/2) / 1024;
			val /= 1024;
		}
		if (frac >= 10) { /* we need to round up here */
			++val;
			frac = 0;
		}
#if 1
		/* If block_size is 0, dont print fractional part */
		if (block_size == 0) {
			if (frac >= 5) {
				++val;
			}
			fmt = "%llu%*c";
			frac = 1;
		}
#endif
	}

	snprintf(str, sizeof(str), fmt, val, frac, *u);

	return str;
}
