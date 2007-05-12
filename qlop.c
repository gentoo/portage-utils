/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qlop.c,v 1.38 2007/05/12 03:29:19 solar Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
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
static const char qlop_rcsid[] = "$Id: qlop.c,v 1.38 2007/05/12 03:29:19 solar Exp $";
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

void print_current_emerge(const char *logfile, const depend_atom *pkg);
#if defined(__linux__) || defined(__FreeBSD__)
void print_current_emerge(const char *logfile, const depend_atom *pkg)
{
	FILE *fp;
	char buf[2][BUFSIZE];
	char *p;
	unsigned long count, merge_time, elapsed_time = 0;
	time_t start_date, t[2];
	depend_atom *atom;

	start_date = t[0] = t[1] = 0UL;
	count = merge_time = 0;

	DBG("Searching for %s in %s\n", pkg->PN, logfile);

	if ((fp = fopen(logfile, "r")) == NULL) {
		warnp("Could not open logfile '%s'", logfile);
		return;
	}

	while ((fgets(buf[0], sizeof(buf[0]), fp)) != NULL) {
		if (strstr(buf[0], pkg->PN) == NULL)
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

			if (pkg->CATEGORY) {
				if ((strcmp(pkg->CATEGORY, atom->CATEGORY) == 0) && (strcmp(pkg->PN, atom->PN) == 0))
					matched = 1;
			} else if (strcmp(pkg->PN, atom->PN) == 0)
				matched = 1;

			if (matched) {
				start_date = t[0];
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

	elapsed_time = time(0) - start_date;
	printf(	" %s*%s %s%s%s%s\n"
			"     started: %s%s%s\n"
			"     elapsed: ",
	BOLD, NORM, BLUE, (pkg->CATEGORY ? strcat(pkg->CATEGORY, "/") : ""), pkg->P, NORM,
	GREEN, chop_ctime(start_date), NORM);
	print_seconds_for_earthlings(elapsed_time);
	printf("\n     ETA:     ");
	if (merge_time == 0)
		printf("Unknown");
	else if (merge_time / count < elapsed_time)
		printf("Sometime soon");
	else
		print_seconds_for_earthlings(merge_time / count - elapsed_time);
	puts("");
}
#else
void print_current_emerge(const char *logfile, const depend_atom *pkg)
{
	errf("not supported on your crapbox OS");
}
#endif

void show_current_emerge(const char *logfile);
#ifdef __linux__
void show_current_emerge(const char *logfile)
{
	DIR *proc;
	struct dirent *de;
	pid_t pid;
	char buf[2][BUFSIZE], path[50];
	char *p;
	depend_atom *atom;

	if ((proc = opendir("/proc")) == NULL) {
		warnp("Could not open /proc");
		return;
	}

	while ((de = readdir(proc)) != NULL) {

		if ((pid = (pid_t)atol(de->d_name)) == 0)
			continue;

		/* portage renames the cmdline so the package name is first */
		snprintf(path, sizeof(path), "/proc/%i/cmdline", pid);
		if (!eat_file(path, buf[0], sizeof(buf[0])))
			continue;

		if (buf[0][0] == '[' && (p = strchr(buf[0], ']')) != NULL && strstr(buf[0], "sandbox") != NULL) {
			/* try to fetch category from process env */
			snprintf(path, sizeof(path), "/proc/%i/environ", pid);
			if (eat_file(path, buf[1], sizeof(buf[1]))) {
				p = buf[1];
				while (strstr(p, "PORTAGE_BUILDDIR=") == NULL)
					p = strchr(p, '\0')+1;
				while (strchr(p, '/') != strrchr(p, '/'))
					p = strchr(p, '/')+1;
				if ((atom = atom_explode(p)) == NULL)
					continue;
			}
			/* fallback to packagename if not allowed */
			else {
			*p = '\0';
				if ((atom = atom_explode(buf[0]+1)) == NULL)
					continue;
			}
			print_current_emerge(logfile, atom);
			atom_implode(atom);
		}
	}

	closedir(proc);
}
#elif defined(__FreeBSD__)
void show_current_emerge(const char *logfile)
{
	kvm_t *kd = NULL;
	struct kinfo_proc *ip;
	int i, total_processes;
	char *p;
	depend_atom *atom;

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
		atom = atom_explode(buf+1);
		print_current_emerge(logfile, atom);
		atom_implode(atom);
		free(buf);
	}
}
#else
void show_current_emerge(const char *logfile)
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
		show_current_emerge(logfile);
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
