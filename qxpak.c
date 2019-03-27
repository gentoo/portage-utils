/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "basename.h"
#include "copy_file.h"
#include "safe_io.h"
#include "scandirat.h"
#include "xpak.h"

#define QXPAK_FLAGS "lxcd:O" COMMON_FLAGS
static struct option const qxpak_long_opts[] = {
	{"list",      no_argument, NULL, 'l'},
	{"extract",   no_argument, NULL, 'x'},
	{"create",    no_argument, NULL, 'c'},
	{"dir",        a_argument, NULL, 'd'},
	{"stdout",    no_argument, NULL, 'O'},
	COMMON_LONG_OPTS
};
static const char * const qxpak_opts_help[] = {
	"List the contents of an archive",
	"Extract the contents of an archive",
	"Create an archive of a directory/files",
	"Change to specified directory",
	"Write files to stdout",
	COMMON_OPTS_HELP
};
#define qxpak_usage(ret) usage(ret, QXPAK_FLAGS, qxpak_long_opts, qxpak_opts_help, NULL, lookup_applet_idx("qxpak"))

static char xpak_stdout;

static void
_xpak_list_callback(
		int dir_fd,
		char *pathname,
		int pathname_len,
		int data_offset,
		int data_len,
		char *data)
{
	(void)dir_fd;
	(void)pathname_len;
	(void)data;

	if (!verbose)
		puts(pathname);
	else if (verbose == 1)
		printf("%s: %i byte%c\n", pathname, data_len, (data_len>1?'s':' '));
	else
		printf("%s: %i byte%c @ offset byte %i\n",
			pathname, data_len, (data_len>1?'s':' '), data_offset);
}

static void
_xpak_extract_callback(
	int dir_fd,
	char *pathname,
	int pathname_len,
	int data_offset,
	int data_len,
	char *data)
{
	FILE *out;
	(void)pathname_len;

	if (verbose == 1)
		puts(pathname);
	else if (verbose > 1)
		printf("%s: %i byte%c\n", pathname, data_len, (data_len>1?'s':' '));

	if (!xpak_stdout) {
		int fd = openat(dir_fd, pathname,
				O_WRONLY|O_CLOEXEC|O_CREAT|O_TRUNC, 0644);
		if (fd < 0)
			return;
		out = fdopen(fd, "w");
		if (!out)
			return;
	} else
		out = stdout;

	fwrite(data + data_offset, 1, data_len, out);

	if (!xpak_stdout)
		fclose(out);
}

int qxpak_main(int argc, char **argv)
{
	enum { XPAK_ACT_NONE, XPAK_ACT_LIST, XPAK_ACT_EXTRACT, XPAK_ACT_CREATE };
	int i, ret, dir_fd;
	char *xpak;
	char action = XPAK_ACT_NONE;

	dir_fd = AT_FDCWD;
	xpak_stdout = 0;

	while ((i = GETOPT_LONG(QXPAK, qxpak, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qxpak)
		case 'l': action = XPAK_ACT_LIST; break;
		case 'x': action = XPAK_ACT_EXTRACT; break;
		case 'c': action = XPAK_ACT_CREATE; break;
		case 'O': xpak_stdout = 1; break;
		case 'd':
			if (dir_fd != AT_FDCWD)
				err("Only use -d once");
			dir_fd = open(optarg, O_RDONLY|O_CLOEXEC|O_PATH);
			if (dir_fd < 0)
				errp("Could not open directory %s", optarg);
			break;
		}
	}
	if (optind == argc || action == XPAK_ACT_NONE)
		qxpak_usage(EXIT_FAILURE);

	xpak = argv[optind++];
	argc -= optind;
	argv += optind;

	switch (action) {
	case XPAK_ACT_LIST:
		ret = xpak_list(dir_fd, xpak, argc, argv, &_xpak_list_callback);
		break;
	case XPAK_ACT_EXTRACT:
		ret = xpak_extract(dir_fd, xpak, argc, argv, &_xpak_extract_callback);
		break;
	case XPAK_ACT_CREATE:
		ret = xpak_create(dir_fd, xpak, argc, argv, 0, verbose);
		break;
	default:
		ret = EXIT_FAILURE;
	}

	if (dir_fd != AT_FDCWD)
		close(dir_fd);

	return ret;
}
