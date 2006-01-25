/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qglsa.c,v 1.3 2006/01/25 01:51:42 vapier Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_glsa

#define QGLSA_DB "/var/cache/edb/glsa"


#define QGLSA_FLAGS "ldtpfi" COMMON_FLAGS
static struct option const qglsa_long_opts[] = {
	{"list",      no_argument, NULL, 'l'},
	{"dump",      no_argument, NULL, 'd'},
	{"test",      no_argument, NULL, 't'},
	{"pretend",   no_argument, NULL, 'p'},
	{"fix",       no_argument, NULL, 'f'},
	{"inject",    no_argument, NULL, 'i'},
	COMMON_LONG_OPTS
};
static const char *qglsa_opts_help[] = {
	"List GLSAs",
	"Dump info about GLSAs",
	"Test if system is affected by GLSAs",
	"Show what would be done to fix the GLSA",
	"Auto-apply GLSAs to the system",
	"Mark specified GLSAs as fixed",
	COMMON_OPTS_HELP
};
static const char qglsa_rcsid[] = "$Id: qglsa.c,v 1.3 2006/01/25 01:51:42 vapier Exp $";
#define qglsa_usage(ret) usage(ret, QGLSA_FLAGS, qglsa_long_opts, qglsa_opts_help, lookup_applet_idx("qglsa"))


static char *qglsa_get_xml_tag(const char *xml_buf, const char *tag)
{
	static char tmp_buf[BUFSIZE];
	char *start, *end;

	sprintf(tmp_buf, "<%s>", tag);
	if ((start = strstr(xml_buf, tmp_buf)) == NULL) {
		sprintf(tmp_buf, "<%s ", tag);
		if ((start = strstr(xml_buf, tmp_buf)) == NULL)
			return NULL;
	}
	start += strlen(tmp_buf);
	sprintf(tmp_buf, "</%s>", tag);
	if ((end = strstr(xml_buf, tmp_buf)) == NULL)
		return NULL;
	assert(end - start < sizeof(tmp_buf));
	memcpy(tmp_buf, start, end - start);
	tmp_buf[end - start] = '\0';
	return tmp_buf;
}

/*
static const char *qglsa_opts_glsa[] = {
	"le", "lt", "eq", "gt", "ge", "rge", "rle", "rgt", "rlt", NULL
};
static const char *qglsa_opts_portage[] = {
	"<=",  "<",  "=",  ">", ">=", ">=~", "<=~", " >~", " <~", NULL
};

static void qglsa_act_list(char *glsa)
{
	
}
*/
int qglsa_main(int argc, char **argv)
{
	enum { GLSA_FUNKYTOWN, GLSA_LIST, GLSA_DUMP, GLSA_TEST, GLSA_PRETEND, GLSA_FIX, GLSA_INJECT };
	int i;
	DIR *dir;
	struct dirent *dentry;
	char buf[BUFSIZE*4];
	char *s, *p;
	int action = GLSA_FUNKYTOWN;
	int all_glsas = 0;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QGLSA, qglsa, "")) != -1) {
#define set_action(a) { if (action == 0) action = a; else err("cannot specify more than one action at a time"); }
		switch (i) {
		case 'l': set_action(GLSA_LIST); break;
		case 'd': set_action(GLSA_DUMP); break;
		case 't': set_action(GLSA_TEST); break;
		case 'p': set_action(GLSA_PRETEND); break;
		case 'f': set_action(GLSA_FIX); break;
		case 'i': set_action(GLSA_INJECT); break;
		COMMON_GETOPTS_CASES(qglsa)
		}
	}
	if (action == GLSA_FUNKYTOWN)
		qglsa_usage(EXIT_FAILURE);
	if (action != GLSA_LIST && optind == argc)
		err("specified action requires a list, either 'all', 'new', or GLSA numbers");

	if (optind+1 == argc) {
		if (!strcmp(argv[optind], "all"))
			all_glsas = 1;
		else if (!strcmp(argv[optind], "new"))
			all_glsas = 0;
	}

	if (chdir(portdir) != 0)
		errp("could not chdir to portdir %s", portdir);
	if (chdir("./metadata/glsa") != 0)
		errp("could not chdir to glsa dir");
	if ((dir = opendir(".")) == NULL)
		return EXIT_FAILURE;

	while ((dentry = readdir(dir)) != NULL) {
		if (strncmp(dentry->d_name, "glsa-", 5))
			continue;

		if (eat_file(dentry->d_name, buf, sizeof(buf)) == 0)
			errp("could not eat %s", dentry->d_name);

		if ((s = strchr(dentry->d_name, '.')) != NULL)
			*s = '\0';

		switch (action) {
		case GLSA_LIST:
			s = qglsa_get_xml_tag(buf, "title");
			p = dentry->d_name + 5;
			printf("%s%s%s: %s\n", GREEN, p, NORM, s);
			break;
		case GLSA_DUMP:
			s = qglsa_get_xml_tag(buf, "title");
			p = dentry->d_name + 5;
			printf("%s%s%s: %s\n", GREEN, p, NORM, s);
			s = qglsa_get_xml_tag(buf, "bug");
			printf("  %sRef%s: http://bugs.gentoo.org/%s\n", BLUE, NORM, s);
			s = qglsa_get_xml_tag(buf, "access");
			printf("  %saccess%s: %s\n", BLUE, NORM, s);
			s = qglsa_get_xml_tag(buf, "synopsis");
			printf("  %ssynopsis%s:\n%s\n", BLUE, NORM, s);
			s = qglsa_get_xml_tag(buf, "affected");
			printf("  %saffected%s:\n%s\n", BLUE, NORM, s);
			if (verbose) {
				s = qglsa_get_xml_tag(buf, "description");
				printf("  %sdescription%s:\n%s\n", BLUE, NORM, s);
				s = qglsa_get_xml_tag(buf, "workaround");
				printf("  %sworkaround%s:\n%s\n", BLUE, NORM, s);
			}
			break;
		}
	}
	closedir(dir);

	return EXIT_SUCCESS;
}

#endif
