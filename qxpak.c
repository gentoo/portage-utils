/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qxpak.c,v 1.1 2005/06/21 23:43:44 vapier Exp $
 *
 * 2005 Ned Ludd        - <solar@gentoo.org>
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



/*
# The format for a tbz2/xpak:
#
#  tbz2: tar.bz2 + xpak + (xpak_offset) + "STOP"
#  xpak: "XPAKPACK" + (index_len) + (data_len) + index + data + "XPAKSTOP"
# index: (pathname_len) + pathname + (data_offset) + (data_len)
#        index entries are concatenated end-to-end.
#  data: concatenated data chunks, end-to-end.
#
# [tarball]XPAKPACKIIIIDDDD[index][data]XPAKSTOPOOOOSTOP
#
# (integer) == encodeint(integer)  ===> 4 characters (big-endian copy)
# '+' means concatenate the fields ===> All chunks are strings
*/
#define XPAK_START_MSG       "XPAKPACK"
#define XPAK_START_MSG_LEN   8
#define XPAK_START_LEN       (8 + 4 + 4)
#define XPAK_END_MSG         "XPAKSTOP"
#define XPAK_END_MSG_LEN     8



#define QXPAK_FLAGS "lxd:O" COMMON_FLAGS
static struct option const qxpak_long_opts[] = {
	{"list",      no_argument, NULL, 'l'},
	{"extract",   no_argument, NULL, 'x'},
	{"create",    no_argument, NULL, 'c'},
	{"dir",        a_argument, NULL, 'd'},
	{"stdout",    no_argument, NULL, 'O'},
	COMMON_LONG_OPTS
};
static const char *qxpak_opts_help[] = {
	"List the contents of an archive",
	"Extract the contents of an archive",
	"Create an archive of a directory/files",
	"Change to specified directory",
	"Write files to stdout",
	COMMON_OPTS_HELP
};
#define qxpak_usage(ret) usage(ret, QXPAK_FLAGS, qxpak_long_opts, qxpak_opts_help, APPLET_QXPAK)



typedef struct {
	FILE *fp;
	int index_len;
	int data_len;
	char *index, *data;
} _xpak_archive;

static char *xpak_chdir = NULL;
static char xpak_stdout = 0;



void _xpak_walk_index(_xpak_archive *x, int argc, char **argv, void (*func)(char*,int,int,int,char*));
void _xpak_walk_index(_xpak_archive *x, int argc, char **argv, void (*func)(char*,int,int,int,char*))
{
	int i, pathname_len, data_offset, data_len;
	char *p, pathname[100];

	p = x->index;
	while ((p - x->index) < x->index_len) {
		pathname_len = tbz2_decode_int(p);
		assert((size_t)pathname_len < sizeof(pathname));
		p += 4;
		memcpy(pathname, p, pathname_len);
		pathname[pathname_len] = '\0';
		if (strchr(pathname, '/') != NULL || strchr(pathname, '\\') != NULL)
			err("Index contains a file with a path: '%s'", pathname);
		p += pathname_len;
		data_offset = tbz2_decode_int(p);
		p += 4;
		data_len = tbz2_decode_int(p);
		p += 4;
		if (argc) {
			for (i = 0; i < argc; ++i)
				if (!strcmp(pathname, argv[i]))
					break;
			if (i == argc)
				continue;
		}
		(*func)(pathname, pathname_len, data_offset, data_len, x->data);
	}
}

_xpak_archive *_xpak_open(const char *file);
_xpak_archive *_xpak_open(const char *file)
{
	static _xpak_archive ret;
	char buf[XPAK_START_LEN];

	/* init the file */
	memset(&ret, 0x00, sizeof(ret));
	if (file[0] == '-' && file[1] == '\0')
		ret.fp = stdin;
	else if ((ret.fp = fopen(file, "r")) == NULL)
		return NULL;

	/* verify this xpak doesnt suck */
	if (fread(buf, 1, XPAK_START_LEN, ret.fp) != XPAK_START_LEN)
		goto close_and_ret;
	if (memcmp(buf, XPAK_START_MSG, XPAK_START_MSG_LEN)) {
		warn("%s: Invalid xpak", file);
		goto close_and_ret;
	}

	/* calc index and data sizes */
	ret.index_len = tbz2_decode_int(buf+XPAK_START_MSG_LEN);
	ret.data_len = tbz2_decode_int(buf+XPAK_START_MSG_LEN+4);

	/* clean up before returning */
	if (xpak_chdir) chdir(xpak_chdir);
	if (ret.fp != stdin)
		fclose(ret.fp);

	return &ret;

close_and_ret:
	if (ret.fp != stdin)
		fclose(ret.fp);
	return NULL;
}

void _xpak_close(_xpak_archive *x);
void _xpak_close(_xpak_archive *x)
{
	fclose(x->fp);
}

void _xpak_list_callback(char *pathname, _q_unused_ int pathname_len, int data_offset, int data_len, _q_unused_ char *data);
void _xpak_list_callback(char *pathname, _q_unused_ int pathname_len, int data_offset, int data_len, _q_unused_ char *data)
{
	if (!verbose)
		puts(pathname);
	else if (verbose == 1)
		printf("%s: %i byte%c\n", pathname, data_len, (data_len>1?'s':' '));
	else
		printf("%s: %i byte%c @ offset byte %i\n",
			pathname, data_len, (data_len>1?'s':' '), data_offset);
}
void xpak_list(const char *file, int argc, char **argv);
void xpak_list(const char *file, int argc, char **argv)
{
	_xpak_archive *x;
	char buf[BUFSIZE];

	x = _xpak_open(file);
	if (!x) return;

	x->index = buf;
	assert((size_t)x->index_len < sizeof(buf));
	assert(fread(x->index, 1, x->index_len, x->fp) == (size_t)x->index_len);
	_xpak_walk_index(x, argc, argv, &_xpak_list_callback);

	_xpak_close(x);
}

void _xpak_extract_callback(char *pathname, _q_unused_ int pathname_len, int data_offset, int data_len, char *data);
void _xpak_extract_callback(char *pathname, _q_unused_ int pathname_len, int data_offset, int data_len, char *data)
{
	FILE *out;
	if (verbose == 1)
		puts(pathname);
	else if (verbose > 1)
		printf("%s: %i byte%c\n", pathname, data_len, (data_len>1?'s':' '));
	if (xpak_stdout)
		out = stdout;
	else if ((out = fopen(pathname, "w")) == NULL)
		return;
	fwrite(data+data_offset, 1, data_len, out);
	if (!xpak_stdout)
		fclose(out);
}
void xpak_extract(const char *file, int argc, char **argv);
void xpak_extract(const char *file, int argc, char **argv)
{
	_xpak_archive *x;
	char buf[BUFSIZE], ext[BUFSIZE*32];

	x = _xpak_open(file);
	if (!x) return;

	x->index = buf;
	x->data = ext;
	assert((size_t)x->index_len < sizeof(buf));
	assert(fread(x->index, 1, x->index_len, x->fp) == (size_t)x->index_len);
	assert((size_t)x->data_len < sizeof(ext));
	assert(fread(x->data, 1, x->data_len, x->fp) == (size_t)x->data_len);
	_xpak_walk_index(x, argc, argv, &_xpak_extract_callback);

	_xpak_close(x);
}

void xpak_create(const char *file, int argc, char **argv);
void xpak_create(const char *file, int argc, char **argv)
{
	err("TODO: create xpak %s", file);
	argc = 0;
	argv = NULL;
}



enum {
	XPAK_ACT_NONE,
	XPAK_ACT_LIST,
	XPAK_ACT_EXTRACT,
	XPAK_ACT_CREATE
};
int qxpak_main(int argc, char **argv)
{
	int i;
	char *xpak;
	char action = XPAK_ACT_NONE;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QXPAK, qxpak, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qxpak)
		case 'l': action = XPAK_ACT_LIST; break;
		case 'x': action = XPAK_ACT_EXTRACT; break;
		case 'c': action = XPAK_ACT_CREATE; break;
		case 'O': xpak_stdout = 1; break;
		case 'd':
			if (xpak_chdir) err("Only use -c once");
			xpak_chdir = xstrdup(optarg);
			break;
		}
	}
	if (optind == argc || action == XPAK_ACT_NONE)
		qxpak_usage(EXIT_FAILURE);

	xpak = argv[optind++];
	argc -= optind;
	argv += optind;

	switch (action) {
	case XPAK_ACT_LIST:    xpak_list(xpak, argc, argv); break;
	case XPAK_ACT_EXTRACT: xpak_extract(xpak, argc, argv); break;
	case XPAK_ACT_CREATE:  xpak_create(xpak, argc, argv); break;
	}

	if (xpak_chdir) free(xpak_chdir);

	return EXIT_SUCCESS;
}
