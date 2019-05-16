/*
 * Copyright 2005-2019 Gentoo Foundation
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

#include "atom.h"
#include "eat_file.h"
#include "xarray.h"
#include "xasprintf.h"

#define QLOP_DEFAULT_LOGFILE "emerge.log"

#define QLOP_FLAGS "ctaHMmuUslerd:f:w:" COMMON_FLAGS
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
	{"endtime",   no_argument, NULL, 'e'},
	{"running",   no_argument, NULL, 'r'},
	{"date",       a_argument, NULL, 'd'},
	{"lastmerge", no_argument, NULL, 'l'},
	{"logfile",    a_argument, NULL, 'f'},
	{"atoms",      a_argument, NULL, 'w'},
	COMMON_LONG_OPTS
};
static const char * const qlop_opts_help[] = {
	"Print summary of average merges (implies -a)",
	"Print time taken to complete action",
	"Print average time taken to complete action",
	"Print elapsed time in human readable format (use with -t or -a)",
	"Print elapsed time as seconds with no formatting",
	"Show merge history",
	"Show unmerge history",
	"Show autoclean unmerge history",
	"Show sync history",
	"Report time at which the operation finished (iso started)",
	"Show current emerging packages",
	"Limit selection to this time (1st -d is start, 2nd -d is end)",
	"Limit selection to last Portage emerge action",
	"Read emerge logfile instead of $EMERGE_LOG_DIR/" QLOP_DEFAULT_LOGFILE,
	"Read package atoms to report from file",
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
	time_t t;

	t = flags->do_endtime ? te : ts;
	strftime(_date_buf, sizeof(_date_buf), "%Y-%m-%dT%H:%M:%S", localtime(&t));
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

	if (flags->do_machine) {
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
	time_t last_merge = 0;
	time_t sync_start = 0;
	time_t sync_time = 0;
	size_t sync_cnt = 0;
	time_t elapsed;
	depend_atom *atom;
	depend_atom *atomw;
	DECLARE_ARRAY(merge_matches);
	DECLARE_ARRAY(merge_averages);
	DECLARE_ARRAY(unmerge_matches);
	DECLARE_ARRAY(unmerge_averages);
	size_t i;
	size_t parallel_emerge = 0;
	bool all_atoms = false;

	struct pkg_match {
		char id[BUFSIZ];
		depend_atom *atom;
		time_t tbegin;
		time_t time;
		size_t cnt;
	};
	struct pkg_match *pkg;
	struct pkg_match *pkgw;
	const char *afmt = "%[CATEGORY]%[PN]";

	if (verbose)
		afmt = "%[CATEGORY]%[PF]";

	if ((fp = fopen(log, "r")) == NULL) {
		warnp("Could not open logfile '%s'", log);
		return 1;
	}

	all_atoms = array_cnt(atoms) == 0;
	if (all_atoms || flags->show_lastmerge) {
		/* assemble list of atoms */
		while (fgets(buf, sizeof(buf), fp) != NULL) {
			if ((p = strchr(buf, ':')) == NULL)
				continue;
			*p++ = '\0';

			tstart = atol(buf);
			if (tstart < tbegin || tstart > tend)
				continue;

			if (flags->show_lastmerge) {
				if (strncmp(p, "  *** emerge ", 13) == 0) {
					last_merge = tstart;
					array_for_each(atoms, i, atomw)
						atom_implode(atomw);
					xarrayfree_int(atoms);
				}
				if (!all_atoms)
					continue;
			}

			atom = NULL;
			if (strncmp(p, "  >>> emerge ", 13) == 0 &&
					(p = strchr(p + 13, ')')) != NULL)
			{
				p += 2;
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

				atomw = NULL;
				array_for_each(atoms, i, atomw) {
					if (atom_compare(atom, atomw) == EQUAL)
						break;
					atomw = NULL;
				}
				if (atomw == NULL) {
					xarraypush_ptr(atoms, atom);
				} else {
					atom_implode(atom);
				}
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
				 strncmp(p, "  *** terminating.", 18) == 0 ||
				 strncmp(p, "  *** exiting ", 14) == 0))
		{
			if (p[7] == 'm') {
				parallel_emerge++;
			} else if (parallel_emerge > 0) {
				parallel_emerge--;
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
		}

		tstart = atol(buf);
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
				array_for_each(atoms, i, atomw) {
					if (atom_compare(atom, atomw) == EQUAL)
						break;
					atomw = NULL;
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
						size_t n;

						pkg = NULL;
						array_for_each(merge_averages, n, pkg) {
							if (atom_compare(pkg->atom, pkgw->atom) == EQUAL) {
								pkg->cnt++;
								pkg->time += elapsed;
								/* store max time for do_running */
								if (elapsed > pkg->tbegin)
									pkg->tbegin = elapsed;
								atom_implode(pkgw->atom);
								xarraydelete(merge_matches, i);
								break;
							}
							pkg = NULL;
						}
						if (pkg == NULL) {  /* push new entry */
							if (!verbose || flags->do_running) {
								/* strip off version info */
								pkgw->atom->PV = NULL;
								pkgw->atom->PVR = NULL;
								pkgw->atom->PR_int = 0;
							}
							pkgw->id[0] = '\0';
							pkgw->cnt = 1;
							pkgw->time = elapsed;
							pkgw->tbegin = elapsed;
							xarraypush_ptr(merge_averages, pkgw);
							xarraydelete_ptr(merge_matches, i);
						}
						break;
					}
					if (quiet && !flags->do_average) {
						printf("%s%s%s\n",
								atom_format(afmt, pkgw->atom, 0),
								flags->do_time ? ": " : "",
								flags->do_time ?
									fmt_elapsedtime(flags, elapsed) : "");
					} else if (flags->do_time) {
						printf("%s >>> %s: %s\n",
								fmt_date(flags, pkgw->tbegin, tstart),
								atom_format(afmt, pkgw->atom, 0),
								fmt_elapsedtime(flags, elapsed));
					} else if (!flags->do_average) {
						printf("%s >>> %s\n",
								fmt_date(flags, pkgw->tbegin, tstart),
								atom_format(afmt, pkgw->atom, 0));
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

				/* see if we need this atom */
				atomw = NULL;
				array_for_each(atoms, i, atomw) {
					if (atom_compare(atom, atomw) == EQUAL)
						break;
					atomw = NULL;
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
						size_t n;

						pkg = NULL;
						array_for_each(unmerge_averages, n, pkg) {
							if (atom_compare(pkg->atom, pkgw->atom) == EQUAL) {
								pkg->cnt++;
								pkg->time += elapsed;
								/* store max time for do_running */
								if (elapsed > pkg->tbegin)
									pkg->tbegin = elapsed;
								atom_implode(pkgw->atom);
								xarraydelete(unmerge_matches, i);
								break;
							}
							pkg = NULL;
						}
						if (pkg == NULL) {  /* push new entry */
							if (!verbose || flags->do_running) {
								/* strip off version info */
								pkgw->atom->PV = NULL;
								pkgw->atom->PVR = NULL;
								pkgw->atom->PR_int = 0;
							}
							pkgw->id[0] = '\0';
							pkgw->cnt = 1;
							pkgw->time = elapsed;
							pkgw->tbegin = elapsed;
							xarraypush_ptr(unmerge_averages, pkgw);
							xarraydelete_ptr(unmerge_matches, i);
						}
						break;
					}
					if (quiet && !flags->do_average) {
						printf("%s%s%s\n",
								atom_format(afmt, pkgw->atom, 0),
								flags->do_time ? ": " : "",
								flags->do_time ?
									fmt_elapsedtime(flags, elapsed) : "");
					} else if (flags->do_time) {
						printf("%s <<< %s: %s\n",
								fmt_date(flags, pkgw->tbegin, tstart),
								atom_format(afmt, pkgw->atom, 0),
								fmt_elapsedtime(flags, elapsed));
					} else if (!flags->do_average) {
						printf("%s <<< %s\n",
								fmt_date(flags, pkgw->tbegin, tstart),
								atom_format(afmt, pkgw->atom, 0));
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
		/* can't report endtime for non-finished operations */
		flags->do_endtime = 0;
		tstart = time(NULL);
		sync_time /= sync_cnt;
		if (sync_start > 0) {
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
		array_for_each(merge_matches, i, pkgw) {
			size_t j;
			time_t maxtime = 0;

			elapsed = tstart - pkgw->tbegin;
			pkg = NULL;
			array_for_each(merge_averages, j, pkg) {
				if (atom_compare(pkg->atom, pkgw->atom) == EQUAL) {
					maxtime = pkg->time / pkg->cnt;
					if (elapsed >= maxtime)
						maxtime = elapsed >= pkg->tbegin ? 0 : pkg->tbegin;
					break;
				}
				pkg = NULL;
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
				printf("%s >>> %s: %s...%s ETA: %s\n",
						fmt_date(flags, pkgw->tbegin, 0),
						atom_format(afmt, pkgw->atom, 0),
						fmt_elapsedtime(flags, elapsed),
						p == NULL ? "" : p,
						maxtime == 0 ? "unknown" :
							fmt_elapsedtime(flags, maxtime - elapsed));
			} else {
				printf("%s >>> %s...%s ETA: %s\n",
						fmt_date(flags, pkgw->tbegin, 0),
						atom_format(afmt, pkgw->atom, 0),
						p == NULL ? "" : p,
						maxtime == 0 ? "unknown" :
							fmt_elapsedtime(flags, maxtime - elapsed));
			}
		}
		array_for_each(unmerge_matches, i, pkgw) {
			size_t j;
			time_t maxtime = 0;

			elapsed = tstart - pkgw->tbegin;
			pkg = NULL;
			array_for_each(unmerge_averages, j, pkg) {
				if (atom_compare(pkg->atom, pkgw->atom) == EQUAL) {
					maxtime = pkg->time / pkg->cnt;
					if (elapsed >= maxtime)
						maxtime = elapsed >= pkg->tbegin ? 0 : pkg->tbegin;
					break;
				}
				pkg = NULL;
			}

			if (flags->do_time) {
				printf("%s <<< %s: %s... ETA: %s\n",
						fmt_date(flags, pkgw->tbegin, 0),
						atom_format(afmt, pkgw->atom, 0),
						fmt_elapsedtime(flags, elapsed),
						maxtime == 0 ? "unknown" :
							fmt_elapsedtime(flags, maxtime - elapsed));
			} else {
				printf("%s <<< %s... ETA: %s\n",
						fmt_date(flags, pkgw->tbegin, 0),
						atom_format(afmt, pkgw->atom, 0),
						maxtime == 0 ? "unknown" :
							fmt_elapsedtime(flags, maxtime - elapsed));
			}
		}
	} else if (flags->do_average) {
		size_t total_merges = 0;
		size_t total_unmerges = 0;
		time_t total_time = (time_t)0;

		array_for_each(merge_averages, i, pkg) {
			printf("%s: %s average for %s%zd%s merge%s\n",
					atom_format(afmt, pkg->atom, 0),
					fmt_elapsedtime(flags, pkg->time / pkg->cnt),
					GREEN, pkg->cnt, NORM, pkg->cnt == 1 ? "" : "s");
			total_merges += pkg->cnt;
			total_time += pkg->time;
		}
		array_for_each(unmerge_averages, i, pkg) {
			printf("%s: %s average for %s%zd%s unmerge%s\n",
					atom_format(afmt, pkg->atom, 0),
					fmt_elapsedtime(flags, pkg->time / pkg->cnt),
					GREEN, pkg->cnt, NORM, pkg->cnt == 1 ? "" : "s");
			total_unmerges += pkg->cnt;
			total_time += pkg->time;
		}
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
	return 0;
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

	while ((ret = GETOPT_LONG(QLOP, qlop, "")) != -1) {
		switch (ret) {
			COMMON_GETOPTS_CASES(qlop)

			case 't': m.do_time = 1;        break;
			case 'm': m.do_merge = 1;       break;
			case 'u': m.do_unmerge = 1;     break;
			case 'U': m.do_autoclean = 1;   break;
			case 's': m.do_sync = 1;        break;
			case 'r': m.do_running = 1;     break;
			case 'a': m.do_average = 1;     break;
			case 'c': m.do_summary = 1;     break;
			case 'H': m.do_human = 1;       break;
			case 'M': m.do_machine = 1;     break;
			case 'e': m.do_endtime = 1;     break;
			case 'l': m.show_lastmerge = 1; break;
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
			!(m.do_merge || m.do_unmerge || m.do_sync))
	{
		m.do_merge = 1;
		m.do_unmerge = 1;
		if (array_cnt(atoms) == 0)
			m.do_sync = 1;
	}

	do_emerge_log(logfile, &m, atoms, start_time, end_time);

	array_for_each(atoms, i, atom)
		atom_implode(atom);
	xarrayfree_int(atoms);
	free(logfile);

	return EXIT_SUCCESS;
}
