/*
 * Copyright 2005-2018 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qtemp

#define QTEMP_FLAGS "" COMMON_FLAGS
static struct option const qtemp_long_opts[] = {
	COMMON_LONG_OPTS
};
static const char * const qtemp_opts_help[] = {
	COMMON_OPTS_HELP
};
#define qtemp_usage(ret) usage(ret, QTEMP_FLAGS, qtemp_long_opts, qtemp_opts_help, NULL, lookup_applet_idx("qtemp"))

int qtemp_main(int argc, char **argv)
{
	int i;

	while ((i = GETOPT_LONG(QTEMP, qtemp, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qtemp)
		}
	}

	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qtemp)
#endif
