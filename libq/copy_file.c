static int copy_file_fd(int fd_src, int fd_dst)
{
	FILE *fp_src, *fp_dst;
	size_t rcnt, wcnt;
	char buf[BUFSIZE];

	/* dont fclose() as that implicitly close()'s */

	fp_src = fdopen(fd_src, "r");
	if (!fp_src)
		return -1;

	fp_dst = fdopen(fd_dst, "w");
	if (!fp_dst)
		return -1;

	while (1) {
		rcnt = fread(buf, sizeof(buf[0]), sizeof(buf), fp_src);
		if (!rcnt) {
			fflush(fp_dst);
			return feof(fp_src) ? 0 : -1;
		}

		wcnt = fwrite(buf, sizeof(buf[0]), rcnt, fp_dst);
		if (wcnt != rcnt) {
			if (ferror(fp_dst))
				return -1;
			fseek(fp_src, wcnt - rcnt, SEEK_CUR);
		}
	}
}
