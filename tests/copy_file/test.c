/*
 * Copyright 2005-2018 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#include "tests/tests.h"

#include "libq/safe_io.c"
#include "libq/copy_file.c"

static int src_fd, dst_fd;

static void testone(const char *src_buf, size_t len)
{
	int ret;
	static char dst_buf[50 * 1024 * 1024];

	assert(len <= sizeof(dst_buf));

	ret = ftruncate(src_fd, 0);
	assert(ret == 0);
	ret = lseek(src_fd, 0, SEEK_SET);
	assert(ret == 0);
	ret = ftruncate(dst_fd, 0);
	assert(ret == 0);
	ret = lseek(dst_fd, 0, SEEK_SET);
	assert(ret == 0);

	ret = write(src_fd, src_buf, len);
	assert(ret == len);
	ret = lseek(src_fd, 0, SEEK_SET);
	assert(ret == 0);
	ret = copy_file_fd(src_fd, dst_fd);
	assert(ret == 0);

	ret = lseek(dst_fd, 0, SEEK_SET);
	assert(ret == 0);
	ret = read(dst_fd, dst_buf, len);
	assert(ret == len);

	assert(memcmp(dst_buf, src_buf, len) == 0);
}

int main(int argc, char *argv[])
{
	size_t len;
	char *buf;
	char src_path[] = "portage-utils.src.XXXXXX";
	char dst_path[] = "portage-utils.dst.XXXXXX";

	src_fd = mkstemp(src_path);
	assert(src_fd != -1);
	unlink(src_path);
	dst_fd = mkstemp(dst_path);
	assert(dst_fd != -1);
	unlink(dst_path);

	testone("foo", 4);

	len = 10 * 1024 * 1024;
	buf = malloc(len);
	assert(buf != NULL);
	memset(buf, 0xaf, len);
	testone(buf, len);

	return 0;
}
