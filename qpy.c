/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/Attic/qpy.c,v 1.2 2006/11/09 00:18:05 vapier Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#if defined(APPLET_qpy) && defined(WANT_PYTHON)

#include <python2.4/Python.h>

#define QPY_FLAGS "e" COMMON_FLAGS
static struct option const qpy_long_opts[] = {
	{"envvar",     no_argument, NULL, 'e'},
	COMMON_LONG_OPTS
};
static const char *qpy_opts_help[] = {
	"environment variables",
	COMMON_OPTS_HELP
};

static const char qpy_rcsid[] = "$Id: qpy.c,v 1.2 2006/11/09 00:18:05 vapier Exp $";
#define qpy_usage(ret) usage(ret, QPY_FLAGS, qpy_long_opts, qpy_opts_help, lookup_applet_idx("qpy"))

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

#if 0
void portage_reload(void);
void portage_reload(void)
{
	Py_Finalize();
	Py_Initialize();
	import_portage_settings();
}
#endif

int qpy_main(int argc, char **argv);
int qpy_main(int argc, char **argv)
{
	char do_env = 0;
	int i, result;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QPY, qpy, "")) != -1) {
		switch (i) {
		case 'e':
			do_env = 1;
			break;
		COMMON_GETOPTS_CASES(qpy)
		}
	}

	if (argc == optind)
		qpy_usage(EXIT_FAILURE);

	Py_Initialize();
	result = import_portage_settings();

	while (optind < argc) {
		if (do_env) qpy_envvar(argv[optind]);
		optind++;
	}
	Py_Finalize();
	return result;
}

#else
DEFINE_APPLET_STUB(qpy)
#endif
