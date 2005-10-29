/* 
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/virtuals.c,v 1.2 2005/10/29 06:10:01 solar Exp $
 *
 * These functions should be made to use a linked list and cache all 
 * the virtuals in 1 profile read pass to reduce file i/o
 * We would also need to make a pass on provides.
 *
 */

#include <stdio.h>
#include <unistd.h>

#if 0
 <jstubbs> portage/{,profile/}virtuals should both behave the same now.
 <jstubbs> but it's /etc/portage/.../virtuals ; /var/db/pkg/*/*/PROVIDE ; /etc/make.profile stacking ; */
#endif

static char *resolve_local_profile_virtual(char *virtual) {
	char buf[BUFSIZ];
	FILE *fp;
	char *p;
	char *paths[] = { (char *) "/etc/portage/profile/virtuals", (char *) "/etc/portage/virtuals" };
	size_t i;

	for (i = 0; i < sizeof(paths)/sizeof(paths[0]); i++) {
		if ((fp = fopen(paths[i], "r")) != NULL) {
			while((fgets(buf, sizeof(buf), fp)) != NULL) {
				if (*buf != 'v') continue;
				for (p = buf ; *p != 0; ++p) if (isspace(*p)) *p = ' ';
				if ((p = strchr(buf, ' ')) != NULL) {
					*p = 0;
					if ((strcmp(virtual, buf)) == 0) {
						fclose(fp);
						return rmspace(++p);
					}
				}
			}
			fclose(fp);
		}
	}
	return NULL;
}


/*
static char *resolve_profile_virtual(char *virtual);
static char *resolve_profile_virtual(char *virtual) {
	static char buf[BUFSIZ];
	static char *p;
	FILE *fp;

	if ((p = resolve_local_profile_virtual(virtual)) != NULL)
		return p;

	if ((chdir("/etc/")) == (-1))
		return virtual;

	memset(buf, 0, sizeof(buf));

	if ((readlink("make.profile", buf, sizeof(buf))) != (-1)) {
		chdir(buf);
		getcwd(buf, sizeof(buf));
		if (access(buf, R_OK) != 0)
			return virtual;
	vstart:
		if ((fp = fopen("virtuals", "r")) != NULL) {
			while((fgets(buf, sizeof(buf), fp)) != NULL) {
				if (*buf != 'v') continue;
				for (p = buf ; *p != 0; ++p) if (isspace(*p)) *p = ' ';
				if ((p = strchr(buf, ' ')) != NULL) {
					*p = 0;
					if ((strcmp(virtual, buf)) == 0) {
						fclose(fp);
						return rmspace(++p);
					}
				}
			}
			fclose(fp);
		}
		if ((fp = fopen("parent", "r")) != NULL) {
			while((fgets(buf, sizeof(buf), fp)) != NULL) {
				rmspace(buf);
				if (!*buf) continue;
				if (*buf == '#') continue;
				if (isspace(*buf)) continue;
				fclose(fp);
				if ((chdir(buf)) == (-1)) {
					fclose(fp);
					return virtual;
				}
				goto vstart;
			}
			fclose(fp);
		}
	}
	return virtual;
}

*/

queue *virtuals = NULL;

static queue *resolve_virtuals();
static queue *resolve_virtuals() {
	static char buf[BUFSIZ];
	static char *p;
	FILE *fp;

	if ((chdir("/etc/")) == (-1))
		return virtuals;

	memset(buf, 0, sizeof(buf));

	if ((readlink("make.profile", buf, sizeof(buf))) != (-1)) {
		chdir(buf);
		getcwd(buf, sizeof(buf));
		if (access(buf, R_OK) != 0)
			return virtuals;
	vstart:
		if ((fp = fopen("virtuals", "r")) != NULL) {
			while((fgets(buf, sizeof(buf), fp)) != NULL) {
				if (*buf != 'v') continue;
				for (p = buf ; *p != 0; ++p) if (isspace(*p)) *p = ' ';
				if ((p = strchr(buf, ' ')) != NULL) {
					*p = 0;
					virtuals = addq(rmspace(++p), virtuals);
				}
			}
			fclose(fp);
		}
		if ((fp = fopen("parent", "r")) != NULL) {
			while((fgets(buf, sizeof(buf), fp)) != NULL) {
				rmspace(buf);
				if (!*buf) continue;
				if (*buf == '#') continue;
				if (isspace(*buf)) continue;
				fclose(fp);
				if ((chdir(buf)) == (-1)) {
					fclose(fp);
					return virtuals;
				}
				goto vstart;
			}
			fclose(fp);
		}
	}
	return virtuals;
}
