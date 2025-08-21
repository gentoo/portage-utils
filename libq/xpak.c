/*
 * Copyright 2005-2025 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
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
#define TBZ2_END_MSG         "STOP"
#define TBZ2_END_MSG_LEN     4
#define TBZ2_END_SIZE_LEN    4
#define TBZ2_FOOTER_LEN      (TBZ2_END_MSG_LEN + TBZ2_END_SIZE_LEN)

typedef struct {
	void *ctx;
	FILE *fp;
	unsigned int xpakstart;  /* offset in the file for XPAKPACK */
	unsigned int index_len;
	unsigned int data_len;
	char *index;
	char *data;
} _xpak_archive;

static void _xpak_walk_index(
		_xpak_archive *x,
		xpak_callback_t func)
{
	unsigned int pathname_len;
	unsigned int data_offset;
	unsigned int data_len;
	char *p;
	char pathname[100];

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

		/* check offset and len individually to deal with overflow */
		if (x->data != NULL &&
				(data_offset > x->data_len ||
				 data_len > x->data_len ||
				 data_offset + data_len > x->data_len))
			err("Data for '%s' is out of bounds: offset=%u, len=%u, size=%u\n",
					pathname, data_len, data_offset, x->data_len);

		(*func)(x->ctx, pathname, pathname_len,
				data_offset, data_len, x->data);
	}
}

static _xpak_archive *_xpak_open(const int fd)
{
	static _xpak_archive ret;
	char buf[XPAK_START_LEN];

	/* init the file */
	memset(&ret, 0x00, sizeof(ret));
	if ((ret.fp = fdopen(fd, "r")) == NULL)
		return NULL;

	/* verify this xpak doesn't suck */
	if (fread(buf, 1, XPAK_START_LEN, ret.fp) != XPAK_START_LEN)
		goto close_and_ret;
	if (memcmp(buf, XPAK_START_MSG, XPAK_START_MSG_LEN) != 0) {
		/* stream not positioned at XPAKSTART, let's see if we can
		 * reposition the stream */
		if (fseek(ret.fp, -TBZ2_FOOTER_LEN, SEEK_END) == 0)
		{
			if (fread(buf, 1, TBZ2_FOOTER_LEN, ret.fp) != TBZ2_FOOTER_LEN)
				goto close_and_ret;

			if (memcmp(buf + TBZ2_END_SIZE_LEN,
						TBZ2_END_MSG, TBZ2_END_MSG_LEN) == 0)
			{
				int xpaklen = READ_BE_INT32(buf);

				if (fseek(ret.fp, -(xpaklen + TBZ2_FOOTER_LEN), SEEK_END) == 0)
				{
					ret.xpakstart = (unsigned int)ftell(ret.fp);
					if (fread(buf, 1, XPAK_START_LEN, ret.fp) != XPAK_START_LEN)
						goto close_and_ret;
					if (memcmp(buf, XPAK_START_MSG, XPAK_START_MSG_LEN) == 0)
						goto setup_lens;
				}
			}
		}
		warn("Not an xpak file");
		goto close_and_ret;
	}
	ret.xpakstart = 0;  /* pure xpak file */

setup_lens:
	/* calc index and data sizes */
	ret.index_len = READ_BE_INT32((unsigned char*)buf+XPAK_START_MSG_LEN);
	ret.data_len = READ_BE_INT32((unsigned char*)buf+XPAK_START_MSG_LEN+4);
	if (!ret.index_len || !ret.data_len) {
		warn("Skipping empty archive");
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
xpak_process_fd(
	int fd,
	bool get_data,
	void *ctx,
	xpak_callback_t func)
{
	_xpak_archive *x;
	char buf[BUFSIZE];
	size_t in;

	x = _xpak_open(fd);
	if (!x)
		return -1;

	x->ctx = ctx;
	x->index = buf;

	if (x->index_len >= sizeof(buf))
		err("index length %d exceeds limit %zd", x->index_len, sizeof(buf));
	in = fread(x->index, 1, x->index_len, x->fp);
	if (in != (size_t)x->index_len)
		err("insufficient data read, got %zd, requested %d", in, x->index_len);

	if (get_data) {
		/* the xpak may be large (like when it has CONTENTS) #300744 */
		x->data = xmalloc(x->data_len);

		in = fread(x->data, 1, x->data_len, x->fp);
		if (in != (size_t)x->data_len)
			err("insufficient data read, got %zd, requested %d",
					in, x->data_len);
	} else {
		x->data = NULL;
		x->data_len = 0;
	}

	_xpak_walk_index(x, func);

	_xpak_close(x);

	if (get_data)
		free(x->data);

	return x->xpakstart;
}

int
xpak_process(
	const char *file,
	bool get_data,
	void *ctx,
	xpak_callback_t func)
{
	int fd = -1;
	int ret;

	if (file[0] == '-' && file[1] == '\0')
		fd = 0;
	else if ((fd = open(file, O_RDONLY | O_CLOEXEC)) == -1)
		return -1;

	ret = xpak_process_fd(fd, get_data, ctx, func);
	if (ret < 0)
		warn("Unable to open file '%s'", file);

	return ret;
}

static void
_xpak_add_file(
		int fd,
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
	int in_len;

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
	if ((fin = fdopen(fd, "r")) == NULL) {
		warnp("could not open for reading: %s", filename);
		WRITE_BE_INT32(p, 0);
		fwrite(p, 1, 4, findex);
		return;
	}

	in_len = st->st_size;
	/* the xpak format can only store files whose size is a 32bit int
	 * so we have to make sure we don't store a big file */
	if (in_len != st->st_size) {
		warnf("File is too big: %zu", (size_t)st->st_size);
		fclose(fin);
		WRITE_BE_INT32(p, 0);
		fwrite(p, 1, 4, findex);
		return;
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
		bool append,
		int verbose)
{
	FILE *findex, *fdata, *fout;
	struct dirent **dir = NULL;
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
		int fd;

		if (fstatat(dir_fd, argv[i], &st, 0)) {
			warnp("fstatat(%s) failed", argv[i]);
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			dir = NULL;
			if ((numfiles =
						scandir(argv[i], &dir, filter_hidden, alphasort)) < 0)
			{
				warn("Directory '%s' is empty; skipping", argv[i]);
				continue;
			}
			for (fidx = 0; fidx < numfiles; ++fidx) {
				int ret = snprintf(path, sizeof(path), "%s/%s",
						argv[i], dir[fidx]->d_name);

				if (ret < 0 || (size_t)ret >= sizeof(path)) {
					warn("skipping path too long: %s/%s",
							argv[i], dir[fidx]->d_name);
					continue;
				}

				fd = openat(dir_fd, path, O_RDONLY|O_CLOEXEC);
				if (fd < 0 || fstat(fd, &st) < 0) {
					warnp("could not read %s", path);
					continue;
				}
				_xpak_add_file(fd, path, &st,
						findex, &index_len, fdata, &data_len, verbose);
				close(fd);
			}
			scandir_free(dir, numfiles);
		} else if (S_ISREG(st.st_mode)) {
			fd = openat(dir_fd, argv[i], O_RDONLY|O_CLOEXEC);
			if (fd < 0 || fstat(fd, &st) < 0) {
				warnp("could not read %s", path);
				continue;
			}
			_xpak_add_file(fd, argv[i], &st,
					findex, &index_len, fdata, &data_len, verbose);
			close(fd);
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
