/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/template.c,v 1.4 2005/11/14 01:16:27 solar Exp $
 *
 * Copyright 2005 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005 Mike Frysinger  - <vapier@gentoo.org>
 */

#define QTEMP_FLAGS "" COMMON_FLAGS
static struct option const qtemp_long_opts[] = {
	COMMON_LONG_OPTS
};
static const char *qtemp_opts_help[] = {
	COMMON_OPTS_HELP
};

static char qtemp_rcsid[] = "$Id: template.c,v 1.4 2005/11/14 01:16:27 solar Exp $";
#define qtemp_usage(ret) usage(ret, QTEMP_FLAGS, qtemp_long_opts, qtemp_opts_help, APPLET_QTEMP)


int qtemp_main(int argc, char **argv)
{
	int i;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QTEMP, qtemp, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qtemp)
		}
	}



	return EXIT_SUCCESS;
}
