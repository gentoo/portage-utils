/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qlop.c,v 1.13 2005/07/07 11:28:31 solar Exp $
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

#ifdef __linux__
# include <asm/param.h>
# define __QLOP_CURRENT__
#endif

#define QLOP_DEFAULT_LOGFILE "/var/log/emerge.log"
#define QLOP_DEFAULT_PIDFILE "/tmp/sandboxpids.tmp"



#define QLOP_FLAGS "gtluscf:F:" COMMON_FLAGS
static struct option const qlop_long_opts[] = {
	{"guage",     no_argument, NULL, 'g'},
	{"time",      no_argument, NULL, 't'},
	{"list",      no_argument, NULL, 'l'},
	{"unlist",    no_argument, NULL, 'u'},
	{"sync",      no_argument, NULL, 's'},
	{"current",   no_argument, NULL, 'c'},
	{"logfile",    a_argument, NULL, 'f'},
	{"pidfile",    a_argument, NULL, 'F'},
	COMMON_LONG_OPTS
};

static const char *qlop_opts_help[] = {
	"Guage the total number of merge times for a specific package",
	"Calculate merge time for a specific package",
	"Show merge history",
	"Show unmerge history",
	"Show sync history",
	"Show current emerging packages",
	"Read emerge logfile instead of " QLOP_DEFAULT_LOGFILE,
	"Read emerge pidfile instead of " QLOP_DEFAULT_PIDFILE,
	COMMON_OPTS_HELP
};

#define qlop_usage(ret) usage(ret, QLOP_FLAGS, qlop_long_opts, qlop_opts_help, APPLET_QLOP)



static const char *chop_ctime(time_t t);
static const char *chop_ctime(time_t t)
{
	static char ctime_out[50];
	char *p;
	snprintf(ctime_out, sizeof(ctime_out), "%s", ctime(&t));
	if ((p = strchr(ctime_out, '\n')) != NULL)
		*p = '\0';
	return ctime_out;
}

unsigned long calculate_merge_time(char *pkg, const char *logfile, int average);
unsigned long calculate_merge_time(char *pkg, const char *logfile, int average)
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
	if (average == 1)
		return (merge_time / count);
	return count;
}

void show_emerge_history(char merged, int argc, char **argv, const char *logfile);
void show_emerge_history(char merged, int argc, char **argv, const char *logfile)
{
	FILE *fp;
	char buf[BUFSIZ];
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

			printf("%s %s %s%s%s\n", chop_ctime(t), (merged ? ">>>" : "<<<"), GREEN, q, NORM);
		}
	}
	fclose(fp);
}

void show_sync_history(const char *logfile);
void show_sync_history(const char *logfile)
{
	FILE *fp;
	char buf[BUFSIZ];
	char *p, *q;
	time_t t;

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

		printf("%s >>> %s%s%s\n", chop_ctime(t), GREEN, q, NORM);
	}
	fclose(fp);
}

void show_current_emerge(const char *pidfile);
#ifdef __QLOP_CURRENT__
void show_current_emerge(const char *pidfile)
{
	FILE *fp;
	pid_t pid;
	char buf[BUFSIZE], bufstat[300];
	char path[_POSIX_PATH_MAX];
	char *p, *q;
	unsigned long long start_time;
	double uptime_secs;
	time_t start_date;

	if ((fp = fopen(pidfile, "r")) == NULL) {
		warnp("Could not open pidfile '%s'", pidfile);
		return;
	}

	/* each line in the sandbox file is a pid */
	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		if ((p = strchr(buf, '\n')) != NULL)
			*p = '\0';
		pid = (pid_t)atol(buf);

		/* portage renames the cmdline so the package name is first */
		sprintf(path, "/proc/%i/cmdline", pid);
		if (!eat_file(path, buf, sizeof(buf)))
			continue;
		if (buf[0] == '[' && (p = strchr(buf, ']')) != NULL) {
			*p = '\0';
			p = buf+1;
			q = p + strlen(p) + 1;

			/* open the stat file to figure out how long we have been running */
			sprintf(path, "/proc/%i/stat", pid);
			if (!eat_file(path, bufstat, sizeof(bufstat)))
				continue;

			/* ripped from procps/proc/readproc.c */
			if ((q = strchr(bufstat, ')')) == NULL)
				continue;
			/* grab the start time */
			sscanf(q + 2,
				"%*c "
				"%*d %*d %*d %*d %*d "
				"%*u %*u %*u %*u %*u "
				"%*u %*u %*u %*u "
				"%*d %*d "
				"%*d "
				"%*d "
				"%Lu ",
				&start_time);
			/* get uptime */
			if (!eat_file("/proc/uptime", bufstat, sizeof(bufstat)))
				continue;
			sscanf(bufstat, "%lf", &uptime_secs);

			/* figure out when this thing started and then show it */
			start_date = time(0) - (uptime_secs - (start_time / HZ));
			printf(
				" %s*%s %s%s%s\n"
				"     started: %s%s%s\n"
				"     elapsed: ", /*%s%llu%s seconds\n",*/
				BOLD, NORM, BLUE, p, NORM,
				GREEN, chop_ctime(start_date), NORM);
			{
				/* ripped from procps/ps/output.c */
				unsigned long t;
				unsigned dd,hh,mm,ss;
				t = uptime_secs - (start_time / HZ);
				ss = t%60; t /= 60;
				mm = t%60; t /= 60;
				hh = t%24; t /= 24;
				dd = t;
				if (dd) printf("%s%u%s days, ", GREEN, dd, NORM);
				if (hh) printf("%s%u%s hours, ", GREEN, hh, NORM);
				if (mm) printf("%s%u%s minutes, ", GREEN, mm, NORM);
				printf("%s%u%s second%s\n", GREEN, ss, NORM, (ss==1?"":"s"));
			}
		}
	}

	fclose(fp);
}
#else
void show_current_emerge(const char _q_unused_ *pidfile)
{
	errf("show_current_emerge() is not supported on your OS");
}
#endif

int qlop_main(int argc, char **argv)
{
	int i, average = 1;
	char do_time, do_list, do_unlist, do_sync, do_current;
	char *opt_logfile, *opt_pidfile;
	const char *logfile = QLOP_DEFAULT_LOGFILE,
	           *pidfile = QLOP_DEFAULT_PIDFILE;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
		argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	opt_logfile = opt_pidfile = NULL;
	do_time = do_list = do_unlist = do_sync = do_current = 0;

	while ((i = GETOPT_LONG(QLOP, qlop, "")) != -1) {
		switch (i) {
			COMMON_GETOPTS_CASES(qlop)

			case 't': do_time = 1; break;
			case 'l': do_list = 1; break;
			case 'u': do_unlist = 1; break;
			case 's': do_sync = 1; break;
			case 'c': do_current = 1; break;
			case 'g': do_time = 1; average = 0; break;
			case 'f':
				if (opt_logfile) err("Only use -f once");
				opt_logfile = xstrdup(optarg);
				break;
			case 'F':
				if (opt_pidfile) err("Only use -F once");
				opt_pidfile = xstrdup(optarg);
				break;
		}
	}
	if (!do_list && !do_unlist && !do_time && !do_sync && !do_current)
		qlop_usage(EXIT_FAILURE);
	if (opt_logfile != NULL)
		logfile = opt_logfile;
	if (opt_pidfile != NULL)
		pidfile = opt_pidfile;

	argc -= optind;
	argv += optind;

	if (do_list)
		show_emerge_history(1, argc, argv, logfile);
	if (do_unlist)
		show_emerge_history(0, argc, argv, logfile);
	if (do_current)
		show_current_emerge(pidfile);
	if (do_sync)
		show_sync_history(logfile);

	if (do_time) {
		if (average)
			printf("Average merge time (in seconds)\n");
		for (i = 0; i < argc; ++i)
			printf("%s%s%s: %lu\n", BLUE, argv[i], NORM, 
			       calculate_merge_time(argv[i], logfile, average));
	}

	if (opt_logfile) free(opt_logfile);
	if (opt_pidfile) free(opt_pidfile);

	return EXIT_SUCCESS;
}
