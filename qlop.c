/*
 * Copyright 2005-2007 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qlop.c,v 1.42 2009/03/15 09:57:30 vapier Exp $
 *
 * Copyright 2005-2007 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2007 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qlop

#ifdef __linux__
# include <asm/param.h>
#endif

#ifdef __FreeBSD__
# include <kvm.h>
# include <sys/param.h>
# include <sys/sysctl.h>
# include <sys/user.h>
# include <sys/time.h>
#endif

#define QLOP_DEFAULT_LOGFILE "/var/log/emerge.log"

#define QLOP_FLAGS "gtHluscf:" COMMON_FLAGS
static struct option const qlop_long_opts[] = {
	{"gauge",     no_argument, NULL, 'g'},
	{"time",      no_argument, NULL, 't'},
	{"human",     no_argument, NULL, 'H'},
	{"list",      no_argument, NULL, 'l'},
	{"unlist",    no_argument, NULL, 'u'},
	{"sync",      no_argument, NULL, 's'},
	{"current",   no_argument, NULL, 'c'},
	{"logfile",    a_argument, NULL, 'f'},
	COMMON_LONG_OPTS
};
static const char *qlop_opts_help[] = {
	"Gauge number of times a package has been merged",
	"Calculate merge time for a specific package",
	"Print seconds in human readable format (needs -t)",
	"Show merge history",
	"Show unmerge history",
	"Show sync history",
	"Show current emerging packages",
	"Read emerge logfile instead of " QLOP_DEFAULT_LOGFILE,
	COMMON_OPTS_HELP
};
static const char qlop_rcsid[] = "$Id: qlop.c,v 1.42 2009/03/15 09:57:30 vapier Exp $";
#define qlop_usage(ret) usage(ret, QLOP_FLAGS, qlop_long_opts, qlop_opts_help, lookup_applet_idx("qlop"))

#define QLOP_LIST    0x01
#define QLOP_UNLIST  0x02

void print_seconds_for_earthlings(const unsigned long t);
void print_seconds_for_earthlings(const unsigned long t) {
	unsigned dd, hh, mm, ss;
	unsigned long tt = t;
	ss = tt % 60; tt /= 60;
	mm = tt % 60; tt /= 60;
	hh = tt % 24; tt /= 24;
	dd = tt;
	if (dd) printf("%s%u%s day%s, ", GREEN, dd, NORM, (dd == 1 ? "" : "s"));
	if (hh) printf("%s%u%s hour%s, ", GREEN, hh, NORM, (hh == 1 ? "" : "s"));
	if (mm) printf("%s%u%s minute%s, ", GREEN, mm, NORM, (mm == 1 ? "" : "s"));
	printf("%s%u%s second%s", GREEN, ss, NORM, (ss == 1 ? "" : "s"));
}

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

unsigned long show_merge_times(char *package, const char *logfile, int average, char human_readable);
unsigned long show_merge_times(char *package, const char *logfile, int average, char human_readable)
{
	FILE *fp;
	char cat[126], buf[2][BUFSIZ];
	char *pkg, *p;
	unsigned long count, merge_time;
	time_t t[2];
	depend_atom *atom;

	t[0] = t[1] = 0UL;
	count = merge_time = 0;
	cat[0] = 0;

	if ((p = strchr(package, '/')) != NULL) {
		pkg = p + 1;
		strncpy(cat, package, sizeof(cat));
		if ((p = strchr(cat, '/')) != NULL)
			*p = 0;
	} else {
		pkg = package;
	}

	DBG("Searching for %s in %s\n", pkg, logfile);

	if ((fp = fopen(logfile, "r")) == NULL) {
		warnp("Could not open logfile '%s'", logfile);
		return 1;
	}

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
			char matched = 0;
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

			if (*cat) {
				if ((strcmp(cat, atom->CATEGORY) == 0) && (strcmp(pkg, atom->PN) == 0))
					matched = 1;
			} else if (strcmp(pkg, atom->PN) == 0)
				matched = 1;

			if (matched) {
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
						if (!average) {
							strcpy(buf[1], "");
							if (verbose) {
								if (atom->PR_int)
									snprintf(buf[1], sizeof(buf[1]), "-%s-r%i", atom->PV,  atom->PR_int);
								else
									snprintf(buf[1], sizeof(buf[1]), "-%s", atom->PV);
							}
							printf("%s%s%s%s: %s: ", BLUE, atom->PN, buf[1], NORM, chop_ctime(t[0]));
							if (human_readable)
								print_seconds_for_earthlings(t[1] - t[0]);
							else
								printf("%s%lu%s seconds", GREEN, (t[1] - t[0]), NORM);
							puts("");
						}
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
	if (average == 1) {
		printf("%s%s%s: ", BLUE, pkg, NORM);
		if (human_readable)
			print_seconds_for_earthlings(merge_time / count);
		else
			printf("%s%lu%s seconds average", GREEN, merge_time / count, NORM);
		printf(" for %s%lu%s merges\n", GREEN, count, NORM);
	} else {
		printf("%s%s%s: %s%lu%s times\n", BLUE, pkg, NORM, GREEN, count, NORM);
	}
	return 0;
}

void show_emerge_history(char listflag, int argc, char **argv, const char *logfile);
void show_emerge_history(char listflag, int argc, char **argv, const char *logfile)
{
	FILE *fp;
	char buf[BUFSIZ], merged;
	char *p, *q;
	int i;
	time_t t;

	if ((fp = fopen(logfile, "r")) == NULL) {
		warnp("Could not open logfile '%s'", logfile);
		return;
	}

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

		t = (time_t) atol(buf);

		if ((listflag & QLOP_LIST) && !strncmp(q, "::: completed emerge (", 22)) {
			merged = 1;
			if ((p = strchr(q, ')')) == NULL)
				continue;
			q = p + 2;
			if ((p = strchr(q, ' ')) == NULL)
				continue;
			*p = 0;
		} else if ((listflag & QLOP_UNLIST) && !strncmp(q, ">>> unmerge success: ", 21)) {
			merged = 0;
			if ((p = strchr(q, ':')) == NULL)
				continue;
			q = p + 2;
		}
		else
			continue;
		if (!quiet)
			printf("%s %s %s%s%s\n", chop_ctime(t), (merged ? ">>>" : "<<<"), (merged ? GREEN : RED), q, NORM);
		else {
			depend_atom *atom;
			atom = atom_explode(q);
			if (quiet == 1)
				printf("%s ", chop_ctime(t));
			if (quiet <= 2)
				printf("%s ", (merged ? ">>>" : "<<<"));
			printf("%s%s/%s%s\n", (merged ? GREEN : RED), atom->CATEGORY, atom->PN, NORM);
			atom_implode(atom);
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

	if ((fp = fopen(logfile, "r")) == NULL) {
		warnp("Could not open logfile '%s'", logfile);
		return;
	}

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

void show_current_emerge(void);
#ifdef __linux__
#include <elf.h>
static unsigned long hz = 0;
static void init_hz(void)
{
#ifdef HZ
	hz = HZ;
#endif
	/* kernel pushes elf notes onto stack */
	unsigned long *elf_note = (unsigned long *)environ;
	while (!*elf_note++)
		continue;
	while (elf_note[0]) {
		if (elf_note[0] == AT_CLKTCK) {
			hz = elf_note[1];
			break;
		}
		elf_note += 2;
	}
	if (!hz)
		hz = 100;
}
void show_current_emerge(void)
{
	DIR *proc;
	struct dirent *de;
	pid_t pid;
	char buf[BUFSIZE], bufstat[300];
	char path[50];
	char *p, *q;
	unsigned long long start_time = 0;
	double uptime_secs;
	time_t start_date;

	if ((proc = opendir("/proc")) == NULL) {
		warnp("Could not open /proc");
		return;
	}

	if (!hz)
		init_hz();

	while ((de = readdir(proc)) != NULL) {

		if ((pid = (pid_t)atol(de->d_name)) == 0)
			continue;

		/* portage renames the cmdline so the package name is first */
		snprintf(path, sizeof(path), "/proc/%i/cmdline", pid);
		if (!eat_file(path, buf, sizeof(buf)))
			continue;

		if (buf[0] == '[' && (p = strchr(buf, ']')) != NULL && strstr(buf, "sandbox") != NULL) {
			*p = '\0';
			p = buf+1;
			q = p + strlen(p) + 1;

			/* open the stat file to figure out how long we have been running */
			snprintf(path, sizeof(path), "/proc/%i/stat", pid);
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
			start_date = time(0) - (uptime_secs - (start_time / hz));
			printf(
				" %s*%s %s%s%s\n"
				"     started: %s%s%s\n"
				"     elapsed: ", /*%s%llu%s seconds\n",*/
				BOLD, NORM, BLUE, p, NORM,
				GREEN, chop_ctime(start_date), NORM);
			print_seconds_for_earthlings(uptime_secs - (start_time / hz));
			puts(NORM);
		}
	}

	closedir(proc);

	if (start_time == 0 && verbose)
		puts("No emerge processes located");
}
#elif defined(__FreeBSD__)
void show_current_emerge(void)
{
	kvm_t *kd = NULL;
	struct kinfo_proc *ip;
	int i; int total_processes;
	char *p, *q;
	time_t start_date = 0;

	if (! (kd = kvm_open("/dev/null", "/dev/null", "/dev/null", O_RDONLY, "kvm_open"))) {
		warnp("Could not open kvm: %s", kvm_geterr(kd));
		return;
	}

	ip = kvm_getprocs(kd, KERN_PROC_PROC, 0, &total_processes);

	for (i = 0; i < total_processes; i++) {
		char **proc_argv = NULL;
		char *buf = NULL;

		if (strcmp(ip[i].ki_comm, "sandbox") != 0)
			continue;

		proc_argv = kvm_getargv(kd, &(ip[i]), 0);

		if (!proc_argv || (buf = xstrdup(proc_argv[0])) == NULL ||
		    buf[0] != '[' || (p = strchr(buf, ']')) == NULL) {
			free(buf);
			continue;
		}

		*p = '\0';
		p = buf+1;
		q = p + strlen(p) + 1;

		printf(
			" %s*%s %s%s%s\n"
			"     started: %s%s%s\n"
			"     elapsed: ", /*%s%llu%s seconds\n",*/
			BOLD, NORM, BLUE, p, NORM,
			GREEN, chop_ctime(ip[i].ki_start.tv_sec), NORM);
		print_seconds_for_earthlings(time(0) - ip[i].ki_start.tv_sec);
		puts(NORM);

		free(buf);
	}

	if (start_date == 0 && verbose)
		puts("No emerge processes located");
}
#else
void show_current_emerge(void)
{
	errf("not supported on your crapbox OS");
}
#endif

int qlop_main(int argc, char **argv)
{
	int i, average = 1;
	char do_time, do_list, do_unlist, do_sync, do_current, do_human_readable = 0;
	char *opt_logfile;
	const char *logfile = QLOP_DEFAULT_LOGFILE;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
		argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	opt_logfile = NULL;
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
			case 'H': do_human_readable = 1; break;
			case 'f':
				if (opt_logfile) err("Only use -f once");
				opt_logfile = xstrdup(optarg);
				break;
		}
	}
	if (!do_list && !do_unlist && !do_time && !do_sync && !do_current)
		qlop_usage(EXIT_FAILURE);
	if (opt_logfile != NULL)
		logfile = opt_logfile;

	argc -= optind;
	argv += optind;

	if (do_list && do_unlist)
		show_emerge_history(QLOP_LIST | QLOP_UNLIST, argc, argv, logfile);
	else if (do_list)
		show_emerge_history(QLOP_LIST, argc, argv, logfile);
	else if (do_unlist)
		show_emerge_history(QLOP_UNLIST, argc, argv, logfile);
	if (do_current)
		show_current_emerge();
	if (do_sync)
		show_sync_history(logfile);

	if (do_time) {
		for (i = 0; i < argc; ++i)
			show_merge_times(argv[i], logfile, average, do_human_readable);
	}

	if (opt_logfile) free(opt_logfile);

	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qlop)
#endif
