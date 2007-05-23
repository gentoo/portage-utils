/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/mod/Attic/py.c,v 1.2 2007/05/23 13:47:31 solar Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#define _GNU_SOURCE

#include <getopt.h>

extern int verbose;
extern void usage();

#include "../main.h"
#include "Python.h"

#define QPY_FLAGS "e"
static struct option const qpy_long_opts[] = {
	{"envvar",     no_argument, NULL, 'e'},
	{NULL, 0, NULL, 0}
};

static const char *qpy_opts_help[] = {
	"environment variables"
};

static const char qpy_rcsid[] = "$Id: py.c,v 1.2 2007/05/23 13:47:31 solar Exp $";

#define qpy_usage(ret) usage(ret, QPY_FLAGS, qpy_long_opts, qpy_opts_help, -1)

struct portage_t {
	PyObject *interp;
	PyObject *settings;
} portage;

/* the (hopefully) right way: create and access python/portage objects from c */
char *portage_setting(char *envvar);
char *portage_setting(char *envvar)
{
	char *value = NULL;
	PyObject *result;

	if (!(result = PyMapping_GetItemString(portage.settings, envvar))) {
		Py_DECREF(portage.interp);
		if (verbose)
			fprintf(stderr, "could not get %s, bailing\n", envvar);
		return NULL;
	}
	if (!PyString_Check(result)) {
		if (verbose)
			fprintf(stderr, "got a non-string, bailing\n");
		Py_DECREF(result);
		Py_DECREF(portage.interp);
		return NULL;
	}
	asprintf(&value, "%s", PyString_AsString(result));
	Py_DECREF(result);
	return value;
}

int qpy_envvar(char *envvar);
int qpy_envvar(char *envvar)
{
	char *value;
	if ((value = portage_setting(envvar)) != NULL) {
		printf("%s%s%s%s\n", verbose ? envvar : "", verbose ? "='" : "", value, verbose ? "'" : "");
		free(value);
		return 0;
	}
	return 1;
}

int import_portage_settings(void);
int import_portage_settings(void)
{
	if (!(portage.interp = PyImport_ImportModule((char *) "portage"))) {
		fprintf(stderr, "could not import portage, bailing\n");
		return 1;
	}
	if (!(portage.settings = PyObject_GetAttrString(portage.interp, (char *) "settings"))) {
		Py_DECREF(portage.interp);
		fprintf(stderr, "getting settings failed");
		return 1;
	}
	return 0;
}

int py_main(int argc, char **argv);
int py_main(int argc, char **argv)
{
	char do_env = 0;
	int i, result;

	i=0;
	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QPY, qpy, "")) != -1) {
		switch (i) {
		case 'e':
			do_env = 1;
			break;
		}
	}

	if (argc == optind)
		qpy_usage(EXIT_FAILURE);

	/* pass argv[0] to the python interpreter, initialize it and fill argc, argv */

	Py_SetProgramName(argv[0]);
	Py_Initialize();
	PySys_SetArgv(argc, argv);

	result = import_portage_settings();

	while (optind < argc) {
		if (do_env) qpy_envvar(argv[optind]);
		optind++;
	}
	Py_Finalize();
	return result;
}
