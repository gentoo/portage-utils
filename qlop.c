/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qlop.c,v 1.9 2005/06/19 20:34:20 vapier Exp $
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

#define QLOP_DEFAULT_LOG "/var/log/emerge.log"



#define QLOP_FLAGS "tlusf:" COMMON_FLAGS
static struct option const qlop_long_opts[] = {
	{"time",      no_argument, NULL, 't'},
	{"list",      no_argument, NULL, 'l'},
	{"unlist",    no_argument, NULL, 'u'},
	{"sync",      no_argument, NULL, 's'},
	{"file",       a_argument, NULL, 'f'},
	COMMON_LONG_OPTS
};

static const char *qlop_opts_help[] = {
	"Calculate merge time for a specific package",
	"Show merge history",
	"Show unmerge history",
	"Show sync history",
	"Read emerge logfile instead of " QLOP_DEFAULT_LOG,
	COMMON_OPTS_HELP
};

#define qlop_usage(ret) usage(ret, QLOP_FLAGS, qlop_long_opts, qlop_opts_help, APPLET_QLOP)



unsigned long calculate_average_merge_time(char *pkg, const char *logfile);
unsigned long calculate_average_merge_time(char *pkg, const char *logfile)
{
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
		if (strstr(buf[0], pkg) == NULL)
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

void show_emerge_history(char merged, int argc, char **argv, const char *logfile);
void show_emerge_history(char merged, int argc, char **argv, const char *logfile)
{
	FILE *fp;
	char buf[BUFSIZ];
	char ctime_out[50];
	char *p, *q;
	int i;
	time_t t;

	if ((fp = fopen(logfile, "r")) == NULL)
		return;

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		if (strlen(buf) < 30)
			continue;

		for (i = 0; i < argc; ++i)
			if (strstr(buf, argv[i]) != NULL)
				break;
		if (argc && i == argc)
			continue;

		if ((p = strchr(buf, '\n')) != NULL)
			*p = 0;
		if ((p = strchr(buf, ':')) == NULL)
			continue;
		*p = 0;
		q = p + 3;

		t = (time_t)atol(buf);

		if ((merged && !strncmp(q, "::: completed emerge (", 22))
		    || (!merged && !strncmp(q, ">>> unmerge success: ", 21))) {

			if (merged) {
				if ((p = strchr(q, ')')) == NULL)
					continue;
				q = p+2;
				if ((p = strchr(q, ' ')) == NULL)
					continue;
				*p = 0;
			} else {
				if ((p = strchr(q, ':')) == NULL)
					continue;
				q = p+2;
			}

			sprintf(ctime_out, "%s", ctime(&t));
			if ((p = strchr(ctime_out, '\n')) != NULL)
				*p = '\0';
			printf("\t%s %s %s%s%s\n", ctime_out, (merged ? ">>>" : "<<<"), GREEN, q, NORM);
		}
	}
	fclose(fp);
}

void show_sync_history(const char *logfile);
void show_sync_history(const char *logfile)
{
	FILE *fp;
	char buf[BUFSIZ];
	char ctime_out[50];
	char *p, *q;
	time_t t;

	DBG("Searching for %s in %s\n", pkg, logfile);

	if ((fp = fopen(logfile, "r")) == NULL)
		return;

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		if (strlen(buf) < 35)
			continue;
		if (strncmp(buf+12, "=== Sync completed with", 23) != 0)
			continue;

		if ((p = strchr(buf, '\n')) != NULL)
			*p = 0;
		if ((p = strchr(buf, ':')) == NULL)
			continue;
		*p = 0;
		q = p+2;

		t = (time_t)atol(buf);

		if ((p = strstr(q, "with")) == NULL)
			continue;
		q = p + 5;

		sprintf(ctime_out, "%s", ctime(&t));
		if ((p = strchr(ctime_out, '\n')) != NULL)
			*p = '\0';
		printf("\t%s >>> %s%s%s\n", ctime_out, GREEN, q, NORM);
	}
	fclose(fp);
}

int qlop_main(int argc, char **argv)
{
	int i;
	char do_time, do_list, do_unlist, do_sync;
	char *opt_logfile;
	const char *logfile = QLOP_DEFAULT_LOG;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
		argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	opt_logfile = NULL;
	do_time = do_list = do_unlist = do_sync = 0;

	while ((i = GETOPT_LONG(QLOP, qlop, "")) != -1) {
		switch (i) {
			COMMON_GETOPTS_CASES(qlop)

			case 't': do_time = 1; break;
			case 'l': do_list = 1; break;
			case 'u': do_unlist = 1; break;
			case 's': do_sync = 1; break;
			case 'f':
				if (opt_logfile) err("Only use -f once");
				opt_logfile = xstrdup(optarg);
				break;
		}
	}
	if (!do_list && !do_unlist && !do_time && !do_sync)
		qlop_usage(EXIT_FAILURE);
	if (opt_logfile != NULL)
		logfile = opt_logfile;

	argc -= optind;
	argv += optind;

	if (do_list || do_unlist)
		show_emerge_history(do_list, argc, argv, logfile);

	if (do_sync)
		show_sync_history(logfile);

	if (do_time) {
		printf("Average merge time (in seconds)\n");
		for (i = 0; i < argc; ++i)
			printf("%s%s%s: %lu\n", BLUE, argv[i], NORM, 
			       calculate_average_merge_time(argv[i], logfile));
	}

	if (opt_logfile) free(opt_logfile);

	return EXIT_SUCCESS;
}
