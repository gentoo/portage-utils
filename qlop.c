/*
 * Copyright 2005-2020 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>

#include "atom.h"
#include "eat_file.h"
#include "scandirat.h"
#include "set.h"
#include "xarray.h"
#include "xasprintf.h"

#define QLOP_DEFAULT_LOGFILE "emerge.log"

#define QLOP_FLAGS "ctaHMmuUsElerd:f:w:F:" COMMON_FLAGS
static struct option const qlop_long_opts[] = {
	{"summary",   no_argument, NULL, 'c'},
	{"time",      no_argument, NULL, 't'},
	{"average",   no_argument, NULL, 'a'},
	{"human",     no_argument, NULL, 'H'},
	{"machine",   no_argument, NULL, 'M'},
	{"merge",     no_argument, NULL, 'm'},
	{"unmerge",   no_argument, NULL, 'u'},
	{"autoclean", no_argument, NULL, 'U'},
	{"sync",      no_argument, NULL, 's'},
	{"emerge",    no_argument, NULL, 'E'},
	{"endtime",   no_argument, NULL, 'e'},
	{"running",   no_argument, NULL, 'r'},
	{"date",       a_argument, NULL, 'd'},
	{"lastmerge", no_argument, NULL, 'l'},
	{"logfile",    a_argument, NULL, 'f'},
	{"atoms",      a_argument, NULL, 'w'},
	{"format",     a_argument, NULL, 'F'},
	COMMON_LONG_OPTS
};
static const char * const qlop_opts_help[] = {
	"Print summary of average merges (implies -a)",
	"Print time taken to complete action",
	"Print average time taken to complete action",
	"Print elapsed time in human readable format (use with -t or -a)",
	"Print start/elapsed time as seconds with no formatting",
	"Show merge history",
	"Show unmerge history",
	"Show autoclean unmerge history",
	"Show sync history",
	"Show last merge like how emerge(1) -v would show it",
	"Report time at which the operation finished (iso started)",
	"Show current emerging packages",
	"Limit selection to this time (1st -d is start, 2nd -d is end)",
	"Limit selection to last Portage emerge action",
	"Read emerge logfile instead of $EMERGE_LOG_DIR/" QLOP_DEFAULT_LOGFILE,
	"Read package atoms to report from file",
	"Print matched atom using given format string",
	COMMON_OPTS_HELP
};
static const char qlop_desc[] =
	"The --date option can take a few forms:\n"
	"  -d '# <day|week|month|year>[s] [ago]'  (e.g. '3 days ago')\n"
	"Or using strptime(3) formats:\n"
	"  -d '2015-12-25'           (detected as %Y-%m-%d)\n"
	"  -d '1459101740'           (detected as %s)\n"
	"  -d '%d.%m.%Y|25.12.2015'  (format is specified)";
#define qlop_usage(ret) usage(ret, QLOP_FLAGS, qlop_long_opts, qlop_opts_help, qlop_desc, lookup_applet_idx("qlop"))

struct qlop_mode {
	char do_time:1;
	char do_merge:1;
	char do_unmerge:1;
	char do_autoclean:1;
	char do_sync:1;
	char do_running:1;
	char do_average:1;
	char do_summary:1;
	char do_human:1;
	char do_machine:1;
	char do_endtime:1;
	char show_lastmerge:1;
	char show_emerge:1;
	const char *fmt;
};

static bool
parse_date(const char *sdate, time_t *t)
{
	struct tm tm;
	const char *s;

	memset(&tm, 0, sizeof(tm));

	s = strchr(sdate, '|');
	if (s) {
		/* Handle custom format like "%Y|2012". */
		size_t fmtlen = s - sdate;
		char fmt[fmtlen + 1];
		memcpy(fmt, sdate, fmtlen);
		fmt[fmtlen] = '\0';
		sdate = s + 1;
		s = strptime(sdate, fmt, &tm);
		if (s == NULL || s[0] != '\0')
			return false;
	} else {
		/* Handle automatic formats:
		 * - "12315128"            -> %s
		 * - "2015-12-24"          -> %Y-%m-%d
		 * - "2019-03-28T13:52:31" -> %Y-%m-%dT%H:%M:%s"
		 * - human readable format (see below)
		 */
		size_t len = strspn(sdate, "0123456789-:T");
		if (sdate[len] == '\0') {
			const char *fmt;
			if (strchr(sdate, '-') == NULL) {
				fmt = "%s";
			} else if ((s = strchr(sdate, 'T')) == NULL) {
				fmt = "%Y-%m-%d";
			} else {
				fmt = "%Y-%m-%dT%H:%M:%S";
			}

			s = strptime(sdate, fmt, &tm);
			if (s == NULL || s[0] != '\0')
				return false;
		} else {
			/* Handle the formats:
			 * <#> <day|week|month|year>[s] [ago]
			 */
			len = strlen(sdate);

			unsigned long num;
			char dur[len];
			char ago[len];
			int ret = sscanf(sdate, "%lu %s %s", &num, dur, ago);

			if (ret < 2) {
				if (strcmp(sdate, "today") == 0) {
					num = 1;
					snprintf(dur, len, "%s", "day");
				} else if (strcmp(sdate, "yesterday") == 0) {
					num = 2;
					snprintf(dur, len, "%s", "day");
				} else {
					return false;
				}
			}
			if (ret == 3 && strcmp(ago, "ago") != 0)
				return false;

			if (time(t) == -1)
				return false;
			if (localtime_r(t, &tm) == NULL)
				return false;

			/* Chop and trailing "s" sizes. */
			len = strlen(dur);
			if (dur[len - 1] == 's')
				dur[len - 1] = '\0';

			/* Step down the current time. */
			if (!strcmp(dur, "year")) {
				tm.tm_year -= num;
			} else if (!strcmp(dur, "month")) {
				if (num >= 12) {
					tm.tm_year -= (num / 12);
					num %= 12;
				}
				tm.tm_mon -= num;
				if (tm.tm_mon < 0) {
					tm.tm_mon += 12;
					tm.tm_year -= 1;
				}
			} else if (!strcmp(dur, "week")) {
				num *= 7;
				goto days;
			} else if (!strcmp(dur, "day")) {
 days:
				/* This is in seconds, so scale w/that.  */
				*t -= (num * 24 * 60 * 60);
				if (localtime_r(t, &tm) == NULL)
					return false;
			} else
				return false;
		}
	}

	*t = mktime(&tm);
	return (*t == -1) ? false : true;
}

static char _date_buf[48];
static char *fmt_date(struct qlop_mode *flags, time_t ts, time_t te)
{
	time_t t = flags->do_endtime ? te : ts;
	struct tm lt;

	if (flags->do_machine || localtime_r(&t, &lt) == NULL)
		snprintf(_date_buf, sizeof(_date_buf),
				"%zd", (size_t)t);
	else
		strftime(_date_buf, sizeof(_date_buf),
				"%Y-%m-%dT%H:%M:%S", &lt);

	return _date_buf;
}

static char _elapsed_buf[256];
static char *fmt_elapsedtime(struct qlop_mode *flags, time_t e)
{
	time_t dd;
	time_t hh;
	time_t mm;
	time_t ss;
	size_t bufpos = 0;

	if (flags->do_machine || e < 0) {
		snprintf(_elapsed_buf, sizeof(_elapsed_buf),
				"%s%zd%s",
				GREEN, (size_t)e, NORM);
		return _elapsed_buf;
	}

	ss = e % 60;
	e /= 60;
	mm = e % 60;
	e /= 60;
	hh = e % 24;
	e /= 24;
	dd = e;

	if (flags->do_human) {
		if (dd > 0)
			bufpos += snprintf(_elapsed_buf + bufpos,
					sizeof(_elapsed_buf) - bufpos,
					"%s%zd%s day%s",
					GREEN, (size_t)dd, NORM, dd == 1 ? "" : "s");
		if (hh > 0)
			bufpos += snprintf(_elapsed_buf + bufpos,
					sizeof(_elapsed_buf) - bufpos,
					"%s%s%zd%s hour%s",
					bufpos == 0 ? "" : ", ",
					GREEN, (size_t)hh, NORM, hh == 1 ? "" : "s");
		if (mm > 0)
			bufpos += snprintf(_elapsed_buf + bufpos,
					sizeof(_elapsed_buf) - bufpos,
					"%s%s%zd%s minute%s",
					bufpos == 0 ? "" : ", ",
					GREEN, (size_t)mm, NORM, mm == 1 ? "" : "s");
		if (ss > 0 || (mm + hh + dd) == 0)
			bufpos += snprintf(_elapsed_buf + bufpos,
					sizeof(_elapsed_buf) - bufpos,
					"%s%s%zd%s second%s",
					bufpos == 0 ? "" : ", ",
					GREEN, (size_t)ss, NORM, ss == 1 ? "" : "s");
	} else {
		hh += 24 * dd;
		if (hh > 0) {
			snprintf(_elapsed_buf, sizeof(_elapsed_buf),
					"%s%zd%s:%s%02zd%s:%s%02zd%s",
					GREEN, (size_t)hh, NORM,
					GREEN, (size_t)mm, NORM,
					GREEN, (size_t)ss, NORM);
		} else if (mm > 0) {
			snprintf(_elapsed_buf, sizeof(_elapsed_buf),
					"%s%zd%s′%s%02zd%s″",
					GREEN, (size_t)mm, NORM,
					GREEN, (size_t)ss, NORM);
		} else {
			snprintf(_elapsed_buf, sizeof(_elapsed_buf),
					"%s%zd%ss",
					GREEN, (size_t)ss, NORM);
		}
	}

	return _elapsed_buf;
}

struct pkg_match {
	char id[BUFSIZ];
	depend_atom *atom;
	time_t tbegin;
	time_t time;
	size_t cnt;
};

static int
pkg_sort_cb(const void *l, const void *r)
{
	struct pkg_match *pl = *(struct pkg_match **)l;
	struct pkg_match *pr = *(struct pkg_match **)r;
	depend_atom *al = pl->atom;
	depend_atom *ar = pr->atom;

	return atom_compar_cb(al, ar);
}

/* The format of the sync log has changed over time.

Old format:
1106804103: Started emerge on: Jan 27, 2005 05:35:03
1106804103:  *** emerge  sync
1106804103:  === sync
1106804103: >>> starting rsync with rsync://192.168.0.5/gentoo-portage
1106804537: === Sync completed with rsync://192.168.0.5/gentoo-portage
1106804538:  *** terminating.

New format:
1431764402: Started emerge on: May 16, 2015 04:20:01
1431764402:  *** emerge --quiet --keep-going --verbose --nospinner --oneshot --quiet-build=n --sync
1431764402:  === sync
1431764402: >>> Syncing repository 'gentoo' into '/usr/portage'...
1431764402: >>> Starting rsync with rsync://[2a01:90:200:10::1a]/gentoo-portage
1431764460: === Sync completed for gentoo
1431764493:  *** terminating.

*** packages

1547475773:  >>> emerge (53 of 74) app-shells/bash-5.0 to /gentoo/prefix64/
1547475774:  === (53 of 74) Cleaning (app-shells/bash-5.0::/path/to/app-shells/bash/bash-5.0.ebuild)
1547475774:  === (53 of 74) Compiling/Merging (app-shells/bash-5.0::/path/to/app-shells/bash/bash-5.0.ebuild)
1547475913:  === (53 of 74) Merging (app-shells/bash-5.0::/path/to/app-shells/bash/bash-5.0.ebuild)
1547475916:  >>> AUTOCLEAN: app-shells/bash:0
1547475916:  === Unmerging... (app-shells/bash-4.4_p23)
1547475918:  >>> unmerge success: app-shells/bash-4.4_p23
1547475921:  === (53 of 74) Post-Build Cleaning (app-shells/bash-5.0::/path/to/app-shells/bash/bash-5.0.ebuild)
1547475921:  ::: completed emerge (53 of 74) app-shells/bash-5.0 to /gentoo/prefix64/

1550953093: Started emerge on: Feb 23, 2019 21:18:12
1550953093:  *** emerge --ask --verbose --depclean pwgen
1550953093:  >>> depclean
1550953118: === Unmerging... (app-admin/pwgen-2.08)
1550953125:  >>> unmerge success: app-admin/pwgen-2.08
1550953125:  *** exiting successfully.
1550953125:  *** terminating.

Currently running merges can be found in the /proc filesystem:
- Linux: readlink(/proc/<pid>/fd/X)
- Solaris: readlink(/proc/<pid>/path/X)
from here a file should be there that points to the build.log file
$CAT/$P/work/build.log.  If so, it's running for $CAT/$P.
This requires being the portage user though, or root.

Should there be no /proc, we can deduce from the log whether a package
is being emerged, if and only if, there are no parallel merges, and
portage never got interrupted in a way where it could not write its
interruption to the log.  Unfortunately these scenarios happen a lot.
As such, we can try to remedy this somewhat by using a rule of thumb
that currently merging packages need to be withinin the last 10 days.
*/
static int do_emerge_log(
		const char *log,
		struct qlop_mode *flags,
		array_t *atoms,
		time_t tbegin,
		time_t tend)
{
	FILE *fp;
	char buf[BUFSIZ];
	char *p;
	char *q;
	time_t tstart = LONG_MAX;
	time_t tlast = tbegin;
	time_t tstart_emerge = 0;
	time_t last_merge = 0;
	time_t sync_start = 0;
	time_t sync_time = 0;
	size_t sync_cnt = 0;
	time_t elapsed;
	depend_atom *atom;
	depend_atom *atomw;
	depend_atom *upgrade_atom = NULL;
	DECLARE_ARRAY(merge_matches);
	set *merge_averages = create_set();
	DECLARE_ARRAY(unmerge_matches);
	set *unmerge_averages = create_set();
	set *atomset = NULL;
	size_t i;
	size_t parallel_emerge = 0;
	bool all_atoms = false;
	char afmt[BUFSIZ];
	struct pkg_match *pkg;
	struct pkg_match *pkgw;

	/* support relative path in here and now, when using ROOT, stick to
	 * it, turning relative into a moot point */
	if (portroot[1] == '\0')
		snprintf(buf, sizeof(buf), "%s", log);
	else
		snprintf(buf, sizeof(buf), "%s%s", portroot, log);
	if ((fp = fopen(buf, "r")) == NULL)
	{
		warnp("Could not open logfile '%s'", log);
		return 1;
	}

	all_atoms = array_cnt(atoms) == 0;
	if (all_atoms || flags->show_lastmerge) {
		atomset = create_set();

		/* assemble list of atoms */
		while (fgets(buf, sizeof(buf), fp) != NULL) {
			if ((p = strchr(buf, ':')) == NULL)
				continue;
			*p++ = '\0';

			tstart = atol(buf);
			if (tstart < tbegin || tstart > tend)
				continue;

			if (flags->show_lastmerge) {
				if (strncmp(p, "  *** emerge ", 13) == 0)
					tstart_emerge = tstart;
				if (!all_atoms)
					continue;
			}

			atom = NULL;
			if (strncmp(p, "  >>> emerge ", 13) == 0 &&
					(q = strchr(p + 13, ')')) != NULL)
			{
				p = q + 2;
				q = strchr(p, ' ');
				if (q != NULL) {
					*q = '\0';
					atom = atom_explode(p);
				}
			} else if (strncmp(p, "  === Unmerging... (", 20) == 0 ||
					strncmp(p, " === Unmerging... (", 19) == 0)
			{
				if (p[1] == ' ')
					p++;
				p += 19;
				q = strchr(p, ')');
				if (q != NULL) {
					*q = '\0';
					atom = atom_explode(p);
				}
			}
			if (atom != NULL) {
				/* strip off version info, if we generate a list
				 * ourselves, we will always print everything, so as
				 * well can keep memory footprint a bit lower by only
				 * having package matches */
				atom->PV = NULL;
				atom->PVR = NULL;
				atom->PR_int = 0;
				snprintf(afmt, sizeof(afmt), "%s/%s", atom->CATEGORY, atom->PN);

				/* now we found a package, register this merge as a
				 * "valid" one, such that dummy emerge calls (e.g.
				 * emerge -pv foo) are ignored */
				if (last_merge != tstart_emerge) {
					array_t vals;

					values_set(atomset, &vals);
					array_for_each(&vals, i, atomw)
						atom_implode(atomw);
					xarrayfree_int(&vals);

					clear_set(atomset);
					last_merge = tstart_emerge;
				}

				atomw = add_set_value(afmt, atom, atomset);
				if (atomw != NULL)
					atom_implode(atom);
			}
		}

		rewind(fp);
	}

	if (flags->show_lastmerge) {
		tbegin = last_merge;
		tend = tstart;
	}

	/* loop over lines searching for atoms */
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if ((p = strchr(buf, ':')) == NULL)
			continue;
		*p++ = '\0';

		/* keeping track of parallel merges needs to be done before
		 * applying dates, for a subset of the log might show emerge
		 * finished without knowledge of another instance */
		if (flags->do_running &&
				(strncmp(p, "  *** emerge ", 13) == 0 ||
				 strncmp(p, "  *** terminating.", 18) == 0))
		{
			if (p[7] == 'm') {
				parallel_emerge++;
			} else if (parallel_emerge > 0) {
				parallel_emerge--;
			}

			/* for bug #687508, this cannot be in the else if case
			 * above, if the log is truncated somehow, the leading
			 * *** emerge might be missing, but a termination in that
			 * case better means we forget about everything that was
			 * unfinished not to keep reporting some packages forever */
			if (parallel_emerge == 0) {
				/* we just finished the only emerge we found to be
				 * running, so if there were "running" (unfinished)
				 * merges, they must have been terminated */
				sync_start = 0;
				while ((i = array_cnt(merge_matches)) > 0) {
					i--;
					pkgw = xarrayget(merge_matches, i);
					atom_implode(pkgw->atom);
					xarraydelete(merge_matches, i);
				}
				while ((i = array_cnt(unmerge_matches)) > 0) {
					i--;
					pkgw = xarrayget(unmerge_matches, i);
					atom_implode(pkgw->atom);
					xarraydelete(unmerge_matches, i);
				}
			}
		}

		tstart = atol(buf);
		if (tstart < tlast)
			continue;
		tlast = tstart;
		if (tstart < tbegin || tstart > tend)
			continue;

		/* are we interested in this line? */
		if (flags->do_sync && (
					strncmp(p, " === Sync completed ", 20) == 0 ||
					strcmp(p, "  === sync\n") == 0))
		{
			/* sync start or stop, we have nothing to detect parallel
			 * syncs with, so don't bother and assume this doesn't
			 * happen */
			if (p[6] == 's') {
				sync_start = tstart;
			} else {
				if (sync_start == 0)
					continue;  /* sync without start, exclude */
				elapsed = tstart - sync_start;

				p += 20;
				if (strncmp(p, "for ", 4) == 0) {
					p += 4;
				} else {  /* "with " */
					p += 5;
				}
				if ((q = strchr(p, '\n')) != NULL)
					*q = '\0';

				if (flags->do_average || flags->do_running)
				{
					sync_cnt++;
					sync_time += elapsed;
					sync_start = 0;  /* reset */
					continue;
				}
				if (quiet) {
					printf("%s%s%s%s%s\n",
							GREEN, p, NORM,
							flags->do_time ? ": " : "",
							flags->do_time ?
								fmt_elapsedtime(flags, elapsed) : "");
				} else if (flags->do_time) {
					printf("%s *** %s%s%s: %s\n",
							fmt_date(flags, sync_start, tstart),
							GREEN, p, NORM,
							fmt_elapsedtime(flags, elapsed));
				} else {
					printf("%s *** %s%s%s\n",
							fmt_date(flags, sync_start, tstart),
							GREEN, p, NORM);
				}
				sync_start = 0;  /* reset */
			}
		} else if (flags->do_merge && (
					strncmp(p, "  >>> emerge (", 14) == 0 ||
					strncmp(p, "  ::: completed emerge (", 24) == 0))
		{
			/* merge start/stop (including potential unmerge of old pkg) */
			if (p[3] == '>') {  /* >>> emerge */
				char *id;

				q = strchr(p + 14, ')');
				if (q == NULL)
					continue;

				/* keep a copy of the relevant string in case we need to
				 * match this */
				id = p + 6;

				q += 2;  /* ") " */
				p = strchr(q, ' ');
				if (p == NULL)
					continue;

				*p = '\0';
				atom = atom_explode(q);
				*p = ' ';
				if (atom == NULL)
					continue;

				/* see if we need this atom */
				atomw = NULL;
				if (atomset == NULL) {
					array_for_each(atoms, i, atomw) {
						if (atom_compare(atom, atomw) == EQUAL)
							break;
						atomw = NULL;
					}
				} else {
					snprintf(afmt, sizeof(afmt), "%s/%s",
							atom->CATEGORY, atom->PN);
					atomw = get_set(afmt, atomset);
				}
				if (atomw == NULL) {
					atom_implode(atom);
					continue;
				}

				pkg = xmalloc(sizeof(struct pkg_match));
				snprintf(pkg->id, sizeof(pkg->id), "%s", id);
				pkg->atom = atom;
				pkg->tbegin = tstart;
				pkg->time = (time_t)0;
				pkg->cnt = 0;
				xarraypush_ptr(merge_matches, pkg);
			} else {  /* ::: completed */
				array_for_each_rev(merge_matches, i, pkgw) {
					if (strcmp(p + 16, pkgw->id) != 0)
						continue;

					/* found, do report */
					elapsed = tstart - pkgw->tbegin;

					if (flags->do_average || flags->do_running)
					{
						/* find in list of averages */
						if (!verbose || flags->do_running) {
							snprintf(afmt, sizeof(afmt), "%s/%s",
								pkgw->atom->CATEGORY, pkgw->atom->PN);
						} else {
							snprintf(afmt, sizeof(afmt), "%s/%s",
								pkgw->atom->CATEGORY, pkgw->atom->PF);
						}

						pkg = add_set_value(afmt, pkgw, merge_averages);
						if (pkg != NULL) {
							pkg->cnt++;
							pkg->time += elapsed;
							/* store max time for do_running */
							if (elapsed > pkg->tbegin)
								pkg->tbegin = elapsed;
							atom_implode(pkgw->atom);
							xarraydelete(merge_matches, i);
						} else {
							/* new entry */
							pkgw->id[0] = '\0';
							pkgw->cnt = 1;
							pkgw->time = elapsed;
							pkgw->tbegin = elapsed;
							xarraydelete_ptr(merge_matches, i);
						}
						break;
					}
					if (quiet && !flags->do_average) {
						printf("%s%s%s\n",
								atom_format(flags->fmt, pkgw->atom),
								flags->do_time ? ": " : "",
								flags->do_time ?
									fmt_elapsedtime(flags, elapsed) : "");
					} else if (flags->show_emerge) {
						int state = NOT_EQUAL;
						if (upgrade_atom != NULL)
							state = atom_compare(pkgw->atom, upgrade_atom);
						switch (state) {
							/*         "NRUD " */
							case EQUAL:
								printf(" %sR%s   ", YELLOW, NORM);
								break;
							case NOT_EQUAL:
								printf("%sN%s    ", GREEN, NORM);
								break;
							case NEWER:
								printf("  %sU%s  ", BLUE, NORM);
								break;
							case OLDER:
								printf("  %sU%sD%s ", BLUE, DKBLUE, NORM);
								break;
						}
						printf("%s", atom_format(flags->fmt, pkgw->atom));
						if (state == NEWER || state == OLDER)
							printf(" %s[%s%s%s]%s", DKBLUE, NORM,
									atom_format("%[PVR]", upgrade_atom),
									DKBLUE, NORM);
						if (flags->do_time)
							printf(": %s\n", fmt_elapsedtime(flags, elapsed));
						else
							printf("\n");
					} else if (flags->do_time) {
						printf("%s >>> %s: %s\n",
								fmt_date(flags, pkgw->tbegin, tstart),
								atom_format(flags->fmt, pkgw->atom),
								fmt_elapsedtime(flags, elapsed));
					} else if (!flags->do_average) {
						printf("%s >>> %s\n",
								fmt_date(flags, pkgw->tbegin, tstart),
								atom_format(flags->fmt, pkgw->atom));
					}
					atom_implode(pkgw->atom);
					xarraydelete(merge_matches, i);
					break;
				}
			}
		} else if (
				(flags->do_unmerge &&
				 strncmp(p, " === Unmerging... (", 19) == 0) ||
				(flags->do_autoclean &&
				 strncmp(p, "  === Unmerging... (", 20) == 0) ||
				((flags->do_unmerge || flags->do_autoclean) &&
				 strncmp(p, "  >>> unmerge success: ", 23) == 0))
		{
			/* unmerge action */
			if (p[2] == '=') {
				if (p[1] == ' ')
					p++;
				p += 19;

				if ((q = strchr(p, ')')) == NULL)
					continue;

				*q = '\0';
				atom = atom_explode(p);
				if (atom == NULL)
					continue;

				if (p[-20] == ' ' && flags->show_emerge)
				{
					if (upgrade_atom != NULL)
						atom_implode(upgrade_atom);
					upgrade_atom = atom;
					continue;
				}

				/* see if we need this atom */
				atomw = NULL;
				if (atomset == NULL) {
					array_for_each(atoms, i, atomw) {
						if (atom_compare(atom, atomw) == EQUAL)
							break;
						atomw = NULL;
					}
				} else {
					snprintf(afmt, sizeof(afmt), "%s/%s",
							atom->CATEGORY, atom->PN);
					atomw = get_set(afmt, atomset);
				}
				if (atomw == NULL) {
					atom_implode(atom);
					continue;
				}

				pkg = xmalloc(sizeof(struct pkg_match));
				snprintf(pkg->id, sizeof(pkg->id), "%s\n", p);  /* \n !!! */
				pkg->atom = atom;
				pkg->tbegin = tstart;
				pkg->time = (time_t)0;
				pkg->cnt = 0;
				xarraypush_ptr(unmerge_matches, pkg);
			} else {
				array_for_each_rev(unmerge_matches, i, pkgw) {
					if (strcmp(p + 23, pkgw->id) != 0)
						continue;

					/* found, do report */
					elapsed = tstart - pkgw->tbegin;

					if (flags->do_average || flags->do_running)
					{
						/* find in list of averages */
						if (!verbose || flags->do_running) {
							snprintf(afmt, sizeof(afmt), "%s/%s",
								pkgw->atom->CATEGORY, pkgw->atom->PN);
						} else {
							snprintf(afmt, sizeof(afmt), "%s/%s",
								pkgw->atom->CATEGORY, pkgw->atom->PF);
						}

						pkg = add_set_value(afmt, pkgw, unmerge_averages);
						if (pkg != NULL) {
							pkg->cnt++;
							pkg->time += elapsed;
							/* store max time for do_running */
							if (elapsed > pkg->tbegin)
								pkg->tbegin = elapsed;
							atom_implode(pkgw->atom);
							xarraydelete(unmerge_matches, i);
						} else {
							/* new entry */
							pkgw->id[0] = '\0';
							pkgw->cnt = 1;
							pkgw->time = elapsed;
							pkgw->tbegin = elapsed;
							xarraydelete_ptr(unmerge_matches, i);
						}
						break;
					}
					if (quiet && !flags->do_average) {
						printf("%s%s%s\n",
								atom_format(flags->fmt, pkgw->atom),
								flags->do_time ? ": " : "",
								flags->do_time ?
									fmt_elapsedtime(flags, elapsed) : "");
					} else if (flags->do_time) {
						printf("%s <<< %s: %s\n",
								fmt_date(flags, pkgw->tbegin, tstart),
								atom_format(flags->fmt, pkgw->atom),
								fmt_elapsedtime(flags, elapsed));
					} else if (!flags->do_average) {
						printf("%s <<< %s\n",
								fmt_date(flags, pkgw->tbegin, tstart),
								atom_format(flags->fmt, pkgw->atom));
					}
					atom_implode(pkgw->atom);
					xarraydelete(unmerge_matches, i);
					break;
				}
			}
		}
	}
	fclose(fp);
	if (flags->do_running) {
		time_t cutofftime;
		set *pkgs_seen = create_set();

		tstart = time(NULL);

		/* emerge.log can be interrupted, incorrect and hopelessly lost,
		 * so to eliminate some unfinished crap from there, we just
		 * ignore anything that's > cutofftime, 10 days for now. */
		cutofftime = 10 * 24 * 60 * 60;  /* when we consider entries stale */
		cutofftime = (tbegin > 0 ? tbegin : tstart) - cutofftime;

		/* can't report endtime for non-finished operations */
		flags->do_endtime = 0;
		if (sync_time > 0)
			sync_time /= sync_cnt;
		if (sync_start >= cutofftime) {
			elapsed = tstart - sync_start;
			if (elapsed >= sync_time)
				sync_time = 0;
			if (flags->do_time) {
				elapsed = tstart - sync_start;
				printf("%s *** %s%s%s: %s... ETA: %s\n",
						fmt_date(flags, sync_start, 0),
						YELLOW, "sync", NORM, fmt_elapsedtime(flags, elapsed),
						sync_time == 0 ? "unknown" :
							fmt_elapsedtime(flags, sync_time - elapsed));
			} else {
				printf("%s *** %s%s%s... ETA: %s\n",
						fmt_date(flags, sync_start, 0),
						YELLOW, "sync", NORM,
						sync_time == 0 ? "unknown" :
							fmt_elapsedtime(flags, sync_time - elapsed));
			}
		}
		array_for_each_rev(merge_matches, i, pkgw) {
			time_t maxtime = 0;
			bool isMax = false;
			bool notseen;

			if (pkgw->tbegin < cutofftime)
				continue;

			snprintf(afmt, sizeof(afmt), "%s/%s",
					pkgw->atom->CATEGORY, pkgw->atom->PN);

			/* eliminate dups, bug #701392 */
			add_set_unique(afmt, pkgs_seen, &notseen);
			if (!notseen)
				continue;

			elapsed = tstart - pkgw->tbegin;
			pkg = get_set(afmt, merge_averages);
			if (pkg != NULL) {
				maxtime = pkg->time / pkg->cnt;
				if (elapsed >= maxtime) {
					maxtime = pkg->tbegin;
					if (elapsed >= maxtime)
						maxtime -= elapsed;
					isMax = true;
				}
			}

			/* extract (X of Y) from id, bug #442406 */
			if ((p = strchr(pkgw->id, '(')) != NULL) {
				if ((q = strchr(p, ')')) != NULL) {
					q[1] = '\0';
					p--;
				} else {
					p = NULL;
				}
			}

			if (flags->do_time) {
				printf("%s >>> %s: %s",
						fmt_date(flags, pkgw->tbegin, 0),
						atom_format(flags->fmt, pkgw->atom),
						fmt_elapsedtime(flags, elapsed));
			} else {
				printf("%s >>> %s",
						fmt_date(flags, pkgw->tbegin, 0),
						atom_format(flags->fmt, pkgw->atom));
			}
			if (maxtime < 0)
				printf("... +%s\n", fmt_elapsedtime(flags, -maxtime));
			else
				printf("...%s ETA: %s%s\n",
						p == NULL ? "" : p,
						maxtime == 0 ? "unknown" :
							fmt_elapsedtime(flags, maxtime - elapsed),
						maxtime > 0 && verbose ?
							isMax ? " (longest run)" : " (average run)" : "");
		}
		clear_set(pkgs_seen);
		array_for_each(unmerge_matches, i, pkgw) {
			time_t maxtime = 0;
			bool isMax = false;
			bool notseen;

			if (pkgw->tbegin < cutofftime)
				continue;

			snprintf(afmt, sizeof(afmt), "%s/%s",
					pkgw->atom->CATEGORY, pkgw->atom->PN);

			/* eliminate dups, bug #701392 */
			add_set_unique(afmt, pkgs_seen, &notseen);
			if (!notseen)
				continue;

			elapsed = tstart - pkgw->tbegin;
			pkg = get_set(afmt, unmerge_averages);
			if (pkg != NULL) {
				maxtime = pkg->time / pkg->cnt;
				if (elapsed >= maxtime) {
					maxtime = pkg->tbegin;
					if (elapsed >= maxtime)
						maxtime -= elapsed;
					isMax = true;
				}
			}

			if (flags->do_time) {
				printf("%s <<< %s: %s",
						fmt_date(flags, pkgw->tbegin, 0),
						atom_format(flags->fmt, pkgw->atom),
						fmt_elapsedtime(flags, elapsed));
			} else {
				printf("%s <<< %s",
						fmt_date(flags, pkgw->tbegin, 0),
						atom_format(flags->fmt, pkgw->atom));
			}
			if (maxtime < 0)
				printf("... +%s\n", fmt_elapsedtime(flags, -maxtime));
			else
				printf("... ETA: %s%s\n",
						maxtime == 0 ? "unknown" :
							fmt_elapsedtime(flags, maxtime - elapsed),
						maxtime > 0 && verbose ?
							isMax ? " (longest run)" : " (average run)" : "");
		}
		free_set(pkgs_seen);
	} else if (flags->do_average) {
		size_t total_merges = 0;
		size_t total_unmerges = 0;
		time_t total_time = (time_t)0;
		DECLARE_ARRAY(avgs);

		values_set(merge_averages, avgs);
		xarraysort(avgs, pkg_sort_cb);
		array_for_each(avgs, i, pkg) {
			printf("%s: %s average for %s%zd%s merge%s\n",
					atom_format(flags->fmt, pkg->atom),
					fmt_elapsedtime(flags, pkg->time / pkg->cnt),
					GREEN, pkg->cnt, NORM, pkg->cnt == 1 ? "" : "s");
			total_merges += pkg->cnt;
			total_time += pkg->time;
		}
		xarrayfree_int(avgs);

		values_set(unmerge_averages, avgs);
		xarraysort(avgs, pkg_sort_cb);
		array_for_each(avgs, i, pkg) {
			printf("%s: %s average for %s%zd%s unmerge%s\n",
					atom_format(flags->fmt, pkg->atom),
					fmt_elapsedtime(flags, pkg->time / pkg->cnt),
					GREEN, pkg->cnt, NORM, pkg->cnt == 1 ? "" : "s");
			total_unmerges += pkg->cnt;
			total_time += pkg->time;
		}
		xarrayfree_int(avgs);

		if (sync_cnt > 0) {
			printf("%ssync%s: %s average for %s%zd%s sync%s\n",
					BLUE, NORM, fmt_elapsedtime(flags, sync_time / sync_cnt),
					GREEN, sync_cnt, NORM, sync_cnt == 1 ? "" : "s");
			total_time += sync_time;
		}
		if (flags->do_summary) {
			/* 123 seconds for 5 merges, 3 unmerges, 1 sync */
			printf("%stotal%s: %s for ",
					BLUE, NORM, fmt_elapsedtime(flags, total_time));
			if (total_merges > 0)
				printf("%s%zd%s merge%s",
					GREEN, total_merges, NORM, total_merges == 1 ? "" : "s");
			if (total_unmerges > 0)
				printf("%s%s%zd%s unmerge%s",
						total_merges == 0 ? "" : ", ",
						GREEN, total_unmerges, NORM,
						total_unmerges == 1 ? "" : "s");
			if (sync_cnt > 0)
				printf("%s%s%zd%s sync%s",
						total_merges + total_unmerges == 0 ? "" : ", ",
						GREEN, sync_cnt, NORM, sync_cnt == 1 ? "" : "s");
			printf("\n");
		}
	}

	{
		DECLARE_ARRAY(t);
		values_set(merge_averages, t);
		array_for_each(t, i, pkgw) {
			atom_implode(pkgw->atom);
			free(pkgw);
		}
		xarrayfree_int(t);
		values_set(unmerge_averages, t);
		array_for_each(t, i, pkgw) {
			atom_implode(pkgw->atom);
			free(pkgw);
		}
		xarrayfree_int(t);
	}
	free_set(merge_averages);
	free_set(unmerge_averages);
	array_for_each_rev(merge_matches, i, pkgw) {
		atom_implode(pkgw->atom);
		xarraydelete(merge_matches, i);
	}
	xarrayfree(merge_matches);
	array_for_each_rev(unmerge_matches, i, pkgw) {
		atom_implode(pkgw->atom);
		xarraydelete(unmerge_matches, i);
	}
	xarrayfree(unmerge_matches);
	if (atomset != NULL) {
		DECLARE_ARRAY(t);
		values_set(atomset, t);
		array_for_each(t, i, atom)
			atom_implode(atom);
		xarrayfree_int(t);
		free_set(atomset);
	}
	return 0;
}

/* scan through /proc for running merges, this requires portage user
 * or root */
static array_t *probe_proc(array_t *atoms)
{
	struct dirent **procs;
	int procslen;
	int pi;
	struct dirent **links;
	int linkslen;
	int li;
	struct dirent *d;
	char npath[_Q_PATH_MAX * 2];
	char rpath[_Q_PATH_MAX];
	const char *subdir = NULL;
	const char *pid;
	ssize_t rpathlen;
	char *p;
	depend_atom *atom;
	DECLARE_ARRAY(ret_atoms);
	size_t i;

	/* /proc/<pid>/path/<[0-9]+link>
	 * /proc/<pid>/fd/<[0-9]+link> */
	if ((procslen = scandir("/proc", &procs, NULL, NULL)) > 0) {
		for (pi = 0; pi < procslen; pi++) {
			d = procs[pi];
			/* must be [0-9]+ */
			if (d->d_name[0] < '0' || d->d_name[0] > '9')
				continue;

			if (subdir == NULL) {
				struct stat st;

				snprintf(npath, sizeof(npath), "/proc/%s/path", d->d_name);
				if (stat(npath, &st) < 0)
					subdir = "fd";
				else
					subdir = "path";
			}

			pid = d->d_name;
			snprintf(npath, sizeof(npath), "/proc/%s/%s", pid, subdir);
			if ((linkslen = scandir(npath, &links, NULL, NULL)) > 0) {
				for (li = 0; li < linkslen; li++) {
					d = links[li];
					/* must be [0-9]+ */
					if (d->d_name[0] < '0' || d->d_name[0] > '9')
						continue;
					snprintf(npath, sizeof(npath), "/proc/%s/%s/%s",
							pid, subdir, d->d_name);
					rpathlen = readlink(npath, rpath, sizeof(rpath));
					if (rpathlen <= 0)
						continue;
					rpath[rpathlen] = '\0';
					/* check if this points to a portage build:
					 * <somepath>/portage/<cat>/<pf>/temp/build.log */
					if ((size_t)rpathlen > sizeof("/temp/build.log") &&
								strcmp(rpath + rpathlen -
								(sizeof("/temp/build.log") - 1),
								"/temp/build.log") == 0 &&
							(p = strstr(rpath, "/portage/")) != NULL)
					{
						p += sizeof("/portage/") - 1;
						rpath[rpathlen - (sizeof("/temp/build.log") - 1)] =
							'\0';
						atom = atom_explode(p);
						if (atom == NULL ||
								atom->CATEGORY == NULL || atom->P == NULL)
						{
							if (atom != NULL)
								atom_implode(atom);
							continue;
						}
						xarraypush_ptr(ret_atoms, atom);
					}
				}
				scandir_free(links, linkslen);
			}
		}
		scandir_free(procs, procslen);
	} else {
		/* flag /proc doesn't exist */
		warn("/proc doesn't exist, running merges are based on heuristics");
		return NULL;
	}

	if (array_cnt(ret_atoms) == 0) {
		/* if we didn't find anything, this is either because nothing is
		 * running, or because we didn't have appropriate permissions --
		 * try to figure out which of the two is it (there is no good
		 * way) */
		if (geteuid() != 0) {
			warn("insufficient privileges for full /proc access, "
					"running merges are based on heuristics");
			return NULL;
		}
	}

	if (array_cnt(atoms) > 0) {
		size_t j;
		depend_atom *atomr;

		/* calculate intersection */
		array_for_each(atoms, i, atom) {
			array_for_each(ret_atoms, j, atomr) {
				if (atom_compare(atomr, atom) != EQUAL) {
					xarraydelete_ptr(ret_atoms, j);
					atom_implode(atomr);
					break;
				}
			}
			atom_implode(atom);
		}
		xarrayfree_int(atoms);
	}

	/* ret_atoms is allocated on the stack, so copy into atoms which is
	 * empty at this point */
	array_for_each(ret_atoms, i, atom)
		xarraypush_ptr(atoms, atom);

	xarrayfree_int(ret_atoms);

	return atoms;
}

int qlop_main(int argc, char **argv)
{
	size_t i;
	int ret;
	time_t start_time;
	time_t end_time;
	struct qlop_mode m;
	char *logfile = NULL;
	char *atomfile = NULL;
	char *p;
	char *q;
	depend_atom *atom;
	DECLARE_ARRAY(atoms);
	int runningmode = 0;

	start_time = 0;
	end_time = LONG_MAX;
	m.do_time = 0;
	m.do_merge = 0;
	m.do_unmerge = 0;
	m.do_autoclean = 0;
	m.do_sync = 0;
	m.do_running = 0;
	m.do_average = 0;
	m.do_summary = 0;
	m.do_human = 0;
	m.do_machine = 0;
	m.do_endtime = 0;
	m.show_lastmerge = 0;
	m.show_emerge = 0;
	m.fmt = NULL;

	while ((ret = GETOPT_LONG(QLOP, qlop, "")) != -1) {
		switch (ret) {
			COMMON_GETOPTS_CASES(qlop)

			case 't': m.do_time = 1;        break;
			case 'm': m.do_merge = 1;       break;
			case 'u': m.do_unmerge = 1;     break;
			case 'U': m.do_autoclean = 1;   break;
			case 's': m.do_sync = 1;        break;
			case 'E': m.do_merge = 1;
					  m.do_unmerge = 1;
					  m.do_autoclean = 1;
					  m.show_lastmerge = 1;
					  m.show_emerge = 1;
					  verbose = 1;          break;
			case 'r': m.do_running = 1;
					  runningmode++;        break;
			case 'a': m.do_average = 1;     break;
			case 'c': m.do_summary = 1;     break;
			case 'H': m.do_human = 1;       break;
			case 'M': m.do_machine = 1;     break;
			case 'e': m.do_endtime = 1;     break;
			case 'l': m.show_lastmerge = 1; break;
			case 'F': m.fmt = optarg;       break;
			case 'd':
				if (start_time == 0) {
					if (!parse_date(optarg, &start_time))
						err("invalid date: %s", optarg);
				} else if (end_time == LONG_MAX) {
					if (!parse_date(optarg, &end_time))
						err("invalid date: %s", optarg);
				} else
					err("too many -d options");
				break;
			case 'f':
				if (logfile != NULL)
					err("Only use -f once");
				logfile = xstrdup(optarg);
				break;
			case 'w':
				if (atomfile != NULL)
					err("Only use -w once");
				if (!eat_file(optarg, &atomfile, &i))
					err("failed to open file %s", optarg);
				break;
		}
	}

	if (logfile == NULL)
		xasprintf(&logfile, "%s/%s", portlogdir, QLOP_DEFAULT_LOGFILE);

	argc -= optind;
	argv += optind;
	for (i = 0; i < (size_t)argc; ++i) {
		atom = atom_explode(argv[i]);
		if (!atom)
			warn("invalid atom: %s", argv[i]);
		else
			xarraypush_ptr(atoms, atom);
	}
	for (p = atomfile; p != NULL && *p != '\0'; p = q) {
		while (isspace((int)(*p)))
			p++;
		q = strchr(p, '\n');
		if (q != NULL) {
			*q = '\0';
			q++;
		}
		atom = atom_explode(p);
		if (!atom)
			warn("invalid atom: %s", p);
		else
			xarraypush_ptr(atoms, atom);
	}
	if (atomfile)
		free(atomfile);

	/* default operation: -slumt */
	if (
			m.do_time == 0 &&
			m.do_merge == 0 &&
			m.do_unmerge == 0 &&
			m.do_autoclean == 0 &&
			m.do_sync == 0 &&
			m.do_running == 0 &&
			m.do_average == 0 &&
			m.do_summary == 0
		)
	{
		m.do_merge = 1;
		m.do_unmerge = 1;
		if (array_cnt(atoms) == 0) {
			m.do_sync = 1;
			if (start_time == 0)
				m.show_lastmerge = 1;
		}
		m.do_time = 1;
	}

	/* handle deps */
	if (m.do_summary)
		m.do_average = 1;

	/* handle -a / -t conflict */
	if (m.do_average && m.do_time) {
		warn("-a (or -c) and -t cannot be used together, dropping -t");
		m.do_time = 0;
	}

	/* handle -a / -r conflict */
	if (m.do_average && m.do_running) {
		warn("-a (or -c) and -r cannot be used together, dropping -a");
		m.do_average = 0;
	}

	/* handle -H / -M conflict */
	if (m.do_human && m.do_machine) {
		warn("-H and -M cannot be used together, dropping -M: "
				"only humans make mistakes");
		m.do_machine = 0;
	}

	/* handle -s + atoms */
	if (m.do_sync && array_cnt(atoms) > 0) {
		warn("-s cannot be used when specifying atoms, dropping -s");
		m.do_sync = 0;
	}

	/* handle -l / -d conflict */
	if (start_time != 0 && m.show_lastmerge) {
		warn("-l and -d cannot be used together, dropping -l");
		m.show_lastmerge = 0;
	}

	/* set default for -t, -a or -r */
	if ((m.do_average || m.do_time || m.do_running) &&
			!(m.do_merge || m.do_unmerge || m.do_autoclean || m.do_sync))
	{
		m.do_merge = 1;
		m.do_unmerge = 1;
		if (array_cnt(atoms) == 0)
			m.do_sync = 1;
	}

	/* set format if none given */
	if (m.fmt == NULL) {
		if (verbose)
			m.fmt = "%[CATEGORY]%[PF]";
		else
			m.fmt = "%[CATEGORY]%[PN]";
	}

	if (m.do_running) {
		array_t *new_atoms = NULL;

		if (runningmode > 1) {
			warn("running without /proc scanning, heuristics only");
		} else {
			new_atoms = probe_proc(atoms);
		}

		if (new_atoms != NULL && array_cnt(new_atoms) == 0) {
			/* proc supported, found nothing running */
			start_time = LONG_MAX;
		}
		/* NOTE: new_atoms == atoms when new_atoms != NULL */
	}

	if (start_time < LONG_MAX)
		do_emerge_log(logfile, &m, atoms, start_time, end_time);

	array_for_each(atoms, i, atom)
		atom_implode(atom);
	xarrayfree_int(atoms);
	free(logfile);

	return EXIT_SUCCESS;
}
