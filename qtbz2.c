/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qtbz2.c,v 1.19 2011/10/02 21:52:29 vapier Exp $
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2010 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qtbz2

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
#define TBZ2_END_MSG      "STOP"
#define TBZ2_END_MSG_LEN  4
#define TBZ2_END_LEN      (4 + TBZ2_END_MSG_LEN)

#define QTBZ2_FLAGS "d:jstxO" COMMON_FLAGS
static struct option const qtbz2_long_opts[] = {
	{"dir",        a_argument, NULL, 'd'},
	{"join",      no_argument, NULL, 'j'},
	{"split",     no_argument, NULL, 's'},
	{"tarbz2",    no_argument, NULL, 't'},
	{"xpak",      no_argument, NULL, 'x'},
	{"stdout",    no_argument, NULL, 'O'},
	COMMON_LONG_OPTS
};
static const char * const qtbz2_opts_help[] = {
	"Change to specified directory",
	"Join tar.bz2 + xpak into a tbz2",
	"Split a tbz2 into a tar.bz2 + xpak",
	"Just split the tar.bz2",
	"Just split the xpak",
	"Write files to stdout",
	COMMON_OPTS_HELP
};
static const char qtbz2_rcsid[] = "$Id: qtbz2.c,v 1.19 2011/10/02 21:52:29 vapier Exp $";
#define qtbz2_usage(ret) usage(ret, QTBZ2_FLAGS, qtbz2_long_opts, qtbz2_opts_help, lookup_applet_idx("qtbz2"))

static char tbz2_stdout = 0;

unsigned char *tbz2_encode_int(int enc);
unsigned char *tbz2_encode_int(int enc)
{
	static unsigned char ret[4];
	ret[0] = (enc & 0xff000000) >> 24;
	ret[1] = (enc & 0x00ff0000) >> 16;
	ret[2] = (enc & 0x0000ff00) >> 8;
	ret[3] = (enc & 0x000000ff);
	return ret;
}
int tbz2_decode_int(unsigned char *buf);
int tbz2_decode_int(unsigned char *buf)
{
	int ret;
	ret = 0;
	ret += (buf[0] << 24);
	ret += (buf[1] << 16);
	ret += (buf[2] << 8);
	ret += (buf[3]);
	return ret;
}

void _tbz2_copy_file(FILE *src, FILE *dst);
void _tbz2_copy_file(FILE *src, FILE *dst)
{
	int count = 1;
	unsigned char buffer[BUFSIZE*32];
	while (count) {
		count = fread(buffer, 1, sizeof(buffer), src);
		if (!count) return;
		fwrite(buffer, 1, count, dst);
	}
}

static int
tbz2_compose(int dir_fd, const char *tarbz2, const char *xpak, const char *tbz2)
{
	FILE *out, *in_tarbz2, *in_xpak;
	struct stat st;
	int ret = 1, fd;

	if (verbose)
		printf("input xpak: %s\ninput tar.bz2: %s\noutput tbz2: %s\n",
			xpak, tarbz2, tbz2);

	/* open tbz2 output */
	if ((out = fopen(tbz2, "w")) == NULL)
		return ret;
	/* open tar.bz2 input */
	fd = openat(dir_fd, tarbz2, O_RDONLY|O_CLOEXEC);
	if (fd < 0) {
		fclose(out);
		return ret;
	}
	in_tarbz2 = fdopen(fd, "r");
	if (in_tarbz2 == NULL) {
		fclose(out);
		close(fd);
		return ret;
	}
	/* open xpak input */
	fd = openat(dir_fd, xpak, O_RDONLY|O_CLOEXEC);
	if (fd < 0) {
		fclose(out);
		fclose(in_tarbz2);
		return ret;
	}
	in_xpak = fdopen(fd, "r");
	if (in_xpak == NULL) {
		fclose(out);
		fclose(in_tarbz2);
		close(fd);
		return ret;
	}
	if (fstat(fd, &st)) {
		fclose(out);
		fclose(in_tarbz2);
		fclose(in_xpak);
		return ret;
	}

	/* save [tarball] */
	_tbz2_copy_file(in_tarbz2, out);
	fclose(in_tarbz2);
	/* save [xpak] */
	_tbz2_copy_file(in_xpak, out);
	fclose(in_xpak);

	/* save tbz2 tail: OOOOSTOP */
	fwrite(tbz2_encode_int(st.st_size), 1, 4, out);
	fwrite(TBZ2_END_MSG, 1, TBZ2_END_MSG_LEN, out);

	fclose(out);
	ret = 0;
	return ret;
}

static void
_tbz2_write_file(FILE *src, int dir_fd, const char *dst, size_t len)
{
	unsigned char buffer[BUFSIZE*32];
	size_t this_write;
	FILE *out;

	if (!dst) {
		fseek(src, len, SEEK_CUR);
		return;
	}

	if (!tbz2_stdout) {
		int fd;

		out = NULL;
		fd = openat(dir_fd, dst, O_WRONLY|O_CLOEXEC|O_CREAT|O_TRUNC, 0644);
		if (fd >= 0)
			out = fdopen(fd, "w");
		if (out == NULL)
			errp("cannot write to '%s'", dst);
	} else
		out = stdout;

	do {
		this_write = fread(buffer, 1, MIN(len, sizeof(buffer)), src);
		fwrite(buffer, 1, this_write, out);
		len -= this_write;
	} while (len && this_write);

	if (out != stdout)
		fclose(out);
}

static int
tbz2_decompose(int dir_fd, const char *tbz2, const char *tarbz2, const char *xpak)
{
	FILE *in;
	unsigned char tbz2_tail[TBZ2_END_LEN];
	long xpak_size, tarbz2_size;
	struct stat st;
	int ret = 1;

	/* open tbz2 input */
	in = fopen(tbz2, "r");
	if (in == NULL)
		return ret;
	if (fstat(fileno(in), &st))
		goto close_in_and_ret;

	if (verbose)
		printf("input tbz2: %s (%s)\n", tbz2, make_human_readable_str(st.st_size, 1, 0));

	/* verify the tail signature */
	if (fseek(in, -TBZ2_END_LEN, SEEK_END) != 0)
		goto close_in_and_ret;
	if (fread(tbz2_tail, 1, TBZ2_END_LEN, in) != TBZ2_END_LEN)
		goto close_in_and_ret;
	if (memcmp(tbz2_tail + 4, TBZ2_END_MSG, TBZ2_END_MSG_LEN)) {
		warn("%s: Invalid tbz2", tbz2);
		goto close_in_and_ret;
	}

	/* calculate xpak's size */
	xpak_size = tbz2_decode_int(tbz2_tail);
	/* calculate tarbz2's size */
	tarbz2_size = st.st_size - xpak_size - TBZ2_END_LEN;

	/* reset to the start of the tbz2 */
	rewind(in);
	/* dump the tar.bz2 */
	if (verbose)
		printf("output tar.bz2: %s (%s)\n", tarbz2, make_human_readable_str(tarbz2_size, 1, 0));
	_tbz2_write_file(in, dir_fd, tarbz2, tarbz2_size);
	/* dump the xpak */
	if (verbose)
		printf("output xpak: %s (%s)\n", xpak, make_human_readable_str(xpak_size, 1, 0));
	_tbz2_write_file(in, dir_fd, xpak, xpak_size);

	ret = 0;
 close_in_and_ret:
	fclose(in);
	return ret;
}

int qtbz2_main(int argc, char **argv)
{
	enum { TBZ2_ACT_NONE, TBZ2_ACT_JOIN, TBZ2_ACT_SPLIT };
	int i, dir_fd;
	char action, split_xpak = 1, split_tarbz2 = 1;
	char *heap_tbz2, *heap_xpak, *heap_tarbz2;
	char *tbz2, *xpak, *tarbz2;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	action = TBZ2_ACT_NONE;
	dir_fd = AT_FDCWD;

	while ((i = GETOPT_LONG(QTBZ2, qtbz2, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qtbz2)
		case 'j': action = TBZ2_ACT_JOIN; break;
		case 's': action = TBZ2_ACT_SPLIT; break;
		case 't': split_xpak = 0; break;
		case 'x': split_tarbz2 = 0; break;
		case 'O': tbz2_stdout = 1; break;
		case 'd':
			if (dir_fd != AT_FDCWD)
				err("Only use -d once");
			dir_fd = open(optarg, O_RDONLY|O_CLOEXEC);
			break;
		}
	}
	if (optind == argc) {
		switch (action) {
		case TBZ2_ACT_JOIN:  err("Join usage: <input tar.bz2> <input xpak> [<output tbz2>]");
		case TBZ2_ACT_SPLIT: err("Split usage: <input tbz2> [<output tar.bz2> <output xpak>]");
		default:             qtbz2_usage(EXIT_FAILURE);
		}
	}

	heap_tbz2 = heap_xpak = heap_tarbz2 = NULL;
	tbz2 = xpak = tarbz2 = NULL;

	if (action == TBZ2_ACT_NONE) {
		if (strstr(argv[optind], ".tar.bz2") != NULL)
			action = TBZ2_ACT_JOIN;
		else if (strstr(argv[optind], ".tbz2") != NULL)
			action = TBZ2_ACT_SPLIT;
		else
			qtbz2_usage(EXIT_FAILURE);
	}

	/* tbz2tool join .tar.bz2 .xpak .tbz2 */
	if (action == TBZ2_ACT_JOIN) {
		/* grab the params if the user gave them */
		tarbz2 = argv[optind++];
		if (optind < argc) {
			xpak = argv[optind++];
			if (optind < argc)
				tbz2 = argv[optind];
		}
		/* otherwise guess what they should be */
		if (!xpak || !tbz2) {
			const char *s = basename(tarbz2);
			size_t len = strlen(s);

			/* autostrip the tarball extension */
			if (len >= 8 && !strcmp(s + len - 8, ".tar.bz2"))
				len -= 8;

			if (!xpak) {
				xpak = heap_xpak = xmalloc(len + 5 + 1);
				memcpy(xpak, s, len);
				strcpy(xpak + len, ".xpak");
			}
			if (!tbz2) {
				tbz2 = heap_tbz2 = xmalloc(len + 5 + 1);
				memcpy(tbz2, s, len);
				strcpy(tbz2 + len, ".tbz2");
			}
		}

		if (tbz2_compose(dir_fd, tarbz2, xpak, tbz2))
			warn("Could not compose '%s' and '%s'", tarbz2, xpak);

	/* tbz2tool split .tbz2 .tar.bz2 .xpak */
	} else {
		/* grab the params if the user gave them */
		tbz2 = argv[optind++];
		if (optind < argc) {
			tarbz2 = argv[optind++];
			if (optind < argc)
				xpak = argv[optind];
		}
		/* otherwise guess what they should be */
		if ((!tarbz2 && split_tarbz2) || (!xpak && split_xpak)) {
			const char *s = basename(tbz2);
			size_t len = strlen(s);

			/* autostrip the package extension */
			if (len >= 5 && !strcmp(s + len - 5, ".tbz2"))
				len -= 5;

			if (!tarbz2 && split_tarbz2) {
				tarbz2 = heap_tarbz2 = xmalloc(len + 8 + 1);
				memcpy(tarbz2, s, len);
				strcpy(tarbz2 + len, ".tar.bz2");
			} else if (!split_tarbz2)
				tarbz2 = NULL;

			if (!xpak && split_xpak) {
				xpak = heap_xpak = xmalloc(len + 5 + 1);
				memcpy(xpak, s, len);
				strcpy(xpak + len, ".xpak");
			} else if (!split_xpak)
				xpak = NULL;
		}

		if (tbz2_decompose(dir_fd, tbz2, tarbz2, xpak))
			warn("Could not decompose '%s'", tbz2);
	}

	free(heap_tbz2);
	free(heap_xpak);
	free(heap_tarbz2);

	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qtbz2)
#endif
