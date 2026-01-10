/*
 * Copyright 2005-2025 Gentoo Foundation
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

#define QLOP_FLAGS "ctapHMmuUsElerd:f:w:F:" COMMON_FLAGS
static struct option const qlop_long_opts[] = {
	{"summary",   no_argument, NULL, 'c'},
	{"time",      no_argument, NULL, 't'},
	{"average",   no_argument, NULL, 'a'},
	{"predict",   no_argument, NULL, 'p'},
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
	"Print prediction of time it takes to complete action",
	"Print elapsed time in human readable format (use with -t or -a)",
	"Print start/elapsed time as seconds with no formatting",
	"Show merge history",
	"Show unmerge history",
	"Show autoclean unmerge history",
	"Show sync history",
	"Show last merge similar to how emerge(1) -v would show it",
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
	char do_predict:1;
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
		 * - "@12315128"            -> %s
		 * - "2015-12-24"          -> %Y-%m-%d
		 * - "2019-03-28T13:52:31" -> %Y-%m-%dT%H:%M:%s"
		 * - human readable format (see below)
		 */
		size_t len = strspn(sdate, "0123456789-:T@");
		if (sdate[len] == '\0') {
			if (sdate[0] == '@') {
				time_t d = (time_t)strtoll(&sdate[1], (char **)&s, 10);
				localtime_r(&d, &tm);
			} else if (strchr(sdate, '-') == NULL) {
				time_t d = (time_t)strtoll(sdate, (char **)&s, 10);
				localtime_r(&d, &tm);
			} else if ((s = strchr(sdate, 'T')) == NULL) {
				s = strptime(sdate, "%Y-%m-%d", &tm);
			} else {
				s = strptime(sdate, "%Y-%m-%dT%H:%M:%S", &tm);
			}

			if (s == NULL || s[0] != '\0')
				return false;
		} else {
			/* Handle the formats:
			 * <#> <day|week|month|year>[s] [ago]
			 */
			len = strlen(sdate) + 1;

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
					"%s%zd%sm%s%02zd%ss",
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

	return atom_compar_cb(&al, &ar);
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

Latest format:
1764512151: Started emerge on: Nov 30, 2025 15:15:51
1764512151:  *** emerge --regex-search-auto=y --sync                            1764512151:  === sync
1764512233:  *** terminating.

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

Averages
Compile time of packages typically changes over time (assuming the
machine stays the same).  New major releases introduce extra code, or
improve stuff considerably.  New compilers might take more or less time
compiling the code.  The environment (like the toolchain) we cannot take
into account, packages themselves, we could do something with.
Portage unfortunately does not record the SLOT of the package it
emerges, so we cannot use SLOT.
- Gentoo revisions (-rX) typically do not change the code a lot
- minor and below revisions usually are part of the same code branch
- major revisions typically indicate a next generation of the code
So when there is a running emerge:
1. we value most an identical version, minus Gentoo revisions
2. we look for merges with the same major version
3. when there's still not enough (or anything) to balance an average
   off, we look at merges for the same package (older major versions)
Think of this value system as something like the weight of 1 is 5x, 2 is
3x and 3 is just 1x.
While this might sound really cool, it often results in the same average
as a normal average, and so it is useless.  In particular: the best
prediction is the most recent matching version.  If there is no exact
match (ignoring Gentoo revisions), a projection of runtimes for previous
merges is the next best thing, ignoring major or minor revisions, e.g.
for GCC major versions would hold very well, but for binutils, the
minors differ a lot (for it's on major version 2 for ages).

So it seems what it boils down to, is that prediction of the simplest
kind is probably the best here.  What we do, is we order the merge
history of the package, and try to extrapolate the growth between the
two last merges, e.g. 10 and 12 becomes 12 + (12 - 10) = 14.
Now if there is only one point of history here, there's nothing to
predict, and we best just take the value and add a 10% fuzz.
There's two cases in this setup:
1. predict for a version that we already have merge history for,
   ignoring revisions, or
2. predict for a version that's different from what we've seen before
In case of 1. we only look at the same versions, and apply extrapolation
based on the revisions, e.g. -r2 -> -r4.  In case of 2. we only look at
versions that are older than the version we're predicting for.  This to
deal with e.g. merging python-3.6 with 3.9 installed as well.
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
	time_t last_exit = 0;
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
	bool emerge_line;
	char afmt[BUFSIZ];
	struct pkg_match *pkg;
	struct pkg_match *pkgw;
#define strpfx(X, Y)  strncmp(X, Y, sizeof(Y) - 1)

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
				if (strpfx(p, "  *** emerge ") == 0)
					tstart_emerge = tstart;
				if (!all_atoms)
					continue;
			}

			atom = NULL;
			if (strpfx(p, "  >>> emerge ") == 0 &&
					(q = strchr(p + 13, ')')) != NULL)
			{
				p = q + 2;
				q = strchr(p, ' ');
				if (q != NULL) {
					*q = '\0';
					atom = atom_explode(p);
				}
			} else if (strpfx(p, "  === Unmerging... (") == 0 ||
					strpfx(p, " === Unmerging... (") == 0)
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
					DECLARE_ARRAY(vals);

					values_set(atomset, vals);
					array_for_each(vals, i, atomw)
						atom_implode(atomw);
					xarrayfree_int(vals);

					clear_set(atomset);
					last_merge = tstart_emerge;
				}

				atomset = add_set_value(afmt, atom, (void **)&atomw, atomset);
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

		tstart = atol(buf);

		emerge_line = false;
		if (((flags->do_running ||
			  (flags->show_emerge && verbose)) &&
			 strpfx(p, "  *** emerge ") == 0) ||
			(flags->do_running &&
			 (strpfx(p, "  *** exiting ") == 0 ||
			  strpfx(p, "  *** terminating.") == 0)))
		{
			emerge_line = true;
		}

		/* keeping track of parallel merges needs to be done before
		 * applying dates, for a subset of the log might show emerge
		 * finished without knowledge of another instance */
		if (emerge_line && flags->do_running) {
			if (p[7] == 'm') {
				parallel_emerge++;
			} else if (parallel_emerge > 0) {
				if (p[7] != 'e' || (tstart - 4) <= last_exit)
					parallel_emerge--;
				if (p[7] == 'x')
					last_exit = tstart;
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

		if (tstart < tlast)
			continue;
		tlast = tstart;
		if (tstart < tbegin || tstart > tend)
			continue;

		/* are we interested in this line? */
		if (emerge_line && flags->show_emerge && verbose) {
			char shortopts[8];  /* must hold as many opts converted below */
			int numopts = 0;

			printf("%semerge%s", DKBLUE, NORM);
			for (p += 13; (q = strtok(p, " \n")) != NULL; p = NULL) {
				if (strpfx(q, "--") == 0) {
					/* portage seems to normalise options given into
					 * their long forms always; I don't want to keep a
					 * mapping table to short forms here, but it's
					 * tempting, so I just do a few of the often used
					 * ones */
					q += 2;
					if (strcmp(q, "ask") == 0) {
						shortopts[numopts++] = 'a';
						q = NULL;
					} else if (strcmp(q, "verbose") == 0) {
						shortopts[numopts++] = 'v';
						q = NULL;
					} else if (strcmp(q, "oneshot") == 0) {
						shortopts[numopts++] = '1';
						q = NULL;
					} else if (strcmp(q, "deep") == 0) {
						shortopts[numopts++] = 'D';
						q = NULL;
					} else if (strcmp(q, "update") == 0) {
						shortopts[numopts++] = 'u';
						q = NULL;
					} else if (strcmp(q, "depclean") == 0) {
						shortopts[numopts++] = 'c';
						q = NULL;
					} else if (strcmp(q, "unmerge") == 0) {
						shortopts[numopts++] = 'C';
						q = NULL;
					} else {
						q -= 2;
					}

					/* process next token */
					if (q == NULL)
						continue;
				}

				/* if we're here, we've assembled opts whatever we could */
				if (numopts > 0) {
					printf(" %s-%.*s%s",
							GREEN, numopts, shortopts, NORM);
					numopts = 0;
				}

				if (*q == '\0') {
					/* skip empty token, likely the trailing \n */
					continue;
				}

				if (strpfx(q, "--") == 0) {
					printf(" %s%s%s", GREEN, q, NORM);
				} else if (q[0] == '@' ||
						   strcmp(q, "world") == 0 ||
						   strcmp(q, "system") == 0)
				{
					printf(" %s%s%s", YELLOW, q, NORM);
				} else {
					/* this should be an atom */
					atom = atom_explode(q);
					if (atom == NULL) {
						/* or not ... just print it */
						printf(" %s", q);
					} else {
						printf(" %s", atom_format(
									"%[pfx]%[CAT]%[PF]%[sfx]"
									"%[SLOT]%[SUBSLOT]%[REPO]", atom));
						atom_implode(atom);
					}
				}
			}
			printf("\n");
		} else if (flags->do_sync && (
					strpfx(p, "  *** terminating.") == 0 ||
					strpfx(p, " === Sync completed ") == 0 ||
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

				if (p[2] == '*' &&
					(q = strchr(p, '\n')) != NULL)
				{
					p = (char *)"sync";
				} else {
					p += 20;
					if (strpfx(p, "for ") == 0) {
						p += 4;
					} else {  /* "with " */
						p += 5;
					}
					if ((q = strchr(p, '\n')) != NULL)
						*q = '\0';
				}

				if (flags->do_predict || flags->do_average ||
						flags->do_running)
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
					strpfx(p, "  >>> emerge (") == 0 ||
					strpfx(p, "  ::: completed emerge (") == 0))
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
					/* match without revisions when we try to predict,
					 * such that our set remains rich enough to cover
					 * various predictions */
					array_for_each(atoms, i, atomw) {
						if (atom_compare_flg(atom, atomw,
									flags->do_predict
									? ATOM_COMP_NOREV
									: ATOM_COMP_DEFAULT) == EQUAL)
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

					if (flags->do_average || flags->do_predict
							|| flags->do_running)
					{
						/* find in list of averages */
						if (flags->do_predict ||
							(verbose && !flags->do_running))
						{
							snprintf(afmt, sizeof(afmt), "%s/%s",
									pkgw->atom->CATEGORY, pkgw->atom->PF);
						} else {
							snprintf(afmt, sizeof(afmt), "%s/%s",
									pkgw->atom->CATEGORY, pkgw->atom->PN);
						}

						merge_averages =
							add_set_value(afmt, pkgw,
										  (void **)&pkg, merge_averages);
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
						printf("%s", atom_format("%[CAT]%[PF]", pkgw->atom));
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
				 strpfx(p, " === Unmerging... (") == 0) ||
				(flags->do_autoclean &&
				 strpfx(p, "  === Unmerging... (") == 0) ||
				((flags->do_unmerge || flags->do_autoclean) &&
				 strpfx(p, "  >>> unmerge success: ") == 0))
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

					if (flags->do_average || flags->do_predict
							|| flags->do_running)
					{
						/* find in list of averages */
						if (flags->do_predict ||
							(verbose && !flags->do_running))
						{
							snprintf(afmt, sizeof(afmt), "%s/%s",
									pkgw->atom->CATEGORY, pkgw->atom->PF);
						} else {
							snprintf(afmt, sizeof(afmt), "%s/%s",
									pkgw->atom->CATEGORY, pkgw->atom->PN);
						}

						unmerge_averages =
							add_set_value(afmt, pkgw,
										  (void **)&pkg, unmerge_averages);
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
					} else if (flags->show_emerge) {
						/* emerge never lists packages it unmerges, only
						 * when part of installation of another package,
						 * so there is nothing to follow here.  We could
						 * use D(elete), R(emove) or (U)nmerge here,
						 * which all three are already in use.  I've
						 * chosen to use D in the first column here,
						 * because D is only used together with U, so it
						 * is the only distinquishable choice, appearing
						 * in the place of N(ew). */
						printf("%sD%s    %s", RED, NORM,
								atom_format("%[CAT]%[PF]", pkgw->atom));
						if (flags->do_time)
							printf(": %s\n", fmt_elapsedtime(flags, elapsed));
						else
							printf("\n");
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
				/* add 14% of the diff between avg and max, to avoid
				 * frequently swapping to maxtime */
				maxtime += (pkg->tbegin - maxtime) / 7;
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
	} else if (flags->do_predict) {
		DECLARE_ARRAY(avgs);
		enum { P_INIT = 0, P_SREV, P_SRCH } pkgstate;
		size_t j;
		time_t ptime;
		char found;

		values_set(merge_averages, avgs);
		xarraysort(avgs, pkg_sort_cb);
		xarraysort(atoms, atom_compar_cb);

		/* each atom has its own matches in here, but since it's all
		 * sorted, we basically can go around here jumping from CAT-PN
		 * to CAT-PN (yes this means python-3.6 python-3.9 will result
		 * in a single record) */
		array_for_each(atoms, i, atom) {
			pkgstate = P_INIT;
			pkgw = NULL;
			found = 0;

			/* we added "<=" to the atoms in main() to make sure we
			 * would collect version ranges, but here we want to know if
			 * the atom is equal or newer, older, etc. so strip it off
			 * again */
			if (atom->pfx_op == ATOM_OP_OLDER_EQUAL)
				atom->pfx_op = ATOM_OP_NONE;

			array_for_each(avgs, j, pkg) {
				if (pkgstate == P_INIT) {
					atom_equality eq;
					eq = atom_compare_flg(pkg->atom, atom, ATOM_COMP_NOREV);
					switch (eq) {
						case EQUAL:
							/* version-less atoms equal any versioned
							 * atom, so check if atom (user input) was
							 * versioned */
							if (atom->PV != NULL)
							{
								/* 100% match, there's no better
								 * prediction */
								found = 1;
								ptime = pkg->time / pkg->cnt;
								break;
							} else {
								pkgstate = P_SRCH;
								pkgw = pkg;
								continue;
							}
						case NEWER:
							if (atom->pfx_op != ATOM_OP_NEWER &&
									atom->pfx_op != ATOM_OP_NEWER_EQUAL)
								continue;
							/* fall through */
						case OLDER:
							if (atom->PV != NULL && pkg->atom->PV != NULL &&
									strcmp(atom->PV, pkg->atom->PV) == 0)
								pkgstate = P_SREV;
							else
								pkgstate = P_SRCH;
							pkgw = pkg;
							continue;
						default:
							continue;
					}
				} else { /* P_SREV, P_SRCH */
					if (atom_compare(pkg->atom, pkgw->atom) == OLDER) {
						if (pkgstate == P_SREV) {
							/* in here, we only compute a diff if we
							 * have another (previous) revision,
							 * else we stick to the version we have,
							 * because we're compiling a new
							 * revision, so likely to be the same */
							if (pkg->atom->PV != NULL &&
									pkgw->atom->PV != NULL &&
									strcmp(pkg->atom->PV,
										pkgw->atom->PV) == 0)
							{
								time_t wtime;
								/* take diff with previous revision */
								wtime = pkgw->time / pkgw->cnt;
								ptime = pkg->time / pkg->cnt;
								ptime = wtime + (wtime - ptime);
							} else {
								ptime = pkgw->time / pkgw->cnt;
							}
						} else {
							time_t wtime;
							/* we got another version, just compute
							 * the diff, and return it */
							wtime = pkgw->time / pkgw->cnt;
							ptime = pkg->time / pkg->cnt;
							ptime = wtime + (wtime - ptime);
						}
						found = 1;
					} else {
						/* it cannot be newer (because we
						 * applied sort) and it cannot be equal
						 * (because it comes from a set) */
						break;
					}
				}

				if (found) {
					printf("%s: prediction %s\n",
							atom_format(flags->fmt, atom),
							fmt_elapsedtime(flags, ptime));
					break;
				}
			}
		}
		xarrayfree_int(avgs);
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
	struct dirent **procs = NULL;
	int procslen;
	int pi;
	struct dirent **links = NULL;
	int linkslen;
	int li;
	struct dirent *d;
	char npath[(_Q_PATH_MAX * 2) + 16];
	char rpath[_Q_PATH_MAX];
	const char *subdir = NULL;
	const char *pid;
	ssize_t rpathlen;
	char *p;
	depend_atom *atom;
	DECLARE_ARRAY(ret_atoms);
	size_t i;
	char *cmdline = NULL;
	size_t cmdlinesize = 0;

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

			/* first try old-fashioned (good old) sandbox approach; this
			 * one may not have a long life in Gentoo any more, but for
			 * now it's still being used quite a lot, and the advantage
			 * is that it doesn't require root access, for cmdline can
			 * be read by anyone */
			snprintf(npath, sizeof(npath), "/proc/%s/cmdline", pid);
			if (eat_file(npath, &cmdline, &cmdlinesize)) {
				if (cmdlinesize > 0 && cmdline[0] == '[' &&
						(p = strchr(cmdline, ']')) != NULL &&
						strpfx(p, "] sandbox") == 0)
				{
					*p = '\0';
					atom = atom_explode(cmdline + 1);

					if (atom != NULL) {
						if (atom->CATEGORY == NULL || atom->P == NULL) {
							atom_implode(atom);
						} else {
							xarraypush_ptr(ret_atoms, atom);
							continue;
						}
					}
				}
			}

			/* now try and see if Portage opened a build log somewhere */
			snprintf(npath, sizeof(npath), "/proc/%s/%s", pid, subdir);
			if ((linkslen = scandir(npath, &links, NULL, NULL)) > 0) {
				for (li = 0; li < linkslen; li++) {
					d = links[li];
					/* must be [0-9]+ */
					if (d->d_name[0] < '0' || d->d_name[0] > '9')
						continue;
					snprintf(npath, sizeof(npath), "/proc/%s/%s/%s",
							pid, subdir, d->d_name);
					rpathlen = readlink(npath, rpath, sizeof(rpath) - 1);
					if (rpathlen <= 0)
						continue;
					rpath[rpathlen] = '\0';

					/* in bug #745798, it seems Portage optionally
					 * compresses the buildlog -- to make matching below
					 * here easier, strip such compression extension off
					 * first here, leaving .log */
					if ((size_t)rpathlen > sizeof(".log.gz") &&
							(p = strrchr(rpath, '.')) != NULL &&
							p - (sizeof(".log") - 1) > rpath &&
							strpfx(p - (sizeof(".log") - 1), ".log") == 0)
					{
						*p = '\0';
						rpathlen -= rpath - p;
					}

					/* check if this points to a portage build:
					 * <somepath>/portage/<cat>/<pf>/temp/build.log
					 * <somepath>/<cat>:<pf>:YYYYMMDD-HHMMSS.log */
					atom = NULL;
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
					} else if ((size_t)rpathlen > sizeof(".log") &&
							strcmp(rpath + rpathlen -
								(sizeof(".log") - 1), ".log") == 0 &&
							(p = strrchr(rpath, '/')) != NULL)
					{
						char *q;

						p++;  /* skip / */
						if ((q = strchr(p, ':')) != NULL) {
							*q++ = '/';
							if ((q = strchr(q, ':')) != NULL) {
								*q = '\0';
								atom = atom_explode(p);
							}
						}
					}

					if (atom == NULL)
						continue;
					if (atom->CATEGORY == NULL || atom->P == NULL) {
						atom_implode(atom);
						continue;
					}

					xarraypush_ptr(ret_atoms, atom);
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
	{
		/* bug #731122: match running packages without version */
		atom->PV = NULL;
		atom->PVR = NULL;
		atom->PR_int = 0;
		xarraypush_ptr(atoms, atom);
	}

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

	start_time = -1;
	end_time = -1;
	m.do_time = 0;
	m.do_merge = 0;
	m.do_unmerge = 0;
	m.do_autoclean = 0;
	m.do_sync = 0;
	m.do_running = 0;
	m.do_average = 0;
	m.do_predict = 0;
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
					  m.show_emerge = 1;    break;
			case 'r': m.do_running = 1;
					  runningmode++;        break;
			case 'a': m.do_average = 1;     break;
			case 'p': m.do_predict = 1;     break;
			case 'c': m.do_summary = 1;     break;
			case 'H': m.do_human = 1;       break;
			case 'M': m.do_machine = 1;     break;
			case 'e': m.do_endtime = 1;     break;
			case 'l': m.show_lastmerge = 1; break;
			case 'F': m.fmt = optarg;       break;
			case 'd':
				if (start_time == -1) {
					if (!parse_date(optarg, &start_time))
						err("invalid date: %s", optarg);
				} else if (end_time == -1) {
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
			m.do_predict == 0 &&
			m.do_summary == 0
		)
	{
		m.do_merge = 1;
		m.do_unmerge = 1;
		if (array_cnt(atoms) == 0) {
			m.do_sync = 1;
			if (start_time == -1)
				m.show_lastmerge = 1;
		}
		m.do_time = 1;
	}

	/* handle deps */
	if (m.do_summary)
		m.do_average = 1;

	/* handle -a / -p conflict */
	if (m.do_average && m.do_predict) {
		warn("-a and -p cannot be used together, dropping -a");
		m.do_average = 0;
	}

	/* handle -a / -p / -t conflict */
	if ((m.do_average || m.do_predict) && m.do_time) {
		warn("-a/-p (or -c) and -t cannot be used together, dropping -t");
		m.do_time = 0;
	}

	/* handle -a / -r conflict */
	if ((m.do_average || m.do_predict) && m.do_running) {
		warn("-a/-p (or -c) and -r cannot be used together, dropping -a/-p");
		m.do_average = 0;
		m.do_predict = 0;
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

	/* handle -p without atoms */
	if (m.do_predict && array_cnt(atoms) == 0) {
		err("-p requires at least one atom");
		return EXIT_FAILURE;
	}

	/* handle -l / -d conflict */
	if (start_time != -1 && m.show_lastmerge) {
		if (!m.show_emerge)
			warn("-l and -d cannot be used together, dropping -l");
		m.show_lastmerge = 0;
	}

	/* set default for -t, -a, -p or -r */
	if ((m.do_average || m.do_predict || m.do_time || m.do_running) &&
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

	/* adjust time ranges when unset */
	if (start_time == -1)
		start_time = 0;
	if (end_time == -1)
		end_time = LONG_MAX;

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

	if (m.do_predict) {
		/* turn non-ranged atoms into a <= match such that the
		 * prediction magic has something to compare against when a
		 * version is listed */
		array_for_each(atoms, i, atom) {
			if (atom->pfx_op == ATOM_OP_NONE ||
					atom->pfx_op == ATOM_OP_EQUAL ||
					atom->pfx_op == ATOM_OP_PV_EQUAL ||
					atom->pfx_op == ATOM_OP_STAR)
				atom->pfx_op = ATOM_OP_OLDER_EQUAL;
		}
	}

	if (start_time < LONG_MAX)
		do_emerge_log(logfile, &m, atoms, start_time, end_time);

	array_for_each(atoms, i, atom)
		atom_implode(atom);
	xarrayfree_int(atoms);
	free(logfile);

	return EXIT_SUCCESS;
}
