/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/template.c,v 1.1 2005/06/21 00:07:14 vapier Exp $
 *
 * 2005 Ned Ludd        - <solar@gentoo.org>
 * 2005 Mike Frysinger  - <vapier@gentoo.org>
 *
 ********************************************************************
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 *
 */



#define QTEMP_FLAGS "" COMMON_FLAGS
static struct option const qtemp_long_opts[] = {
	COMMON_LONG_OPTS
};
static const char *qtemp_opts_help[] = {
	COMMON_OPTS_HELP
};
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
