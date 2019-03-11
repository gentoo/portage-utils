static int copy_file_fd(int fd_src, int fd_dst)
{
	ssize_t rcnt, wcnt;
	char buf[64 * 1024];

	while (1) {
		rcnt = safe_read(fd_src, buf, sizeof(buf));
		if (rcnt < 0)
			return -1;
		else if (rcnt == 0)
			return 0;

		wcnt = safe_write(fd_dst, buf, rcnt);
		if (wcnt == -1)
			return -1;
	}
}
