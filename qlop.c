/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qlop.c,v 1.3 2005/06/19 05:32:12 vapier Exp $
 *
 * 2005 Ned Ludd	- <solar@gentoo.org>
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

#define QLOP_FLAGS "tf" COMMON_FLAGS
static struct option const qlop_long_opts[] = {
	{"time", no_argument, NULL, 't'},
	{"file",  a_argument, NULL, 'f'},
	COMMON_LONG_OPTS
};

static const char *qlop_opts_help[] = {
	"Calculate merge time for a specific package",
	"Read emerge logfile instead of /var/log/emerge.log",
	COMMON_OPTS_HELP
};

#define qlop_usage(ret) usage(ret, QLOP_FLAGS, qlop_long_opts, qlop_opts_help, APPLET_QLOP)


unsigned long calculate_average_merge_time(char *pkg, const char *logfile);
unsigned long calculate_average_merge_time(char *pkg, const char *logfile) {
	FILE *fp;
	char buf[2][BUFSIZ];
	char *p;
	unsigned long count, merge_time;
	time_t t[2];
	depend_atom *atom;
	
	t[0] = t[1] = 0UL;
	count = merge_time = 0;


	DBG("Searching for %s in %s\n", pkg, logfile);

	if ((fp = fopen(logfile, "r")) == NULL)
		return 0;

	while ((fgets(buf[0], sizeof(buf[0]), fp)) != NULL) {
		if ((strstr(buf[0], pkg)) == NULL)
			continue;

		if ((p = strchr(buf[0], '\n')) != NULL)
			*p = 0;
		if ((p = strchr(buf[0], ':')) == NULL)
			continue;
		*p = 0;
		t[0] = atol(buf[0]);
		strcpy(buf[1], p + 1);
		rmspace(buf[1]);
		if ((strncmp(buf[1], ">>> emerge (", 12)) == 0) {
			if ((p = strchr(buf[1], ')')) == NULL)
				continue;
			*p = 0;
			strcpy(buf[0], p + 1);
			rmspace(buf[0]);
			if ((p = strchr(buf[0], ' ')) == NULL)
				continue;
			*p = 0;
			if ((atom = atom_explode(buf[0])) == NULL)
				continue;
			if ((strcmp(pkg, atom->PN)) == 0) {
				while ((fgets(buf[0], sizeof(buf[0]), fp)) != NULL) {
					if ((p = strchr(buf[0], '\n')) != NULL)
						*p = 0;
					if ((p = strchr(buf[0], ':')) == NULL)
						continue;
					*p = 0;
					t[1] = atol(buf[0]);
					strcpy(buf[1], p + 1);
					rmspace(buf[1]);
					if (*buf[1] == '*')
						break;
					if ((strncmp(buf[1], "::: completed emerge (", 22)) == 0) {
						merge_time += (t[1] - t[0]);
						count++;
						break;
					}
				}
			}
			atom_implode(atom);
		}
	}
	fclose(fp);
	if (count == 0)
		return 0;
	return (merge_time / count);
}

int qlop_main(int argc, char **argv)
{
	int i;
	short do_time = 0;
	// char *logfile = NULL;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
		argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QLOP, qlop, "")) != -1) {
		switch (i) {
			COMMON_GETOPTS_CASES(qlop)
			case 't': do_time = 1; break;
			/* add to me: */
			// case 'f': break;
		 
		}
	}

	if (argc == optind)
		qlop_usage(EXIT_FAILURE);
	if (do_time)
		printf("Average merge time in seconds: %lu\n", 
			calculate_average_merge_time(argv[argc-1], "/var/log/emerge.log"));

	return EXIT_SUCCESS;
}
