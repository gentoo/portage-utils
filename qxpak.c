/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qxpak.c,v 1.20 2010/04/07 05:58:16 solar Exp $
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2010 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qxpak

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

#define QXPAK_FLAGS "lxcd:O" COMMON_FLAGS
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
static const char qxpak_rcsid[] = "$Id: qxpak.c,v 1.20 2010/04/07 05:58:16 solar Exp $";
#define qxpak_usage(ret) usage(ret, QXPAK_FLAGS, qxpak_long_opts, qxpak_opts_help, lookup_applet_idx("qxpak"))

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
		pathname_len = tbz2_decode_int((unsigned char*)p);
		assert((size_t)pathname_len < sizeof(pathname));
		p += 4;
		memcpy(pathname, p, pathname_len);
		pathname[pathname_len] = '\0';
		if (strchr(pathname, '/') != NULL || strchr(pathname, '\\') != NULL)
			err("Index contains a file with a path: '%s'", pathname);
		p += pathname_len;
		data_offset = tbz2_decode_int((unsigned char*)p);
		p += 4;
		data_len = tbz2_decode_int((unsigned char*)p);
		p += 4;
		if (argc) {
			for (i = 0; i < argc; ++i)
				if (argv[i] && !strcmp(pathname, argv[i])) {
					argv[i] = NULL;
					break;
				}
			if (i == argc)
				continue;
		}
		(*func)(pathname, pathname_len, data_offset, data_len, x->data);
	}

	if (argc)
		for (i = 0; i < argc; ++i)
			if (argv[i])
				warn("Could not locate '%s' in archive", argv[i]);
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
	ret.index_len = tbz2_decode_int((unsigned char*)buf+XPAK_START_MSG_LEN);
	ret.data_len = tbz2_decode_int((unsigned char*)buf+XPAK_START_MSG_LEN+4);
	if (!ret.index_len || !ret.data_len) {
		warn("Skipping empty archive '%s'", file);
		goto close_and_ret;
	}

	/* clean up before returning */
	if (xpak_chdir)
		xchdir(xpak_chdir);

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
	size_t in;

	if (argc == 0)
		err("Extract usage: <xpak output> <files/dirs to extract>");

	x = _xpak_open(file);
	if (!x) return;

	x->index = buf;

	assert((size_t)x->index_len < sizeof(buf));
	in = fread(x->index, 1, x->index_len, x->fp);
	if (in != x->index_len)
		err("index chunk: read %i bytes, wanted %i bytes", (int)in, x->index_len);

	/* the xpak may be large (like when it has CONTENTS) #300744 */
	x->data = (size_t)x->data_len < sizeof(ext) ? ext : xmalloc(x->data_len);
	in = fread(x->data, 1, x->data_len, x->fp);
	if (in != x->data_len)
		err("data chunk: read %i bytes, wanted %i bytes", (int)in, x->data_len);

	_xpak_walk_index(x, argc, argv, &_xpak_extract_callback);

	_xpak_close(x);

	if (x->data != ext)
		free(x->data);
}

void _xpak_add_file(const char *filename, struct stat *st, FILE *findex, int *index_len, FILE *fdata, int *data_len);
void _xpak_add_file(const char *filename, struct stat *st, FILE *findex, int *index_len, FILE *fdata, int *data_len)
{
	FILE *fin;
	unsigned char *p;
	const char *basefile;
	int in_len;

	if ((basefile = strrchr(filename, '/')) == NULL) {
		basefile = filename;
	} else {
		++basefile;
		assert(*basefile);
	}

	if (verbose == 1)
		printf("%s\n", basefile);
	else if (verbose)
		printf("%s @ offset byte %i\n", basefile, *data_len);

	/* write out the (pathname_len) */
	in_len = strlen(basefile);
	p = tbz2_encode_int(in_len);
	fwrite(p, 1, 4, findex);
	/* write out the pathname */
	fwrite(basefile, 1, in_len, findex);
	/* write out the (data_offset) */
	p = tbz2_encode_int(*data_len);
	fwrite(p, 1, 4, findex);

	*index_len += 4 + in_len + 4 + 4;

	/* now open the file, get (data_len), and append the file to the data file */
	if ((fin = fopen(filename, "r")) == NULL) {
		warnp("Could not open '%s' for reading", filename);
fake_data_len:
		p = tbz2_encode_int(0);
		fwrite(p, 1, 4, findex);
		return;
	}
	in_len = st->st_size;
	/* the xpak format can only store files whose size is a 32bit int
	 * so we have to make sure we don't store a big file */
	if (in_len != st->st_size) {
		warnf("File is too big: %lu", st->st_size);
		fclose(fin);
		goto fake_data_len;
	}
	p = tbz2_encode_int(in_len);
	fwrite(p, 1, 4, findex);
	_tbz2_copy_file(fin, fdata);
	fclose(fin);

	*data_len += in_len;
}
void xpak_create(const char *file, int argc, char **argv);
void xpak_create(const char *file, int argc, char **argv)
{
	FILE *findex, *fdata, *fout;
	struct dirent **dir;
	int i, fidx, numfiles;
	struct stat st;
	char path[_Q_PATH_MAX];
	unsigned char *p;
	int index_len, data_len;

	if (argc == 0)
		err("Create usage: <xpak output> <files/dirs to pack>");

	if (strlen(file) >= sizeof(path)-6)
		err("Pathname is too long: %s", file);

	if ((fout = fopen(file, "w")) == NULL)
		return;
	strcpy(path, file); strcat(path, ".index");
	if ((findex = fopen(path, "w+")) == NULL) {
		fclose(fout);
		return;
	}
	strcpy(path, file); strcat(path, ".dat");
	if ((fdata = fopen(path, "w+")) == NULL) {
		fclose(fout);
		fclose(findex);
		return;
	}

	index_len = data_len = 0;
	for (i = 0; i < argc; ++i) {
		stat(argv[i], &st);
		if (S_ISDIR(st.st_mode)) {
			if ((numfiles = scandir(argv[i], &dir, filter_hidden, alphasort)) < 0)
				warn("Directory '%s' is empty; skipping", argv[i]);
			for (fidx = 0; fidx < numfiles; ++fidx) {
				snprintf(path, sizeof(path), "%s/%s", argv[i], dir[fidx]->d_name);
				stat(path, &st);
				_xpak_add_file(path, &st, findex, &index_len, fdata, &data_len);
			}
			while (numfiles--) free(dir[numfiles]);
			free(dir);
		} else if (S_ISREG(st.st_mode)) {
			_xpak_add_file(argv[i], &st, findex, &index_len, fdata, &data_len);
		} else
			warn("Skipping non file/directory '%s'", argv[i]);
	}

	rewind(findex);
	rewind(fdata);

	/* "XPAKPACK" + (index_len) + (data_len) + index + data + "XPAKSTOP" */
	fwrite(XPAK_START_MSG, 1, XPAK_START_MSG_LEN, fout); /* "XPAKPACK" */
	p = tbz2_encode_int(index_len);
	fwrite(p, 1, 4, fout);                               /* (index_len) */
	p = tbz2_encode_int(data_len);
	fwrite(p, 1, 4, fout);                               /* (data_len) */
	_tbz2_copy_file(findex, fout);                       /* index */
	_tbz2_copy_file(fdata, fout);                        /* data */
	fwrite(XPAK_END_MSG, 1, XPAK_END_MSG_LEN, fout);     /* "XPAKSTOP" */

	strcpy(path, file); strcat(path, ".index"); unlink(path);
	strcpy(path, file); strcat(path, ".dat");   unlink(path);
	fclose(findex);
	fclose(fdata);
	fclose(fout);
}

int qxpak_main(int argc, char **argv)
{
	enum { XPAK_ACT_NONE, XPAK_ACT_LIST, XPAK_ACT_EXTRACT, XPAK_ACT_CREATE };
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
			if (xpak_chdir) err("Only use -d once");
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

#else
DEFINE_APPLET_STUB(qxpak)
#endif
