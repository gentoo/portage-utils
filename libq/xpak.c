/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "main.h"

#include <stdio.h>
#include <string.h>
#include <xalloc.h>

#include "basename.h"
#include "copy_file.h"
#include "safe_io.h"
#include "scandirat.h"
#include "xpak.h"

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

typedef struct {
	int dir_fd;
	FILE *fp;
	int index_len;
	int data_len;
	char *index, *data;
} _xpak_archive;

typedef void (*xpak_callback_t)(int,char*,int,int,int,char*);

static void _xpak_walk_index(
		_xpak_archive *x,
		int argc,
		char **argv,
		xpak_callback_t func)
{
	int i, pathname_len, data_offset, data_len;
	char *p, pathname[100];

	p = x->index;
	while ((p - x->index) < x->index_len) {
		pathname_len = READ_BE_INT32((unsigned char*)p);
		if (pathname_len >= sizeof(pathname))
			err("pathname length %d exceeds limit %zd",
					pathname_len, sizeof(pathname));
		p += 4;
		memcpy(pathname, p, pathname_len);
		pathname[pathname_len] = '\0';
		if (strchr(pathname, '/') != NULL || strchr(pathname, '\\') != NULL)
			err("Index contains a file with a path: '%s'", pathname);
		p += pathname_len;
		data_offset = READ_BE_INT32((unsigned char*)p);
		p += 4;
		data_len = READ_BE_INT32((unsigned char*)p);
		p += 4;
		if (argc) {
			for (i = 0; i < argc; ++i) {
				if (argv[i] && !strcmp(pathname, argv[i])) {
					argv[i] = NULL;
					break;
				}
			}
			if (i == argc)
				continue;
		}
		(*func)(x->dir_fd, pathname, pathname_len,
				data_offset, data_len, x->data);
	}

	if (argc)
		for (i = 0; i < argc; ++i)
			if (argv[i])
				warn("Could not locate '%s' in archive", argv[i]);
}

static _xpak_archive *_xpak_open(const char *file)
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
	ret.index_len = READ_BE_INT32((unsigned char*)buf+XPAK_START_MSG_LEN);
	ret.data_len = READ_BE_INT32((unsigned char*)buf+XPAK_START_MSG_LEN+4);
	if (!ret.index_len || !ret.data_len) {
		warn("Skipping empty archive '%s'", file);
		goto close_and_ret;
	}

	return &ret;

close_and_ret:
	if (ret.fp != stdin)
		fclose(ret.fp);
	return NULL;
}

static void _xpak_close(_xpak_archive *x)
{
	fclose(x->fp);
}

int
xpak_list(
		int dir_fd,
		const char *file,
		int argc,
		char **argv,
		xpak_callback_t func)
{
	_xpak_archive *x;
	char buf[BUFSIZE];
	size_t ret;

	x = _xpak_open(file);
	if (!x)
		return 1;

	x->dir_fd = dir_fd;
	x->index = buf;
	if (x->index_len >= sizeof(buf))
		err("index length %d exceeds limit %zd", x->index_len, sizeof(buf));
	ret = fread(x->index, 1, x->index_len, x->fp);
	if (ret != (size_t)x->index_len)
		err("insufficient data read, got %zd, requested %d", ret, x->index_len);
	_xpak_walk_index(x, argc, argv, func);

	_xpak_close(x);

	return 0;
}

int
xpak_extract(
	int dir_fd,
	const char *file,
	int argc,
	char **argv,
	xpak_callback_t func)
{
	_xpak_archive *x;
	char buf[BUFSIZE], ext[BUFSIZE*32];
	size_t in;

	x = _xpak_open(file);
	if (!x)
		return 1;

	x->dir_fd = dir_fd;
	x->index = buf;

	if (x->index_len >= sizeof(buf))
		err("index length %d exceeds limit %zd", x->index_len, sizeof(buf));
	in = fread(x->index, 1, x->index_len, x->fp);
	if (in != (size_t)x->index_len)
		err("insufficient data read, got %zd, requested %d", in, x->index_len);

	/* the xpak may be large (like when it has CONTENTS) #300744 */
	x->data = (size_t)x->data_len < sizeof(ext) ? ext : xmalloc(x->data_len);
	in = fread(x->data, 1, x->data_len, x->fp);
	if (in != (size_t)x->data_len)
		err("insufficient data read, got %zd, requested %d", in, x->data_len);

	_xpak_walk_index(x, argc, argv, func);

	_xpak_close(x);

	if (x->data != ext)
		free(x->data);

	return 0;
}

static void
_xpak_add_file(
		int dir_fd,
		const char *filename,
		struct stat *st,
		FILE *findex,
		int *index_len,
		FILE *fdata,
		int *data_len,
		int verbose)
{
	FILE *fin;
	unsigned char intbuf[4];
	unsigned char *p = intbuf;
	const char *basefile;
	int fd, in_len;

	basefile = basename(filename);

	if (verbose == 1)
		printf("%s\n", basefile);
	else if (verbose)
		printf("%s @ offset byte %i\n", basefile, *data_len);

	/* write out the (pathname_len) */
	in_len = strlen(basefile);
	WRITE_BE_INT32(p, in_len);
	fwrite(p, 1, 4, findex);
	/* write out the pathname */
	fwrite(basefile, 1, in_len, findex);
	/* write out the (data_offset) */
	WRITE_BE_INT32(p, *data_len);
	fwrite(p, 1, 4, findex);

	*index_len += 4 + in_len + 4 + 4;

	/* now open the file, get (data_len),
	 * and append the file to the data file */
	fd = openat(dir_fd, filename, O_RDONLY|O_CLOEXEC);
	if (fd < 0) {
 open_fail:
		warnp("could not open for reading: %s", filename);
 fake_data_len:
		WRITE_BE_INT32(p, 0);
		fwrite(p, 1, 4, findex);
		return;
	}
	fin = fdopen(fd, "r");
	if (!fin) {
		close(fd);
		goto open_fail;
	}
	in_len = st->st_size;
	/* the xpak format can only store files whose size is a 32bit int
	 * so we have to make sure we don't store a big file */
	if (in_len != st->st_size) {
		warnf("File is too big: %zu", (size_t)st->st_size);
		fclose(fin);
		goto fake_data_len;
	}
	WRITE_BE_INT32(p, in_len);
	fwrite(p, 1, 4, findex);
	copy_file(fin, fdata);
	fclose(fin);

	*data_len += in_len;
}

int
xpak_create(
		int dir_fd,
		const char *file,
		int argc,
		char **argv,
		char append,
		int verbose)
{
	FILE *findex, *fdata, *fout;
	struct dirent **dir;
	int i, fidx, numfiles;
	struct stat st;
	char path[_Q_PATH_MAX];
	unsigned char intbuf[4];
	unsigned char *p;
	int index_len, data_len;

	if (argc == 0)
		err("Create usage: <xpak output> <files/dirs to pack>");

	if (strlen(file) >= sizeof(path)-6)
		err("Pathname is too long: %s", file);

	if ((fout = fopen(file, append ? "a" : "w")) == NULL) {
		warnp("could not open output: %s", file);
		return 1;
	}
	strcpy(path, file); strcat(path, ".index");
	if ((findex = fopen(path, "w+")) == NULL) {
		warnp("could not open output: %s", path);
		fclose(fout);
		return 1;
	}
	strcpy(path, file); strcat(path, ".dat");
	if ((fdata = fopen(path, "w+")) == NULL) {
		warnp("could not open output: %s", path);
		fclose(fout);
		fclose(findex);
		return 1;
	}

	index_len = data_len = 0;
	for (i = 0; i < argc; ++i) {
		if (fstatat(dir_fd, argv[i], &st, 0)) {
			warnp("fstatat(%s) failed", argv[i]);
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			if ((numfiles =
						scandir(argv[i], &dir, filter_hidden, alphasort)) < 0)
				warn("Directory '%s' is empty; skipping", argv[i]);
			for (fidx = 0; fidx < numfiles; ++fidx) {
				int ret = snprintf(path, sizeof(path), "%s/%s",
						argv[i], dir[fidx]->d_name);
				if (ret < 0 || (size_t)ret >= sizeof(path)) {
					warn("skipping path too long: %s/%s",
							argv[i], dir[fidx]->d_name);
					continue;
				}
				if (stat(path, &st) < 0) {
					warnp("could not read %s", path);
					continue;
				}
				_xpak_add_file(dir_fd, path, &st,
						findex, &index_len, fdata, &data_len, verbose);
			}
			scandir_free(dir, numfiles);
		} else if (S_ISREG(st.st_mode)) {
			_xpak_add_file(dir_fd, argv[i], &st,
					findex, &index_len, fdata, &data_len, verbose);
		} else
			warn("Skipping non file/directory '%s'", argv[i]);
	}

	rewind(findex);
	rewind(fdata);

	/* "XPAKPACK" + (index_len) + (data_len) + index + data + "XPAKSTOP" */
	fwrite(XPAK_START_MSG, 1, XPAK_START_MSG_LEN, fout); /* "XPAKPACK" */
	p = intbuf;
	WRITE_BE_INT32(p, index_len);
	fwrite(p, 1, 4, fout);                               /* (index_len) */
	WRITE_BE_INT32(p, data_len);
	fwrite(p, 1, 4, fout);                               /* (data_len) */
	copy_file(findex, fout);                       /* index */
	copy_file(fdata, fout);                        /* data */
	fwrite(XPAK_END_MSG, 1, XPAK_END_MSG_LEN, fout);     /* "XPAKSTOP" */

	strcpy(path, file); strcat(path, ".index"); unlink(path);
	strcpy(path, file); strcat(path, ".dat");   unlink(path);
	fclose(findex);
	fclose(fdata);
	fclose(fout);

	return 0;
}
