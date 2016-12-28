static ssize_t safe_read(int fd, void *buf, size_t len)
{
	ssize_t ret;

	while (1) {
		ret = read(fd, buf, len);
		if (ret >= 0)
			break;
		else if (errno != EINTR)
			break;
	}

	return ret;
}

static ssize_t safe_write(int fd, const void *buf, size_t len)
{
	ssize_t ret;

	while (len) {
		ret = write(fd, buf, len);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		buf += ret;
		len -= ret;
	}

	return ret;
}

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
