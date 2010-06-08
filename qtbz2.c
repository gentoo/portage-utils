/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qtbz2.c,v 1.16 2010/06/08 05:31:09 vapier Exp $
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

#define QTBZ2_FLAGS "jstxO" COMMON_FLAGS
static struct option const qtbz2_long_opts[] = {
	{"join",      no_argument, NULL, 'j'},
	{"split",     no_argument, NULL, 's'},
	{"tarbz2",    no_argument, NULL, 't'},
	{"xpak",      no_argument, NULL, 'x'},
	{"stdout",    no_argument, NULL, 'O'},
	COMMON_LONG_OPTS
};
static const char *qtbz2_opts_help[] = {
	"Join tar.bz2 + xpak into a tbz2",
	"Split a tbz2 into a tar.bz2 + xpak",
	"Just split the tar.bz2",
	"Just split the xpak",
	"Write files to stdout",
	COMMON_OPTS_HELP
};
static const char qtbz2_rcsid[] = "$Id: qtbz2.c,v 1.16 2010/06/08 05:31:09 vapier Exp $";
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

char tbz2_compose(const char *tarbz2, const char *xpak, const char *tbz2);
char tbz2_compose(const char *tarbz2, const char *xpak, const char *tbz2)
{
	FILE *out, *in_tarbz2, *in_xpak;
	struct stat st;
	char ret = 1;

	/* open tbz2 output */
	if ((out = fopen(tbz2, "w")) == NULL)
		return ret;
	/* open tar.bz2 input */
	if ((in_tarbz2 = fopen(tarbz2, "r")) == NULL) {
		fclose(out);
		return ret;
	}
	/* open xpak input */
	if ((in_xpak = fopen(xpak, "r")) == NULL) {
		fclose(out);
		fclose(in_tarbz2);
		return ret;
	}
	fstat(fileno(in_xpak), &st);

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

#define _TBZ2_MIN(a,b) (a < b ? : b)
void _tbz2_write_file(FILE *src, const char *dst, size_t len);
void _tbz2_write_file(FILE *src, const char *dst, size_t len)
{
	unsigned char buffer[BUFSIZE*32];
	size_t this_write;
	FILE *out;

	if (!dst) {
		fseek(src, len, SEEK_CUR);
		return;
	}

	if (tbz2_stdout)
		out = stdout;
	else if ((out = fopen(dst, "w")) == NULL)
		errp("cannot write to '%s'", dst);

	do {
		this_write = fread(buffer, 1, _TBZ2_MIN(len, sizeof(buffer)), src);
		fwrite(buffer, 1, this_write, out);
		len -= this_write;
	} while (len && this_write);

	if (out != stdout)
		fclose(out);
}

char tbz2_decompose(const char *tbz2, const char *tarbz2, const char *xpak);
char tbz2_decompose(const char *tbz2, const char *tarbz2, const char *xpak)
{
	FILE *in;
	unsigned char tbz2_tail[TBZ2_END_LEN];
	long xpak_size, tarbz2_size;
	struct stat st;
	char ret = 1;

	/* open tbz2 input */
	if ((in = fopen(tbz2, "r")) == NULL)
		return ret;
	fstat(fileno(in), &st);
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
	_tbz2_write_file(in, tarbz2, tarbz2_size);
	/* dump the xpak */
	_tbz2_write_file(in, xpak, xpak_size);

	ret = 0;
close_in_and_ret:
	fclose(in);
	return ret;
}

int qtbz2_main(int argc, char **argv)
{
	int i;
	char action = 0, split_xpak = 1, split_tarbz2 = 1;
	char *heap_tbz2, *heap_xpak, *heap_tarbz2;
	char *tbz2, *xpak, *tarbz2;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QTBZ2, qtbz2, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qtbz2)
		case 'j': action = 1; break;
		case 's': action = 2; break;
		case 't': split_xpak = 0; break;
		case 'x': split_tarbz2 = 0; break;
		case 'O': tbz2_stdout = 1; break;
		}
	}
	if (optind == argc) {
		switch (action) {
		case 1: join_usage:
			err("Join usage: <input tar.bz2> <input xpak> [<output tbz2>]");
		case 2: split_usage:
			err("Split usage  <input tbz2> [<output tar.bz2> <output xpak>]");
		default: qtbz2_usage(EXIT_FAILURE);
		}
	}

	heap_tbz2 = heap_xpak = heap_tarbz2 = NULL;
	tbz2 = xpak = tarbz2 = NULL;

	if (action == 0) {
		if (strstr(argv[optind], ".tar.bz2") != NULL)
			action = 1;
		else if (strstr(argv[optind], ".tbz2") != NULL)
			action = 2;
		else
			qtbz2_usage(EXIT_FAILURE);
	}

	/* tbz2tool join .tar.bz2 .xpak .tbz2 */
	if (action == 1) {
		/* grab the params if the user gave them */
		tarbz2 = argv[optind++];
		if (optind < argc) {
			xpak = argv[optind++];
			if (optind < argc)
				tbz2 = argv[optind];
		}
		/* otherwise guess what they should be */
		if (!xpak) {
			i = strlen(tarbz2);
			if (i <= 5) goto join_usage;
			xpak = heap_xpak = xstrdup(tarbz2);
			strcpy(xpak+i-7, "xpak");
		}
		if (!tbz2) {
			i = strlen(tarbz2);
			if (i <= 5) goto join_usage;
			tbz2 = heap_tbz2 = xstrdup(tarbz2);
			strcpy(tbz2+i-6, "bz2");
		}

		if (tbz2_compose(tarbz2, xpak, tbz2))
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
		if (!tarbz2 && split_tarbz2) {
			i = strlen(tbz2);
			if (i <= 5) goto split_usage;
			tarbz2 = heap_tarbz2 = xmalloc(i + 4);
			strcpy(tarbz2, tbz2);
			strcpy(tarbz2+i-3, "ar.bz2");
		} else if (!split_tarbz2)
			tarbz2 = NULL;
		if (!xpak && split_xpak) {
			i = strlen(tbz2);
			if (i <= 5) goto split_usage;
			xpak = heap_xpak = xstrdup(tbz2);
			strcpy(xpak+i-4, "xpak");
		} else if (!split_xpak)
			xpak = NULL;

		if (tbz2_decompose(tbz2, tarbz2, xpak))
			warn("Could not decompose '%s'", tbz2);
	}

	if (heap_tbz2) free(heap_tbz2);
	if (heap_xpak) free(heap_xpak);
	if (heap_tarbz2) free(heap_tarbz2);

	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qtbz2)
#endif
