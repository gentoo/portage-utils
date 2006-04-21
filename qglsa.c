/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qglsa.c,v 1.6 2006/04/21 22:15:47 vapier Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qglsa

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
	"Do everything but actually emerge the package",
	"Auto-apply GLSAs to the system",
	"Mark specified GLSAs as fixed",
	COMMON_OPTS_HELP
};
static const char qglsa_rcsid[] = "$Id: qglsa.c,v 1.6 2006/04/21 22:15:47 vapier Exp $";
#define qglsa_usage(ret) usage(ret, QGLSA_FLAGS, qglsa_long_opts, qglsa_opts_help, lookup_applet_idx("qglsa"))


static char *qglsa_load_list(void);
static char *qglsa_load_list(void)
{
	char *ret;
	struct stat st;

	if (stat(QGLSA_DB, &st))
		return NULL;

	ret = xmalloc(st.st_size+1);

	if (!eat_file(QGLSA_DB, ret, st.st_size)) {
		free(ret);
		return NULL;
	}

	return ret;
}
static void qglsa_append_to_list(const char *glsa);
static void qglsa_append_to_list(const char *glsa)
{
	FILE *f;
	if ((f = fopen(QGLSA_DB, "a")) != NULL) {
		fprintf(f, "%s\n", glsa);
		fclose(f);
	}
}

static void qglsa_decode_entities(char *xml_buf, size_t len);
static void qglsa_decode_entities(char *xml_buf, size_t len)
{
	const char const *encoded[] = { "&lt;", "&gt;", "&quot;", "&amp;"};
	const char const *decoded[] = {  "<",    ">",    "\"",     "&"};
	int i;
	char *p, *q;

	/* most things dont have entities so let's just bail real quick */
	if (strchr(xml_buf, '&') == NULL)
		return;

	for (i=0; i<ARRAY_SIZE(encoded); ++i) {
		/* for now, we assume that strlen(decoded) is always 1 ... if
		 * this changes, we have to update the 'p++' accordingly */
		while ((p=strstr(xml_buf, encoded[i])) != NULL) {
			strcpy(p, decoded[i]);
			q = p++ + strlen(encoded[i]);
			memmove(p, q, len-(q-xml_buf)+1);
		}
	}
}

static char *qglsa_get_xml_tag_attribute(const char *xml_buf, const char *tag, const char *attribute)
{
	static char tmp_buf[BUFSIZE];
	char *start, *end, *start_attr, *end_attr;

	/* find the start of this tag */
	sprintf(tmp_buf, "<%s ", tag);
	if ((start = strstr(xml_buf, tmp_buf)) == NULL)
		return NULL;

	/* find the end of this tag */
	start += strlen(tmp_buf) - 1;
	if ((end = strchr(start, '>')) == NULL)
		return NULL;

	/* find the attribute in this tag */
	sprintf(tmp_buf, " %s=", attribute);
	if ((start_attr = strstr(start, tmp_buf)) == NULL)
		return NULL;

	/* get the value */
	start_attr += strlen(tmp_buf);
	if (*start_attr == '"') {
		end_attr = strchr(++start_attr, '"');
	} else {
		end_attr = strchr(start_attr, ' ');
	}
	assert(end_attr - start_attr < sizeof(tmp_buf));
	memcpy(tmp_buf, start_attr, end_attr-start_attr);
	tmp_buf[end_attr-start_attr] = '\0';

	qglsa_decode_entities(tmp_buf, end-start);
	return tmp_buf;
}
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
	if ((end = strstr(start, tmp_buf)) == NULL)
		return NULL;
	assert(end - start < sizeof(tmp_buf));
	memcpy(tmp_buf, start, end - start);
	tmp_buf[end - start] = '\0';

	qglsa_decode_entities(tmp_buf, end-start);
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
	enum { GLSA_FUNKYTOWN, GLSA_LIST, GLSA_DUMP, GLSA_TEST, GLSA_FIX, GLSA_INJECT };
	int i;
	DIR *dir;
	struct dirent *dentry;
	struct stat st;
	char buf[BUFSIZE*4];
	char *s, *p, *glsa_fixed_list;
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
		case 'p': pretend = 1; break;
		case 'f': set_action(GLSA_FIX); break;
		case 'i': set_action(GLSA_INJECT); break;
		COMMON_GETOPTS_CASES(qglsa)
		}
	}
	if (action == GLSA_FUNKYTOWN)
		qglsa_usage(EXIT_FAILURE);
	if (action != GLSA_LIST && optind == argc)
		err("specified action requires a list, either 'all', 'new', or GLSA numbers");

	glsa_fixed_list = NULL;
	for (i = optind; i < argc; ++i) {
		if (!strcmp(argv[i], "all")) {
			all_glsas = 1;
			if (optind+1 != argc)
				err("You may only use class names by themselves");
		} else if (!strcmp(argv[i], "new")) {
			all_glsas = 0;
			if (optind+1 != argc)
				err("You may only use class names by themselves");
		}
	}
	glsa_fixed_list = qglsa_load_list();

	if (chdir(portdir) != 0)
		errp("could not chdir to portdir %s", portdir);
	if (chdir("./metadata/glsa") != 0)
		errp("could not chdir to glsa dir");

	switch (action) {
	/*case GLSA_FIX:*/
	case GLSA_INJECT:
		for (i = optind; i < argc; ++i) {
			snprintf(buf, sizeof(buf), "glsa-%s.xml", argv[i]);
			if (stat(buf, &st)) {
				warn("Skipping invalid GLSA '%s'", argv[i]);
				continue;
			}
			if (glsa_fixed_list) {
				if (strstr(glsa_fixed_list, argv[i])) {
					warn("Skipping already installed GLSA %s", argv[i]);
					continue;
				}
			}
			if (action == GLSA_FIX) {
				printf("Fixing GLSA %s%s%s\n", GREEN, argv[i], NORM);
				continue;
			} else if (action == GLSA_INJECT)
				printf("Injecting GLSA %s%s%s\n", GREEN, argv[i], NORM);
			qglsa_append_to_list(argv[i]);
		}
		break;

	default:
		if ((dir = opendir(".")) == NULL)
			return EXIT_FAILURE;

		while ((dentry = readdir(dir)) != NULL) {
			/* validate this file as a proper glsa */
			char glsa_id[20];
			if (strncmp(dentry->d_name, "glsa-", 5))
				continue;
			strcpy(glsa_id, dentry->d_name + 5);
			if ((s = strchr(glsa_id, '.')) == NULL || memcmp(s, ".xml\0", 5))
				continue;
			*s = '\0';

			/* see if we want to skip glsa's already fixed */
			if (!all_glsas && glsa_fixed_list) {
				if (strstr(glsa_fixed_list, glsa_id))
					continue;
			}

			/* load the glsa into memory */
			if (eat_file(dentry->d_name, buf, sizeof(buf)) == 0)
				errp("could not eat %s", dentry->d_name);

			/* now lets figure out what to do with this memory */
			switch (action) {
			case GLSA_LIST:
				s = qglsa_get_xml_tag(buf, "title");
				printf("%s%s%s: %s", GREEN, glsa_id, NORM, s);
				if (verbose) {
					int num_shown = 0;
					p = qglsa_get_xml_tag(buf, "affected");
					if (p) {
						printf(" (");
						while (p++) {
							s = qglsa_get_xml_tag_attribute(p, "package", "name");
							if (s) {
								if (verbose < 2 && ++num_shown > 3) {
									printf(" ...");
									break;
								}
								printf(" %s", s);
							} else
								break;
							p = strstr(p, "</package>");
						}
						printf(" )");
					}
				}
				printf("\n");
				break;
			case GLSA_DUMP:
				s = qglsa_get_xml_tag(buf, "title");
				printf("%s%s%s: %s\n", GREEN, glsa_id, NORM, s);
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
	}

	free(glsa_fixed_list);

	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qglsa)
#endif
